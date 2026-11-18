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
  // Faster than this between two states is a teleport (fast travel, a
  // quest, a regroup), not motion: the body is not flown across the map, it
  // stays until the moment of the jump and is then put there; a velocity
  // that fast is not extrapolated.
  static constexpr float kTeleportSpeed = 150.f;   // m/s
  static constexpr float kTeleportDistance = 20.f; // m

  explicit Track(std::uint32_t sendRateHz = 30);

  // A state from the network. `arrivalHostUs` is when it arrived, on the host clock.
  void Push(const proto::PlayerState &state, std::uint64_t arrivalHostUs);

  // Where to draw the player at `nowHostUs` (the delay is applied inside).
  View At(std::uint64_t nowHostUs);

  std::uint64_t DelayUs() const;
  TrackStats Stats() const;
  bool Empty() const { return m_samples.empty(); }

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

  TrackStats m_stats;
};

// Yaw (around Z) of a quaternion, and back.
float YawOf(const proto::Quat &q);
proto::Quat FromYaw(float yaw);
} // namespace tandem::sync
