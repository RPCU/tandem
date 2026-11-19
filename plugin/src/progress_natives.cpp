// What scripts/tandem/Levels.reds, Scaling.reds, Saves.reds and Vendors.reds
// call: `TandemProgress` static functions (docs/architecture/progression.md,
// and npcs.md for enemy scaling). Declared on the
// script side in scripts/tandem/ProgressNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread; the level messages only queue (the hit outcomes' queue).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "combat.hpp"
#include "log.hpp"
#include "runtime.hpp"

#include "tandem/progress.hpp"
#include "tandem/scaling.hpp"
#include "tandem/vehicle.hpp"
#include "tandem/vendors.hpp"

#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace tandem::plugin {
namespace {
// A guest's way out of a session (tandem/progress.hpp). One per process:
// the script that drives it lives and dies with each loaded world and
// resets it when one loads.
progress::Leave &TheLeave() {
  static progress::Leave leave;
  return leave;
}

// Enemy scaling: who counts for each NPC our game runs (tandem/scaling.hpp).
struct Counting {
  std::mutex lock;
  scaling::Counter counter;
};
Counting &TheCounting() {
  static Counting counting;
  return counting;
}

std::uint8_t ClampedCount(std::int32_t players) {
  return static_cast<std::uint8_t>(std::clamp<std::int32_t>(players, 1, proto::kMaxPlayerId));
}

scaling::Multipliers MultipliersFor(std::int32_t players) {
  return scaling::For(ClampedCount(players), scaling::RuleOf(Combat::Get().Rules()));
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

  // ---- enemy scaling (docs/architecture/npcs.md, "Enemy scaling") ----

  // The session's rule as this game knows it (the host's Rules): on, and
  // the health added per extra player.
  static bool EnemyScaling() { return scaling::RuleOf(Combat::Get().Rules()).on; }
  static float EnemyScalingHealth() { return scaling::RuleOf(Combat::Get().Rules()).health; }
  // How many players count for one of the NPCs our game runs this round
  // (1..4). distances: player p's distance to it at p - 1 (< 0: not placed
  // here); engaged: bit p - 1, that player fights it (in combat with it, its
  // target, or hit it a moment ago). Only players of the session count, our
  // V always. Alone, or out of a session: 1.
  static std::int32_t ScaleCount(std::uint32_t npc, Red::DynArray<float> distances,
                                 std::int32_t engaged, float now) {
    auto &runtime = Runtime::Get();
    const auto you = runtime.You();
    if (you == proto::kNoPlayer)
      return 1;
    std::vector<scaling::Seen> seen;
    for (std::uint32_t i = 0; i < distances.Size() && i < proto::kMaxPlayerId; ++i) {
      const auto player = static_cast<proto::PlayerId>(i + 1);
      if (player != you && !runtime.InRoster(player))
        continue; // gone, or never there: it stops counting at once
      const float distance = std::isfinite(distances[i]) ? distances[i] : -1.f;
      seen.push_back(
          {.player = player, .distance = distance, .engaged = (engaged & (1 << i)) != 0});
    }
    auto &counting = TheCounting();
    std::scoped_lock hold(counting.lock);
    return counting.counter.Count(npc, seen, now);
  }
  // One of ours died, went or was handed over: nothing of its count is kept.
  static void ForgetScale(std::uint32_t npc) {
    auto &counting = TheCounting();
    std::scoped_lock hold(counting.lock);
    counting.counter.Forget(npc);
  }
  // The session is over.
  static void ClearScales() {
    auto &counting = TheCounting();
    std::scoped_lock hold(counting.lock);
    counting.counter.Clear();
  }
  // The multipliers an NPC scaled for `players` carries, by the session's
  // rule (1: none).
  static float ScaleHealth(std::int32_t players) { return MultipliersFor(players).health; }
  static float ScalePoise(std::int32_t players) { return MultipliersFor(players).poise; }
  static float ScaleArmor(std::int32_t players) { return MultipliersFor(players).armor; }
  // The multiplier of its hit reaction thresholds (tandem/scaling.hpp
  // ThresholdFactor): its health without our modifier, and the total health
  // its HitReactionComponent read.
  static float ScaleThreshold(std::int32_t players, float baseHealth, float componentHealth) {
    return scaling::ThresholdFactor(MultipliersFor(players).poise, baseHealth, componentHealth);
  }
  // Our word on one of our NPCs: how many players it is scaled for.
  static void SendNpcScale(std::uint32_t npc, std::int32_t players) {
    Combat::Get().Send(proto::NpcScale{.npc = npc, .players = ClampedCount(players)});
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

  // ---- vendors (docs/architecture/progression.md, "Vendors") ----

  // Owner: one of our NPCs is a vendor placed in the world (its entity's
  // static id), for the players whose games show it as a proxy.
  static void SendVendor(std::uint32_t npc, Red::EntityID entity) {
    if (npc != 0 && entity.hash != 0)
      Combat::Get().Send(proto::NpcVendor{.npc = npc, .entity = entity.hash});
  }
  // The placed entity of a shared NPC its owner named a vendor (0: none).
  static Red::EntityID VendorOf(std::uint32_t npc) {
    return Red::EntityID(Combat::Get().VendorOf(npc));
  }
  // The session says that entity's vendor died.
  static bool VendorDead(Red::EntityID entity) { return Combat::Get().VendorDead(entity.hash); }
  // The vendors (entity hashes) the session said died since the last call.
  static Red::DynArray<std::uint64_t> TakeDeadVendors() {
    Red::DynArray<std::uint64_t> out;
    for (const auto entity : Combat::Get().TakeDeadVendors())
      out.EmplaceBack(entity);
    return out;
  }
  // A new session: the last one's vendors are forgotten.
  static void VendorsReset() { Combat::Get().ClearVendors(); }
  // May our V trade with that vendor now (vendors::Shop: 0 open, 1 not a
  // vendor, 2 no placed vendor, 3 out of reach, 4 dead, 5 not now, 6 not in
  // our world, 7 locked for us)?
  static std::int32_t VendorShop(bool vendorRecord, Red::EntityID entity, float distance,
                                 bool proxyDead, bool sessionDead, bool free, bool ownFound,
                                 bool ownVendor, bool ownAlive, bool accessible) {
    return static_cast<std::int32_t>(vendors::Decide({.vendorRecord = vendorRecord,
                                                      .entity = entity.hash,
                                                      .distance = distance,
                                                      .proxyDead = proxyDead,
                                                      .sessionDead = sessionDead,
                                                      .free = free,
                                                      .ownFound = ownFound,
                                                      .ownVendor = ownVendor,
                                                      .ownAlive = ownAlive,
                                                      .accessible = accessible}));
  }
  static bool VendorPrompts(std::int32_t shop) {
    return shop >= 0 && shop <= static_cast<std::int32_t>(vendors::Shop::Locked) &&
           vendors::Prompts(static_cast<vendors::Shop>(shop));
  }
  static Red::CString VendorShopName(std::int32_t shop) {
    if (shop < 0 || shop > static_cast<std::int32_t>(vendors::Shop::Locked))
      return Red::CString("?");
    return Red::CString(vendors::NameOf(static_cast<vendors::Shop>(shop)));
  }
  static float VendorReach() { return vendors::kReach; }

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
  RTTI_METHOD(EnemyScaling);
  RTTI_METHOD(EnemyScalingHealth);
  RTTI_METHOD(ScaleCount);
  RTTI_METHOD(ForgetScale);
  RTTI_METHOD(ClearScales);
  RTTI_METHOD(ScaleHealth);
  RTTI_METHOD(ScalePoise);
  RTTI_METHOD(ScaleArmor);
  RTTI_METHOD(ScaleThreshold);
  RTTI_METHOD(SendNpcScale);
  RTTI_METHOD(OwnLend);
  RTTI_METHOD(SendCarLend);
  RTTI_METHOD(CarLendOf);
  RTTI_METHOD(MayDrive);
  RTTI_METHOD(SendVendor);
  RTTI_METHOD(VendorOf);
  RTTI_METHOD(VendorDead);
  RTTI_METHOD(TakeDeadVendors);
  RTTI_METHOD(VendorsReset);
  RTTI_METHOD(VendorShop);
  RTTI_METHOD(VendorPrompts);
  RTTI_METHOD(VendorShopName);
  RTTI_METHOD(VendorReach);
  RTTI_METHOD(LeaveMode);
  RTTI_METHOD(LeaveTick);
  RTTI_METHOD(LeaveLocked);
  RTTI_METHOD(LeaveStep);
  RTTI_METHOD(LeaveReset);
  RTTI_METHOD(LeftNames);
});
