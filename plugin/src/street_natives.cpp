// What scripts/tandem/Traffic.reds and Police.reds can call: `TandemStreet`
// static functions and their structs (docs/architecture/traffic.md,
// police.md). Declared on the script side in
// scripts/tandem/StreetNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "mods.hpp"
#include "runtime.hpp"

#include "tandem/regions.hpp"
#include "tandem/traffic.hpp"

#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <algorithm>

namespace tandem::plugin {
namespace {
// A world vehicle its owner's game has and ours lacks (a modded car in
// traffic): the vanilla default car, in its own look.
std::uint64_t VehicleHere(const proto::TrafficSpawn &spawn) {
  return ModRecords::Get().Resolve(mods::Kind::Vehicle, spawn.record, {},
                                   regions::Announcer(spawn.id));
}
} // namespace

// One thing that happened to a shared vehicle, or a crime of our V seen by
// the area's owner. Plain fields only (the script VM aligns struct locals
// to 8).

struct TandemStreetEvent {
  std::int32_t kind{}; // 1 spawn, 2 gone, 3 status, 4 hit (owner), 5 crime (ours)
  std::uint32_t id{};
  Red::TweakDBID record;
  Red::CName appearance;
  Red::EntityID entity;
  float x{}, y{}, z{};
  float i{}, j{}, k{}, r{1.f};
  std::int32_t flags{};   // spawn, status: kTraffic*; hit: kTrafficHit*; crime: kCrime*
  std::int32_t reason{};  // gone: 0 away, 1 taken, 2 removed; crime: its kind
  std::uint32_t player{}; // gone taken: whose car it is now; hit: who
  float amount{};         // status: health 0..1; hit: damage; crime: share of health
  float dx{}, dy{}, dz{}; // hit: the push (collision) or the way it went
  std::uint32_t npc{};    // crime: the victim or witness as shared
  std::int32_t attack{};  // crime: gamedataAttackType
};
static_assert(alignof(TandemStreetEvent) <= 8, "the script VM aligns struct locals to 8");

// One shared vehicle as the session's record has it.
struct TandemStreetCar {
  std::uint32_t id{};
  Red::TweakDBID record;
  Red::CName appearance;
  Red::EntityID entity;
  float x{}, y{}, z{};
  float i{}, j{}, k{}, r{1.f};
  std::int32_t flags{};
  float health{1.f};
};
static_assert(alignof(TandemStreetCar) <= 8, "the script VM aligns struct locals to 8");

struct TandemStreet : Red::IScriptable {
  // A shared vehicle's id: the owner's player id and its own counter.
  static std::uint32_t MakeId(std::uint32_t owner, std::uint32_t counter) {
    return proto::MakeTrafficId(owner, counter);
  }
  static std::uint32_t OwnerOf(std::uint32_t id) { return proto::TrafficOwner(id); }
  // NpcSeat's owner for a seat in a shared vehicle, and back.
  static std::uint32_t SeatOwner(std::uint32_t id) { return proto::TrafficSeatOwner(id); }
  static bool IsStreetSeat(std::uint32_t owner) { return proto::IsTrafficSeat(owner); }
  static std::uint32_t CarOfSeat(std::uint32_t owner) { return proto::TrafficOfSeat(owner); }

