// A vehicle our game does not simulate, moved as the engine moves one it
// drives kinematically (docs/research/engine/vehicles.md §13.141).
//
// vehicleBaseObject's "move to this transform in dt" (address-library hash
// 2933986803, 0x1402e3270 on 2.50; five callers in the exe): it writes the
// transform to the vehicle's runtime data (+0x5F0), gives the rigid body
// (+0x2D8) the linear and angular velocity that reach it in dt, or puts the
// body there at rest when that is 100 m/s or more, and moves the physics
// proxy. Unlike the teleportation facility it is not a teleport: a player
// mounted in the vehicle is not teleported with it (a teleport of the
// player's mount every frame stops the population system's spawns, §13.141).

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

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kMoveTo = 2933986803;
using MoveToFn = void (*)(RED4ext::ent::Entity *, const RED4ext::WorldTransform &, float);

// vehicleBaseObject as measured on 2.50 (RTTI size; the function's own
// reads): it dereferences both without a check.
constexpr std::uint32_t kVehicleSize = 0xBC0;
constexpr std::size_t kRigidBody = 0x2D8;
constexpr std::size_t kRuntimeData = 0x5F0;

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
} // namespace

namespace {
bool g_asked = false;
} // namespace

void AskVehicleMove(bool asked) { g_asked = asked; }

bool CheckVehicleMove() {
  // Not asked for (kinematic_ride = on is opt-in; teleports are the
  // default, vehicles.md "How a copy is moved"): not a degradation.
  if (!g_asked) {
    FeatureNotAsked(feature::kVehicleMove,
                    "tandem.ini kinematic_ride is not on (teleports, the default)");
    return false;
  }
  if (!VehicleClass()) {
    FeatureOff(feature::kVehicleMove, "vehicleBaseObject is not the size measured on 2.50");
    return false;
  }
  if (!MoveTo())
    return false;
  FeatureOn(feature::kVehicleMove);
  return true;
}

bool CanMoveVehicles() { return VehicleClass() != nullptr && MoveTo() != nullptr; }

bool MoveVehicle(RED4ext::ent::Entity *entity, float x, float y, float z, float i, float j, float k,
                 float r, float seconds) {
  auto *cls = VehicleClass();
  if (!cls || !entity || !entity->GetType() || !entity->GetType()->IsA(cls))
    return false;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(i) ||
      !std::isfinite(j) || !std::isfinite(k) || !std::isfinite(r) || !std::isfinite(seconds))
    return false;
  const float norm = std::sqrt(i * i + j * j + k * k + r * r);
  if (norm < 0.5f || seconds <= 0.f)
    return false;
  const auto *base = reinterpret_cast<const std::uint8_t *>(entity);
  if (!*reinterpret_cast<void *const *>(base + kRigidBody) ||
      !*reinterpret_cast<void *const *>(base + kRuntimeData))
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
} // namespace tandem::plugin::engine
