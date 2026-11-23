// What scripts/tandem/Wheels.reds can call: `TandemWheels` static functions
// and their structs (docs/architecture/vehicle-combat.md). Declared on the
// script side in scripts/tandem/WheelsNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "engine/guards.hpp"
#include "engine/vehiclemove.hpp"
#include "runtime.hpp"

#include "tandem/vehicle_combat.hpp"

#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/Entity.hpp>
#include <RedLib.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace tandem::plugin {
namespace {
proto::Vec3 Of(const Red::Vector4 &v) { return {v.X, v.Y, v.Z}; }
// The disposals asked for by the scripts (which may run on job threads),
// done on the game thread's frame (engine::RunDisposals).
std::mutex g_disposeLock;
std::vector<Red::Handle<Red::ent::Entity>> g_disposals;
bool Finite(const Red::Vector4 &v) {
  return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
}
} // namespace

// One thing that happened to a vehicle, or to a V in one. Plain fields only
// (the script VM aligns struct locals to 8).
struct TandemWheelsEvent {
  // 1 damage, 2 hit (ours), 3 exit, 4 weapons in a vehicle, 5 mounted shot,
  // 6 gunner seat, 7 ram (area owner), 8 chase
  std::int32_t kind{};
  std::uint32_t player{};
  std::uint32_t vehicle{};
  std::int32_t a{}, b{}, c{};
  Red::TweakDBID item; // weapon, or the damaged vehicle's record
  Red::TweakDBID slot; // a mounted shot's slot
  Red::CName seat;     // a gunner seat
  float amount{};
  std::uint32_t npc{};
  float x{}, y{}, z{};
  float dx{}, dy{}, dz{};
};
static_assert(alignof(TandemWheelsEvent) <= 8, "the script VM aligns struct locals to 8");

// How a vehicle another game owns is damaged, as last heard.
struct TandemWheelsDamage {
  bool known{};
  std::uint32_t vehicle{};
  Red::TweakDBID record;
  float health{1.f};
  std::int32_t flags{};
  std::int32_t tyres{};
  std::int32_t windows{};
  std::uint32_t instigator{};
};
static_assert(alignof(TandemWheelsDamage) <= 8, "the script VM aligns struct locals to 8");

// A push another player's car gave ours, due now.
struct TandemWheelsPush {
  std::uint32_t player{};
  float dx{}, dy{}, dz{};
  float x{}, y{}, z{};
};
static_assert(alignof(TandemWheelsPush) <= 8, "the script VM aligns struct locals to 8");

struct TandemWheels : Red::IScriptable {
  // A vehicle another game drives, moved to where its driver's game says in
  // `seconds`: the engine's own kinematic move (engine/vehiclemove.hpp), with
  // the velocity that takes it there. False: not moved (the caller teleports).
  // The kinematic move can be used in this game (the vehicle class is the one
  // measured): checked once, a mismatch logged once.
  static bool CanMove() { return engine::CanMoveVehicles(); }
  static bool Move(Red::Handle<Red::ent::Entity> vehicle, Red::Vector4 position,
                   Red::Quaternion orientation, float seconds) {
    return engine::MoveVehicle(vehicle.instance, position.X, position.Y, position.Z, orientation.i,
                               orientation.j, orientation.k, orientation.r, seconds);
  }
  // The engine's kinematic state for a vehicle driven by Move
  // (engine/vehiclemove.hpp): on, its body is kinematic and Move puts it
  // where told; off, its physics are its own again. False: not so now (not
  // a vehicle, or the setter is not the one measured: CanHold).
  static bool CanHold() { return engine::CanHoldVehicles(); }
  static bool Hold(Red::Handle<Red::ent::Entity> vehicle, bool on) {
    return engine::HoldVehicle(vehicle.instance, on);
  }
  static bool Held(Red::Handle<Red::ent::Entity> vehicle) {
    return engine::VehicleHeld(vehicle.instance);
  }
  // The engine's own removal of an entity from the world (entEntity.Dispose,
  // a native the scripts do not declare): our own crowd's vehicles and their
  // riders where another game runs the street (Traffic.reds Remove).
  // Queued for the game thread's frame (RunDisposals): never in a script
  // tick, which may run on a job thread beside the scene's (§13.157).
  static bool Dispose(Red::Handle<Red::ent::Entity> entity) {
    if (!entity)
      return false;
    std::scoped_lock lock(g_disposeLock);
    if (g_disposals.size() < 256)
      g_disposals.push_back(entity);
    return true;
  }

