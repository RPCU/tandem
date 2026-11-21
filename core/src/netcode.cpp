#include "tandem/netcode.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace tandem::netcode {
namespace {
constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;

float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

float Length(const proto::Vec3 &v) { return std::hypot(v.x, v.y, v.z); }

float AngleBetween(float a, float b) {
  float d = std::fmod(std::abs(a - b), kTwoPi);
  return d > std::numbers::pi_v<float> ? kTwoPi - d : d;
}

// A gap a frame shorter than asked still counts: frames come every 16-33 ms
// and do not fall on the interval.
constexpr std::uint64_t kSlackUs = 12'000;
// A V or car this far from where its last state sent puts it now (moving
// on at that velocity) is sent at once, near or far (a teleport, a fast
// travel, regions' places): the receivers would draw it where it no longer
// is until the next state is due.
constexpr float kJumpM = 5.f;

template <class State> bool Jumped(const State &state, const State &last) {
  const float seconds = static_cast<float>(state.timeUs - last.timeUs) / 1e6f;
  const proto::Vec3 expected{last.position.x + last.velocity.x * seconds,
                             last.position.y + last.velocity.y * seconds,
                             last.position.z + last.velocity.z * seconds};
  return Distance(state.position, expected) >= kJumpM;
}
} // namespace

std::uint64_t PoseIntervalUs(float nearest) {
  if (nearest <= 30.f)
    return 0;
  if (nearest <= 80.f)
    return 66'667;
  if (nearest <= 200.f)
    return 200'000;
  return 1'000'000;
}

std::uint64_t StateIntervalUs(float nearest) {
  if (nearest <= 80.f)
    return 0;
  if (nearest <= 200.f)
    return 66'667;
  return 200'000;
}

bool Gate::Due(std::uint64_t nowUs, std::uint64_t intervalUs) {
  if (intervalUs != 0 && m_any && nowUs - m_lastUs + kSlackUs < intervalUs)
    return false;
  m_any = true;
  m_lastUs = nowUs;
  return true;
}

bool IsStill(const proto::PlayerState &state, const proto::PlayerState &last) {
  return Distance(state.position, last.position) < 0.01f && Length(state.velocity) < 0.05f &&
         AngleBetween(sync::YawOf(state.orientation), sync::YawOf(last.orientation)) < 0.005f &&
         state.stance == last.stance && state.flags == last.flags;
}

bool IsStill(const proto::VehicleState &state, const proto::VehicleState &last) {
  const auto &a = state.orientation, &b = last.orientation;
  const float dot = std::abs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r);
  return Distance(state.position, last.position) < 0.01f && Length(state.velocity) < 0.05f &&
         dot > 0.99999f;
}

template <class State>
std::vector<State> StillGate<State>::Offer(const State &state, std::uint64_t relevanceUs) {
  std::vector<State> out;
  if (m_last && IsStill(state, *m_last)) {
    // Standing: now and then, so the receiver knows it is there.
    if (state.timeUs - m_last->timeUs < std::max(kStillIntervalUs, relevanceUs)) {
      m_held = state;
      ++m_heldCount;
      return out;
    }
  } else if (m_last && relevanceUs && state.timeUs - m_last->timeUs + kSlackUs < relevanceUs &&
             !Jumped(state, *m_last)) {
    // Far away: fewer states, the motion between them is the receiver's.
    m_held.reset();
    return out;
  } else if (m_held &&
             state.timeUs - m_held->timeUs <= std::max(kStillIntervalUs, relevanceUs) + kSlackUs) {
    // Off again: from where, and when, it last stood. Not after a pause in
    // the offers (our V elsewhere, a loading screen): that one is stale, the
    // receiver would draw V back there for a moment.
    out.push_back(*m_held);
  }
  m_held.reset();
  out.push_back(state);
  m_last = state;
  return out;
}

namespace {
std::array<std::int16_t, 3> Centimetres(const proto::Vec3 &v) {
  const auto one = [](float x) {
    return static_cast<std::int16_t>(std::clamp(std::round(x * 100.f), -32767.f, 32767.f));
  };
  return {one(v.x), one(v.y), one(v.z)};
}

proto::Vec3 Metres(const std::array<std::int16_t, 3> &v) {
  return {v[0] / 100.f, v[1] / 100.f, v[2] / 100.f};
}

std::uint16_t Turn(float yaw) {
  float turn = std::fmod(yaw, kTwoPi);
  if (turn < 0)
    turn += kTwoPi;
  return static_cast<std::uint16_t>(std::lround(turn / kTwoPi * 65536.f) & 0xFFFF);
}

float Yaw(std::uint16_t turn) {
  float yaw = static_cast<float>(turn) / 65536.f * kTwoPi;
  return yaw > std::numbers::pi_v<float> ? yaw - kTwoPi : yaw;
}
} // namespace

proto::PlayerMotion Compact(const proto::PlayerState &state) {
  return {.player = state.player,
          .seq = state.seq,
          .timeUs = state.timeUs,
          .position = state.position,
          .yaw = Turn(sync::YawOf(state.orientation)),
          .velocity = Centimetres(state.velocity),
          .stance = state.stance,
          .flags = state.flags};
}

proto::PlayerState Whole(const proto::PlayerMotion &motion) {
  return {.player = motion.player,
          .seq = motion.seq,
          .timeUs = motion.timeUs,
          .position = motion.position,
          .orientation = sync::FromYaw(Yaw(motion.yaw)),
          .velocity = Metres(motion.velocity),
          .stance = motion.stance,
          .flags = motion.flags};
}

