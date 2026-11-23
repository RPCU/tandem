#pragma once

// World vehicles shared by their area's owner (docs/architecture/traffic.md):
// how a moving one is packed into a TrafficEntry, and which ones go into each
// TrafficStates when the street holds more than a packet does.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace tandem::traffic {
// A rotation in 6 bytes: i, j, k of the unit quaternion with r >= 0 (q and -q
// are the same rotation), each in 1/32767. r comes back as the rest of the
// unit length. The error stays under 0.01 degree.
std::array<std::int16_t, 3> PackOrientation(const proto::Quat &orientation);
proto::Quat UnpackOrientation(const std::array<std::int16_t, 3> &packed);

// A velocity in cm/s, clamped to +-327 m/s (nothing on a road goes faster).
std::array<std::int16_t, 3> PackVelocity(const proto::Vec3 &velocity);
proto::Vec3 UnpackVelocity(const std::array<std::int16_t, 3> &packed);

// A shared vehicle as its owner's game has it now.
struct Car {
  proto::TrafficId id{};
  proto::Vec3 position;
  proto::Quat orientation;
  proto::Vec3 velocity;
};

proto::TrafficEntry Pack(const Car &car);
Car Unpack(const proto::TrafficEntry &entry);

// How far from a player a world vehicle is shared, and how far it must go
// before it is dropped again (hysteresis: a car at the edge does not flicker).
inline constexpr float kShareRange = 120.f;
inline constexpr float kDropRange = 150.f;
// The most proxies a game is asked to show at once.
inline constexpr std::size_t kMaxShared = 48;
// Slower than this for kRestUs, a car is at rest: it leaves the stream until
// it moves again (one last state says where it stopped).
inline constexpr float kRestSpeed = 0.15f;

// Picks what goes into this round's TrafficStates. Every car that moved
// since it was last sent is a candidate; when they do not all fit, the ones
// nearest to a player go every round and the others take turns in what is
// left, so a far car is refreshed less often but never starved. Cars at
// rest are sent once when they stop, then once every `kRestEvery` rounds
// (the State channel may have lost the last one).
class Budget {
public:
  static constexpr std::uint32_t kRestEvery = 20; // 2 s at 10 Hz
  // The share of a packet the nearest cars always get.
  static constexpr std::size_t kNearShare = 3; // of 4

  // `viewers`: where the players are. Returns at most `max` entries.
  std::vector<proto::TrafficEntry> Pick(std::span<const Car> cars,
                                        std::span<const proto::Vec3> viewers,
                                        std::size_t max = proto::kMaxTrafficPerState);
  // A car is gone: forget what was said of it.
  void Forget(proto::TrafficId id);
  void Clear();

private:
  struct Said {
    proto::Vec3 position;
    std::uint32_t round{};
    bool resting{};
  };
  std::map<proto::TrafficId, Said> m_said;
  std::uint32_t m_round{};
};

// Between two rounds (30 times a second): the moving cars within kNearRange
// of a player, nearest first, at most `max` (traffic.md, Near either V: the
// kinematic path follows them closely where a V could touch them).
inline constexpr float kNearRange = 50.f;
inline constexpr std::size_t kNearMax = 16;
std::vector<proto::TrafficEntry> PickNear(std::span<const Car> cars,
                                          std::span<const proto::Vec3> viewers,
                                          std::size_t max = kNearMax);

// Whether a world vehicle at `distance` metres from the nearest player is
// shared, given whether it is now.
constexpr bool InRange(float distance, bool shared) {
  return distance <= (shared ? kDropRange : kShareRange);
}

// The distance from `point` to the nearest of `viewers` (a large number
// when there is none).
float Nearest(const proto::Vec3 &point, std::span<const proto::Vec3> viewers);
} // namespace tandem::traffic