  // A vehicle named across games: a player's car, a shared world vehicle.
  static std::uint32_t CarOf(std::uint32_t player) { return proto::PlayerCar(player); }
  static std::uint32_t StreetOf(std::uint32_t id) { return proto::StreetCar(id); }
  static bool IsStreet(std::uint32_t vehicle) { return proto::IsStreetCar(vehicle); }
  static std::uint32_t TrafficIdOf(std::uint32_t vehicle) { return proto::TrafficOfSeat(vehicle); }
  // The game that runs it: a player's car its player's; a shared one its
  // owner's as the session has it now (a hand-over moves it).
  static std::uint32_t OwnerOf(std::uint32_t vehicle) {
    if (proto::IsStreetCar(vehicle))
      if (const auto owner = Runtime::Get().TrafficOwnerOf(proto::TrafficOfSeat(vehicle)))
        return owner;
    return proto::OwnerOfVehicle(vehicle);
  }

  // ---- our own vehicles ----

  // The destruction grid our car's game reports (VehicleGridDestructionEvent.state).
  static void NoteGrid(std::uint32_t vehicle, Red::DynArray<float> cells) {
    std::vector<float> values;
    for (std::uint32_t n = 0; n < cells.Size() && n < proto::kDamageGrid; ++n)
      values.push_back(cells[n]);
    Runtime::Get().NoteOwnGrid(vehicle, values);
  }
  // A part came off our car (VehicleOnPartDetachedEvent).
  static void NotePart(std::uint32_t vehicle, Red::CName part) {
    Runtime::Get().NoteOwnPart(vehicle, part.hash);
  }
  // How one of our vehicles stands now; sent when it changed. `exploded`:
  // it blew up (the first time it says so is when).
  static void SendDamage(std::uint32_t vehicle, Red::TweakDBID record, float health,
                         std::int32_t flags, std::int32_t tyres, std::int32_t windows,
                         std::uint32_t instigator) {
    auto &runtime = Runtime::Get();
    auto damage = runtime.OwnDamage(vehicle);
    const bool wasDestroyed = (damage.flags & proto::kDamageDestroyed) != 0;
    damage.vehicle = vehicle;
    damage.record = record.value;
    damage.health = std::isfinite(health) ? std::clamp(health, 0.f, 1.f) : 1.f;
    damage.flags = static_cast<std::uint8_t>(flags);
    damage.tyres = static_cast<std::uint8_t>(tyres);
    damage.windows = static_cast<std::uint8_t>(windows);
    if ((damage.flags & proto::kDamageDestroyed) && !wasDestroyed) {
      damage.explodedUs = runtime.HostNowUs();
      damage.instigator = instigator;
    }
    if ((damage.flags & proto::kDamageDestroyed) == 0) {
      damage.explodedUs = 0; // repaired
      damage.instigator = 0;
    }
    runtime.SetOwnDamage(damage);
    runtime.SubmitWheels(damage);
  }
  // A car we no longer own (another player took it): forget what we said.
  static void ForgetDamage(std::uint32_t vehicle) { Runtime::Get().ForgetOwnDamage(vehicle); }

  // ---- what our V does ----

