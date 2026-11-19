#include "tandem/interpolation.hpp"

#include "tandem/movement.hpp"

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
// A V in the air keeps falling while its states are late (m/s², assumed: the
// game's own value for V was not measured).
constexpr float kAirGravity = 9.81f;

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

float Length(const proto::Vec3 &v) { return std::hypot(v.x, v.y, v.z); }

// Seconds between two host times: only differences become floating point.
float Seconds(std::uint64_t from, std::uint64_t to) {
  return to >= from ? static_cast<float>(to - from) / 1e6f : -static_cast<float>(from - to) / 1e6f;
}

// Standing in both: their gap is the sender's still rate, not the stream's.
bool BothStill(const proto::PlayerState &a, const proto::PlayerState &b) {
  return Distance(a.position, b.position) < 0.01f && Length(a.velocity) < 0.05f &&
         Length(b.velocity) < 0.05f;
}
} // namespace

float YawOf(const proto::Quat &q) {
  return std::atan2(2.f * (q.r * q.k + q.i * q.j), 1.f - 2.f * (q.j * q.j + q.k * q.k));
}

proto::Quat FromYaw(float yaw) { return {0.f, 0.f, std::sin(yaw / 2.f), std::cos(yaw / 2.f)}; }

proto::Vec3 Fade(const proto::Vec3 &offset, float seconds, float timeConstant, float maxSpeed) {
  const float t = std::max(seconds, 0.f);
  const float length = Length(offset);
  if (length <= 0.f)
    return offset;
  // Exponentially, but the change capped at maxSpeed.
  const float faded = std::max(length * std::exp(-t / timeConstant), length - maxSpeed * t);
  const float keep = faded / length;
  return {offset.x * keep, offset.y * keep, offset.z * keep};
}

