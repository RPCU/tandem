#include "tandem/interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tandem::sync {
namespace {
constexpr double kAlpha = 1.0 / 16.0;
// Arrival gaps longer than this are a pause, not jitter.
constexpr std::uint64_t kIgnoreGapUs = 500'000;
// The speed shown follows the measured one this fast (per second).
constexpr float kSpeedFollowPerSecond = 10.f;

float WrapAngle(float a) {
  constexpr float pi = std::numbers::pi_v<float>;
  while (a > pi)
    a -= 2 * pi;
  while (a < -pi)
    a += 2 * pi;
  return a;
}

float LerpAngle(float a, float b, float t) { return a + WrapAngle(b - a) * t; }

proto::Vec3 Lerp(const proto::Vec3 &a, const proto::Vec3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

float GroundDistance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(b.x - a.x, b.y - a.y);
}

float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
}

// Seconds between two host times: only differences become floating point.
float Seconds(std::uint64_t from, std::uint64_t to) {
  return to >= from ? static_cast<float>(to - from) / 1e6f : -static_cast<float>(from - to) / 1e6f;
}
} // namespace

float YawOf(const proto::Quat &q) {
  return std::atan2(2.f * (q.r * q.k + q.i * q.j), 1.f - 2.f * (q.j * q.j + q.k * q.k));
}

proto::Quat FromYaw(float yaw) { return {0.f, 0.f, std::sin(yaw / 2.f), std::cos(yaw / 2.f)}; }

