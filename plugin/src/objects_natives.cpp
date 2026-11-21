// What scripts/tandem/Objects.reds can call: `TandemObjects` static
// functions and the `TandemObjectEvent` struct (docs/architecture/devices.md,
// "Glass and breakable scenery", "A device's fire"). Declared on the script
// side in scripts/tandem/ObjectsNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread (the wraps that feed them only queue: streaming_natives.cpp).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "runtime.hpp"

#include "tandem/devices.hpp"
#include "tandem/protocol.hpp"
#include "tandem/session.hpp"

#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <algorithm>
#include <cstdint>
#include <format>

namespace tandem::plugin {
// Something of the world for the script to show. Plain fields only: the
// script VM aligns struct locals to 8.
struct TandemObjectEvent {
  // 1 something broke, 2 a device's fire, 3 a turret in a player's hands,
  // 4 our claim on a turret refused (another player holds it)
  std::int32_t kind{};
  // 1: whose game saw it break; 2 (seen, lost): whose V; 3: whose hands; 4: who holds it
  std::uint32_t player{};
  // 1: the placed entity it is a part of (none: the world's); 2 to 4: the device
  Red::EntityID entity;
  Red::CName name;      // 1: its destruction component
  float x{}, y{}, z{};  // where (3: where it points)
  std::int32_t value{}; // 1: the level of destruction; 2: proto::DeviceFireKind; 3: proto::kHands*
  std::int32_t targetKind{};  // 2: proto::TargetKind
  std::uint32_t target{};     // 2: the NPC, or the player
  Red::EntityID targetEntity; // 2: the placed entity
};
static_assert(alignof(TandemObjectEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
// What broke lately, ours and theirs (the game thread only).
devices::Fractures &Glass() {
  static devices::Fractures glass;
  return glass;
}
// A device's shot older than this on the host clock is not fired (a stall,
// a loading screen: a pile of them at once would be wrong).
constexpr std::uint64_t kShotMaxAgeUs = 1'500'000;
std::uint64_t g_staleShots = 0;
} // namespace

struct TandemObjects : Red::IScriptable {
  // Something broke in our game: a destruction component of a placed entity
  // (entity and name), or a piece of the world at a place. True: the others
  // are told (not the echo of a break we replayed, not a piece told already).
  static bool Broke(Red::EntityID entity, Red::CName name, Red::Vector4 position,
                    std::int32_t level) {
    proto::Fracture fracture{.entity = entity.hash,
                             .component = name.hash,
                             .at = {position.X, position.Y, position.Z},
                             .level = static_cast<std::uint8_t>(std::clamp(level, 0, 255))};
    if (fracture.entity == 0 || fracture.component == 0)
      fracture.entity = fracture.component = 0;
    if (!Glass().Ours(fracture, net::NowUs()))
      return false;
    Runtime::Get().SubmitObject(fracture);
    return true;
  }

  // Another game's break that waited for our V (Objects.reds), replayed
  // now: its echo is not told.
  static void Replaying(Red::EntityID entity, Red::CName name, Red::Vector4 position) {
    proto::Fracture fracture{
        .entity = entity.hash, .component = name.hash, .at = {position.X, position.Y, position.Z}};
    if (fracture.entity == 0 || fracture.component == 0)
      fracture.entity = fracture.component = 0;
    Glass().Replaying(fracture, net::NowUs());
  }

  // A device's fire. Host: our turret turned to a target (kind 1), shot at
  // it (2) or follows nobody (0); the target a shared NPC, a player, a
  // placed entity, else the place alone. Guest: our copy of it locked our
  // own V (3) or lost it (4).
  static void Fire(Red::EntityID entity, std::int32_t kind, std::uint32_t npc, std::uint32_t player,
                   Red::EntityID target, Red::Vector4 position) {
    if (entity.hash == 0 || kind < 0 ||
        kind > static_cast<std::int32_t>(proto::DeviceFireKind::Lost))
      return;
    proto::DeviceFire fire{.entity = entity.hash,
                           .kind = static_cast<proto::DeviceFireKind>(kind),
                           .at = {position.X, position.Y, position.Z}};
    if (npc != 0)
      fire.target = {proto::TargetKind::Npc, npc};
    else if (player != 0)
      fire.target = {proto::TargetKind::Player, player};
    else if (target.hash != 0)
      fire.target = {proto::TargetKind::Entity, target.hash};
    Runtime::Get().SubmitObject(fire);
  }

  // A turret in our V's hands: where it points, whether it fires (1), or
  // that we let go of it (2).
  static void Hands(Red::EntityID entity, Red::Vector4 position, std::int32_t flags) {
    if (entity.hash == 0)
      return;
    Runtime::Get().SubmitObject(
        proto::TurretHands{.entity = entity.hash,
                           .at = {position.X, position.Y, position.Z},
                           .flags = static_cast<std::uint8_t>(flags & proto::kHandsFlags)});
  }

  // What the others' games say since the last call: breaks for ours to
  // replay (those not broken here already), a device's fire.
  static Red::DynArray<TandemObjectEvent> TakeEvents() {
    Red::DynArray<TandemObjectEvent> out;
    for (const auto &message : Runtime::Get().TakeObjects()) {
      if (const auto *fracture = std::get_if<proto::Fracture>(&message)) {
        if (!Glass().Theirs(*fracture, net::NowUs()))
          continue;
        out.EmplaceBack(TandemObjectEvent{.kind = 1,
                                          .player = fracture->player,
                                          .entity = Red::EntityID(fracture->entity),
                                          .name = Red::CName(fracture->component),
                                          .x = fracture->at.x,
                                          .y = fracture->at.y,
                                          .z = fracture->at.z,
                                          .value = fracture->level});
      } else if (const auto *hands = std::get_if<proto::TurretHands>(&message)) {
        out.EmplaceBack(TandemObjectEvent{.kind = 3,
                                          .player = hands->player,
                                          .entity = Red::EntityID(hands->entity),
                                          .x = hands->at.x,
                                          .y = hands->at.y,
                                          .z = hands->at.z,
                                          .value = hands->flags});
      } else if (const auto *busy = std::get_if<proto::TurretBusy>(&message)) {
        out.EmplaceBack(TandemObjectEvent{
            .kind = 4, .player = busy->holder, .entity = Red::EntityID(busy->entity)});
      } else if (const auto *fire = std::get_if<proto::DeviceFire>(&message)) {
        if (fire->kind == proto::DeviceFireKind::Shot) {
          const auto now = Runtime::Get().HostNowUs();
          if (now > fire->timeUs && now - fire->timeUs > kShotMaxAgeUs) {
            ++g_staleShots;
            continue;
          }
        }
        const auto &target = fire->target;
        out.EmplaceBack(TandemObjectEvent{
            .kind = 2,
            .player = fire->player,
            .entity = Red::EntityID(fire->entity),
            .x = fire->at.x,
            .y = fire->at.y,
            .z = fire->at.z,
            .value = static_cast<std::int32_t>(fire->kind),
            .targetKind = static_cast<std::int32_t>(target.kind),
            .target =
                target.kind == proto::TargetKind::Npc || target.kind == proto::TargetKind::Player
                    ? static_cast<std::uint32_t>(target.id)
                    : 0,
            .targetEntity =
                Red::EntityID(target.kind == proto::TargetKind::Entity ? target.id : 0)});
      }
    }
    return out;
  }

  // A new session (or its end): nothing broken is remembered.
  static void Reset() {
    Glass().Clear();
    Runtime::Get().TakeObjects();
  }

  // (tools/game.sh cmd glass) one line for the log.
  static Red::CString Report() {
    const auto &glass = Glass();
    return Red::CString(std::format("objects: {} break(s) told, {} replayed, {} echo(es) of our "
                                    "replays, {} repeat(s), {} held by our rate; {} stale device "
                                    "shot(s) dropped",
                                    glass.Told(), glass.Replayed(), glass.Echoes(), glass.Repeats(),
                                    glass.Held(), g_staleShots)
                            .c_str());
  }

  RTTI_IMPL_TYPEINFO(TandemObjects);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemObjectEvent, "TandemObjectEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(value);
  RTTI_PROPERTY(targetKind);
  RTTI_PROPERTY(target);
  RTTI_PROPERTY(targetEntity);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemObjects, "TandemObjects", {
  RTTI_METHOD(Broke);
  RTTI_METHOD(Replaying);
  RTTI_METHOD(Fire);
  RTTI_METHOD(Hands);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(Reset);
  RTTI_METHOD(Report);
});
