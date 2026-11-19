#pragma once

// A remote driver's car: its recent states, and where to draw the copy now.
// Drawn in the past by the driver's own delay (the same as their body, so
// the driver stays in the seat), on a cubic through the two states around
// that moment and their velocities; short gaps are extrapolated along the
// velocity (500 ms, as docs/architecture/smoothing.md says for vehicles),
// then the car holds; a correction after a gap fades out, as for a body.
//
// And whose wheel it is (docs/architecture/garage.md): who may drive a
// partner's car.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <optional>

namespace tandem::sync {
struct VehicleView {
  bool valid{};
  proto::Vec3 position;
  proto::Quat orientation;
  proto::Vec3 velocity;
  bool extrapolated{};
};

// May a partner take the wheel of `owner`'s car? Its owner's word
// (proto::CarLend; Crew when it said none), whether the owner sits in that
// car now, and whether the owner is still in the session (a car whose owner
// left is no one's: nobody drives it off).
struct DriveAsk {
  proto::Lend lend{proto::Lend::Crew};
  bool ownerAboard{};
  bool ownerInSession{};
};
bool MayDrive(const DriveAsk &ask);

class VehicleTrack {
public:
  static constexpr std::uint64_t kExtrapolateUs = 500'000;
  static constexpr std::size_t kMaxSamples = 32;
  static constexpr float kMaxBlendM = 5.f;  // a bigger correction is a snap
  static constexpr float kBlendSpeed = 6.f; // m/s at most

  void Push(const proto::VehicleState &state);
  // Where the car is drawn at `renderUs` (host clock, the body's delay
  // applied). Called once a frame: it fades corrections as time goes.
  VehicleView At(std::uint64_t renderUs);
  bool Empty() const { return m_samples.empty(); }

private:
  std::deque<proto::VehicleState> m_samples;
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};
  std::uint64_t m_lastRenderUs{};
  proto::Vec3 m_offset{};
  std::optional<proto::VehicleState> m_guess;
};
} // namespace tandem::sync
