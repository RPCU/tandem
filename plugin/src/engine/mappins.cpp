// A point-of-interest mappin's phase, set the engine's way (see mappins.hpp;
// docs/research/engine/story.md §13.167).
//
// gamemappinsPointOfInterestMappin as the 2.50 RTTI has it: 0x100 bytes
// (the SDK's generated struct agrees), its journal path hash at +0x8C
// (Codeware's Raw::PointOfInterestMappin::JournalHash). Its phase setter is
// a plain function of the game, PointOfInterestMappin_SetPhase in the
// address library (hash 803085170, Codeware's
// Raw::PointOfInterestMappin::SetPhase: bool (void *mappin, phase)),
// decompiled (§13.167): it writes the phase into its MappinSystem's table of
// saved point-of-interest states (what a save keeps), untracks a pin set
// completed, and tells the system the pin changed. So every pin a guest
// sets is put back before its saving unlocks (OpenWorld.reds Leave). A pin
// the player tracked, untracked by that setter, is tracked again at its
// put-back by the system's own tracker (MappinSystem's TrackMappin,
// address-library hash 3769177737, the call behind the world map's own
// TrackMappin native, §13.168). Not yet run by tandem in game: the scenario
// openworld-guest is its check.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "mappins.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/mappins/IMappin.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/mappins/PointOfInterestMappin.hpp>

#include <windows.h>

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kSetPhase = 803085170; // PointOfInterestMappin_SetPhase
constexpr std::uint32_t kTrack = 3769177737;   // MappinSystem's TrackMappin (§13.168)
constexpr std::uint32_t kPoiSize = 0x100;
static_assert(sizeof(RED4ext::game::mappins::PointOfInterestMappin) == kPoiSize);

// The phase as the engine's enum (gamedataMappinPhase, 4 bytes): all of
// its register set, never a byte with garbage above it.
using SetPhaseFn = bool (*)(void *mappin, std::uint32_t phase);
SetPhaseFn g_setPhase = nullptr;
// The system's own tracker: (the MappinSystem, the pin's NewMappinID by
// value, 8 bytes in a register). It stores the id as the manually tracked
// pin under the system's lock and tells the system (decompiled, §13.168).
using TrackFn = void (*)(void *system, std::uint64_t id);
TrackFn g_track = nullptr;

RED4ext::CClass *ClassOf(const char *name) {
  auto *rtti = RED4ext::CRTTISystem::Get();
  return rtti ? rtti->GetClass(name) : nullptr;
}

RED4ext::CClass *PoiClass() {
  static RED4ext::CClass *const cls = ClassOf("gamemappinsPointOfInterestMappin");
  return cls;
}

RED4ext::CClass *SystemClass() {
  static RED4ext::CClass *const cls = ClassOf("gamemappinsMappinSystem");
  return cls;
}

// The engine's call, guarded: a refusal by fault is a refusal (no C++
// objects with destructors in a __try function).
bool CallSetPhase(void *mappin, std::uint32_t phase, bool &faulted) {
  __try {
    return g_setPhase(mappin, phase);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    faulted = true;
    return false;
  }
}

bool CallTrack(void *system, std::uint64_t id) {
  __try {
    g_track(system, id);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
} // namespace

bool CheckActivityMarks() {
  const auto f = feature::kActivityMarks;
  bool ok = ClassSizeIs(f, "gamemappinsPointOfInterestMappin", kPoiSize);
  g_setPhase = Find<SetPhaseFn>(f, "PointOfInterestMappin_SetPhase", kSetPhase);
  // Without the tracker a pin the player tracked would stay untracked after
  // the session: no pin is set at all then.
  g_track = Find<TrackFn>(f, "MappinSystem_TrackMappin", kTrack);
  ok = g_setPhase != nullptr && g_track != nullptr && PoiClass() != nullptr &&
       SystemClass() != nullptr && ok;
  if (ok)
    FeatureOn(f);
  return ok;
}

bool SetPoiPhase(RED4ext::game::mappins::IMappin *mappin, std::uint8_t phase) {
  if (!mappin || phase > 3 || !g_setPhase || !FeatureIsOn(feature::kActivityMarks))
    return false;
  auto *type = mappin->GetType();
  if (!type || !type->IsA(PoiClass()))
    return false;
  bool faulted = false;
  static_cast<void>(CallSetPhase(mappin, phase, faulted));
  if (faulted) {
    FeatureOff(feature::kActivityMarks, "the engine's phase setter faulted");
    log::Error("openworld: setting a map pin's phase faulted: activity marks off");
    return false;
  }
  // Its own answer is not trusted alone: the pin's phase read back says.
  return true;
}

bool TrackPin(RED4ext::IScriptable *system, std::uint64_t id) {
  if (!system || id == 0 || !g_track || !FeatureIsOn(feature::kActivityMarks))
    return false;
  auto *type = system->GetType();
  if (!type || !type->IsA(SystemClass()))
    return false;
  if (!CallTrack(system, id)) {
    FeatureOff(feature::kActivityMarks, "the engine's pin tracker faulted");
    log::Error("openworld: tracking a map pin again faulted: activity marks off");
    return false;
  }
  // The pin's IsPlayerTracked read back says whether it took.
  return true;
}
} // namespace tandem::plugin::engine
