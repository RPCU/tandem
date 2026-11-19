#pragma once

// A V in another world (docs/architecture/elsewhere.md, other worlds), the parts that
// need no game: what a received state may say, and the owner's side, which
// tells when the scene has taken V away from its body and where that body is.
//
// The game decides the place (cyberspace, a braindance, Johnny, someone else:
// scripts/tandem/Elsewhere.reds); the tracker watches V's position. A jump of
// more than kJumpMetres between two samples, outside a vehicle, is a
// teleport: V's body stays where V was just before it (the anchor). A
// teleport is the scene's other world when a place other than the city comes
// with it, or within kLateWorldUs after it; a teleport with none for
// kTeleportUs was a teleport in the city (fast travel, a quest moving V) and
// the body goes with V. In cyberspace and in a braindance V's entity is that
// world's camera (a braindance flies it, final.reds:515134): the body stays
// where the world began even with no jump. Back from the world (its place
// ends), the body follows V again as soon as V is near the anchor or jumps
// (the teleport back), or kReturnUs after the end at the latest.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace tandem::elsewhere {
inline constexpr float kJumpMetres = 50.f;
inline constexpr std::uint64_t kTeleportUs = 3'000'000;
inline constexpr std::uint64_t kLateWorldUs = 4'000'000;
inline constexpr std::uint64_t kReturnUs = 3'000'000;
// How far back our own poses are kept (the pose of the body as it was left).
inline constexpr std::uint64_t kPoseHistoryUs = 5'000'000;

// What a received state may say: an unknown place is the city, the city has
// no flags, settled only with moved, a broken anchor is no place to show a
// body at (moved, not settled: hidden).
proto::Elsewhere Sanitized(proto::Elsewhere state);

// Worlds whose start leaves the body behind, teleport or not.
bool LeavesBody(proto::Place place);

// One sample of our V, every state sent (30 Hz).
struct Sample {
  std::uint64_t timeUs{};
  proto::Vec3 position;
  proto::Quat orientation;
  bool mounted{}; // in a vehicle: its jumps are the vehicle's
  bool settled{}; // sitting or lying (a workspot)
  proto::Place place{};
};

// What to send of our V for this sample.
struct Body {
  bool held{}; // the body is elsewhere: send `position`, `orientation` (not V's), no velocity
  proto::Vec3 position;
  proto::Quat orientation;
  // The body was just left: hold its pose as it was at this time (the
  // sample's clock).
  std::optional<std::uint64_t> holdPoseFrom;
  bool releasePose{}; // back in the body: the live pose again
};

class Tracker {
public:
  Body Feed(const Sample &sample);
  // What the others are told (player unset). Changes only with the place or
  // the flags, not with every sample.
  const proto::Elsewhere &State() const { return m_state; }
  // The body is held, an unexplained teleport included.
  bool Holding() const { return m_moved; }
  void Reset() { *this = {}; }

private:
  struct Kept {
    std::uint64_t timeUs{};
    proto::Vec3 position;
    proto::Quat orientation;
    bool settled{};
  };
  void Leave(const Kept &body, std::uint64_t nowUs, bool confirmed, Body &out);
  void Return(const Sample &sample, Body &out);

  std::optional<Sample> m_last;                             // the previous sample
  std::optional<Kept> m_real;                               // the last sample of V with its body
  std::optional<std::pair<std::uint64_t, Kept>> m_lastJump; // when, and V just before it
  bool m_moved{}, m_confirmed{};
  std::uint64_t m_movedAtUs{}, m_endedAtUs{};
  proto::Place m_place{}; // as told (kept on the way back)
  Kept m_anchor;
  proto::Elsewhere m_state;
};

// Our own V's recent poses (the plugin's pose stream), to hold the body's
// pose as it was before the jump. Keeps kPoseHistoryUs behind the newest.
class PoseHistory {
public:
  void Push(std::uint64_t timeUs, const proto::PoseState &pose);
  // The newest pose at or before `timeUs` (the oldest kept when all are
  // newer); nullopt when there is none.
  std::optional<proto::PoseState> At(std::uint64_t timeUs) const;
  std::size_t Size() const { return m_poses.size(); }

private:
  std::deque<std::pair<std::uint64_t, proto::PoseState>> m_poses;
};
} // namespace tandem::elsewhere