proto::VehicleMotion Compact(const proto::VehicleState &state) {
  proto::VehicleMotion motion{.player = state.player,
                              .seq = state.seq,
                              .timeUs = state.timeUs,
                              .position = state.position,
                              .velocity = Centimetres(state.velocity)};
  pose::PackQuat5(state.orientation, motion.orientation.data());
  return motion;
}

proto::VehicleState Whole(const proto::VehicleMotion &motion) {
  return {.player = motion.player,
          .seq = motion.seq,
          .timeUs = motion.timeUs,
          .position = motion.position,
          .orientation = pose::UnpackQuat5(motion.orientation.data()),
          .velocity = Metres(motion.velocity)};
}

template class StillGate<proto::PlayerState>;
template class StillGate<proto::VehicleState>;

std::optional<proto::NpcMotion> NpcCoder::Encode(const proto::NpcStates &states) {
  proto::NpcMotion motion{.seq = states.seq, .timeUs = states.timeUs};
  if (!states.npcs.empty()) {
    proto::Vec3 low = states.npcs.front().position, high = low;
    for (const auto &entry : states.npcs) {
      low = {std::min(low.x, entry.position.x), std::min(low.y, entry.position.y),
             std::min(low.z, entry.position.z)};
      high = {std::max(high.x, entry.position.x), std::max(high.y, entry.position.y),
              std::max(high.z, entry.position.z)};
    }
    constexpr float kRange = 32767.f * kStep;
    if (high.x - low.x > 2 * kRange || high.y - low.y > 2 * kRange || high.z - low.z > 2 * kRange)
      return std::nullopt;
    motion.origin = {std::round((low.x + high.x) / 2), std::round((low.y + high.y) / 2),
                     std::round((low.z + high.z) / 2)};
  }
  std::set<proto::NpcId> listed;
  for (const auto &entry : states.npcs) {
    listed.insert(entry.npc);
    const auto quantize = [&](float value, float origin) {
      return static_cast<std::int16_t>(
          std::clamp(std::round((value - origin) / kStep), -32767.f, 32767.f));
    };
    float yaw = std::fmod(entry.yaw, kTwoPi);
    if (yaw < 0)
      yaw += kTwoPi;
    proto::NpcMove move{
        .npc = entry.npc,
        .offset = {quantize(entry.position.x, motion.origin.x),
                   quantize(entry.position.y, motion.origin.y),
                   quantize(entry.position.z, motion.origin.z)},
        .yaw = static_cast<std::uint16_t>(std::lround(yaw / kTwoPi * 65536.f) & 0xFFFF),
        .detected = entry.detected,
    };
    const auto read = Dequantize(motion, move);
    const auto it = m_sent.find(entry.npc);
    if (it != m_sent.end()) {
      const auto &sent = it->second;
      const bool changed = Distance(read.position, sent.position) > kMoveM ||
                           AngleBetween(read.yaw, sent.yaw) > kTurnRad ||
                           read.detected != sent.detected;
      if (!changed && states.seq - sent.seq < kRefreshFrames)
        continue;
    }
    motion.npcs.push_back(move);
    // A newcomer's next refresh falls on a frame of its own: they spread.
    const auto seq = it == m_sent.end() ? states.seq - entry.npc % kRefreshFrames : states.seq;
    m_sent[entry.npc] = {read.position, read.yaw, read.detected, seq};
  }
  // Out of the list (not shared, or out of range): said once.
  for (auto it = m_sent.begin(); it != m_sent.end();) {
    if (listed.contains(it->first)) {
      ++it;
      continue;
    }
    if (motion.npcs.size() < proto::kMaxNpcsPerState)
      motion.npcs.push_back({.npc = it->first, .flags = kNpcUnlisted});
    it = m_sent.erase(it);
  }
  if (motion.npcs.size() > proto::kMaxNpcsPerState)
    motion.npcs.resize(proto::kMaxNpcsPerState);
  return motion;
}

proto::NpcEntry Dequantize(const proto::NpcMotion &motion, const proto::NpcMove &move) {
  float yaw = static_cast<float>(move.yaw) / 65536.f * kTwoPi;
  if (yaw > std::numbers::pi_v<float>)
    yaw -= kTwoPi;
  return {.npc = move.npc,
          .position = {motion.origin.x + move.offset[0] * NpcCoder::kStep,
                       motion.origin.y + move.offset[1] * NpcCoder::kStep,
                       motion.origin.z + move.offset[2] * NpcCoder::kStep},
          .yaw = yaw,
          .detected = move.detected};
}

proto::NpcStates NpcBook::Apply(const proto::NpcMotion &motion,
                                const std::map<proto::NpcId, proto::NpcSpawn> &known) {
  for (const auto &move : motion.npcs) {
    if (move.flags & kNpcUnlisted)
      m_npcs.erase(move.npc);
    else
      m_npcs[move.npc] = Dequantize(motion, move);
  }
  proto::NpcStates states{.seq = motion.seq, .timeUs = motion.timeUs};
  for (const auto &[npc, entry] : m_npcs)
    if (known.contains(npc) && states.npcs.size() < proto::kMaxNpcsPerState)
      states.npcs.push_back(entry);
  return states;
}

void NpcBook::Note(const proto::NpcStates &states) {
  m_npcs.clear();
  for (const auto &entry : states.npcs)
    m_npcs[entry.npc] = entry;
}

void NpcBook::Seed(const proto::NpcSpawn &spawn) {
  m_npcs.try_emplace(
      spawn.npc, proto::NpcEntry{.npc = spawn.npc, .position = spawn.position, .yaw = spawn.yaw});
}
} // namespace tandem::netcode
