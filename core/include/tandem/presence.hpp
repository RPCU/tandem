#pragma once

// Presence: what a player perceives of their partner beyond the body's pose
// (docs/architecture/presence.md). The pose stream shows a remote V; this
// is what makes it heard and read: the sounds a puppet must make, the
// flinch of a puppet that is hit, and the party HUD's state.
//
// Most of it is derived on the receiving game from messages that exist
// already, so it costs nothing on the wire: footsteps, jumps and landings
// from PlayerState (position and movement flags), draws and holsters from
// Held, pain from Presence's health, going down from Vitals, time abilities
// from TimeEffect. Only what the receiver cannot know is sent: where a
// shot ended and what a melee swing landed on (Cue), health and the menu
// (Presence), the status effects that show on a V (StatusVisuals).

#include "tandem/movement.hpp"
#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tandem::presence {
// ---- what a puppet must be heard doing ----

// How a V walks: it picks the footstep the game plays (its own locomotion
// audio has the same four).
enum class Gait : std::uint8_t { Sneak = 0, Walk = 1, Run = 2, Sprint = 3 };

enum class Kind : std::uint8_t {
  Step = 1,        // arg: Gait | 0x10 for the left foot | 0x20 on a ladder
  Jump = 2,        // arg: 1 a double or charged jump
  Land = 3,        // arg: 0 regular, 1 hard, 2 very hard
  Draw = 4,        // a weapon came into its right hand
  Holster = 5,     // its right hand is empty again
  Hurt = 6,        // arg: 0 light, 1 medium, 2 heavy (share of its health lost)
  Down = 7,        // it went down or out: its death cry
  Up = 8,          // it is back up
  AbilityOn = 9,   // arg: the proto::kTime* bits that began
  AbilityOff = 10, // arg: the bits that ended
  Climb = 11,      // it grabbed a ledge
  Vault = 12,      // it went over an obstacle
  Dash = 13,       // a dodge or dash; arg: 1 in the air
  SlideEnd = 14,   // a slide came to its end
  // Water (movement::Swim, the state machine's own word):
  WaterEnter = 15, // arg: kWater* (under at once; already in when first seen)
  WaterExit = 16,  // out of the water: onto the ground, or climbing out
  Dive = 17,       // from the surface under it
  Surface = 18,    // from under it back to the surface
  Stroke = 19,     // one swimming stroke; arg: kStroke*
};
inline constexpr std::uint8_t kStepLeft = 0x10, kStepLadder = 0x20, kStepGaitMask = 0x0F;
inline constexpr std::uint8_t kWaterUnder = 1; // WaterEnter: straight under the surface
inline constexpr std::uint8_t kWaterQuiet = 2; // WaterEnter: it swam before we saw it (no splash)
inline constexpr std::uint8_t kStrokeDiving = 1, kStrokeFast = 2;

struct Event {
  Kind kind{};
  std::uint8_t arg{};
  friend bool operator==(const Event &, const Event &) = default;
};

// One remote V, watched through what its game already sends. The first
// value of each input is where it starts from: someone who joins hears
// nothing for a weapon that was drawn before they came.
class Observer {
public:
  // Distance between two foot plants (m), by gait, as the game's own
  // third-person locomotion clips stride.
  static constexpr float kStride[4] = {0.55f, 0.72f, 1.15f, 1.55f};
  static constexpr float kRung = 0.45f;     // a ladder: one hand or foot per rung
  static constexpr float kIdleSpeed = 0.3f; // m/s: below it nobody walks
  static constexpr float kRunSpeed = 2.6f;  // m/s: walk below, run above
  static constexpr float kSprintSpeed = 5.8f;
  static constexpr float kTeleport = 5.f;         // m in one sample: put, not walked
  static constexpr float kCarried = 14.f;         // m/s: faster than any V runs (a car, a train)
  static constexpr float kHurtGapSeconds = 0.35f; // at most one grunt per
  static constexpr float kHardFall = 9.f;         // m/s down: a hard landing
  static constexpr float kVeryHardFall = 14.f;
  // Swimming: seconds between two strokes while it moves (the state
  // machine's `timeBetween*Stroke` parameters are TweakDB's; these are
  // their script defaults), the speed above which it swims fast, and below
  // which it treads water (no stroke).
  static constexpr float kStrokeSeconds = 1.f;
  static constexpr float kFastStrokeSeconds = 0.6f;
  static constexpr float kSwimFastSpeed = 3.2f; // m/s
  static constexpr float kSwimIdleSpeed = 0.4f; // m/s

