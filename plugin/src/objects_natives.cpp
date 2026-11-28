// What scripts/tandem/Objects.reds and Props.reds can call: `TandemObjects`
// static functions and the `TandemObjectEvent`, `TandemPropEvent` and
// `TandemPropPose` structs (docs/architecture/devices.md, "Glass and
// breakable scenery", "A device's fire"; docs/architecture/props.md). Declared on the script
// side in scripts/tandem/ObjectsNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread (the wraps that feed them only queue: streaming_natives.cpp).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "runtime.hpp"

#include "tandem/consistency.hpp"
#include "tandem/devices.hpp"
#include "tandem/props.hpp"
#include "tandem/protocol.hpp"
#include "tandem/session.hpp"

#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string_view>
#include <vector>

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

// A physics prop for the script (Props.reds). Plain fields only (no Vector4
// or Quaternion: the script VM aligns struct locals to 8).
struct TandemPropEvent {
  // 1 another game's prop starts to move (its first state of a motion),
  // 2 a prop came to rest (another game's word, or the record round our V),
  // 3 another V pushed a prop our game runs
  std::int32_t kind{};
  std::uint32_t player{}; // 1, 2: whose game; 3: whose V
  Red::EntityID entity;
  std::int32_t part{};             // its body's index among the entity's physical components
  float x{}, y{}, z{};             // 2: where it rests; 3: where the pusher's instance stood
  float qi{}, qj{}, qk{}, qr{1.f}; // 2: its pose
  float vx{}, vy{}, vz{};          // 3: the velocity the push gave it
};
static_assert(alignof(TandemPropEvent) <= 8, "the script VM aligns struct locals to 8");

// Where another game's moving prop is drawn now.
struct TandemPropPose {
  bool found{}; // false: no word for it lately (its game went quiet: let it go)
  std::uint32_t owner{};
  float x{}, y{}, z{};
  float qi{}, qj{}, qk{}, qr{1.f};
};
static_assert(alignof(TandemPropPose) <= 8, "the script VM aligns struct locals to 8");

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

// Physics props (the game thread only): the others' moving props as their
// states came (drawn in the past), the record of rests as the host sent it
// (our own written at once), what the scripts are to hear, ours moving this
// round.
struct PropStore {
  struct Moving {
    proto::PlayerId owner{};
    props::Track track;
  };
  std::map<props::Key, Moving> theirs;
  props::Record record;
  std::vector<TandemPropEvent> events;
  std::vector<props::Prop> ours;
  props::Budget budget;
  std::uint64_t states{}, rests{}, pushes{}, told{}, restsTold{}, pushesTold{}, stalePushes{};
  bool velocity{true}; // the body's own velocity setter is there (RTTI)
};
PropStore &Props() {
  static PropStore store;
  return store;
}

props::Key KeyOf(Red::EntityID entity, std::int32_t part) {
  return {entity.hash, static_cast<std::uint8_t>(std::clamp(part, 0, 255))};
}

proto::Vec3 VecOf(const Red::Vector4 &v) { return {v.X, v.Y, v.Z}; }
proto::Quat QuatOf(const Red::Quaternion &q) { return {q.i, q.j, q.k, q.r}; }

TandemPropEvent RestEvent(const proto::PropRest &rest) {
  return {.kind = 2,
          .player = rest.owner,
          .entity = Red::EntityID(rest.entity),
          .part = rest.part,
          .x = rest.position.x,
          .y = rest.position.y,
          .z = rest.position.z,
          .qi = rest.orientation.i,
          .qj = rest.orientation.j,
          .qk = rest.orientation.k,
          .qr = rest.orientation.r};
}

