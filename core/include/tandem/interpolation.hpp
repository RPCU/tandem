#pragma once

// A remote V's recent states, and where to draw it now.
//
// Remote things are drawn slightly in the past, between the two states around
// that moment, so they move smoothly whatever the network does. The delay
// adapts per player: a base of 1.5 send intervals plus margin, widened by the
// measured jitter and transit, capped, and moved towards that at a bounded
// pace (the drawn timeline never skips). Short gaps are extrapolated along
// the last velocity (falling too, when the V is in the air:
// tandem/movement.hpp), then the body holds; when states come back the
// correction fades out instead of snapping (docs/architecture/netcode.md).
// Numbers from NCO's measurements on 2.5 (docs/research/engine-knowledge.md §1).
//
// Times are uint64 microseconds on the host clock. Never put one in a float.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <optional>

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
  std::uint32_t dropped{};   // out of order or duplicates
  std::uint32_t recovered{}; // lost, put back from the next one's `before`
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
  // The drawn timeline runs at most 10 % slower while the delay grows, 3 %
  // faster while it shrinks.
  static constexpr double kDelayGrow = 0.10;
  static constexpr double kDelayCatchUp = 0.03;
  // A correction fades with this time constant; a larger one is a snap.
  static constexpr float kBlendSeconds = 0.1f;
  static constexpr float kMaxBlendM = 3.f;
  static constexpr float kBlendSpeed = 1.5f; // m/s at most: no faster than a walk
  // Asked again this soon (the scripts and the controller each ask once a
  // frame): the view already computed.
  static constexpr std::uint64_t kSameFrameUs = 1'000;

  explicit Track(std::uint32_t sendRateHz = 30);

  // A state from the network. `arrivalHostUs` is when it arrived, on the host clock.
  // `recovered`: one whose datagram was lost, put back from the next one's
  // `before` (it came with it: no arrival of its own to measure). While
  // states are lost, the delay holds one more interval, so the next
  // datagram brings the lost one back before it is drawn: a lost datagram is
  // neither a guess nor a snap (netcode.md, Loss).
  void Push(const proto::PlayerState &state, std::uint64_t arrivalHostUs, bool recovered = false);
  // Loss above this share of the stream (EWMA) widens the delay by an interval.
  static constexpr double kLossMargin = 0.004;

  // Where to draw the player at `nowHostUs` (the delay is applied inside).
  View At(std::uint64_t nowHostUs);

  // The delay drawn with (poses and cars use the same); before the first At,
  // the target.
  std::uint64_t DelayUs() const;
  TrackStats Stats() const;
  bool Empty() const { return m_samples.empty(); }

private:
  struct Sample {
    std::uint64_t timeUs;
    proto::PlayerState state;
  };

  std::uint64_t TargetDelayUs() const;

  std::deque<Sample> m_samples;
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};

  std::uint64_t m_baseUs;
  // EWMA, alpha 1/16, in microseconds.
  double m_jitterUs{};
  double m_transitUs{};
  double m_intervalUs{};
  double m_lastTransitUs{};
  double m_loss{};  // share of states lost (put back), EWMA 1/32
  bool m_lostOne{}; // a recovered one came just before
  std::uint64_t m_lastArrivalUs{};
  bool m_haveTransit{};

  float m_speed{};
  std::uint64_t m_lastViewUs{};
  double m_delayUs{};
  bool m_haveDelay{};
  // What is drawn minus the stream (a correction fading out), and the state
  // last frame's guess went from.
  proto::Vec3 m_offset{};
  struct Guess {
    std::uint64_t timeUs{};
    std::uint32_t seq{};
    proto::PlayerState state;
  };
  std::optional<Guess> m_guess;
  View m_lastView; // asked again within kSameFrameUs: the same, not recomputed
  bool m_pushed{}; // ...unless a state came since

  TrackStats m_stats;
};

// An offset faded for `seconds` with `timeConstant` (corrections), by at
// most `maxSpeed` m/s.
proto::Vec3 Fade(const proto::Vec3 &offset, float seconds, float timeConstant, float maxSpeed);

// Yaw (around Z) of a quaternion, and back.
float YawOf(const proto::Quat &q);
proto::Quat FromYaw(float yaw);
} // namespace tandem::sync