  // Area owner: a world vehicle we share from now on.
  static void SendSpawn(std::uint32_t id, Red::TweakDBID record, Red::CName appearance,
                        Red::EntityID entity, Red::Vector4 position, Red::Quaternion orientation,
                        std::int32_t flags) {
    Runtime::Get().SubmitStreet(proto::TrafficSpawn{
        .id = id,
        .record = record.value,
        .appearance = appearance.hash,
        .entity = entity.hash,
        .position = {position.X, position.Y, position.Z},
        .orientation = {orientation.i, orientation.j, orientation.k, orientation.r},
        .flags = static_cast<std::uint16_t>(flags)});
  }
  // Area owner, ten times a second: every shared vehicle as it is now, and
  // where the players are. What moved goes out, the nearest first.
  static void SendStates(Red::DynArray<std::uint32_t> ids, Red::DynArray<Red::Vector4> positions,
                         Red::DynArray<Red::Quaternion> orientations,
                         Red::DynArray<Red::Vector4> velocities,
                         Red::DynArray<Red::Vector4> viewers) {
    std::vector<traffic::Car> cars;
    for (std::uint32_t n = 0; n < ids.Size() && n < positions.Size() && n < orientations.Size() &&
                              n < velocities.Size() && cars.size() < proto::kMaxTrafficSet;
         ++n)
      cars.push_back({.id = ids[n],
                      .position = {positions[n].X, positions[n].Y, positions[n].Z},
                      .orientation = {orientations[n].i, orientations[n].j, orientations[n].k,
                                      orientations[n].r},
                      .velocity = {velocities[n].X, velocities[n].Y, velocities[n].Z}});
    std::vector<proto::Vec3> eyes;
    for (std::uint32_t n = 0; n < viewers.Size(); ++n)
      eyes.push_back({viewers[n].X, viewers[n].Y, viewers[n].Z});
    Runtime::Get().SubmitTrafficCars(cars, eyes);
  }
  // Area owner: it is no longer shared (0 out of range, 2 removed for good).
  static void SendGone(std::uint32_t id, std::int32_t reason) {
    if (reason != proto::kTrafficAway && reason != proto::kTrafficRemoved)
      return; // taken is the session's to say (ClaimTraffic)
    Runtime::Get().SubmitStreet(
        proto::TrafficGone{.id = id, .reason = static_cast<std::uint8_t>(reason)});
  }
  // Area owner: how it stands now (health 0..1, kTraffic* flags).
  static void SendStatus(std::uint32_t id, float health, std::int32_t flags) {
    Runtime::Get().SubmitStreet(proto::TrafficStatus{.id = id,
                                                     .health = std::clamp(health, 0.f, 1.f),
                                                     .flags = static_cast<std::uint16_t>(flags)});
  }
  // We hit a vehicle another game owns (its proxy here).
  static void SendHit(std::uint32_t id, float damage, std::int32_t flags, Red::Vector4 position,
                      Red::Vector4 impulse) {
    Runtime::Get().SubmitStreet(proto::TrafficHit{.id = id,
                                                  .damage = damage,
                                                  .flags = static_cast<std::uint8_t>(flags),
                                                  .position = {position.X, position.Y, position.Z},
                                                  .impulse = {impulse.X, impulse.Y, impulse.Z}});
  }
  // Our V gets into a shared vehicle: it becomes ours if we are first.
  static void Claim(std::uint32_t id, Red::CName seat) {
    Runtime::Get().SubmitStreet(proto::TrafficClaim{.id = id, .seat = seat.hash});
  }
  // What happened to the shared vehicles, and our V's crimes seen elsewhere.
  static Red::DynArray<TandemStreetEvent> TakeEvents() {
    Red::DynArray<TandemStreetEvent> out;
    for (const auto &event : Runtime::Get().TakeStreetEvents()) {
      const auto &s = event.spawn;
      const auto record = VehicleHere(s);
      out.EmplaceBack(
          TandemStreetEvent{.kind = static_cast<std::int32_t>(event.kind),
                            .id = s.id,
                            .record = Red::TweakDBID(record),
                            .appearance = Red::CName(record == s.record ? s.appearance : 0),
                            .entity = Red::EntityID(s.entity),
                            .x = s.position.x,
                            .y = s.position.y,
                            .z = s.position.z,
                            .i = s.orientation.i,
                            .j = s.orientation.j,
                            .k = s.orientation.k,
                            .r = s.orientation.r,
                            .flags = event.flags,
                            .reason = event.reason,
                            .player = event.player,
                            .amount = event.amount,
                            .dx = event.vector.x,
                            .dy = event.vector.y,
                            .dz = event.vector.z,
                            .npc = event.npc,
                            .attack = event.attack});
    }
    return out;
  }
  // Where to draw a shared vehicle now; W = 0 when nothing says.
  static Red::Vector4 Position(std::uint32_t id) {
    const auto view = Runtime::Get().TrafficOf(id);
    return {view.position.x, view.position.y, view.position.z, view.valid ? 1.f : 0.f};
  }
  static Red::Quaternion Orientation(std::uint32_t id) {
    const auto q = Runtime::Get().TrafficOf(id).orientation;
    return {q.i, q.j, q.k, q.r};
  }
  static Red::Vector4 Velocity(std::uint32_t id) {
    const auto view = Runtime::Get().TrafficOf(id);
    return {view.velocity.x, view.velocity.y, view.velocity.z, 0.f};
  }
  // The vehicles shared now, as the session's record has them: the proxies
  // our game must show, no more, no less.
  static Red::DynArray<TandemStreetCar> Shared() {
    Red::DynArray<TandemStreetCar> out;
    for (const auto &[spawn, status] : Runtime::Get().SharedTraffic())
      out.EmplaceBack(TandemStreetCar{
          .id = spawn.id,
          .record = Red::TweakDBID(VehicleHere(spawn)),
          .appearance = Red::CName(VehicleHere(spawn) == spawn.record ? spawn.appearance : 0),
          .entity = Red::EntityID(spawn.entity),
          .x = spawn.position.x,
          .y = spawn.position.y,
          .z = spawn.position.z,
          .i = spawn.orientation.i,
          .j = spawn.orientation.j,
          .k = spawn.orientation.k,
          .r = spawn.orientation.r,
          .flags =
              static_cast<std::int32_t>((spawn.flags & ~proto::kTrafficStatusMask) | status.flags),
          .health = status.health});
    return out;
  }
  static bool IsShared(std::uint32_t id) { return Runtime::Get().IsTrafficShared(id); }

