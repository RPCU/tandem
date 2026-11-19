// What scripts/tandem/Regions.reds can call: `TandemRegions` static functions
// and their structs (docs/architecture/regions.md). Declared on the script
// side in scripts/tandem/RegionNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "regions.hpp"
#include "runtime.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/regions.hpp"

#include <RedLib.hpp>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace tandem::plugin {
// One thing for the game to do about who runs an NPC. Plain fields only (the
// script VM aligns struct locals to 8).
struct TandemRegionEvent {
  // 1 give `npc` to `player` (answer Give or Refuse); 2 `npc` is ours now,
  // from `player`, with the state below (answer Adopt); 3 `npc` is
  // `player`'s now (0: it left the session), `was` before; 4 the players
  // linked with us changed.
  std::int32_t kind{};
  std::uint32_t npc{};
  std::uint32_t player{};
  std::uint32_t was{};
  float x{}, y{}, z{};
  float yaw{};
  float health{-1.f}; // fraction (negative: unknown)
  float poise{-1.f};
  std::int32_t flags{};     // proto::kHandover*
  std::uint32_t target{};   // the V it fights (0: none)
  std::int32_t hostileTo{}; // bit (player - 1)
  std::int32_t highLevel{}; // gamedataNPCHighLevelState
  std::int32_t effects{};   // how many status effects came with it (EffectsOf)
};
static_assert(alignof(TandemRegionEvent) <= 8, "the script VM aligns struct locals to 8");

// One status effect of an NPC taken over.
struct TandemRegionEffect {
  Red::TweakDBID record;
  float remaining{}; // seconds (0: no duration, or unknown)
  std::int32_t stacks{1};
};
static_assert(alignof(TandemRegionEffect) <= 8, "the script VM aligns struct locals to 8");

// The status effects of each NPC taken over, until the next batch of events
// (game thread only).
static std::map<proto::NpcId, std::vector<proto::NpcEffectState>> s_effects;

struct TandemRegions : Red::IScriptable {
  static Red::DynArray<TandemRegionEvent> TakeEvents() {
    Red::DynArray<TandemRegionEvent> out;
    auto events = Regions::Get().Take();
    if (!events.empty())
      s_effects.clear();
    for (const auto &event : events) {
      const auto &s = event.state;
      TandemRegionEvent e{.kind = static_cast<std::int32_t>(event.kind),
                          .npc = event.npc,
                          .player = event.player,
                          .was = event.was};
      if (event.kind == RegionEvent::Kind::Take) {
        e.x = s.position.x;
        e.y = s.position.y;
        e.z = s.position.z;
        e.yaw = s.yaw;
        e.health = s.health;
        e.poise = s.poise;
        e.flags = s.flags;
        e.target = s.target;
        e.hostileTo = s.hostileTo;
        e.highLevel = s.highLevel;
        e.effects = static_cast<std::int32_t>(s.effects.size());
        s_effects[event.npc] = s.effects;
      }
      out.EmplaceBack(e);
    }
    return out;
  }

  static Red::DynArray<TandemRegionEffect> EffectsOf(std::uint32_t npc) {
    Red::DynArray<TandemRegionEffect> out;
    if (const auto it = s_effects.find(npc); it != s_effects.end())
      for (const auto &effect : it->second)
        out.EmplaceBack(TandemRegionEffect{.record = Red::TweakDBID(effect.record),
                                           .remaining = effect.remaining,
                                           .stacks = effect.stacks});
    return out;
  }

  // Who runs an NPC of the session (0: not one of them), and the players
  // whose region meets ours.
  static std::uint32_t OwnerOf(std::uint32_t npc) { return Regions::Get().OwnerOf(npc); }
  static bool IsLinked(std::uint32_t player) { return Regions::Get().IsLinked(player); }
  static Red::DynArray<std::uint32_t> Linked() {
    Red::DynArray<std::uint32_t> out;
    for (const auto player : Regions::Get().Linked())
      out.EmplaceBack(player);
    return out;
  }

  // Grouped or apart, for quests (docs/architecture/quest-carryover.md):
  // apart, our world is our own; grouped, the leader's world is the one
  // played (0: no session). A player that holds its region (a gig of its
  // world runs where it stands) leads its group and keeps its NPCs.
  static bool IsApart() { return Regions::Get().IsApart(); }
  static std::uint32_t Leader() { return Regions::Get().Leader(); }
  static bool Holds(std::uint32_t player) { return Regions::Get().Holds(player); }
  static void Hold(bool held) { Regions::Get().Answer(RegionHeld{held}); }

