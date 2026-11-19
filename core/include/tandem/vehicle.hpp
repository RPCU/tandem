#pragma once

// A remote driver's car: its recent states, and where to draw the copy now.
// Drawn in the past by the driver's own delay (the same as their body, so
// the driver stays in the seat), interpolated between the two states around
// that moment; short gaps are extrapolated along the velocity (500 ms, as
// docs/architecture/smoothing.md says for vehicles), then the car holds.
//
// And whose wheel it is (docs/architecture/garage.md): who may drive a
// partner's car.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>

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

  void Push(const proto::VehicleState &state);
  VehicleView At(std::uint64_t renderUs) const;
  bool Empty() const { return m_samples.empty(); }

private:
  std::deque<proto::VehicleState> m_samples;
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};
};
} // namespace tandem::sync
