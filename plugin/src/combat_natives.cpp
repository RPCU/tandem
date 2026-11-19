// What scripts/tandem/Outcomes.reds and Combat.reds call for hit outcomes:
// `TandemFight` static functions and the `TandemFightEvent` struct. Declared
// on the script side in scripts/tandem/CombatNatives.reds: keep the two in
// sync (tools/check-natives.py reads every pair). All of these run on the
// game thread, or on a job thread inside the damage pipeline: they only queue.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "combat.hpp"
#include "log.hpp"

#include "tandem/combat.hpp"

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
};
static_assert(alignof(TandemFightEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
enum Kind : std::int32_t {
  kStrike = 1,
  kWound = 2,
  kPoise = 3,
  kDifficulty = 4,
  kAreaHit = 5,
  kSessionLevel = 6,
  kNpcLevel = 7
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
  } else if (const auto *s = std::get_if<proto::SessionLevel>(&message)) {
    out = {.kind = kSessionLevel, .player = s->players, .level = s->level};
  } else if (const auto *n = std::get_if<proto::NpcLevel>(&message)) {
    out = {.kind = kNpcLevel, .npc = n->npc, .damage = n->level};
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
  // What the session said about hits since the last call.
  static Red::DynArray<TandemFightEvent> TakeEvents() {
    Red::DynArray<TandemFightEvent> out;
    for (const auto &message : Combat::Get().TakeInbound()) {
      // Who may drive whose cars: kept here for the vehicle scripts.
      if (const auto *lend = std::get_if<proto::CarLend>(&message)) {
        Combat::Get().NoteLend(*lend);
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
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemFight, "TandemFight", {
  RTTI_METHOD(SendStrike);
  RTTI_METHOD(SendWound);
  RTTI_METHOD(SendPoise);
  RTTI_METHOD(SendDifficulty);
  RTTI_METHOD(SendAreaHit);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(StrikeLoss);
});
