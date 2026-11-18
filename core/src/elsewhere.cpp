#include "tandem/elsewhere.hpp"

#include "tandem/interpolation.hpp"

#include <cmath>

namespace tandem::elsewhere {
namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool Finite(const proto::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

bool LeavesBody(proto::Place place) {
  return place == proto::Place::Cyberspace || place == proto::Place::Braindance;
}

proto::Elsewhere Sanitized(proto::Elsewhere state) {
  if (state.place > proto::Place::Other)
    state.place = proto::Place::City;
  state.flags &= proto::kElsewhereMoved | proto::kElsewhereSettled;
  if (state.place == proto::Place::City || !(state.flags & proto::kElsewhereMoved))
    state.flags = 0;
  if (state.place == proto::Place::City || !state.flags) {
    state.anchor = {};
    state.yaw = 0.f;
  }
  if (!Finite(state.anchor) || !std::isfinite(state.yaw)) {
    state.anchor = {};
    state.yaw = 0.f;
    state.flags &= ~proto::kElsewhereSettled;
  }
  return state;
}

void Tracker::Leave(const Kept &body, std::uint64_t nowUs, bool confirmed, Body &out) {
  m_moved = true;
  m_confirmed = confirmed;
  m_movedAtUs = nowUs;
  m_endedAtUs = 0;
  m_anchor = body;
  out.holdPoseFrom = body.timeUs;
}

void Tracker::Return(const Sample &sample, Body &out) {
  m_moved = m_confirmed = false;
  m_endedAtUs = 0;
  m_real = Kept{sample.timeUs, sample.position, sample.orientation, sample.settled};
  out.releasePose = true;
}

Body Tracker::Feed(const Sample &sample) {
  Body out{.position = sample.position, .orientation = sample.orientation};
  const bool elsewhere = sample.place != proto::Place::City;
  const bool jump = m_last && !sample.mounted && !m_last->mounted &&
                    Distance(sample.position, m_last->position) > kJumpMetres;
  const Kept here{sample.timeUs, sample.position, sample.orientation, sample.settled};

  if (!m_moved) {
    const bool begins = elsewhere && m_place == proto::Place::City;
    if (jump && m_real) {
      m_lastJump = {sample.timeUs, *m_real};
      Leave(*m_real, sample.timeUs, elsewhere, out);
    } else if (begins && m_lastJump && sample.timeUs - m_lastJump->first <= kLateWorldUs) {
      // The world began just after a teleport we let go: the body is where
      // V was before it.
      Leave(m_lastJump->second, sample.timeUs, true, out);
    } else if (begins && LeavesBody(sample.place)) {
      // V's entity is the world's camera from now on, wherever it goes.
      Leave(here, sample.timeUs, true, out);
    } else {
      m_real = here;
    }
  } else if (elsewhere) {
    m_confirmed = true;
    m_endedAtUs = 0;
  } else if (!m_confirmed) {
    // A teleport with no world: one in the city.
    if (sample.timeUs - m_movedAtUs > kTeleportUs)
      Return(sample, out);
  } else {
    // The world is over; the way back may come a moment later.
    if (!m_endedAtUs)
      m_endedAtUs = sample.timeUs;
    if (jump || Distance(sample.position, m_anchor.position) <= kJumpMetres ||
        sample.timeUs - m_endedAtUs > kReturnUs)
      Return(sample, out);
  }

  if (m_moved) {
    out.held = true;
    out.position = m_anchor.position;
    out.orientation = m_anchor.orientation;
  }
  // The place told: kept until the body is back.
  if (elsewhere || !(m_moved && m_confirmed))
    m_place = sample.place;
  proto::Elsewhere state{.place = m_place};
  if (m_moved && m_confirmed && m_place != proto::Place::City) {
    state.flags = proto::kElsewhereMoved | (m_anchor.settled ? proto::kElsewhereSettled : 0);
    state.anchor = m_anchor.position;
    state.yaw = sync::YawOf(m_anchor.orientation);
  }
  m_state = state;
  m_last = sample;
  return out;
}

void PoseHistory::Push(std::uint64_t timeUs, const proto::PoseState &pose) {
  if (!m_poses.empty() && timeUs < m_poses.back().first)
    m_poses.clear(); // a clock that went back: start again
  m_poses.emplace_back(timeUs, pose);
  while (m_poses.size() > 1 && timeUs - m_poses.front().first > kPoseHistoryUs)
    m_poses.pop_front();
}

std::optional<proto::PoseState> PoseHistory::At(std::uint64_t timeUs) const {
  if (m_poses.empty())
    return std::nullopt;
  const proto::PoseState *pick = &m_poses.front().second;
  for (const auto &[at, pose] : m_poses) {
    if (at > timeUs)
      break;
    pick = &pose;
  }
  return *pick;
}
} // namespace tandem::elsewhere
