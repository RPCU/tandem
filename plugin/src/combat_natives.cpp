// What scripts/tandem/Outcomes.reds and Combat.reds call for hit outcomes:
// `TandemFight` static functions and the `TandemFightEvent` struct. Declared
// on the script side in scripts/tandem/CombatNatives.reds: keep the two in
// sync (tools/check-natives.py reads every pair). All of these run on the
// game thread, or on a job thread inside the damage pipeline: they only queue.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "combat.hpp"
#include "log.hpp"
#include "regions.hpp"
#include "runtime.hpp"

#include "tandem/combat.hpp"
#include "tandem/scaling.hpp"

#include <RedLib.hpp>

#include <algorithm>

namespace tandem::plugin {
// One thing the session said about hits (kinds in CombatNatives.reds).
// Plain fields only (the script VM aligns struct locals to 8).
struct TandemFightEvent {
  std::int32_t kind{};
  std::uint32_t npc{};
  std::uint32_t player{};
  float damage{};
  float poise{};
  float minHealth{};
  std::int32_t flags{};
  std::int32_t attackType{};
  std::int32_t zone{};
  std::int32_t part{};
  std::int32_t shape{};
  std::int32_t level{};
  Red::TweakDBID attack;
  Red::TweakDBID weapon;
  float physical{}, thermal{}, electric{}, chemical{};
  float x{}, y{}, z{};
  float dx{}, dy{}, dz{};
  std::uint32_t id{};
  std::uint32_t target{};
};
static_assert(alignof(TandemFightEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
enum Kind : std::int32_t {
  kStrike = 1,
  kWound = 2,
  kPoise = 3,
  kDifficulty = 4,
  kAreaHit = 5,
  kThrow = 6,
  kThrowEnd = 7,
  kRules = 8,
  kDefended = 9,
  kSessionLevel = 10,
  kNpcLevel = 11,
  kNpcScale = 12
};

proto::Vec3 Vec(const Red::Vector4 &v) { return {v.X, v.Y, v.Z}; }

TandemFightEvent EventOf(const proto::Message &message) {
  TandemFightEvent out;
  if (const auto *s = std::get_if<proto::NpcStrike>(&message)) {
    out = {.kind = kStrike,
           .npc = s->npc,
           .player = s->player,
           .damage = s->damage,
           .poise = s->poise,
           .minHealth = s->minHealth,
           .flags = s->flags,
           .attackType = s->attackType,
           .zone = s->zone,
           .part = s->part,
           .shape = s->shape,
           .attack = Red::TweakDBID(s->attack)};
    out.x = s->position.x, out.y = s->position.y, out.z = s->position.z;
    out.dx = s->direction.x, out.dy = s->direction.y, out.dz = s->direction.z;
  } else if (const auto *w = std::get_if<proto::NpcWound>(&message)) {
    out = {.kind = kWound, .npc = w->npc, .player = w->player, .part = w->part, .shape = w->wound};
    out.x = w->position.x, out.y = w->position.y, out.z = w->position.z;
  } else if (const auto *p = std::get_if<proto::NpcPoise>(&message)) {
    out = {.kind = kPoise, .npc = p->npc, .poise = p->poise};
  } else if (const auto *d = std::get_if<proto::Difficulty>(&message)) {
    out = {.kind = kDifficulty, .level = d->level};
  } else if (const auto *a = std::get_if<proto::AreaHit>(&message)) {
    out = {.kind = kAreaHit,
           .player = a->from,
           .flags = a->flags,
           .attackType = a->attackType,
           .attack = Red::TweakDBID(a->attack),
           .weapon = Red::TweakDBID(a->weapon),
           .physical = a->physical,
           .thermal = a->thermal,
           .electric = a->electric,
           .chemical = a->chemical};
    out.x = a->position.x, out.y = a->position.y, out.z = a->position.z;
    out.dx = a->direction.x, out.dy = a->direction.y, out.dz = a->direction.z;
  } else if (const auto *t = std::get_if<proto::Throw>(&message)) {
    out = {.kind = kThrow,
           .npc = t->npc,
           .player = t->player,
           .flags = t->flags,
           .level = static_cast<std::int32_t>(t->kind),
           .attack = Red::TweakDBID(t->item)};
    out.x = t->origin.x, out.y = t->origin.y, out.z = t->origin.z;
    out.dx = t->velocity.x, out.dy = t->velocity.y, out.dz = t->velocity.z;
    out.id = t->id;
    out.target = t->at;
  } else if (const auto *e = std::get_if<proto::ThrowEnd>(&message)) {
    out = {.kind = kThrowEnd, .player = e->player, .level = static_cast<std::int32_t>(e->how)};
    out.x = e->position.x, out.y = e->position.y, out.z = e->position.z;
    out.id = e->id;
  } else if (const auto *r = std::get_if<proto::Rules>(&message)) {
    out = {.kind = kRules, .flags = r->flags, .level = r->enemyHealth};
  } else if (const auto *d = std::get_if<proto::Defended>(&message)) {
    out = {.kind = kDefended,
           .npc = d->npc,
           .player = d->player,
           .level = static_cast<std::int32_t>(d->how)};
  } else if (const auto *s = std::get_if<proto::SessionLevel>(&message)) {
    out = {.kind = kSessionLevel, .player = s->players, .level = s->level};
  } else if (const auto *n = std::get_if<proto::NpcLevel>(&message)) {
    out = {.kind = kNpcLevel, .npc = n->npc, .damage = n->level};
  } else if (const auto *c = std::get_if<proto::NpcScale>(&message)) {
    out = {.kind = kNpcScale, .npc = c->npc, .player = c->players};
  }
  return out;
}
} // namespace

struct TandemFight : Red::IScriptable {
  // Guest: our hit on the host's NPC (its proxy), with its whole outcome.
  static void SendStrike(std::uint32_t npc, float damage, float poise, float minHealth,
                         std::int32_t flags, std::int32_t attackType, std::int32_t zone,
                         std::int32_t part, std::int32_t shape, Red::TweakDBID attack,
                         Red::Vector4 position, Red::Vector4 direction) {
    Combat::Get().Send(proto::NpcStrike{
        .npc = npc,
        .damage = damage,
        .poise = std::max(poise, 0.f),
        .minHealth = std::clamp(minHealth, 0.f, 100.f),
        .flags = static_cast<std::uint16_t>(flags),
        .attackType = static_cast<std::uint8_t>(attackType),
        .zone = zone < 0 || zone > 254 ? proto::kNoZone : static_cast<std::uint8_t>(zone),
        .part = static_cast<std::int8_t>(std::clamp(part, -1, 5)),
        .shape = static_cast<std::int8_t>(std::clamp(shape, -1, 3)),
        .attack = attack.value,
        .position = Vec(position),
        .direction = Vec(direction)});
  }
  // Host: one of our NPCs lost a limb (gameDismBodyPart) or got a wound.
  static void SendWound(std::uint32_t npc, std::uint32_t player, std::int32_t part,
                        std::int32_t wound, Red::Vector4 position) {
    if (part < 0 || part > 5 || wound < 0 || wound > 2)
      return;
    Combat::Get().Send(proto::NpcWound{.npc = npc,
                                       .player = player,
                                       .part = static_cast<std::uint8_t>(part),
                                       .wound = static_cast<std::uint8_t>(wound),
                                       .position = Vec(position)});
  }
  // Host: one of our NPCs' poise (fraction 0..1) changed.
  static void SendPoise(std::uint32_t npc, float poise) {
    Combat::Get().Send(proto::NpcPoise{.npc = npc, .poise = std::clamp(poise, 0.f, 1.f)});
  }
  // Host: the session's difficulty (gameDifficulty).
  static void SendDifficulty(std::int32_t level) {
    if (level >= 0 && level <= 3)
      Combat::Get().Send(proto::Difficulty{.level = static_cast<std::uint8_t>(level)});
  }
  // Our V's area effect hit `player`'s puppet: the attacker's side, for
  // their game to apply on their V.
  static void SendAreaHit(std::uint32_t player, float physical, float thermal, float electric,
                          float chemical, std::int32_t attackType, std::int32_t flags,
                          Red::Vector4 position, Red::Vector4 direction, Red::TweakDBID attack,
                          Red::TweakDBID weapon) {
    Combat::Get().Send(proto::AreaHit{.player = player,
                                      .physical = physical,
                                      .thermal = thermal,
                                      .electric = electric,
                                      .chemical = chemical,
                                      .attackType = static_cast<std::uint8_t>(attackType),
                                      .flags = static_cast<std::uint8_t>(flags),
                                      .position = Vec(position),
                                      .direction = Vec(direction),
                                      .attack = attack.value,
                                      .weapon = weapon.value});
  }
  // Our V threw, launched or fired (smart) an object the others must see
  // (proto::Throw; kind: 0 grenade, 1 knife, 2 launcher round, 3 smart round).
  static void SendThrow(std::uint32_t id, std::int32_t kind, std::int32_t flags,
                        Red::TweakDBID item, Red::Vector4 origin, Red::Vector4 velocity,
                        std::uint32_t npc, std::uint32_t at) {
    if (kind < 0 || kind > static_cast<std::int32_t>(proto::ThrowKind::Smart))
      return;
    Combat::Get().Send(proto::Throw{.id = id,
                                    .kind = static_cast<proto::ThrowKind>(kind),
                                    .flags = static_cast<std::uint8_t>(flags),
                                    .item = item.value,
                                    .origin = Vec(origin),
                                    .velocity = Vec(velocity),
                                    .npc = npc,
                                    .at = at});
  }
  // Its end (how: 0 exploded, 1 stuck, 2 gone).
  static void SendThrowEnd(std::uint32_t id, std::int32_t how, Red::Vector4 position) {
    if (how < 0 || how > static_cast<std::int32_t>(proto::ThrowEndKind::Gone))
      return;
    Combat::Get().Send(proto::ThrowEnd{
        .id = id, .how = static_cast<proto::ThrowEndKind>(how), .position = Vec(position)});
  }
  // Host: the session's rules, sent when they change: ConfiguredRules' word
  // (proto::kRule* in the low byte, enemyHealth in the next).
  static void SendRules(std::int32_t rules) {
    Combat::Get().Send(proto::Rules{.flags = static_cast<std::uint8_t>(rules & proto::kRuleFlags),
                                    .enemyHealth = static_cast<std::uint8_t>(std::min<std::int32_t>(
                                        (rules >> 8) & 0xFF, proto::kMaxEnemyHealth))});
  }
  // Host: the rules tandem.ini asks for (friendly_fire, enemy_scaling,
  // enemy_scaling_health), as one word: one number for the scripts to
  // compare with what they sent.
  static std::int32_t ConfiguredRules() {
    const auto &config = Runtime::Get().config;
    proto::Rules rules{.flags = config.friendlyFire ? proto::kRuleFriendlyFire : std::uint8_t{0}};
    rules = scaling::WithRule(rules, config.enemyScaling, config.enemyScalingHealth);
    return rules.flags | rules.enemyHealth << 8;
  }
  // The session's rules as this game knows them: the Vs hurt each other.
  static bool FriendlyFire() { return combat::FriendlyFire(Combat::Get().Rules()); }
  // Guest: a host NPC's attack on our V was blocked (1) or parried (2) here.
  static void SendDefended(std::uint32_t npc, std::int32_t how) {
    if (how != static_cast<std::int32_t>(proto::DefendKind::Blocked) &&
        how != static_cast<std::int32_t>(proto::DefendKind::Parried))
      return;
    Combat::Get().Send(proto::Defended{.npc = npc, .how = static_cast<proto::DefendKind>(how)});
  }
  // Guest: how long (seconds) a melee hit of the host's NPC `npc` waits
  // before our game runs it, so it lands when its proxy is drawn landing it.
  static float MeleeHold(std::uint32_t npc) {
    const auto stats = Runtime::Get().StatsOf(kNpcPuppetBase + npc);
    if (!stats)
      return 0.f;
    return static_cast<float>(combat::MeleeHoldUs(stats->delayUs, stats->transitUs)) / 1e6f;
  }
  // Throws dropped since the start for arriving too late (a stall).
  static std::int32_t StaleThrows() {
    return static_cast<std::int32_t>(Combat::Get().StaleThrows());
  }
  // What the session said about hits since the last call (vendors and
  // lending are kept for their own natives instead).
  static Red::DynArray<TandemFightEvent> TakeEvents() {
    Red::DynArray<TandemFightEvent> out;
    for (const auto &message : Combat::Get().TakeInbound()) {
      // Who may drive whose cars: kept here for the vehicle scripts.
      if (const auto *lend = std::get_if<proto::CarLend>(&message)) {
        Combat::Get().NoteLend(*lend);
        continue;
      }
      // The session's vendors: kept here for Vendors.reds (TandemProgress).
      if (const auto *vendor = std::get_if<proto::NpcVendor>(&message)) {
        Combat::Get().NoteVendor(*vendor);
        continue;
      }
      // Placed NPCs and the session's dead: kept for Npcs.reds and
      // Ledger.reds (TandemRegions).
      if (const auto *placed = std::get_if<proto::NpcPlaced>(&message)) {
        Regions::Get().NotePlaced(*placed);
        continue;
      }
      if (const auto *death = std::get_if<proto::NpcDeath>(&message)) {
        Regions::Get().NoteDeath(*death);
        continue;
      }
      if (auto event = EventOf(message); event.kind != 0)
        out.EmplaceBack(event);
    }
    return out;
  }
  // Host: the health points a guest's strike takes (core/combat.hpp).
  static float StrikeLoss(float health, float maxHealth, float damage, std::int32_t flags,
                          float minHealth, bool immortal) {
    return combat::StrikeLoss(
        {.damage = damage, .minHealth = minHealth, .flags = static_cast<std::uint16_t>(flags)},
        health, maxHealth, immortal);
  }

  RTTI_IMPL_TYPEINFO(TandemFight);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemFightEvent, "TandemFightEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(damage);
  RTTI_PROPERTY(poise);
  RTTI_PROPERTY(minHealth);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(attackType);
  RTTI_PROPERTY(zone);
  RTTI_PROPERTY(part);
  RTTI_PROPERTY(shape);
  RTTI_PROPERTY(level);
  RTTI_PROPERTY(attack);
  RTTI_PROPERTY(weapon);
  RTTI_PROPERTY(physical);
  RTTI_PROPERTY(thermal);
  RTTI_PROPERTY(electric);
  RTTI_PROPERTY(chemical);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(target);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemFight, "TandemFight", {
  RTTI_METHOD(SendStrike);
  RTTI_METHOD(SendWound);
  RTTI_METHOD(SendPoise);
  RTTI_METHOD(SendDifficulty);
  RTTI_METHOD(SendAreaHit);
  RTTI_METHOD(SendThrow);
  RTTI_METHOD(SendThrowEnd);
  RTTI_METHOD(SendRules);
  RTTI_METHOD(ConfiguredRules);
  RTTI_METHOD(FriendlyFire);
  RTTI_METHOD(SendDefended);
  RTTI_METHOD(MeleeHold);
  RTTI_METHOD(StaleThrows);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(StrikeLoss);
});
