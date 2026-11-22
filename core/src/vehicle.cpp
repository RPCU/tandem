#include "tandem/vehicle.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::sync {
namespace {
float Length(const proto::Vec3 &v) { return std::hypot(v.x, v.y, v.z); }

proto::Vec3 Along(const proto::VehicleState &last, std::uint64_t renderUs) {
  const auto ahead = std::min<std::uint64_t>(renderUs > last.timeUs ? renderUs - last.timeUs : 0,
                                             VehicleTrack::kExtrapolateUs);
  const float dt = static_cast<float>(ahead) / 1e6f;
  return {last.position.x + last.velocity.x * dt, last.position.y + last.velocity.y * dt,
          last.position.z + last.velocity.z * dt};
}
} // namespace

void VehicleTrack::Push(const proto::VehicleState &state) {
  if (m_haveSeq && static_cast<std::int32_t>(state.seq - m_lastSeq) <= 0)
    return; // late or repeated
  m_lastSeq = state.seq;
  m_haveSeq = true;
  if (!m_samples.empty() && state.timeUs <= m_samples.back().timeUs)
    m_samples.clear(); // the driver's clock jumped: start over
  m_samples.push_back(state);
  while (m_samples.size() > kMaxSamples)
    m_samples.pop_front();
}

VehicleView VehicleTrack::At(std::uint64_t renderUs) {
  VehicleView view;
  if (m_samples.empty())
    return view;
  view.valid = true;
  const float dt = m_lastRenderUs && renderUs > m_lastRenderUs
                       ? std::min(static_cast<float>(renderUs - m_lastRenderUs) / 1e6f, 0.25f)
                       : 0.f;
  m_lastRenderUs = renderUs;
  const auto &first = m_samples.front();
  bool done = false;
  if (renderUs <= first.timeUs) {
    view.position = first.position;
    view.orientation = first.orientation;
    view.velocity = first.velocity;
    done = true;
  }
  for (std::size_t i = 1; i < m_samples.size() && !done; ++i) {
    const auto &b = m_samples[i];
    if (renderUs > b.timeUs)
      continue;
    const auto &a = m_samples[i - 1];
    const float t =
        static_cast<float>(renderUs - a.timeUs) / static_cast<float>(b.timeUs - a.timeUs);
    const float span = static_cast<float>(b.timeUs - a.timeUs) / 1e6f;
    const auto lerp = [t](const proto::Vec3 &x, const proto::Vec3 &y) {
      return proto::Vec3{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t};
    };
    // A cubic through both states with their velocities (Hermite): a car
    // turning or braking follows its curve between states, not the chord.
    // When the velocities disagree with the motion (a teleport, a crash),
    // the chord.
    const proto::Vec3 chord{(b.position.x - a.position.x) / span,
                            (b.position.y - a.position.y) / span,
                            (b.position.z - a.position.z) / span};
    const auto off = [&](const proto::Vec3 &v) {
      return Length({v.x - chord.x, v.y - chord.y, v.z - chord.z});
    };
    const float allowed = 0.5f * Length(chord) + 2.f;
    if (span > 0.f && span < 0.3f && off(a.velocity) < allowed && off(b.velocity) < allowed) {
      const float t2 = t * t, t3 = t2 * t;
      const float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2,
                  h11 = t3 - t2;
      const auto axis = [&](float pa, float va, float pb, float vb) {
        return h00 * pa + h10 * span * va + h01 * pb + h11 * span * vb;
      };
      view.position = {axis(a.position.x, a.velocity.x, b.position.x, b.velocity.x),
                       axis(a.position.y, a.velocity.y, b.position.y, b.velocity.y),
                       axis(a.position.z, a.velocity.z, b.position.z, b.velocity.z)};
    } else {
      view.position = lerp(a.position, b.position);
    }
    view.velocity = lerp(a.velocity, b.velocity);
    view.orientation = pose::Slerp(a.orientation, b.orientation, t);
    done = true;
  }
  const auto &last = m_samples.back();
  if (!done) {
    // Past the newest state: along its velocity for a while, then held.
    view.position = Along(last, renderUs);
    view.orientation = last.orientation;
    view.velocity = renderUs - last.timeUs > kExtrapolateUs ? proto::Vec3{} : last.velocity;
    view.extrapolated = true;
  }
  // A correction after a guess fades out (as for a body, interpolation.hpp).
  if (m_guess && (!view.extrapolated || m_guess->seq != last.seq)) {
    const auto guess = Along(*m_guess, renderUs);
    const proto::Vec3 gap{guess.x + m_offset.x - view.position.x,
                          guess.y + m_offset.y - view.position.y,
                          guess.z + m_offset.z - view.position.z};
    m_offset = Length(gap) <= kMaxBlendM ? gap : proto::Vec3{};
  } else {
    m_offset = Fade(m_offset, dt, Track::kBlendSeconds, kBlendSpeed);
  }
  if (view.extrapolated)
    m_guess = last;
  else
    m_guess.reset();
  view.position = {view.position.x + m_offset.x, view.position.y + m_offset.y,
                   view.position.z + m_offset.z};
  return view;
}

std::uint64_t RenderClock::At(std::uint64_t localUs, std::uint64_t targetUs) {
  const double target = static_cast<double>(targetUs);
  if (!m_have || localUs < m_localUs) {
    m_have = true;
    m_localUs = localUs;
    m_renderUs = target;
    return targetUs;
  }
  const double elapsed = static_cast<double>(localUs - m_localUs);
  m_localUs = localUs;
  const double ahead = m_renderUs + elapsed;
  const double error = target - ahead;
  if (std::abs(error) > static_cast<double>(kSnapUs))
    m_renderUs = target;
  else
    m_renderUs = ahead + std::clamp(error, -kSlew * elapsed, kSlew * elapsed);
  return m_renderUs > 0.0 ? static_cast<std::uint64_t>(m_renderUs) : 0;
}

bool MayDrive(const DriveAsk &ask) {
  if (!ask.ownerInSession)
    return false;
  switch (ask.lend) {
  case proto::Lend::Crew:
    return true;
  case proto::Lend::WithOwner:
    return ask.ownerAboard;
  case proto::Lend::Never:
    return false;
  }
  return false;
}
} // namespace tandem::sync
