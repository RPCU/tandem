#include "tandem/movement.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::movement {
std::uint16_t Pack(const Movement &movement) {
  if (!movement.known)
    return 0;
  auto flags =
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(movement.detailed) & kDetailedMask);
  flags |= static_cast<std::uint16_t>((static_cast<std::uint16_t>(movement.swim) << kSwimShift) &
                                      kSwimMask);
  if (movement.elevator)
    flags |= kMoveElevator;
  if (movement.workspot)
    flags |= kMoveWorkspot;
  if (movement.felled)
    flags |= kMoveFelled;
  return flags | kMoveKnown;
}

Movement Unpack(std::uint16_t flags) {
  if (!(flags & kMoveKnown))
    return {};
  return {.known = true,
          .detailed = static_cast<Detailed>(flags & kDetailedMask),
          .swim = static_cast<Swim>((flags & kSwimMask) >> kSwimShift),
          .elevator = (flags & kMoveElevator) != 0,
          .workspot = (flags & kMoveWorkspot) != 0,
          .felled = (flags & kMoveFelled) != 0};
}

Placement PlacementOf(const Movement &m) {
  if (!m.known)
    return Placement::Ground;
  // The swimming state machine runs instead of the locomotion one: its
  // LocomotionDetailed is whatever was last set before the water.
  if (m.swim == Swim::Climbing)
    return Placement::Climb;
  if (m.swim != Swim::None)
    return Placement::Swim;
  if (m.felled || m.detailed == Detailed::Knockdown || m.detailed == Detailed::Felled)
    return Placement::Down;
  if (m.elevator || m.workspot)
    return Placement::Platform;
  switch (m.detailed) {
  case Detailed::Climb:
  case Detailed::Vault:
    return Placement::Climb;
  case Detailed::Ladder:
  case Detailed::LadderSprint:
  case Detailed::LadderSlide:
    return Placement::Ladder;
  case Detailed::LadderJump: // off the ladder, into the air
  case Detailed::SlideFall:  // slid off a ledge
  case Detailed::Fall:
  case Detailed::AirThrusters:
  case Detailed::AirHover:
  case Detailed::SuperheroFall:
  case Detailed::Jump:
  case Detailed::DoubleJump:
  case Detailed::ChargeJump:
  case Detailed::HoverJump:
  case Detailed::DodgeAir:
    return Placement::Air;
  default:
    return Placement::Ground;
  }
}

const char *NameOf(Placement placement) {
  switch (placement) {
  case Placement::Ground:
    return "ground";
  case Placement::Air:
    return "air";
  case Placement::Climb:
    return "climb";
  case Placement::Ladder:
    return "ladder";
  case Placement::Swim:
    return "swim";
  case Placement::Platform:
    return "platform";
  case Placement::Down:
    return "down";
  }
  return "?";
}

Placer::Step Placer::Update(const proto::Vec3 &body, const proto::Vec3 &target, Placement placement,
                            float dt) {
  dt = std::clamp(dt, 0.f, 0.25f);
  Step step;
  step.placement = placement;
  step.changed = m_started && placement != m_placement;
  if (step.changed) {
    if (FollowsFreely(m_placement) && !FollowsFreely(placement))
      m_landingSeconds = 0.f;
    m_gapSeconds = m_ledgeSeconds = 0.f;
  }
  m_placement = placement;
  m_started = true;
  m_sinceSnap += dt;

  step.delta = {target.x - body.x, target.y - body.y, target.z - body.z};
  const float gap = std::hypot(step.delta.x, step.delta.y, step.delta.z);

  bool snap = gap > kFarGap;
  if (FollowsFreely(placement)) {
    // Exactly where the stream says: nothing to keep to.
    m_gapSeconds = gap > kFreeGap ? m_gapSeconds + dt : 0.f;
    snap = snap || m_gapSeconds >= kFreeGapSeconds;
  } else {
    m_gapSeconds = gap > kGroundGap ? m_gapSeconds + dt : 0.f;
    m_ledgeSeconds = step.delta.z > kLedgeRise ? m_ledgeSeconds + dt : 0.f;
    snap = snap || m_gapSeconds >= kGroundGapSeconds || m_ledgeSeconds >= kLedgeSeconds;
    // Just landed: the height still between the body and its ground target
    // (the body was held up or down in the air) is taken back smoothly.
    if (m_landingSeconds < kLandSeconds) {
      step.delta.z *= 1.f - std::exp(-dt / kLandEaseSeconds);
      m_landingSeconds += dt;
    }
  }

  if (snap && m_sinceSnap >= kSnapCooldown) {
    step.snap = true;
    step.delta = {target.x - body.x, target.y - body.y, target.z - body.z};
    m_sinceSnap = 0.f;
    m_gapSeconds = m_ledgeSeconds = 0.f;
    m_landingSeconds = kLandSeconds;
    ++m_snaps;
  }
  return step;
}
} // namespace tandem::movement