  // We hit a vehicle another game owns (kVehicleHit* flags; -1: no tyre or window).
  static void SendHit(std::uint32_t vehicle, float damage, std::int32_t flags, std::int32_t tyre,
                      std::int32_t window, Red::Vector4 position, Red::Vector4 direction) {
    Runtime::Get().SubmitWheels(proto::VehicleHit{.vehicle = vehicle,
                                                  .damage = damage,
                                                  .flags = static_cast<std::uint8_t>(flags),
                                                  .tyre = static_cast<std::int8_t>(tyre),
                                                  .window = static_cast<std::int8_t>(window),
                                                  .position = Of(position),
                                                  .direction = Of(direction)});
  }
  // Our V left `vehicle` the hard way (proto::ExitKind, kExit* flags).
  static void SendExit(std::uint32_t vehicle, std::int32_t kind, std::int32_t flags,
                       Red::Vector4 velocity) {
    if (kind < 0 || kind > static_cast<std::int32_t>(proto::ExitKind::Exploded))
      return;
    Runtime::Get().SubmitWheels(proto::VehicleExit{.vehicle = vehicle,
                                                   .kind = static_cast<proto::ExitKind>(kind),
                                                   .flags = static_cast<std::uint8_t>(flags),
                                                   .velocity = Of(velocity)});
  }
  // Our V's weapons in a vehicle (proto::SeatCombat; 0: none).
  static void SendCombat(std::int32_t mode, std::uint32_t vehicle, Red::TweakDBID weapon) {
    if (mode < 0 || mode > static_cast<std::int32_t>(proto::SeatCombat::Gunner))
      return;
    Runtime::Get().SubmitWheels(proto::VehicleCombat{
        .mode = static_cast<proto::SeatCombat>(mode), .vehicle = vehicle, .weapon = weapon.value});
  }
  // One shot of a vehicle's weapon our V fired.
  static void SendFire(std::uint32_t vehicle, Red::TweakDBID weapon, Red::TweakDBID slot,
                       Red::Vector4 target) {
    Runtime::Get().SubmitWheels(proto::MountedFire{
        .vehicle = vehicle, .weapon = weapon.value, .slot = slot.value, .target = Of(target)});
  }
  // Our V asks for (or leaves) a gunner seat of another player's vehicle.
  static void AskGunner(std::uint32_t vehicle, Red::CName seat, bool leave) {
    Runtime::Get().SubmitWheels(
        proto::GunnerRequest{.vehicle = vehicle, .seat = seat.hash, .leave = leave});
  }
  // Owner: our game could not seat the gunner of one of our vehicles.
  static void FreeGunner(std::uint32_t vehicle) {
    Runtime::Get().SubmitWheels(proto::Gunner{.vehicle = vehicle});
  }
  // Our aim as a gunner, relative to the vehicle (radians).
  static void SendAim(std::uint32_t vehicle, float yaw, float pitch, bool firing) {
    Runtime::Get().SubmitWheels(
        proto::GunnerAim{.vehicle = vehicle,
                         .yaw = yaw,
                         .pitch = pitch,
                         .flags = firing ? proto::kAimFiring : std::uint8_t{0}});
  }
  // Our car touched `player`'s car (its copy here): our game ran our side.
  static void NoteContact(std::uint32_t player) { Runtime::Get().NoteContact(player); }
  // ...and the push it gave theirs, for their game.
  static void SendBump(std::uint32_t other, Red::Vector4 impulse, Red::Vector4 position) {
    if (!Finite(impulse) || !Finite(position))
      return;
    Runtime::Get().SubmitWheels(
        proto::VehicleBump{.other = other, .impulse = Of(impulse), .position = Of(position)});
  }
  // Our car ran into one of the area owner's NPCs (its proxy here).
  static void SendRam(std::uint32_t npc, Red::Vector4 velocity, Red::Vector4 position,
                      std::int32_t flags) {
    Runtime::Get().SubmitWheels(proto::NpcRam{.npc = npc,
                                              .velocity = Of(velocity),
                                              .position = Of(position),
                                              .flags = static_cast<std::uint8_t>(flags)});
  }
  // Area owner: one of our shared vehicles chases `target` (0: nobody).
  static void SendChase(std::uint32_t id, std::uint32_t target, std::int32_t flags) {
    Runtime::Get().SubmitWheels(
        proto::Chase{.vehicle = id, .target = target, .flags = static_cast<std::uint8_t>(flags)});
  }

  // ---- what the others say ----

