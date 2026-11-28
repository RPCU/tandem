#pragma once

// Engine access that fails soft (docs/ARCHITECTURE.md, "Talking to the
// engine safely"): every address-library hash goes through Find, every
// hand-measured layout through a check, and what does not match this game
// turns one feature off, with one log line, instead of ending the process.
// Never silently: a feature that goes off is registered with what the game
// runs without (../degradations.hpp), so the player and the other players
// are told, and a core one (tandem/degraded.hpp) means no session.
//
// A feature is a named piece of engine access. Its state is decided at the
// first frame (CheckAccess, game thread), listed in one log line
// (`engine: N features on, M off: …`) and by `cmd engine`, and asked by the
// code that uses it (FeatureIsOn) and by scripts (TandemNative.EngineOn).
//
// A new hook or call by hash:
//   static const auto move = engine::Find<MoveToFn>(feature, "MoveTo", kMoveTo);
//   if (!move) return false;        // logged, the feature is off
// and, for the startup line to list it, its check called from CheckAccess.

#include <cstdint>
#include <string>
#include <string_view>

namespace tandem::plugin::engine {
namespace feature {
inline constexpr std::string_view kMovement = "puppet movement";
inline constexpr std::string_view kClipLengths = "clip lengths";
inline constexpr std::string_view kLooks = "looks";
inline constexpr std::string_view kLookKeys = "look keys";
inline constexpr std::string_view kThirdPerson = "third-person mark";
inline constexpr std::string_view kPoseStream = "pose stream";
inline constexpr std::string_view kQuestNodes = "quest nodes";
inline constexpr std::string_view kQuestFreeze = "quest freeze";
inline constexpr std::string_view kPrefabs = "quest prefabs";
inline constexpr std::string_view kWorkspots = "workspots";
inline constexpr std::string_view kPhysics = "physics watch";
inline constexpr std::string_view kTransformGuard = "transform guard";
inline constexpr std::string_view kDismembermentGuard = "dismemberment guard";
inline constexpr std::string_view kSlideGuard = "slide guard";
inline constexpr std::string_view kDisposeGate = "dispose gate";
inline constexpr std::string_view kTrace = "anim trace";
inline constexpr std::string_view kVehicleMove = "vehicle move";
inline constexpr std::string_view kBodySlots = "body slots";
inline constexpr std::string_view kCrowdSpawners = "crowd spawners";
inline constexpr std::string_view kRagdoll = "ragdoll state";
} // namespace feature

// The address of an address-library hash in this game, 0 when its library
// does not have it. RED4ext's own resolver (the export the SDK calls):
// RED4ext::UniversalRelocBase::Resolve and UniversalRelocFunc end the
// process on a missing hash, so nothing of ours uses them.
std::uintptr_t Resolve(std::uint32_t hash);

// Resolve for a feature: a missing hash turns the feature off and logs
// `what` and the hash, once. Any thread.
std::uintptr_t FindAddress(std::string_view feature, const char *what, std::uint32_t hash);
template <class Fn> Fn Find(std::string_view feature, const char *what, std::uint32_t hash) {
  return reinterpret_cast<Fn>(FindAddress(feature, what, hash));
}

// A feature's state. Off stays off (the first reason is kept and logged, as
// an error); NotAsked is for what tandem.ini or the platform leaves out; On
// only takes a feature that is neither. Any thread; no engine call inside.
void FeatureOn(std::string_view feature);
void FeatureOff(std::string_view feature, std::string_view reason);
void FeatureNotAsked(std::string_view feature, std::string_view why);
// Not asked for because tandem.ini switched a path of the sync off (pose,
// quest_freeze…): the lesser path is the player's choice, and still listed
// with what the game runs without (degradations.hpp), as chosen.
void FeatureSwitchedOff(std::string_view feature, std::string_view why);
bool FeatureIsOff(std::string_view feature);
// False for a feature that is off, not asked for, or not checked yet.
bool FeatureIsOn(std::string_view feature);

// Layout checks against the running game's RTTI, each mismatch logged and
// the feature turned off: a class's size, a property's offset in its class.
bool ClassSizeIs(std::string_view feature, const char *type, std::uint32_t expected);
bool PropertyAt(std::string_view feature, const char *type, const char *property,
                std::uint32_t expected);

// Whether an address lies in the game's executable image (a vtable, a
// function): what a pointer of unknown origin is checked against before use.
bool InGameImage(std::uintptr_t address);

// Game thread, every frame until it has run: once the game's classes are
// registered, every feature's startup check, then the summary line.
void CheckAccess();
bool AccessChecked();
// `engine: N features on, M off: name on, name off (why), …`.
std::string AccessSummary();
// The features that are off, comma-separated ("" when none): for the
// player's notice.
std::string FeaturesOff();
} // namespace tandem::plugin::engine
