#pragma once

// How a V moves, beyond walking: what its game sends, and how a puppet's
// body is placed from it.
//
// The sender reads its player state machine (the PSM blackboard,
// final.reds `PlayerStateMachineDef`) and packs what matters for placement
// into PlayerState::flags. The field existed and was always sent as 0, so
// the wire format is unchanged (no protocol version bump); kMoveKnown tells
// a packed value from a sender that fills nothing (an NPC state, an older
// mock player): those are placed as walking, as before.
//
// The puppet's movement controller then asks a Placer, every frame, how far
// to move the body: on the ground as before; airborne, climbing, on a ladder,
// swimming or riding a platform it follows the streamed position exactly (no
// ground to keep to), eases back onto the ground on landing, and asks for a
// teleport when the body is left behind (a ledge an NPC body cannot climb, a
// body the engine refused to move).

#include "tandem/protocol.hpp"

#include <cstdint>

namespace tandem::movement {
// gamePSMDetailedLocomotionStates (final.reds), PSM `LocomotionDetailed`.
enum class Detailed : std::uint8_t {
  NotInBaseLocomotion = 0,
  Stand = 1,
  AimWalk = 2,
  Crouch = 3,
  Sprint = 4,
  Slide = 5,
  SlideFall = 6,
  Dodge = 7,
  Climb = 8,
  Vault = 9,
  Ladder = 10,
  LadderSprint = 11,
  LadderSlide = 12,
  LadderJump = 13,
  Fall = 14,
  AirThrusters = 15,
  AirHover = 16,
  SuperheroFall = 17,
  Jump = 18,
  DoubleJump = 19,
  ChargeJump = 20,
  HoverJump = 21,
  DodgeAir = 22,
  RegularLand = 23,
  HardLand = 24,
  VeryHardLand = 25,
  DeathLand = 26,
  SuperheroLand = 27,
  SuperheroLandRecovery = 28,
  Knockdown = 29,
  CrouchSprint = 30,
  Felled = 31,
};

// gamePSMSwimming, PSM `Swimming`: Climbing is getting out of the water.
enum class Swim : std::uint8_t { None = 0, Surface = 1, Diving = 2, Climbing = 3 };

// PlayerState::flags.
inline constexpr std::uint16_t kDetailedMask = 0x1F; // bits 0-4: Detailed
inline constexpr unsigned kSwimShift = 5;            // bits 5-6: Swim
inline constexpr std::uint16_t kSwimMask = 0x3 << kSwimShift;
inline constexpr std::uint16_t kMoveElevator = 1 << 7; // PSM IsPlayerInsideMovingElevator
inline constexpr std::uint16_t kMoveWorkspot = 1 << 8; // PSM IsInWorkspot (seated, using a device)
inline constexpr std::uint16_t kMoveFelled = 1 << 9;   // PSM Felled (knocked down)
inline constexpr std::uint16_t kMoveKnown = 1 << 15;   // the sender filled the bits above

struct Movement {
  bool known{};
  Detailed detailed{};
  Swim swim{};
  bool elevator{}, workspot{}, felled{};
  friend bool operator==(const Movement &, const Movement &) = default;
};

std::uint16_t Pack(const Movement &movement);
Movement Unpack(std::uint16_t flags);

// How a puppet's body is placed.
enum class Placement : std::uint8_t {
  Ground,   // walking, running, crouching, sliding, dodging, landing
  Air,      // jumps, falls, air dashes, hovering
  Climb,    // vaulting, mantling a ledge, climbing out of the water
  Ladder,   // on a ladder
  Swim,     // on the surface or diving
  Platform, // in a moving elevator or a workspot: carried by something
  Down,     // knocked down, felled: the body is the physics'
};

Placement PlacementOf(const Movement &movement);
inline Placement PlacementOf(std::uint16_t flags) { return PlacementOf(Unpack(flags)); }
// Everywhere but on the ground the body follows the stream exactly.
inline bool FollowsFreely(Placement placement) { return placement != Placement::Ground; }
// Whether a walk/run cycle suits the body (in the air it keeps its stride).
inline bool Walks(Placement placement) {
  return placement == Placement::Ground || placement == Placement::Air;
}
const char *NameOf(Placement placement);

// One body's placement, frame after frame (the animation thread's).
class Placer {
public:
  // Free placement: a body this far from its target for this long was left
  // behind (the engine refused the move): teleport it.
  static constexpr float kFreeGap = 0.75f;       // m
  static constexpr float kFreeGapSeconds = 0.2f; // s
  // On the ground: the target above the body by more than a step (a ledge
  // the remote V climbed or jumped onto) for this long.
  static constexpr float kLedgeRise = 0.6f;        // m
  static constexpr float kLedgeSeconds = 0.3f;     // s
  static constexpr float kGroundGap = 2.5f;        // m, for kGroundGapSeconds
  static constexpr float kGroundGapSeconds = 0.5f; // s
  static constexpr float kFarGap = 8.f;            // m: at once
  static constexpr float kSnapCooldown = 1.f;      // s between two teleports
  // Landing: the height left between the body and the ground target is
  // taken back with this time constant instead of in one frame.
  static constexpr float kLandEaseSeconds = 0.08f;
  static constexpr float kLandSeconds = 0.35f; // how long a landing eases

  struct Step {
    proto::Vec3 delta; // move the body by this, this frame
    bool snap{};       // ...and teleport it to the target
    Placement placement{};
    bool changed{}; // the placement is not last frame's
  };

  // `body` where the body is, `target` where the stream says, `dt` seconds
  // since the last call.
  Step Update(const proto::Vec3 &body, const proto::Vec3 &target, Placement placement, float dt);

  std::uint32_t Snaps() const { return m_snaps; }
  Placement Current() const { return m_placement; }

private:
  Placement m_placement{Placement::Ground};
  bool m_started{};
  float m_gapSeconds{};                 // free: time beyond kFreeGap; ground: beyond kGroundGap
  float m_ledgeSeconds{};               // ground: time with the target a ledge above
  float m_landingSeconds{kLandSeconds}; // since the last landing
  float m_sinceSnap{kSnapCooldown};
  std::uint32_t m_snaps{};
};
} // namespace tandem::movement