Track::Track(std::uint32_t sendRateHz)
    // 15 ms + 1.5 send intervals: 65 ms at 30 Hz.
    : m_baseUs(15'000 + 1'500'000 / std::max<std::uint32_t>(sendRateHz, 1)) {}

void Track::Push(const proto::PlayerState &stateIn, std::uint64_t arrivalHostUs) {
  // Drop what is older than what we have: newest wins on the state channel.
  if (m_haveSeq && static_cast<std::int32_t>(stateIn.seq - m_lastSeq) <= 0) {
    ++m_stats.dropped;
    return;
  }
  if (stateIn.timeUs == 0 || (!m_samples.empty() && stateIn.timeUs <= m_samples.back().timeUs)) {
    ++m_stats.dropped;
    return;
  }
  m_haveSeq = true;
  m_lastSeq = stateIn.seq;
  ++m_stats.received;

  auto state = stateIn;
  // No velocity sent (an NPC's state): the one between the last two
  // states, so a late state is carried on rather than held.
  if (!m_samples.empty() && state.velocity == proto::Vec3{}) {
    const auto &last = m_samples.back();
    const float span = Seconds(last.timeUs, state.timeUs);
    const float moved = Distance(last.state.position, state.position);
    if (span > 0.f && span < 0.3f && moved > 0.01f && moved < kTeleportSpeed * span)
      state.velocity = {(state.position.x - last.state.position.x) / span,
                        (state.position.y - last.state.position.y) / span,
                        (state.position.z - last.state.position.z) / span};
  }

  // Transit = arrival - send time (both host clock). Its frame-to-frame change
  // is the jitter (RFC 3550); the clock offset error cancels out.
  const double transit =
      static_cast<double>(static_cast<std::int64_t>(arrivalHostUs - state.timeUs));
  if (m_haveTransit) {
    const auto gap = arrivalHostUs - m_lastArrivalUs;
    if (gap < kIgnoreGapUs) {
      m_jitterUs += (std::abs(transit - m_lastTransitUs) - m_jitterUs) * kAlpha;
      // A still sender's slow rate is not the stream's: the delay needs no
      // room for it (a still body is drawn right whatever comes when).
      if (m_samples.empty() || !BothStill(m_samples.back().state, state))
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
  m_pushed = true;
}

std::uint64_t Track::TargetDelayUs() const {
  const auto jitterExtra =
      std::min<std::uint64_t>(static_cast<std::uint64_t>(2.0 * m_jitterUs), kMaxJitterExtraUs);
  const auto base = m_baseUs + jitterExtra;
  // Enough to always hold the next state: it is in flight for `transit` and
  // comes `interval` after the last.
  const auto needed =
      static_cast<std::uint64_t>(std::max(0.0, m_transitUs) + m_intervalUs) + jitterExtra;
  return std::min(std::max(base, needed), std::max(kMaxDelayUs, base));
}

std::uint64_t Track::DelayUs() const {
  return m_haveDelay ? static_cast<std::uint64_t>(m_delayUs) : TargetDelayUs();
}

namespace {
// Past the newest state: along its velocity for a moment, then held. A
// teleport's velocity (the jump over one sample) is not motion.
proto::Vec3 Extrapolate(const proto::PlayerState &last, std::uint64_t lastUs,
                        std::uint64_t renderUs, float &ahead, bool &moving) {
  const auto &sent = last.velocity;
  moving = std::hypot(sent.x, sent.y, sent.z) <= Track::kTeleportSpeed;
  const proto::Vec3 v = moving ? sent : proto::Vec3{};
  ahead = std::clamp(Seconds(lastUs, renderUs), 0.f, Track::kExtrapolateUs / 1e6f);
  // In the air it keeps falling (not across a teleport).
  const float fall = moving && movement::PlacementOf(last.flags) == movement::Placement::Air
                         ? 0.5f * kAirGravity * ahead * ahead
                         : 0.f;
  return {last.position.x + v.x * ahead, last.position.y + v.y * ahead,
          last.position.z + v.z * ahead - fall};
}
} // namespace

View Track::At(std::uint64_t nowHostUs) {
  View view;
  if (m_samples.empty())
    return view;
  if (m_lastView.valid && nowHostUs >= m_lastViewUs && nowHostUs - m_lastViewUs < kSameFrameUs &&
      !m_pushed)
    return m_lastView;
  m_pushed = false;
  view.valid = true;

  // The drawn timeline: the delay moves to its target at a bounded pace, so
  // what is drawn never jumps in time (a slower or quicker timeline for a
  // moment, not a skip).
  const float dt = m_lastViewUs ? std::clamp(Seconds(m_lastViewUs, nowHostUs), 0.f, 0.25f) : 1.f;
  const auto target = static_cast<double>(TargetDelayUs());
  if (!m_haveDelay || nowHostUs - m_lastViewUs > 1'000'000) {
    m_delayUs = target;
    m_haveDelay = true;
  } else {
    const double elapsed = static_cast<double>(dt) * 1e6;
    m_delayUs += std::clamp(target - m_delayUs, -elapsed * kDelayCatchUp, elapsed * kDelayGrow);
  }
  m_lastViewUs = nowHostUs;
  const auto delay = static_cast<std::uint64_t>(m_delayUs);
  const auto render = nowHostUs > delay ? nowHostUs - delay : 0;

  // Older states than the one just before `render` will never be needed.
  while (m_samples.size() > 2 && m_samples[1].timeUs <= render)
    m_samples.pop_front();

  const auto &first = m_samples.front();
  const auto &last = m_samples.back();
  float measured = 0.f;
  bool teleport = false;

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
      teleport = true;
    } else {
      const float t = std::clamp(Seconds(a.timeUs, render) / span, 0.f, 1.f);
      view.position = Lerp(a.state.position, b.state.position, t);
      view.yaw = LerpAngle(YawOf(a.state.orientation), YawOf(b.state.orientation), t);
      const auto &nearest = t < 0.5f ? a.state : b.state;
      view.stance = nearest.stance;
      view.flags = nearest.flags;
      measured = GroundDistance(a.state.position, b.state.position) / span;
    }
  } else {
    float ahead = 0.f;
    bool moving = false;
    view.position = Extrapolate(last.state, last.timeUs, render, ahead, moving);
    view.yaw = YawOf(last.state.orientation);
    view.stance = last.state.stance;
    view.flags = last.state.flags;
    view.extrapolated = true;
    const proto::Vec3 v = moving ? last.state.velocity : proto::Vec3{};
    measured = Seconds(last.timeUs, render) <= kExtrapolateUs / 1e6f ? std::hypot(v.x, v.y) : 0.f;
  }

  // A correction (states again after a guess along the last velocity, or a
  // newer state to guess from) is not drawn as a snap: the gap between the
  // guess we would draw now and the truth fades out over kBlendSeconds.
  if (m_guess && !teleport && (!view.extrapolated || m_guess->seq != last.state.seq)) {
    float ahead = 0.f;
    bool moving = false;
    const auto guess = Extrapolate(m_guess->state, m_guess->timeUs, render, ahead, moving);
    const proto::Vec3 gap{guess.x + m_offset.x - view.position.x,
                          guess.y + m_offset.y - view.position.y,
                          guess.z + m_offset.z - view.position.z};
    m_offset = Length(gap) <= kMaxBlendM ? gap : proto::Vec3{};
  } else if (teleport) {
    m_offset = {};
  } else {
    m_offset = Fade(m_offset, dt, kBlendSeconds, kBlendSpeed);
  }
  if (view.extrapolated)
    m_guess = Guess{last.timeUs, last.state.seq, last.state};
  else
    m_guess.reset();
  view.position = {view.position.x + m_offset.x, view.position.y + m_offset.y,
                   view.position.z + m_offset.z};

  // Smooth the speed: animation bands must not flicker on a noisy sample.
  if (teleport)
    m_speed -= m_speed * std::min(1.f, dt * kSpeedFollowPerSecond);
  else
    m_speed += (measured - m_speed) * std::min(1.f, dt * kSpeedFollowPerSecond);
  view.speed = m_speed;
  m_lastView = view;
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