  // Once per frame: where the interpolated V is, how it moves
  // (PlayerState::flags), whether it crouches, the frame's length.
  void Move(const proto::Vec3 &position, std::uint16_t flags, bool crouched, float dt);
  // On change (or every frame: repeats say nothing).
  void Health(std::uint8_t code);
  void Held(std::uint64_t right);
  void Life(proto::Life life);
  void Abilities(std::uint8_t bits);

  std::vector<Event> Take();
  Gait CurrentGait() const { return m_gait; }
  movement::Swim Swimming() const { return m_swim; }

private:
  void Emit(Kind kind, std::uint8_t arg = 0) { m_events.push_back({kind, arg}); }

  std::vector<Event> m_events;
  // movement
  bool m_moved{};
  proto::Vec3 m_position;
  movement::Placement m_placement{movement::Placement::Ground};
  movement::Detailed m_detailed{};
  float m_walked{}; // metres since the last plant
  bool m_left{};    // the next foot
  float m_fall{};   // fastest fall of this flight, m/s
  float m_speed{};  // smoothed horizontal speed
  float m_sinceHurt{kHurtGapSeconds};
  Gait m_gait{Gait::Walk};
  // water
  movement::Swim m_swim{movement::Swim::None};
  float m_sinceStroke{}; // seconds swum since the last stroke
  float m_swimSpeed{};   // smoothed, all three axes
  // states
  std::optional<std::uint8_t> m_health;
  std::optional<std::uint64_t> m_right;
  std::optional<proto::Life> m_life;
  std::optional<std::uint8_t> m_abilities;
};

// ---- health on the wire ----

// A fraction 0..1 as Presence::health. 0 only for none at all, so a sliver
// of health never reads as dead.
std::uint8_t HealthCode(float fraction);
float HealthFraction(std::uint8_t code);

// When our V's Presence is worth sending: flags at once, a loss of health
// at once (the others' puppets grunt from it), a gain or a small change at
// most five times a second.
class Gate {
public:
  static constexpr std::uint64_t kGapUs = 200'000;
  static constexpr std::uint8_t kLossNow = 6; // codes: ≈2 % of the bar

  bool ShouldSend(std::uint8_t health, std::uint8_t flags, std::uint64_t nowUs);

private:
  bool m_sent{};
  std::uint8_t m_health{}, m_flags{};
  std::uint64_t m_atUs{};
};

// ---- the party HUD ----

// What the partner's entry says, one thing at a time (the most important).
enum class Status : std::uint8_t {
  Up = 0,
  Down = 1,      // revivable: with the revive timer when someone is at it
  Out = 2,       // waiting to respawn
  Elsewhere = 3, // cyberspace, a braindance, playing someone else
  Scene = 4,     // in a cinematic scene
  Menu = 5,      // in the pause menu or another full-screen menu
};
const char *NameOf(Status status);

// `tier` is StoryState::tier (GameplayTier 1..5: 3 and up are scenes that
// take the controls), `storyFlags` its flags.
Status StatusOf(proto::Life life, proto::Place place, std::uint8_t tier, std::uint8_t storyFlags,
                bool paused, std::uint8_t presenceFlags);

// Where the partner is from our camera: how far, and which way when the
// camera does not see them.
struct Bearing {
  float distance{}; // metres
  float angle{};    // radians, 0 ahead, positive to the right, ±π behind
  float pitch{};    // radians above the view direction
  bool inView{};    // inside the view cone
  // The arrow of an off-screen partner: 0 ahead (above or below the
  // frame), 1 right, 2 behind, 3 left.
  std::uint8_t side{};
  friend bool operator==(const Bearing &, const Bearing &) = default;
};

// `forward` is the camera's direction (any length), `halfFov` half its
// horizontal field of view in radians.
Bearing BearingOf(const proto::Vec3 &camera, const proto::Vec3 &forward, float halfFov,
                  const proto::Vec3 &target);

// Where a point of the world falls on the screen, for a widget drawn over
// it (the nameplate): x and y from the top left corner, 0..1. `right` is
// the camera's right (any length), `halfFov` half its field of view in
// radians, horizontal or `vertical`, `aspect` the frame's width / height.
struct ScreenPoint {
  float x{}, y{};
  bool visible{}; // in front of the camera and inside the frame
  friend bool operator==(const ScreenPoint &, const ScreenPoint &) = default;
};
ScreenPoint ProjectOf(const proto::Vec3 &camera, const proto::Vec3 &forward,
                      const proto::Vec3 &right, float halfFov, bool vertical, float aspect,
                      const proto::Vec3 &target);

// "12 m", "1.2 km": as the game's own markers write a distance.
std::uint32_t RoundedMetres(float distance);