  static Red::DynArray<TandemWheelsEvent> TakeEvents() {
    Red::DynArray<TandemWheelsEvent> out;
    for (const auto &e : Runtime::Get().TakeWheelsEvents())
      out.EmplaceBack(TandemWheelsEvent{.kind = static_cast<std::int32_t>(e.kind),
                                        .player = e.player,
                                        .vehicle = e.vehicle,
                                        .a = e.a,
                                        .b = e.b,
                                        .c = e.c,
                                        .item = Red::TweakDBID(e.id),
                                        .slot = Red::TweakDBID(e.id2),
                                        .seat = Red::CName(e.id),
                                        .amount = e.amount,
                                        .npc = e.npc,
                                        .x = e.position.x,
                                        .y = e.position.y,
                                        .z = e.position.z,
                                        .dx = e.vector.x,
                                        .dy = e.vector.y,
                                        .dz = e.vector.z});
    return out;
  }
  // How a vehicle another game owns stands (known false: nothing heard).
  static TandemWheelsDamage DamageOf(std::uint32_t vehicle) {
    const auto damage = Runtime::Get().DamageOf(vehicle);
    if (!damage)
      return {.vehicle = vehicle};
    return {.known = true,
            .vehicle = vehicle,
            .record = Red::TweakDBID(damage->record),
            .health = damage->health,
            .flags = damage->flags,
            .tyres = damage->tyres,
            .windows = damage->windows,
            .instigator = damage->instigator};
  }
  // Its destruction grid, cell by cell (0..1), and the parts that came off.
  static float GridCell(std::uint32_t vehicle, std::int32_t cell) {
    const auto damage = Runtime::Get().DamageOf(vehicle);
    return damage && cell >= 0
               ? vehiclecombat::GridCell(damage->grid, static_cast<std::size_t>(cell))
               : 0.f;
  }
  static Red::DynArray<Red::CName> Parts(std::uint32_t vehicle) {
    Red::DynArray<Red::CName> out;
    if (const auto damage = Runtime::Get().DamageOf(vehicle))
      for (const auto part : damage->parts)
        out.EmplaceBack(Red::CName(part));
    return out;
  }
  // Who holds a vehicle's gunner seat (0: nobody), and where they aim.
  static std::uint32_t GunnerOf(std::uint32_t vehicle) {
    return Runtime::Get().GunnerOf(vehicle).gunner;
  }
  static Red::Vector4 AimOf(std::uint32_t vehicle) {
    // X yaw, Y pitch, Z 1 when firing, W 0 when nobody aims.
    const auto aim = Runtime::Get().AimOf(vehicle);
    if (!aim)
      return {0.f, 0.f, 0.f, 0.f};
    return {aim->yaw, aim->pitch, (aim->flags & proto::kAimFiring) ? 1.f : 0.f, 1.f};
  }
  // A player's weapons in a vehicle (proto::SeatCombat; 0: none).
  static std::int32_t SeatCombatOf(std::uint32_t player) {
    return static_cast<std::int32_t>(Runtime::Get().SeatCombatOf(player).mode);
  }
  // The pushes other players' cars gave ours that our game did not feel.
  static Red::DynArray<TandemWheelsPush> TakePushes() {
    Red::DynArray<TandemWheelsPush> out;
    for (const auto &push : Runtime::Get().TakeDuePushes())
      out.EmplaceBack(TandemWheelsPush{.player = push.from,
                                       .dx = push.impulse.x,
                                       .dy = push.impulse.y,
                                       .dz = push.impulse.z,
                                       .x = push.position.x,
                                       .y = push.position.y,
                                       .z = push.position.z});
    return out;
  }

  // ---- the rules (core/include/tandem/vehicle_combat.hpp) ----

  // How our V left a vehicle (proto::ExitKind), from the exit state the game ran.
  static std::int32_t ExitKindOf(bool speed, bool collision, bool death, bool forced, bool bike,
                                 bool destroyed) {
    return static_cast<std::int32_t>(vehiclecombat::Classify({.speed = speed,
                                                              .collision = collision,
                                                              .death = death,
                                                              .forced = forced,
                                                              .bike = bike,
                                                              .destroyed = destroyed}));
  }
  static bool Rolls(std::int32_t kind, float speed, bool driver) {
    return vehiclecombat::Rolls(static_cast<proto::ExitKind>(kind), speed, driver);
  }
  static float RestSpeed() { return vehiclecombat::kRestSpeed; }
  static float RollSeconds() { return static_cast<float>(vehiclecombat::kRollUs) / 1e6f; }
  static std::int32_t RamFlags(float speed, bool crowd) {
    return vehiclecombat::RamFlags(speed, crowd);
  }

