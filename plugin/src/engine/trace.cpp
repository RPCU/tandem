// The trace (tandem.ini trace = on), a measuring tool: logs each distinct
// anim feature the engine applies (feature name, feature type, the class of
// the entity it goes to), to learn which inputs make a body crouch, aim or
// jump. Off unless asked: the function runs for every animated entity.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "trace.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/IComponent.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
namespace {
// AnimationControllerComponent::ApplyFeature(component, name, feature). The
// handle is passed by value: under the MSVC ABI that is a pointer to the
// caller's temporary, which the callee destroys; the detour forwards the same
// pointer and never destroys it.
constexpr std::uint32_t kApplyFeature = 316482401;
using ApplyFeature = void (*)(RED4ext::ent::IComponent *, RED4ext::CName,
                              RED4ext::Handle<RED4ext::ISerializable> *);
ApplyFeature g_original = nullptr;
void *g_target = nullptr;

std::mutex g_seenLock;
std::set<std::string> g_seen;
bool g_logging = false;

// Mirror (experiment): every feature applied to the local player is applied
// to these animation controllers too.
std::mutex g_mirrorLock;
std::vector<RED4ext::Handle<RED4ext::ent::IComponent>> g_mirrors;
const RED4ext::CClass *g_playerClass = nullptr;

void Detour(RED4ext::ent::IComponent *component, RED4ext::CName name,
            RED4ext::Handle<RED4ext::ISerializable> *feature) {
  if (!g_logging)
    goto mirror;
  {
    const char *type = feature && *feature ? (*feature)->GetType()->GetName().ToString() : "null";
    const char *owner =
        component && component->owner ? component->owner->GetType()->GetName().ToString() : "?";
    std::string key = std::string(name.ToString() ? name.ToString() : "?") + " " +
                      (type ? type : "?") + " on " + (owner ? owner : "?");
    bool fresh;
    {
      std::lock_guard _(g_seenLock);
      fresh = g_seen.insert(key).second;
    }
    if (fresh)
      log::Info("trace: feature {}", key);
  }
mirror:
  if (g_playerClass && component && component->owner &&
      component->owner->GetType()->IsA(g_playerClass) && feature && *feature) {
    // The targets copied under our lock, the engine called without it.
    std::vector<RED4ext::Handle<RED4ext::ent::IComponent>> targets;
    {
      std::lock_guard _(g_mirrorLock);
      targets = g_mirrors;
    }
    for (auto &target : targets) {
      if (target) {
        // A copy of the handle per call: the callee destroys its argument.
        auto copy = *feature;
        g_original(target.GetPtr(), name, &copy);
      }
    }
  }
  g_original(component, name, feature);
}
} // namespace

void MirrorPlayerTo(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *controllerClass =
      RED4ext::CRTTISystem::Get()->GetClass("entAnimationControllerComponent");
  g_playerClass = RED4ext::CRTTISystem::Get()->GetClass("PlayerPuppet");
  if (!entity)
    return;
  for (const auto &component : entity->components) {
    if (component && component->GetType()->IsA(controllerClass)) {
      std::lock_guard _(g_mirrorLock);
      g_mirrors.push_back(component);
      log::Info("mirror: the local player's anim features go to entity {:x} too",
                entity->entityID.hash);
      return;
    }
  }
}

void ClearMirrors() {
  std::lock_guard _(g_mirrorLock);
  g_mirrors.clear();
}

bool InstallTrace(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle, bool logging) {
  g_logging = logging;
  const auto f = feature::kTrace;
  g_target = Find<void *>(f, "AnimationControllerComponent::ApplyFeature", kApplyFeature);
  if (g_target && !sdk->hooking->Attach(handle, g_target, reinterpret_cast<void *>(&Detour),
                                        reinterpret_cast<void **>(&g_original))) {
    FeatureOff(f, "could not hook ApplyFeature");
    g_target = nullptr;
  }
  if (!g_target) {
    log::Warn("trace: ApplyFeature not hooked");
    return false;
  }
  FeatureOn(f);
  log::Info("trace: anim feature hook on{}", logging ? ", logging every distinct feature" : "");
  return true;
}

void RemoveTrace(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  if (g_target)
    sdk->hooking->Detach(handle, g_target);
}
} // namespace tandem::plugin::engine