// Another game's word on props, from the object queue.
void TakeProp(const proto::Message &message) {
  auto &store = Props();
  if (const auto *states = std::get_if<proto::PropStates>(&message)) {
    for (const auto &entry : states->props) {
      const props::Key key{entry.entity, entry.part};
      auto &moving = store.theirs[key];
      // A new motion: its owner's first word, or one after a silence.
      const bool fresh = moving.track.Empty() || moving.owner != states->owner ||
                         states->timeUs > moving.track.LastUs() + props::kHoldUs;
      if (fresh) {
        moving = {.owner = states->owner};
        store.events.push_back({.kind = 1,
                                .player = states->owner,
                                .entity = Red::EntityID(entry.entity),
                                .part = entry.part,
                                .x = entry.position.x,
                                .y = entry.position.y,
                                .z = entry.position.z});
      }
      moving.track.Add(states->timeUs, props::Unpack(entry));
      // Bounded while nobody takes them (a loading screen).
      if (store.events.size() > 1024)
        store.events.erase(store.events.begin());
      ++store.states;
    }
  } else if (const auto *rest = std::get_if<proto::PropRest>(&message)) {
    // The record as the host has it; ours came back (written already).
    const bool news = store.record.Note(*rest);
    if (rest->owner != Runtime::Get().You() && news) {
      store.theirs.erase({rest->entity, rest->part});
      store.events.push_back(RestEvent(*rest));
      ++store.rests;
    }
  } else if (const auto *push = std::get_if<proto::PropPush>(&message)) {
    // A kick a stall held back is wrong late (consistency::DropStale).
    std::vector<consistency::Timed<proto::PropPush>> one{{*push, push->timeUs}};
    if (consistency::DropStale(one, Runtime::Get().HostNowUs(), proto::kPropPushMaxAgeUs) > 0) {
      ++store.stalePushes;
      return;
    }
    store.events.push_back({.kind = 3,
                            .player = push->player,
                            .entity = Red::EntityID(push->entity),
                            .part = push->part,
                            .x = push->position.x,
                            .y = push->position.y,
                            .z = push->position.z,
                            .vx = push->velocity.x,
                            .vy = push->velocity.y,
                            .vz = push->velocity.z});
    ++store.pushes;
  }
}
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
      if (std::holds_alternative<proto::PropStates>(message) ||
          std::holds_alternative<proto::PropRest>(message) ||
          std::holds_alternative<proto::PropPush>(message)) {
        // Physics props: kept for Props.reds (TakeProps, PropDrawn).
        TakeProp(message);
      } else if (const auto *fracture = std::get_if<proto::Fracture>(&message)) {
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

  // A new session (or its end): nothing broken is remembered. What the
  // others said stays queued: a joiner's replay of the session's breaks
  // comes with the welcome, before the scripts see the new session (the net
  // thread empties the queue between sessions, Runtime::ResetSession).
  static void Reset() {
    Glass().Clear();
    Props() = {};
  }

  // ---- physics props (Props.reds, docs/architecture/props.md) ----

  // A prop our game runs moves: its state for this round (PropsRound).
  static void PropMoving(Red::EntityID entity, std::int32_t part, Red::Vector4 position,
                         Red::Quaternion orientation, Red::Vector4 velocity) {
    if (entity.hash == 0)
      return;
    const auto key = KeyOf(entity, part);
    Props().ours.push_back({.entity = key.first,
                            .part = key.second,
                            .position = VecOf(position),
                            .orientation = QuatOf(orientation),
                            .velocity = VecOf(velocity)});
  }

  // The round's states to the others (ten a second): the nearest to our V
  // first, at most a packet's (props::Budget). True: some went.
  static bool PropsRound(Red::Vector4 ours) {
    auto &store = Props();
    if (store.ours.empty())
      return false;
    const std::vector<proto::Vec3> viewers{VecOf(ours)};
    proto::PropStates states{.props = store.budget.Pick(store.ours, viewers)};
    store.ours.clear();
    store.told += states.props.size();
    Runtime::Get().SubmitObject(std::move(states));
    return true;
  }

  // A prop our game runs came to rest: the session's record (ours at once).
  static void PropRested(Red::EntityID entity, std::int32_t part, Red::Vector4 position,
                         Red::Quaternion orientation) {
    if (entity.hash == 0)
      return;
    const auto key = KeyOf(entity, part);
    proto::PropRest rest{.owner = Runtime::Get().You(),
                         .entity = key.first,
                         .part = key.second,
                         .position = VecOf(position),
                         .orientation = QuatOf(orientation)};
    Props().record.Note(rest);
    ++Props().restsTold;
    Runtime::Get().SubmitObject(rest);
  }

  // Our V pushed our instance of a prop another game runs.
  static void PropPushed(Red::EntityID entity, std::int32_t part, Red::Vector4 position,
                         Red::Vector4 velocity) {
    if (entity.hash == 0)
      return;
    const auto key = KeyOf(entity, part);
    ++Props().pushesTold;
    Runtime::Get().SubmitObject(proto::PropPush{.entity = key.first,
                                                .part = key.second,
                                                .position = VecOf(position),
                                                .velocity = VecOf(velocity)});
  }

  // What the others said of props since the last call.
  static Red::DynArray<TandemPropEvent> TakeProps() {
    Red::DynArray<TandemPropEvent> out;
    for (const auto &event : std::exchange(Props().events, {}))
      out.EmplaceBack(event);
    return out;
  }

  // Where another game's moving prop is drawn now (its states
  // props::kDrawDelayUs in the past); not found once its game has said
  // nothing of it for props::kHoldUs (it went: ours lets it go).
  static TandemPropPose PropDrawn(Red::EntityID entity, std::int32_t part) {
    auto &store = Props();
    const auto it = store.theirs.find(KeyOf(entity, part));
    if (it == store.theirs.end())
      return {};
    const auto now = Runtime::Get().HostNowUs();
    if (now > it->second.track.LastUs() + props::kHoldUs) {
      store.theirs.erase(it);
      return {};
    }
    const auto prop = it->second.track.At(now - std::min(now, props::kDrawDelayUs));
    if (!prop)
      return {};
    return {.found = true,
            .owner = it->second.owner,
            .x = prop->position.x,
            .y = prop->position.y,
            .z = prop->position.z,
            .qi = prop->orientation.i,
            .qj = prop->orientation.j,
            .qk = prop->orientation.k,
            .qr = prop->orientation.r};
  }

  // Another game moves this prop now (ours holds its instance to it).
  static bool PropTheirs(Red::EntityID entity, std::int32_t part) {
    return Props().theirs.contains(KeyOf(entity, part));
  }

  // The record's rests within `radius` of `position` (at most 32): a prop
  // that streamed in where its sector puts it is put where it rests.
  static Red::DynArray<TandemPropEvent> PropsAround(Red::Vector4 position, float radius) {
    Red::DynArray<TandemPropEvent> out;
    for (const auto &rest : Props().record.Around(VecOf(position), radius, 32))
      out.EmplaceBack(RestEvent(rest));
    return out;
  }

  // The body's own velocity, set the engine's way (its RTTI setter, which
  // the game's scripts do not declare). False: the setter is not there.
  static bool SetBodyVelocity(Red::Handle<Red::IScriptable> body, Red::Vector4 velocity) {
    if (!body || !Props().velocity)
      return false;
    if (!Red::CallVirtual(body.instance, "SetLinearVelocity", velocity)) {
      Props().velocity = false;
      return false;
    }
    return true;
  }

  // A placed entity's id from its hex (as EntityHex writes it): the tests'
  // commands (props at HEX, test query prop HEX). None: not hex.
  static Red::EntityID EntityFromHex(Red::CString text) {
    std::string_view hex(text.c_str());
    if (hex.starts_with("0x"))
      hex.remove_prefix(2);
    std::uint64_t value = 0;
    if (hex.empty() || hex.size() > 16)
      return Red::EntityID(0ull);
    for (const char c : hex) {
      const int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
      if (digit < 0)
        return Red::EntityID(0ull);
      value = value * 16 + static_cast<std::uint64_t>(digit);
    }
    return Red::EntityID(value);
  }

  // (tools/game.sh cmd props) one line for the log.
  static Red::CString PropsReport() {
    const auto &store = Props();
    return Red::CString(
        std::format("props: {} state(s) told, {} rest(s) told, {} push(es) told; heard {} "
                    "state(s), {} rest(s), {} push(es) ({} stale); {} moving here by another "
                    "game, {} in the record",
                    store.told, store.restsTold, store.pushesTold, store.states, store.rests,
                    store.pushes, store.stalePushes, store.theirs.size(), store.record.Size())
            .c_str());
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

  // (the parity dump) the session's record of what broke, the same on every
  // game that heard it all: its pieces and their digest.
  static Red::CString Record() {
    const auto record = Runtime::Get().FractureRecord();
    return Red::CString(std::format("record: {} piece(s), digest {:08x}", record >> 32,
                                    static_cast<std::uint32_t>(record))
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemPropEvent, "TandemPropEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(part);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(qi);
  RTTI_PROPERTY(qj);
  RTTI_PROPERTY(qk);
  RTTI_PROPERTY(qr);
  RTTI_PROPERTY(vx);
  RTTI_PROPERTY(vy);
  RTTI_PROPERTY(vz);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemPropPose, "TandemPropPose", {
  RTTI_PROPERTY(found);
  RTTI_PROPERTY(owner);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(qi);
  RTTI_PROPERTY(qj);
  RTTI_PROPERTY(qk);
  RTTI_PROPERTY(qr);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemObjects, "TandemObjects", {
  RTTI_METHOD(Broke);
  RTTI_METHOD(Replaying);
  RTTI_METHOD(Fire);
  RTTI_METHOD(Hands);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(Reset);
  RTTI_METHOD(Report);
  RTTI_METHOD(Record);
  RTTI_METHOD(PropMoving);
  RTTI_METHOD(PropsRound);
  RTTI_METHOD(PropRested);
  RTTI_METHOD(PropPushed);
  RTTI_METHOD(TakeProps);
  RTTI_METHOD(PropDrawn);
  RTTI_METHOD(PropTheirs);
  RTTI_METHOD(PropsAround);
  RTTI_METHOD(SetBodyVelocity);
  RTTI_METHOD(EntityFromHex);
  RTTI_METHOD(PropsReport);
});