  RTTI_IMPL_TYPEINFO(TandemWheels);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemWheelsEvent, "TandemWheelsEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(vehicle);
  RTTI_PROPERTY(a);
  RTTI_PROPERTY(b);
  RTTI_PROPERTY(c);
  RTTI_PROPERTY(item);
  RTTI_PROPERTY(slot);
  RTTI_PROPERTY(seat);
  RTTI_PROPERTY(amount);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemWheelsDamage, "TandemWheelsDamage", {
  RTTI_PROPERTY(known);
  RTTI_PROPERTY(vehicle);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(health);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(tyres);
  RTTI_PROPERTY(windows);
  RTTI_PROPERTY(instigator);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemWheelsPush, "TandemWheelsPush", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(dx);
  RTTI_PROPERTY(dy);
  RTTI_PROPERTY(dz);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemWheels, "TandemWheels", {
  RTTI_METHOD(CanMove);
  RTTI_METHOD(Move);
  RTTI_METHOD(CanHold);
  RTTI_METHOD(Hold);
  RTTI_METHOD(Held);
  RTTI_METHOD(Dispose);
  RTTI_METHOD(CarOf);
  RTTI_METHOD(StreetOf);
  RTTI_METHOD(IsStreet);
  RTTI_METHOD(TrafficIdOf);
  RTTI_METHOD(OwnerOf);
  RTTI_METHOD(NoteGrid);
  RTTI_METHOD(NotePart);
  RTTI_METHOD(SendDamage);
  RTTI_METHOD(ForgetDamage);
  RTTI_METHOD(SendHit);
  RTTI_METHOD(SendExit);
  RTTI_METHOD(SendCombat);
  RTTI_METHOD(SendFire);
  RTTI_METHOD(AskGunner);
  RTTI_METHOD(FreeGunner);
  RTTI_METHOD(SendAim);
  RTTI_METHOD(NoteContact);
  RTTI_METHOD(SendBump);
  RTTI_METHOD(SendRam);
  RTTI_METHOD(SendChase);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(DamageOf);
  RTTI_METHOD(GridCell);
  RTTI_METHOD(Parts);
  RTTI_METHOD(GunnerOf);
  RTTI_METHOD(AimOf);
  RTTI_METHOD(SeatCombatOf);
  RTTI_METHOD(TakePushes);
  RTTI_METHOD(ExitKindOf);
  RTTI_METHOD(Rolls);
  RTTI_METHOD(RestSpeed);
  RTTI_METHOD(RollSeconds);
  RTTI_METHOD(RamFlags);
});

namespace tandem::plugin::engine {
// Game thread, every frame: up to four of the disposals asked for, each
// through the population system when it spawned it, else entEntity.Dispose
// outside the scene's detach phase (the dispose gate); one the gate refuses
// waits for a later frame.
void RunDisposals() {
  std::vector<Red::Handle<Red::ent::Entity>> now;
  {
    std::scoped_lock lock(g_disposeLock);
    if (g_disposals.empty())
      return;
    const auto n = std::min<std::size_t>(g_disposals.size(), 4);
    now.assign(g_disposals.begin(), g_disposals.begin() + static_cast<std::ptrdiff_t>(n));
    g_disposals.erase(g_disposals.begin(), g_disposals.begin() + static_cast<std::ptrdiff_t>(n));
  }
  std::vector<Red::Handle<Red::ent::Entity>> later;
  for (auto &entity : now) {
    if (!entity)
      continue;
    if (!DisposeAllowed(entity.instance)) {
      later.push_back(entity);
      continue;
    }
    if (RemoveIfStubbed(entity.instance) == 0)
      Red::CallVirtual(entity.instance, "Dispose");
  }
  if (!later.empty()) {
    std::scoped_lock lock(g_disposeLock);
    g_disposals.insert(g_disposals.end(), later.begin(), later.end());
  }
}
} // namespace tandem::plugin::engine