  // Police. Our V's wanted level (0..5) and star state (EStarState).
  static void SendHeat(std::int32_t stage, std::int32_t state) {
    Runtime::Get().SubmitLocalHeat(
        {.stage = static_cast<std::uint8_t>(std::clamp(stage, 0, int{proto::kMaxHeat})),
         .state = static_cast<proto::HeatState>(std::clamp(state, 0, 3))});
  }
  // Another player's (0 when not wanted, or unknown).
  static std::int32_t HeatStage(std::uint32_t player) {
    return Runtime::Get().HeatOf(player).stage;
  }
  static std::int32_t HeatState(std::uint32_t player) {
    return static_cast<std::int32_t>(Runtime::Get().HeatOf(player).state);
  }
  // Area owner: `player`'s puppet committed a crime here (proto::CrimeKind,
  // kCrime* flags): for that player's own prevention system.
  static void SendCrime(std::uint32_t player, std::int32_t kind, std::int32_t flags,
                        std::int32_t attack, float amount, std::uint32_t npc, std::uint32_t car,
                        Red::Vector4 position) {
    if (kind < static_cast<std::int32_t>(proto::CrimeKind::Witness) ||
        kind > static_cast<std::int32_t>(proto::CrimeKind::Kill))
      return;
    Runtime::Get().SubmitStreet(proto::Crime{.player = player,
                                             .kind = static_cast<proto::CrimeKind>(kind),
                                             .flags = static_cast<std::uint8_t>(flags),
                                             .attackType = static_cast<std::uint8_t>(attack),
                                             .amount = amount,
                                             .npc = npc,
                                             .traffic = car,
                                             .position = {position.X, position.Y, position.Z}});
  }

  RTTI_IMPL_TYPEINFO(TandemStreet);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreetEvent, "TandemStreetEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(appearance);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(i);
  RTTI_PROPERTY(j);
  RTTI_PROPERTY(k);
  RTTI_PROPERTY(r);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(reason);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(amount);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(attack);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreetCar, "TandemStreetCar", {
  RTTI_PROPERTY(id);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(appearance);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(i);
  RTTI_PROPERTY(j);
  RTTI_PROPERTY(k);
  RTTI_PROPERTY(r);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(health);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreet, "TandemStreet", {
  RTTI_METHOD(MakeId);
  RTTI_METHOD(OwnerOf);
  RTTI_METHOD(SeatOwner);
  RTTI_METHOD(IsStreetSeat);
  RTTI_METHOD(CarOfSeat);
  RTTI_METHOD(SendSpawn);
  RTTI_METHOD(SendStates);
  RTTI_METHOD(SendGone);
  RTTI_METHOD(SendStatus);
  RTTI_METHOD(SendHit);
  RTTI_METHOD(Claim);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(Position);
  RTTI_METHOD(Orientation);
  RTTI_METHOD(Velocity);
  RTTI_METHOD(Shared);
  RTTI_METHOD(IsShared);
  RTTI_METHOD(SendHeat);
  RTTI_METHOD(HeatStage);
  RTTI_METHOD(HeatState);
  RTTI_METHOD(SendCrime);
});
