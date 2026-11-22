// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "../runtime.hpp"
#include "access.hpp"
#include "controller.hpp"
#include "hooks.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/entIComponent.hpp>
#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>

#include <atomic>

namespace tandem::plugin::engine {
namespace {
// IdleController::SetAnimation(controller, AnimationData&): called by the
// engine for every NPC's idle movement controller, on the animation thread.
constexpr std::uint32_t kIdleSetAnimation = 2268141838;
// AttachController(move::Component*, controller*): pushes a controller onto a
// move component's stack.
constexpr std::uint32_t kAttachController = 4018412273;

using SetAnimation = void (*)(void *controller, AnimationData &data);
SetAnimation g_originalSetAnimation = nullptr;
void *g_setAnimationTarget = nullptr;
using AttachFn = float (*)(RED4ext::move::Component *, PuppetController *);
AttachFn g_attach = nullptr;
// Set by CheckMovement once the layouts below are the running game's: until
// then, and for good on a mismatch, the detour reads nothing and only calls
// the engine's function.
std::atomic_bool g_movementOn{false};

// What the detour and the controller read of the SDK's structs (2.50).
static_assert(__builtin_offsetof(RED4ext::ent::IComponent, name) == 0x40);
static_assert(__builtin_offsetof(RED4ext::ent::IComponent, owner) == 0x50);
static_assert(__builtin_offsetof(RED4ext::ent::IComponent, id) == 0x60);
static_assert(__builtin_offsetof(RED4ext::ent::Entity, entityID) == 0x48);
static_assert(__builtin_offsetof(RED4ext::ent::Entity, transformComponent) == 0xB0);
static_assert(__builtin_offsetof(RED4ext::ent::Entity, customCameraTarget) == 0x154);
static_assert(__builtin_offsetof(RED4ext::ent::IPlacedComponent, worldTransform) == 0xE0);

// The engine-visible head of every movement controller.
struct ControllerHead {
  void *vtable;                        // 00
  std::uint32_t type;                  // 08
  bool attached;                       // 0C
  bool highPriority;                   // 0D
  RED4ext::move::Component *component; // 10
};

void DetourSetAnimation(void *controller, AnimationData &data) {
  static std::atomic_bool s_logged{false};
  if (!s_logged.exchange(true))
    log::Info("idle-controller hook is live");

  if (!g_movementOn.load(std::memory_order_relaxed)) {
    g_originalSetAnimation(controller, data);
    return;
  }
  const auto *head = static_cast<const ControllerHead *>(controller);
  // Ours: the engine must never run its idle code on it.
  if (head->type == PuppetController::kType)
    return;

  // Only the entity id is read on this thread: the entity may still be under
  // construction on the game thread. The attach itself happens there.
  if (auto *owner = head->component ? head->component->owner : nullptr) {
    if (Runtime::Get().PuppetPlayer(owner->entityID.hash) != 0)
      Runtime::Get().RequestController(owner->entityID.hash);
  }
  g_originalSetAnimation(controller, data);
}
} // namespace

bool InstallHooks(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_setAnimationTarget =
      Find<void *>(feature::kMovement, "IdleController::SetAnimation", kIdleSetAnimation);
  if (!g_setAnimationTarget)
    return false;
  if (!sdk->hooking->Attach(handle, g_setAnimationTarget,
                            reinterpret_cast<void *>(&DetourSetAnimation),
                            reinterpret_cast<void **>(&g_originalSetAnimation))) {
    FeatureOff(feature::kMovement,
               "could not hook IdleController::SetAnimation: remote players will not move");
    g_setAnimationTarget = nullptr;
    return false;
  }
  return true;
}

void RemoveHooks(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_movementOn = false;
  if (g_setAnimationTarget)
    sdk->hooking->Detach(handle, g_setAnimationTarget);
}

bool CheckMovement() {
  // Every check, so the log names each mismatch at once. Class sizes and
  // property offsets from RTTI, as measured on 2.50 (RED4ext.SDK's structs).
  const auto f = feature::kMovement;
  bool ok = g_setAnimationTarget != nullptr; // (said at load when not)
  ok = ClassSizeIs(f, "moveComponent", 0x2C0) && ok;
  ok = ClassSizeIs(f, "entEntity", 0x160) && ok;
  ok = ClassSizeIs(f, "entIPlacedComponent", 0x120) && ok;
  ok = ClassSizeIs(f, "entIComponent", 0x90) && ok;
  // Not read by the controller, but the 2.5 build fingerprint NCO found.
  ok = ClassSizeIs(f, "gameuiCharacterCustomizationSystem", 0x3D8) && ok;
  ok = PropertyAt(f, "entIComponent", "name", 0x40) && ok;
  ok = PropertyAt(f, "entIComponent", "id", 0x60) && ok;
  ok = PropertyAt(f, "entEntity", "customCameraTarget", 0x154) && ok;
  ok = PropertyAt(f, "entIPlacedComponent", "worldTransform", 0xE0) && ok;
  g_attach = Find<AttachFn>(f, "AttachController", kAttachController);
  ok = g_attach && ok;
  ok = AnimationDriver::Available() && ok;
  if (ok)
    FeatureOn(f);
  else
    log::Error("engine layouts differ from 2.50: puppet movement is off for this game version");
  g_movementOn = ok && FeatureIsOn(f);
  return g_movementOn;
}

bool AttachController(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  if (!g_movementOn.load(std::memory_order_relaxed) || !entity)
    return false;

  auto *moveClass = RED4ext::CRTTISystem::Get()->GetClass("moveComponent");
  RED4ext::move::Component *move = nullptr;
  for (const auto &component : entity->components) {
    if (component && component->GetType()->IsA(moveClass)) {
      move = reinterpret_cast<RED4ext::move::Component *>(component.instance);
      break;
    }
  }
  if (!move) {
    log::Warn("puppet {}: no move component, can't drive it", player);
    return false;
  }

  auto *controller = new PuppetController(player);
  controller->id = entity->entityID;
  g_attach(move, controller);
  log::Debug("puppet {}: controller handed to the engine", player);
  return true;
}
} // namespace tandem::plugin::engine
