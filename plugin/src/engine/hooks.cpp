// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "../runtime.hpp"
#include "controller.hpp"
#include "hooks.hpp"
#include "layout.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Relocation.hpp>

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
      reinterpret_cast<void *>(RED4ext::UniversalRelocBase::Resolve(kIdleSetAnimation));
  if (!g_setAnimationTarget) {
    log::Error("IdleController::SetAnimation not found in this game version");
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_setAnimationTarget,
                            reinterpret_cast<void *>(&DetourSetAnimation),
                            reinterpret_cast<void **>(&g_originalSetAnimation))) {
    log::Error("could not hook IdleController::SetAnimation: remote players will not move");
    return false;
  }
  return true;
}

void RemoveHooks(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  if (g_setAnimationTarget)
    sdk->hooking->Detach(handle, g_setAnimationTarget);
}

bool AttachController(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static const bool s_layoutOk = [] {
    const bool ok = MovementLayoutMatches();
    if (!ok)
      log::Error("engine layouts differ from 2.50: puppet movement is off for this game version");
    return ok;
  }();
  if (!s_layoutOk || !entity)
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

  static RED4ext::UniversalRelocFunc<float (*)(RED4ext::move::Component *, PuppetController *)>
      attach(kAttachController);
  auto *controller = new PuppetController(player);
  controller->id = entity->entityID;
  attach(move, controller);
  log::Debug("puppet {}: controller handed to the engine", player);
  return true;
}
} // namespace tandem::plugin::engine