// The nameplate over a puppet: fully shown up close, fading out, gone
// beyond (the marker takes over there).
inline constexpr float kNameplateFull = 12.f, kNameplateGone = 20.f;
float NameplateOpacity(float distance);

// tandem.ini `nameplates`: near (the above), always (at any distance in
// view, smaller and fainter the farther), off.
enum class Nameplates : std::uint8_t { Near = 0, Always = 1, Off = 2 };

// `always`: full size and strength to kNameplateFull, then down to the
// floor at kNameplateFar and beyond, never gone: a far plate stays a small,
// faint label, so a crowded street is not covered in names.
inline constexpr float kNameplateFar = 150.f, kNameplateFarScale = 0.55f,
                       kNameplateFarOpacity = 0.5f;
struct PlateLook {
  float opacity{}; // 0: not shown
  float scale{};   // of the plate's size up close
  friend bool operator==(const PlateLook &, const PlateLook &) = default;
};
PlateLook NameplateLookOf(float distance, Nameplates mode);

// ---- a puppet that is hit ----

// A flinch laid over the puppet's pose: the upper body is knocked the way
// the hit went and springs back. It never moves the body: the owner's game
// alone says where its V is. Angles in radians for the spine, to add to
// the streamed pose.
struct Lean {
  float pitch{}; // forward (+) or back (−)
  float roll{};  // to its right (+) or left (−)
  friend bool operator==(const Lean &, const Lean &) = default;
};

class Flinch {
public:
  static constexpr std::uint64_t kLengthUs = 450'000; // then it is over
  static constexpr float kMaxAngle = 0.35f;           // radians, for the heaviest hit
  static constexpr float kFrequency = 14.f;           // rad/s of the swing back

  // `strength` 0..1; `direction` the way the hit travelled and `forward`
  // the way the puppet faces, both in the world (no direction: from the
  // front).
  void Hit(float strength, const proto::Vec3 &direction, const proto::Vec3 &forward,
           std::uint64_t nowUs);
  Lean At(std::uint64_t nowUs) const;
  bool Active(std::uint64_t nowUs) const { return m_hit && nowUs - m_atUs < kLengthUs; }

private:
  bool m_hit{};
  std::uint64_t m_atUs{};
  float m_pitch{}, m_roll{};
};

// ---- status effects seen on a puppet (StatusVisuals) ----

// One set, said one way: sorted, without repeats or the null id, at most
// proto::kMaxStatusVisuals of them (the lowest ids kept), as Decode wants.
std::vector<std::uint64_t> StatusSet(std::vector<std::uint64_t> records);

// What a puppet starts and stops showing as its V's set goes from `before`
// to `after` (both sets as StatusSet makes them).
struct StatusChange {
  std::vector<std::uint64_t> started, ended;
  friend bool operator==(const StatusChange &, const StatusChange &) = default;
};
StatusChange StatusDiff(std::span<const std::uint64_t> before,
                        std::span<const std::uint64_t> after);

// ---- a V's noise, for the NPCs another game runs (proto::Noise) ----

// gamedataStimType: what a V's own state machine broadcasts as it moves
// (engine/stealth.md): a footstep at a run (every frame of it in vanilla),
// at a sprint (every 0.2 s, a slide's and a dodge's start too), a landing by
// how hard. A crouched V broadcasts none, nor one whose perks silence it:
// the sender relays what its game gave out, so vanilla's rules hold.
inline constexpr std::uint8_t kStimFootStepRegular = 25, kStimFootStepSprint = 26,
                              kStimLandingHard = 34, kStimLandingRegular = 35,
                              kStimLandingVeryHard = 36, kStimCount = 65;
bool IsFootstep(std::uint8_t stim);
bool IsLanding(std::uint8_t stim);
inline bool IsMovementNoise(std::uint8_t stim) { return IsFootstep(stim) || IsLanding(stim); }

// When a stimulus of our V is worth sending: footsteps (both kinds
// together) four times a second, since an NPC that hears one hears the
// next; any other kind at most ten times a second each (a burst of
// gunshots is one noise). An unknown kind is never sent.
class NoiseGate {
public:
  static constexpr std::uint64_t kFootstepGapUs = 250'000;
  static constexpr std::uint64_t kGapUs = 100'000;

  bool ShouldSend(std::uint8_t stim, std::uint64_t nowUs);
  void Reset() { *this = {}; }

private:
  // By kind; the two footsteps share kStimFootStepRegular's.
  std::array<std::uint64_t, kStimCount> m_atUs{};
  std::array<bool, kStimCount> m_sent{};
};
} // namespace tandem::presence
