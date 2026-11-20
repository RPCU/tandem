// What scripts/tandem/Regions.reds can call: `TandemRegions` static functions
// and their structs (docs/architecture/regions.md). Declared on the script
// side in scripts/tandem/RegionNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "combat.hpp"
#include "mods.hpp"
#include "regions.hpp"
#include "runtime.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/ledger.hpp"
#include "tandem/regions.hpp"
#include "tandem/twins.hpp"

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

// A placed NPC that died in the session (the ledger, regions.md §14).
struct TandemDeath {
  Red::EntityID entity; // its static id, the same in every game
  Red::TweakDBID record;
  Red::CName appearance;
  float x{}, y{}, z{}; // where it fell
  float yaw{};
  std::uint32_t by{}; // whose game ran it
  bool knockedOut{};  // not dead: out for good
  float age{};        // seconds since, on the host clock
};
static_assert(alignof(TandemDeath) <= 8, "the script VM aligns struct locals to 8");

// The status effects of each NPC taken over, until the next batch of events
// (game thread only).
static std::map<proto::NpcId, std::vector<proto::NpcEffectState>> s_effects;

namespace {
TandemDeath DeathOf(const proto::NpcDeath &death, std::uint64_t hostUs) {
  return {.entity = Red::EntityID(death.entity),
          .record = Red::TweakDBID(
              ModRecords::Get().Resolve(mods::Kind::Npc, death.record, {}, death.by)),
          .appearance = Red::CName(death.appearance),
          .x = death.position.x,
          .y = death.position.y,
          .z = death.position.z,
          .yaw = death.yaw,
          .by = death.by,
          .knockedOut = (death.flags & proto::kDeathKnockedOut) != 0,
          .age = hostUs > death.timeUs ? static_cast<float>(hostUs - death.timeUs) / 1e6f : 0.f};
}
} // namespace

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
        out.EmplaceBack(TandemRegionEffect{
            .record = Red::TweakDBID(ModRecords::Get().Resolve(
                mods::Kind::Effect, effect.record, {}, tandem::regions::Announcer(npc))),
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

  // ---- placed NPCs, the death ledger, twins (regions.md §3, §14) ----

  // Owner: one of our NPCs is placed in the world (its entity's static id,
  // the same in every game): its twin in the other games is known by it, and
  // its death is the session's to remember.
  static void Placed(std::uint32_t npc, Red::EntityID entity) {
    if (npc != 0 && entity.hash != 0)
      Combat::Get().Send(proto::NpcPlaced{.npc = npc, .entity = entity.hash});
  }
  // The static entity an NPC's owner named for it (undefined: none).
  static Red::EntityID PlacedOf(std::uint32_t npc) {
    return Red::EntityID(Regions::Get().PlacedOf(npc));
  }
  // The session says the placed NPC of that entity died (or was knocked out
  // for good): whose game ran it then (0: it lives). Our own copy of it
  // stays out of our world.
  static std::uint32_t DiedBy(Red::EntityID entity) {
    return entity.hash != 0 ? Regions::Get().DiedBy(entity.hash) : 0;
  }
  // The deaths the session said since the last call, once each.
  static Red::DynArray<TandemDeath> TakeDeaths() {
    Red::DynArray<TandemDeath> out;
    const auto hostUs = Runtime::Get().HostNowUs();
    for (const auto &death : Regions::Get().TakeDeaths())
      out.EmplaceBack(DeathOf(death, hostUs));
    return out;
  }
  // How many deaths of the ledger lie within `radius` of this point.
  static std::int32_t DeathsNear(float x, float y, float z, float radius) {
    std::int32_t count = 0;
    for (const auto &death : Regions::Get().Deaths())
      count += regions::Distance({x, y, z}, death.position) < radius ? 1 : 0;
    return count;
  }
  // The deaths whose body our game shows with our V at this point: another
  // game's, recent, near (ledger::Bodies; `shown`: the ones it shows now).
  static Red::DynArray<TandemDeath> Bodies(float x, float y, float z,
                                           Red::DynArray<Red::EntityID> shown) {
    Red::DynArray<TandemDeath> out;
    auto &runtime = Runtime::Get();
    const auto you = runtime.You();
    if (you == proto::kNoPlayer)
      return out;
    std::vector<std::uint64_t> has;
    for (const auto &entity : shown)
      has.push_back(entity.hash);
    const auto hostUs = runtime.HostNowUs();
    for (const auto &death : ledger::Bodies(Regions::Get().Deaths(), you, hostUs, {x, y, z}, has))
      out.EmplaceBack(DeathOf(death, hostUs));
    return out;
  }
  // Twins (tandem/twins.hpp): for each proxy that came (its record, where
  // it stands, the static entity its owner named or none), the index of our
  // own NPC that is the same person, -1: none. Ours: record, place, its
  // static id (undefined: a dynamic one), and whether it is busy (never
  // given up). One of ours goes to one proxy at most.
  static Red::DynArray<std::int32_t>
  MatchTwins(Red::DynArray<Red::TweakDBID> theirRecords, Red::DynArray<Red::Vector4> theirPlaces,
             Red::DynArray<Red::EntityID> theirEntities, Red::DynArray<Red::TweakDBID> ourRecords,
             Red::DynArray<Red::Vector4> ourPlaces, Red::DynArray<Red::EntityID> ourEntities,
             Red::DynArray<bool> ourBusy) {
    std::vector<twins::Body> theirs, ours;
    for (std::uint32_t i = 0; i < theirRecords.Size() && i < theirPlaces.Size(); ++i)
      theirs.push_back({.record = theirRecords[i].value,
                        .position = {theirPlaces[i].X, theirPlaces[i].Y, theirPlaces[i].Z},
                        .entity = i < theirEntities.Size() ? theirEntities[i].hash : 0});
    for (std::uint32_t i = 0; i < ourRecords.Size() && i < ourPlaces.Size(); ++i)
      ours.push_back({.record = ourRecords[i].value,
                      .position = {ourPlaces[i].X, ourPlaces[i].Y, ourPlaces[i].Z},
                      .entity = i < ourEntities.Size() ? ourEntities[i].hash : 0,
                      .busy = i < ourBusy.Size() && ourBusy[i]});
    Red::DynArray<std::int32_t> out;
    for (std::size_t i = 0; i < theirs.size(); ++i)
      out.EmplaceBack(-1);
    for (const auto &pair : twins::Match(theirs, ours))
      out[static_cast<std::uint32_t>(pair.theirs)] = static_cast<std::int32_t>(pair.ours);
    return out;
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemDeath, "TandemDeath", {
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(appearance);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
  RTTI_PROPERTY(by);
  RTTI_PROPERTY(knockedOut);
  RTTI_PROPERTY(age);
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
  RTTI_METHOD(Placed);
  RTTI_METHOD(PlacedOf);
  RTTI_METHOD(DiedBy);
  RTTI_METHOD(TakeDeaths);
  RTTI_METHOD(DeathsNear);
  RTTI_METHOD(Bodies);
  RTTI_METHOD(MatchTwins);
  RTTI_METHOD(Pin);
  RTTI_METHOD(Give);
  RTTI_METHOD(Refuse);
  RTTI_METHOD(Adopt);
});
