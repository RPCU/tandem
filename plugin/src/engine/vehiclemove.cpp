// A vehicle our game does not simulate, moved as the engine moves one it
// drives kinematically (docs/research/engine/vehicles.md §13.141, §13.155).
//
// vehicleBaseObject's "move to this transform in dt" (address-library hash
// 2933986803, 0x1402e3270 on 2.50; five callers in the exe): it writes the
// transform to the vehicle's runtime data (+0x5F0) and its placed component,
// gives the rigid body (+0x2D8) the transform and the linear and angular
// velocity that reach it in dt (or puts it there at rest when that is
// 100 m/s or more), then the physics proxy: a dynamic one gets those
// velocities, a kinematic one (below) is put at the transform outright.
// Unlike the teleportation facility it is not a teleport: a player mounted
// in the vehicle is not teleported with it (a teleport of the player's mount
// every frame stops the population system's spawns, §13.141).
//
// The kinematic state (§13.155): bit 0x100 of the vehicle's physics state
// (+0x260), set and cleared by the engine's own setter (hash 2592348558,
// 0x1406e4e80: (vehicle, bit, clear)), which a quest's DriveToPoint
// (3713079867) sets before moving a car with the move above every frame.
// Any bit but 0x1 and 0x10 makes the physics proxy kinematic.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "vehiclemove.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldTransform.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/Entity.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kMoveTo = 2933986803;
using MoveToFn = void (*)(RED4ext::ent::Entity *, const RED4ext::WorldTransform &, float);
// (vehicle, physics state bit, clear): sets the bit when `clear` is false.
constexpr std::uint32_t kSetPhysicsState = 2592348558;
using SetPhysicsStateFn = void (*)(RED4ext::ent::Entity *, std::uint32_t, bool);

// vehicleBaseObject as measured on 2.50 (RTTI size; the functions' own
// reads): the move dereferences both pointers without a check.
constexpr std::uint32_t kVehicleSize = 0xBC0;
constexpr std::size_t kRigidBody = 0x2D8;
constexpr std::size_t kRuntimeData = 0x5F0;
constexpr std::size_t kPhysicsState = 0x260;
constexpr std::uint32_t kDrivenKinematically = 0x100;
// The setter's own read of the state, 0x1D bytes in on 2.50
// (`mov r15d, [rcx+0x260]`): the offset above is the one it uses.
constexpr std::size_t kSetterReadAt = 0x1D;
constexpr std::uint8_t kSetterRead[] = {0x44, 0x8B, 0xB9, 0x60, 0x02, 0x00, 0x00};

enum class Mode { Teleport, Kinematic, Velocity };
Mode g_mode = Mode::Teleport;

RED4ext::CClass *VehicleClass() {
  static RED4ext::CClass *const cls = [] {
    auto *rtti = RED4ext::CRTTISystem::Get();
    auto *found = rtti ? rtti->GetClass("vehicleBaseObject") : nullptr;
    if (!found || found->size != kVehicleSize) {
      log::Error("vehicle move: vehicleBaseObject is 0x{:X} bytes, measured as 0x{:X} on 2.50: "
                 "vehicles are teleported instead",
                 found ? found->size : 0, kVehicleSize);
      return static_cast<RED4ext::CClass *>(nullptr);
    }
    return found;
  }();
  return cls;
}
// The move's address, null when this game's address library lacks it (the
// feature is off then, logged once: engine/access.hpp).
MoveToFn MoveTo() {
  static const MoveToFn fn =
      Find<MoveToFn>(feature::kVehicleMove, "the vehicle's kinematic move", kMoveTo);
  return fn;
}
// The physics state's setter, null when missing or not the one measured.
SetPhysicsStateFn SetPhysicsState() {
  static const SetPhysicsStateFn fn = [] {
    const auto address =
        FindAddress(feature::kVehicleMove, "the vehicle's physics state setter", kSetPhysicsState);
    if (!address)
      return static_cast<SetPhysicsStateFn>(nullptr);
    if (!InGameImage(address + kSetterReadAt + sizeof(kSetterRead)) ||
        std::memcmp(reinterpret_cast<const void *>(address + kSetterReadAt), kSetterRead,
                    sizeof(kSetterRead)) != 0) {
      log::Error("vehicle move: the physics state setter does not read +0x{:X} as on 2.50: no "
                 "kinematic vehicles",
                 kPhysicsState);
      return static_cast<SetPhysicsStateFn>(nullptr);
    }
    return reinterpret_cast<SetPhysicsStateFn>(address);
  }();
  return fn;
}

