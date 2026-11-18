#pragma once

// A remote V's recent states, and where to draw it now.
//
// Remote things are drawn slightly in the past, between the two states around
// that moment, so they move smoothly whatever the network does. The delay
// adapts per player: a base of 1.5 send intervals plus margin, widened by the
// measured jitter and transit, capped. Short gaps are extrapolated along the
// last velocity, then the body holds; when states come back it blends home.
// Numbers from NCO's measurements on 2.5 (docs/research/engine-knowledge.md §1).
//
// Times are uint64 microseconds on the host clock. Never put one in a float.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>

namespace tandem::sync {
struct View {
  bool valid{};
  proto::Vec3 position;
  float yaw{};   // radians around Z (the game's up axis), 0 = facing +Y
  float speed{}; // m/s over the ground, smoothed
  std::uint8_t stance{};
  std::uint16_t flags{};
  bool extrapolated{};
  // How many times the player jumped (fast travel, a skipped ride, a
  // teleport): the body is to be put there at once, not walked across.
  std::uint32_t jumps{};
};

struct TrackStats {
  std::uint64_t delayUs{};
  std::uint64_t jitterUs{};
  std::uint64_t transitUs{};
  std::uint64_t intervalUs{};
  std::uint32_t received{};
  std::uint32_t dropped{}; // out of order or duplicates
};

class Track {
public:
  static constexpr std::uint64_t kExtrapolateUs = 250'000;
  static constexpr std::uint64_t kMaxDelayUs = 400'000;
  static constexpr std::uint64_t kMaxJitterExtraUs = 100'000;
  static constexpr std::size_t kMaxSamples = 64;
  // A jump: further than this between two states, or further than
  // kJumpMinMetres at more than kJumpSpeed (no V runs or drives that fast;
  // a fast-forwarded train, 10 times its speed, stays under it). The track
  // starts again from the new state, standing still.
  static constexpr float kJumpMetres = 200.f;
  static constexpr float kJumpMinMetres = 30.f;
  static constexpr float kJumpSpeed = 150.f;

  explicit Track(std::uint32_t sendRateHz = 30);

  // A state from the network. `arrivalHostUs` is when it arrived, on the host clock.
  void Push(const proto::PlayerState &state, std::uint64_t arrivalHostUs);

  // Where to draw the player at `nowHostUs` (the delay is applied inside).
  View At(std::uint64_t nowHostUs);

  std::uint64_t DelayUs() const;
  TrackStats Stats() const;
  bool Empty() const { return m_samples.empty(); }
  std::uint32_t Jumps() const { return m_jumps; }

private:
  struct Sample {
    std::uint64_t timeUs;
    proto::PlayerState state;
  };

  std::deque<Sample> m_samples;
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};

  std::uint64_t m_baseUs;
  // EWMA, alpha 1/16, in microseconds.
  double m_jitterUs{};
  double m_transitUs{};
  double m_intervalUs{};
  double m_lastTransitUs{};
  std::uint64_t m_lastArrivalUs{};
  bool m_haveTransit{};

  float m_speed{};
  std::uint64_t m_lastViewUs{};
  std::uint32_t m_jumps{};

  TrackStats m_stats;
};

// Yaw (around Z) of a quaternion, and back.
float YawOf(const proto::Quat &q);
proto::Quat FromYaw(float yaw);
} // namespace tandem::sync
