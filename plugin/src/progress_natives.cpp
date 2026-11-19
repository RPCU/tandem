// What scripts/tandem/Levels.reds and Saves.reds call: `TandemProgress`
// static functions (docs/architecture/progression.md). Declared on the
// script side in scripts/tandem/ProgressNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread; the level messages only queue (the hit outcomes' queue).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "combat.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/progress.hpp"
#include "tandem/vehicle.hpp"

#include <RedLib.hpp>

#include <algorithm>
#include <cmath>

namespace tandem::plugin {
namespace {
// A guest's way out of a session (tandem/progress.hpp). One per process:
// the script that drives it lives and dies with each loaded world and
// resets it when one loads.
progress::Leave &TheLeave() {
  static progress::Leave leave;
  return leave;
}
} // namespace

struct TandemProgress : Red::IScriptable {
  // ---- levels ----

  // Our V's level (its PowerLevel, rounded), when it changes: to the host.
  static void SendOwnLevel(std::int32_t level) {
    if (level >= 1 && level <= proto::kMaxLevel)
      Combat::Get().Send(proto::PlayerLevel{.level = static_cast<std::uint8_t>(level)});
  }
  // Host: one of our NPCs' power level, scaling included.
  static void SendNpcLevel(std::uint32_t npc, float level) {
    if (std::isfinite(level) && level >= 0.f)
      Combat::Get().Send(proto::NpcLevel{.npc = npc, .level = level});
  }
  // Host: the PowerLevel modifier one of our NPCs should carry (0: none).
  // curveAtHost, curveAtSession: its content assignment's curve at the two
  // levels (< 0: none read); fixed: a constant content assignment.
  static float NpcLevelShift(float level, float applied, float hostLevel, float sessionLevel,
                             float curveAtHost, float curveAtSession, bool fixed, float maxLevel) {
    return progress::NpcShift({.level = level,
                               .applied = applied,
                               .hostLevel = hostLevel,
                               .sessionLevel = sessionLevel,
                               .curveAtHost = curveAtHost,
                               .curveAtSession = curveAtSession,
                               .fixed = fixed,
                               .maxLevel = maxLevel > 0.f ? maxLevel : progress::kMaxNpcLevel});
  }
  // Guest: the modifier a proxy should carry to be at the host's NPC's level.
  static float ProxyLevelShift(float level, float applied, float hostNpcLevel) {
    return progress::ProxyShift(level, applied, hostNpcLevel);
  }

  // ---- cars (docs/architecture/garage.md) ----

  // tandem.ini `lend`: 0 crew, 1 only with us aboard, 2 never (proto::Lend).
  static std::int32_t OwnLend() { return static_cast<std::int32_t>(Runtime::Get().config.lend); }
  // Our word on our cars, to everyone (the endpoint keeps it).
  static void SendCarLend(std::int32_t lend) {
    if (lend >= 0 && lend <= static_cast<std::int32_t>(proto::Lend::Never))
      Combat::Get().Send(proto::CarLend{.lend = static_cast<proto::Lend>(lend)});
  }
  // A partner's word on its cars (0 crew when it said none).
  static std::int32_t CarLendOf(std::uint32_t player) {
    return static_cast<std::int32_t>(Combat::Get().LendOf(player));
  }
  // May a partner take the wheel of a car under that word (tandem/vehicle.hpp)?
  static bool MayDrive(std::int32_t lend, bool ownerAboard, bool ownerInSession) {
    const auto word = lend >= 0 && lend <= static_cast<std::int32_t>(proto::Lend::Never)
                          ? static_cast<proto::Lend>(lend)
                          : proto::Lend::Never;
    return sync::MayDrive(
        {.lend = word, .ownerAboard = ownerAboard, .ownerInSession = ownerInSession});
  }

  // ---- saves ----

  // tandem.ini `leave`: 0 stay, 1 save, 2 reload.
  static std::int32_t LeaveMode() { return static_cast<std::int32_t>(Runtime::Get().config.leave); }
  // One step of our way out of a session. Returns a progress::LeaveAction:
  // 0 none, 1 clean (saving unlocks), 2 save now, 3 saved, 4 load now,
  // 5 the save failed, 6 gave up (the game never allowed one), 7 dirty.
  static std::int32_t LeaveTick(float now, bool session, bool chosen, std::int32_t left,
                                bool blocked, std::int32_t saved) {
    auto &leave = TheLeave();
    leave.SetMode(Runtime::Get().config.leave);
    const auto before = leave.Step();
    const auto action = leave.Tick({.now = now,
                                    .session = session,
                                    .chosen = chosen,
                                    .left = static_cast<std::uint32_t>(left),
                                    .blocked = blocked,
                                    .saved = static_cast<std::int8_t>(std::clamp(saved, -1, 1))});
    if (leave.Step() != before)
      log::Info("save: leave {} -> {} ({}{}{})", progress::NameOf(before),
                progress::NameOf(leave.Step()), progress::NameOf(leave.Mode()),
                left != 0 ? ", left: " : "", progress::LeftNames(static_cast<std::uint32_t>(left)));
    return static_cast<std::int32_t>(action);
  }
  // Our save lock, and the step (progress::LeaveStep: 0 solo, 1 session,
  // 2 cleaning, 3 waiting, 4 saving, 5 loading, 6 dirty).
  static bool LeaveLocked() { return TheLeave().Locked(); }
  static std::int32_t LeaveStep() { return static_cast<std::int32_t>(TheLeave().Step()); }
  // A world loaded: what the old one left is gone with it.
  static void LeaveReset() { TheLeave().Reset(); }
  // "story, bodies": what a `left` mask names.
  static Red::CString LeftNames(std::int32_t left) {
    return Red::CString(progress::LeftNames(static_cast<std::uint32_t>(left)).c_str());
  }

  RTTI_IMPL_TYPEINFO(TandemProgress);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemProgress, "TandemProgress", {
  RTTI_METHOD(SendOwnLevel);
  RTTI_METHOD(SendNpcLevel);
  RTTI_METHOD(NpcLevelShift);
  RTTI_METHOD(ProxyLevelShift);
  RTTI_METHOD(OwnLend);
  RTTI_METHOD(SendCarLend);
  RTTI_METHOD(CarLendOf);
  RTTI_METHOD(MayDrive);
  RTTI_METHOD(LeaveMode);
  RTTI_METHOD(LeaveTick);
  RTTI_METHOD(LeaveLocked);
  RTTI_METHOD(LeaveStep);
  RTTI_METHOD(LeaveReset);
  RTTI_METHOD(LeftNames);
});