  // The game that runs the world around `player`: its new NPCs, its crowd,
  // its traffic and police (Endpoint::AreaOwner; 0: no session yet), and
  // whether that is ours around our own V (traffic.md, "Who owns an area").
  static std::uint32_t AreaOwner(std::uint32_t player) { return Regions::Get().AreaOwner(player); }
  static bool OwnsArea() {
    const auto you = Runtime::Get().You();
    return you != proto::kNoPlayer && Regions::Get().AreaOwner(you) == you;
  }

  // The linked player that outranks us and whose V is within `radius` of
  // this point (0: none): its game populates the place, ours holds back
  // there. 0x80000000 + player: one that outranks us has not been placed yet
  // (no state of it heard): wait before deciding.
  static std::uint32_t CoveredBy(float x, float y, float z, float radius) {
    auto &runtime = Runtime::Get();
    const auto you = runtime.You();
    if (you == proto::kNoPlayer)
      return 0;
    const auto holders = Regions::Get().Holders();
    std::uint32_t best = 0, unplaced = 0;
    for (const auto player : Regions::Get().Linked()) {
      if (!regions::Outranks(player, you, 1, holders))
        continue;
      const auto view = runtime.ViewOf(player);
      if (!view.valid) {
        unplaced = 0x80000000u + player;
        continue;
      }
      const auto distance = regions::Distance({x, y, z}, view.position);
      if (distance < radius && (best == 0 || regions::Outranks(player, best, 1, holders)))
        best = player;
    }
    return best != 0 ? best : unplaced;
  }

  // One of our NPCs never changes owner (a quest or story NPC, a police unit).
  static void Pin(std::uint32_t npc) { Regions::Get().Answer(RegionPin{npc}); }

  // Our answer to event 1: the NPC as we leave it (its body stays here as a
  // proxy), or no.
  static void Give(std::uint32_t npc, std::uint32_t to, float x, float y, float z, float yaw,
                   float health, float poise, std::int32_t flags, std::uint32_t target,
                   std::int32_t hostileTo, std::int32_t highLevel,
                   Red::DynArray<Red::TweakDBID> effects, Red::DynArray<float> remaining,
                   Red::DynArray<std::int32_t> stacks) {
    proto::NpcHandoverState state{.npc = npc,
                                  .to = to,
                                  .position = {x, y, z},
                                  .yaw = yaw,
                                  .health = health,
                                  .poise = poise,
                                  .flags = static_cast<std::uint8_t>(flags),
                                  .target = target,
                                  .hostileTo = static_cast<std::uint8_t>(hostileTo),
                                  .highLevel = static_cast<std::uint8_t>(highLevel)};
    for (std::uint32_t i = 0; i < effects.Size() && state.effects.size() < proto::kMaxNpcEffects;
         ++i)
      state.effects.push_back({.record = effects[i].value,
                               .remaining = i < remaining.Size() ? remaining[i] : 0.f,
                               .stacks = static_cast<std::uint8_t>(
                                   std::clamp(i < stacks.Size() ? stacks[i] : 1, 1, 255))});
    Regions::Get().Answer(std::move(state));
  }
  static void Refuse(std::uint32_t npc) { Regions::Get().Answer(RegionRefuse{npc}); }
  // Our answer to event 2: we run it now, or cannot (it goes for everyone).
  static void Adopt(std::uint32_t npc, bool accepted) {
    Regions::Get().Answer(RegionAdopt{npc, accepted});
  }

  RTTI_IMPL_TYPEINFO(TandemRegions);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemRegionEvent, "TandemRegionEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(was);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
  RTTI_PROPERTY(health);
  RTTI_PROPERTY(poise);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(target);
  RTTI_PROPERTY(hostileTo);
  RTTI_PROPERTY(highLevel);
  RTTI_PROPERTY(effects);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemRegionEffect, "TandemRegionEffect", {
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(remaining);
  RTTI_PROPERTY(stacks);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemRegions, "TandemRegions", {
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(EffectsOf);
  RTTI_METHOD(OwnerOf);
  RTTI_METHOD(IsLinked);
  RTTI_METHOD(Linked);
  RTTI_METHOD(IsApart);
  RTTI_METHOD(Leader);
  RTTI_METHOD(Holds);
  RTTI_METHOD(Hold);
  RTTI_METHOD(AreaOwner);
  RTTI_METHOD(OwnsArea);
  RTTI_METHOD(CoveredBy);
  RTTI_METHOD(Pin);
  RTTI_METHOD(Give);
  RTTI_METHOD(Refuse);
  RTTI_METHOD(Adopt);
});