const char *NameOf(Mode mode) {
  return mode == Mode::Kinematic ? "kinematic" : mode == Mode::Velocity ? "velocity" : "teleport";
}

// A vehicle with a body: the class measured, both pointers set.
std::uint8_t *BodyOf(RED4ext::ent::Entity *entity) {
  auto *cls = VehicleClass();
  if (!cls || !entity || !entity->GetType() || !entity->GetType()->IsA(cls))
    return nullptr;
  auto *base = reinterpret_cast<std::uint8_t *>(entity);
  if (!*reinterpret_cast<void *const *>(base + kRigidBody) ||
      !*reinterpret_cast<void *const *>(base + kRuntimeData))
    return nullptr;
  return base;
}
} // namespace

void AskVehicleMove(std::string_view mode) {
  g_mode = mode == "kinematic"  ? Mode::Kinematic
           : mode == "velocity" ? Mode::Velocity
                                : Mode::Teleport;
}

bool CheckVehicleMove() {
  // Not asked for (teleports are the default, vehicles.md "How a copy is
  // moved"): not a degradation.
  if (g_mode == Mode::Teleport) {
    FeatureNotAsked(feature::kVehicleMove, "tandem.ini vehicle_move = teleport (the default)");
    return false;
  }
  if (!VehicleClass()) {
    FeatureOff(feature::kVehicleMove, "vehicleBaseObject is not the size measured on 2.50");
    return false;
  }
  if (!MoveTo())
    return false;
  if (g_mode == Mode::Kinematic && !SetPhysicsState()) {
    FeatureOff(feature::kVehicleMove, "the physics state setter is not the one measured on 2.50");
    return false;
  }
  FeatureOn(feature::kVehicleMove);
  log::Info("vehicle move: vehicle_move = {}", NameOf(g_mode));
  return true;
}

bool CanMoveVehicles() { return VehicleClass() != nullptr && MoveTo() != nullptr; }
bool CanHoldVehicles() { return CanMoveVehicles() && SetPhysicsState() != nullptr; }

bool MoveVehicle(RED4ext::ent::Entity *entity, float x, float y, float z, float i, float j, float k,
                 float r, float seconds) {
  if (!BodyOf(entity))
    return false;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(i) ||
      !std::isfinite(j) || !std::isfinite(k) || !std::isfinite(r) || !std::isfinite(seconds))
    return false;
  const float norm = std::sqrt(i * i + j * j + k * k + r * r);
  if (norm < 0.5f || seconds <= 0.f)
    return false;
  RED4ext::WorldTransform to{};
  to.Position.x.Bits = static_cast<std::int32_t>(std::lround(x * 131072.0));
  to.Position.y.Bits = static_cast<std::int32_t>(std::lround(y * 131072.0));
  to.Position.z.Bits = static_cast<std::int32_t>(std::lround(z * 131072.0));
  to.Orientation.i = i / norm;
  to.Orientation.j = j / norm;
  to.Orientation.k = k / norm;
  to.Orientation.r = r / norm;
  const MoveToFn move = MoveTo();
  if (!move)
    return false;
  move(entity, to, seconds);
  return true;
}

// Only the kinematic path holds anything: on the others nothing is looked
// up (a hash missing there is no degradation of theirs).
bool VehicleHeld(RED4ext::ent::Entity *entity) {
  if (g_mode != Mode::Kinematic)
    return false;
  const auto *base = BodyOf(entity);
  return base && SetPhysicsState() &&
         (*reinterpret_cast<const std::uint32_t *>(base + kPhysicsState) & kDrivenKinematically) !=
             0;
}

bool HoldVehicle(RED4ext::ent::Entity *entity, bool on) {
  if (g_mode != Mode::Kinematic)
    return !on;
  const auto set = SetPhysicsState();
  if (!set || !BodyOf(entity))
    return false;
  if (VehicleHeld(entity) != on)
    set(entity, kDrivenKinematically, !on);
  return VehicleHeld(entity) == on;
}
} // namespace tandem::plugin::engine