Track::Track(std::uint32_t sendRateHz)
    // 15 ms + 1.5 send intervals: 65 ms at 30 Hz.
    : m_baseUs(15'000 + 1'500'000 / std::max<std::uint32_t>(sendRateHz, 1)) {}

void Track::Push(const proto::PlayerState &state, std::uint64_t arrivalHostUs) {
  // Drop what is older than what we have: newest wins on the state channel.
  if (m_haveSeq && static_cast<std::int32_t>(state.seq - m_lastSeq) <= 0) {
    ++m_stats.dropped;
    return;
  }
  if (state.timeUs == 0 || (!m_samples.empty() && state.timeUs <= m_samples.back().timeUs)) {
    ++m_stats.dropped;
    return;
  }
  m_haveSeq = true;
  m_lastSeq = state.seq;
  ++m_stats.received;

  // Transit = arrival - send time (both host clock). Its frame-to-frame change
  // is the jitter (RFC 3550); the clock offset error cancels out.
  const double transit =
      static_cast<double>(static_cast<std::int64_t>(arrivalHostUs - state.timeUs));
  if (m_haveTransit) {
    const auto gap = arrivalHostUs - m_lastArrivalUs;
    if (gap < kIgnoreGapUs) {
      m_jitterUs += (std::abs(transit - m_lastTransitUs) - m_jitterUs) * kAlpha;
      m_intervalUs += (static_cast<double>(gap) - m_intervalUs) * kAlpha;
    }
    m_transitUs += (transit - m_transitUs) * kAlpha;
  } else {
    m_transitUs = std::max(0.0, transit);
    m_intervalUs = static_cast<double>(m_baseUs) / 1.5;
    m_haveTransit = true;
  }
  m_lastTransitUs = transit;
  m_lastArrivalUs = arrivalHostUs;

  m_samples.push_back({state.timeUs, state});
  while (m_samples.size() > kMaxSamples)
    m_samples.pop_front();
}

std::uint64_t Track::DelayUs() const {
  const auto jitterExtra =
      std::min<std::uint64_t>(static_cast<std::uint64_t>(2.0 * m_jitterUs), kMaxJitterExtraUs);
  const auto base = m_baseUs + jitterExtra;
  // Enough to always hold the next state: it is in flight for `transit` and
  // comes `interval` after the last.
  const auto needed =
      static_cast<std::uint64_t>(std::max(0.0, m_transitUs) + m_intervalUs) + jitterExtra;
  return std::min(std::max(base, needed), std::max(kMaxDelayUs, base));
}

View Track::At(std::uint64_t nowHostUs) {
  View view;
  if (m_samples.empty())
    return view;
  view.valid = true;

  const auto delay = DelayUs();
  const auto render = nowHostUs > delay ? nowHostUs - delay : 0;

  // Older states than the one just before `render` will never be needed.
  while (m_samples.size() > 2 && m_samples[1].timeUs <= render)
    m_samples.pop_front();

  const auto &first = m_samples.front();
  const auto &last = m_samples.back();
  float measured = 0.f;

  if (render <= first.timeUs) {
    view.position = first.state.position;
    view.yaw = YawOf(first.state.orientation);
    view.stance = first.state.stance;
    view.flags = first.state.flags;
  } else if (render < last.timeUs) {
    // Between two states.
    std::size_t i = 0;
    while (m_samples[i + 1].timeUs <= render)
      ++i;
    const auto &a = m_samples[i];
    const auto &b = m_samples[i + 1];
    const float span = Seconds(a.timeUs, b.timeUs);
    const float jump = Distance(a.state.position, b.state.position);
    if (jump > kTeleportDistance && jump > kTeleportSpeed * span) {
      // A teleport: where it was until then (b takes over once reached).
      view.position = a.state.position;
      view.yaw = YawOf(a.state.orientation);
      view.stance = a.state.stance;
      view.flags = a.state.flags;
      const float dt =
          m_lastViewUs ? std::clamp(Seconds(m_lastViewUs, nowHostUs), 0.f, 0.25f) : 1.f;
      m_lastViewUs = nowHostUs;
      m_speed -= m_speed * std::min(1.f, dt * kSpeedFollowPerSecond);
      view.speed = m_speed;
      return view;
    }
    const float t = std::clamp(Seconds(a.timeUs, render) / span, 0.f, 1.f);
    view.position = Lerp(a.state.position, b.state.position, t);
    view.yaw = LerpAngle(YawOf(a.state.orientation), YawOf(b.state.orientation), t);
    const auto &nearest = t < 0.5f ? a.state : b.state;
    view.stance = nearest.stance;
    view.flags = nearest.flags;
    measured = GroundDistance(a.state.position, b.state.position) / span;
  } else {
    // Past the newest state: carry on along its velocity for a moment, then
    // hold. A teleport's velocity (the jump over one sample) is not motion.
    const auto &sent = last.state.velocity;
    const bool moving = std::hypot(sent.x, sent.y, sent.z) <= kTeleportSpeed;
    const proto::Vec3 v = moving ? sent : proto::Vec3{};
    const float ahead = std::min(Seconds(last.timeUs, render), kExtrapolateUs / 1e6f);
    view.position = {last.state.position.x + v.x * ahead, last.state.position.y + v.y * ahead,
                     last.state.position.z + v.z * ahead};
    view.yaw = YawOf(last.state.orientation);
    view.stance = last.state.stance;
    view.flags = last.state.flags;
    view.extrapolated = true;
    measured = Seconds(last.timeUs, render) <= kExtrapolateUs / 1e6f ? std::hypot(v.x, v.y) : 0.f;
  }

  // Smooth the speed: animation bands must not flicker on a noisy sample.
  const float dt = m_lastViewUs ? std::clamp(Seconds(m_lastViewUs, nowHostUs), 0.f, 0.25f) : 1.f;
  m_lastViewUs = nowHostUs;
  m_speed += (measured - m_speed) * std::min(1.f, dt * kSpeedFollowPerSecond);
  view.speed = m_speed;
  return view;
}

TrackStats Track::Stats() const {
  auto stats = m_stats;
  stats.delayUs = DelayUs();
  stats.jitterUs = static_cast<std::uint64_t>(m_jitterUs);
  stats.transitUs = static_cast<std::uint64_t>(std::max(0.0, m_transitUs));
  stats.intervalUs = static_cast<std::uint64_t>(m_intervalUs);
  return stats;
}
} // namespace tandem::sync
