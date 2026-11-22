// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "workspots.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/CResource.hpp>
#include <RED4ext/Scripting/Natives/Generated/work/WorkspotInstance.hpp>
#include <RED4ext/Scripting/Natives/Generated/work/WorkspotSystem.hpp>
#include <RED4ext/SharedSpinLock.hpp>
#include <RedLib.hpp>

#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>

namespace tandem::plugin::engine {
namespace {
// workWorkspotSystem's instance lookup: (system, EntityID hash) -> instance
// or null. The game's IsActorInWorkspot (0x1409ef4ac on 2.50) calls it with
// the system's shared lock (+0x50) held. Address-library hash, in 2.50.
constexpr std::uint32_t kGetWorkspotInstance = 2234523611;
using GetInstanceFn = RED4ext::work::WorkspotInstance *(*)(RED4ext::work::WorkspotSystem *,
                                                           std::uint64_t);

// workWorkspotSystem and workWorkspotInstance as measured on 2.50
// (decompiled IsActorInWorkspot, the instance's constructor and destructor;
// the same as Codeware 1.21.5's): the lock, then the instance's handles and
// names. Every handle is checked for its RTTI class before it is read.
constexpr std::size_t kSystemLock = 0x50;
constexpr std::size_t kInstanceTree = 0x60;     // Handle<workWorkspotTree>
constexpr std::size_t kInstanceResource = 0x70; // Handle<workWorkspotResource>
constexpr std::size_t kInstanceAnim = 0x110;    // CName
constexpr std::size_t kInstanceIdle = 0x118;    // CName
constexpr std::size_t kInstanceEntry = 0x120;   // workWorkEntryId { Uint32 id }
constexpr std::uint32_t kSystemSize = 0xAE0;
constexpr std::uint32_t kInstanceSize = 0x2E0;

RED4ext::CClass *ClassOf(const char *name) {
  auto *rtti = RED4ext::CRTTISystem::Get();
  return rtti ? rtti->GetClass(name) : nullptr;
}

// CheckWorkspots (the first frame): both classes have the sizes measured on
// 2.50 and the lookup is in this game.
GetInstanceFn g_find = nullptr;
std::atomic_bool g_layoutOk{false};

// The object a handle at `offset` of `base` points to, when it is of class
// `type` (or derives from it); null otherwise.
RED4ext::ISerializable *HandleOf(const void *base, std::size_t offset, RED4ext::CClass *type) {
  RED4ext::ISerializable *object = nullptr;
  std::memcpy(&object, static_cast<const std::uint8_t *>(base) + offset, sizeof(object));
  if (!object || !type)
    return nullptr;
  auto *actual = object->GetType();
  return actual && actual->IsA(type) ? object : nullptr;
}

template <class T> T Read(const void *base, std::size_t offset) {
  T value{};
  std::memcpy(&value, static_cast<const std::uint8_t *>(base) + offset, sizeof(T));
  return value;
}
} // namespace

bool CheckWorkspots() {
  const auto f = feature::kWorkspots;
  bool ok = ClassSizeIs(f, "workWorkspotSystem", kSystemSize);
  ok = ClassSizeIs(f, "workWorkspotInstance", kInstanceSize) && ok;
  g_find = Find<GetInstanceFn>(f, "the workspot system's instance lookup", kGetWorkspotInstance);
  g_layoutOk = ok && g_find;
  if (g_layoutOk)
    FeatureOn(f);
  else
    log::Error("workspots: not as measured on 2.50: workspots not read");
  return g_layoutOk;
}

WorkspotRead WorkspotOf(std::uint64_t entity) {
  WorkspotRead read;
  if (entity == 0 || !g_layoutOk)
    return read;
  auto *system = Red::GetRuntimeSystem<Red::workWorkspotSystem>();
  if (!system)
    return read;
  static auto *instanceClass = ClassOf("workWorkspotInstance");
  static auto *treeClass = ClassOf("workWorkspotTree");
  static auto *resourceClass = ClassOf("workWorkspotResource");
  auto &lock = *reinterpret_cast<RED4ext::SharedSpinLock *>(
      reinterpret_cast<std::uint8_t *>(system) + kSystemLock);
  std::shared_lock guard(lock);
  auto *instance = g_find(system, entity);
  if (!instance)
    return read;
  auto *type = instance->GetType();
  if (!type || !instanceClass || !type->IsA(instanceClass) ||
      !HandleOf(instance, kInstanceTree, treeClass)) {
    static std::atomic<bool> said{false};
    if (!said.exchange(true))
      log::Warn("workspots: an instance of entity {:x} is not laid out as measured on 2.50 "
                "(class {}): not read",
                entity, type ? type->GetName().ToString() : "none");
    read.state = WorkspotRead::State::Refused;
    return read;
  }
  read.entry = Read<std::uint32_t>(instance, kInstanceEntry);
  read.anim = Read<std::uint64_t>(instance, kInstanceAnim);
  read.idle = Read<std::uint64_t>(instance, kInstanceIdle);
  auto *resource =
      static_cast<RED4ext::CResource *>(HandleOf(instance, kInstanceResource, resourceClass));
  if (!resource || resource->path.hash == 0) {
    read.state = WorkspotRead::State::NoResource;
    return read;
  }
  read.path = resource->path.hash;
  read.state = WorkspotRead::State::Found;
  return read;
}
} // namespace tandem::plugin::engine
