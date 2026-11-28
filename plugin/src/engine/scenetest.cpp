// (test) A scene started by the scene system's own entry, outside any quest:
// docs/research/engine/story.md §13.180 (read from the 2.50 exe; how the TV
// starts its scenes, `GlobalTvSystem_PlayScene` 0x140670d20, and the cut
// multiplayer's client, 0x142ce8b2c, which this follows call for call).
//
//   CreateSceneInstance (scnSceneSystem vtable +0x230, 0x1405053f8):
//     (system, out scnSceneInstanceId, u64 resource path, u64 owner,
//      params*, extra* (0x38 bytes, zero), u8 (unread by both callers),
//      out CString error) -> out
//   params: built by its constructor (0x1402cae4c), the entry point's name
//     set at +0x58 (0x142607754, from {CName, u32 0}), +0xB8 |= 1; its
//     destructor 0x1402bf4c8. The extra's destructor 0x1401d6f78.
//   requests (vtable +0x1b0, 0x1409dca60): (system, &instance id,
//     &{u64 kind, const char *label, 0, 0}, 0): 1 prepare, 2 start, 6 end.
//   casting (test 3): the dispatcher 0x140381d1c (casting, actor entry: its
//     id u32 at +0, its acquisition plan i32 at +0x28, …) -> bool, calls the
//     bind 0x1401e4138 (casting, performer: id at +0, &resolved {i32 kind,
//     2 = an entity; EntityID u64 at +8}, u8, …) for each actor found.
// Every call goes through the system's own vtable, after checking that its
// slots are these functions: another build turns the feature off.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "scenetest.hpp"

#include <RED4ext/CName.hpp>
#include <RED4ext/CString.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/ResourceLoader.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/Scripting/Natives/Generated/scn/SceneResource.hpp>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>

namespace tandem::plugin::engine {
namespace {
constexpr std::string_view kFeature = feature::kSceneTest;

// Address-library hashes (2.50).
constexpr std::uint32_t kCreate = 171003153;      // CreateSceneInstance, the vtable's
constexpr std::uint32_t kRequest = 2083726687;    // the request queue, the vtable's
constexpr std::uint32_t kParamsCtor = 446828003;  // the create params' constructor
constexpr std::uint32_t kParamsDtor = 449252836;  // and destructor
constexpr std::uint32_t kEntrySet = 3259568574;   // params +0x58 from {CName, u32}
constexpr std::uint32_t kExtraDtor = 731975264;   // the 0x38-byte extra's destructor
constexpr std::uint32_t kDispatch = 3969919645;   // one actor acquired, by its plan
constexpr std::uint32_t kBind = 2566022476;       // the acquired entity bound to it
constexpr std::uint32_t kLoadByPath = 1250309504; // ResourceLoader_IssueLoadingRequestByPath
constexpr std::uint32_t kCreateSlot = 0x230, kRequestSlot = 0x1b0;
constexpr std::uint32_t kEntryAt = 0x58, kFlagsAt = 0xB8;
// An owner hash no quest phase uses: "tand" and a play counter.
constexpr std::uint64_t kOwnerBase = 0x74616e6400000000ull;
constexpr const char *kLabel = "TandemSceneTest";

struct InstanceId { // scnSceneInstanceId (0x20)
  std::uint64_t scene = 0, owner = 0;
  std::uint8_t internal = 0;
  std::uint8_t pad[7] = {};
  std::uint64_t hash = 0;
};
static_assert(sizeof(InstanceId) == 0x20);
struct Request {
  std::uint64_t kind;
  const char *label;
  std::uint64_t zero[2];
};
struct EntryName {
  std::uint64_t name;
  std::uint32_t zero, pad;
};

using CreateFn = InstanceId *(*)(void *system, InstanceId *out, std::uint64_t resource,
                                 std::uint64_t owner, void *params, void *extra,
                                 std::uint8_t unread, RED4ext::CString *error);
using RequestFn = void (*)(void *system, const InstanceId *id, const Request *request,
                           std::uint32_t);
using ObjectFn = void *(*)(void *object);
using EntrySetFn = void (*)(void *params, const EntryName *name);
using DispatchFn = std::uint8_t (*)(void *casting, void *actor, void *more);
using BindFn = void (*)(void *casting, std::int32_t *performer, std::int32_t *resolved,
                        std::uint8_t found, void *spec, void *more);
using LoadFn = std::uintptr_t (*)(RED4ext::ResourceLoader *,
                                  RED4ext::SharedPtr<RED4ext::ResourceToken<>> &,
                                  RED4ext::ResourcePath);

std::uintptr_t g_create = 0, g_request = 0;
ObjectFn g_paramsCtor = nullptr, g_paramsDtor = nullptr, g_extraDtor = nullptr;
EntrySetFn g_entrySet = nullptr;
LoadFn g_load = nullptr;
bool g_asked = false; // tandem.ini scene_test (set at load)
bool g_ok = false;    // CheckSceneTest passed

// Game thread only: our last play.
InstanceId g_last;
std::uint64_t g_lastResource = 0, g_lastOwner = 0;
std::uint32_t g_plays = 0;
RED4ext::SharedPtr<RED4ext::ResourceToken<>> g_info; // kept loaded while asked about
std::uint64_t g_infoPath = 0;

// Casting (any thread: the scene system may acquire actors on a job).
DispatchFn g_dispatchOriginal = nullptr;
BindFn g_bindOriginal = nullptr;
void *g_dispatchTarget = nullptr, *g_bindTarget = nullptr;
std::atomic_bool g_castLog{false};
std::atomic<std::uint64_t> g_hand{0};
thread_local std::int32_t t_plan = -1;
thread_local std::uint32_t t_actor = 0;
std::mutex g_castLock; // g_cast only; nothing called under it
std::vector<std::uint64_t> g_cast;

// The scene system, its vtable's two slots checked.
void *SceneSystem(std::string &why) {
  auto *engine = RED4ext::CGameEngine::Get();
  auto *type = RED4ext::CRTTISystem::Get()->GetClass("scnISceneSystem");
  if (!engine || !engine->framework || !engine->framework->gameInstance || !type) {
    why = "no game";
    return nullptr;
  }
  void *system = engine->framework->gameInstance->GetSystem(type);
  if (!system) {
    why = "no scene system";
    return nullptr;
  }
  const auto vtable = *static_cast<const std::uintptr_t *>(system);
  if (!InGameImage(vtable)) {
    why = "its vtable is not in the game's image";
    return nullptr;
  }
  const auto create = *reinterpret_cast<const std::uintptr_t *>(vtable + kCreateSlot);
  const auto request = *reinterpret_cast<const std::uintptr_t *>(vtable + kRequestSlot);
  if (create != g_create || request != g_request) {
    why = std::format("its vtable slots differ from 2.50 (+{:#x} {:#x}, +{:#x} {:#x})", kCreateSlot,
                      create, kRequestSlot, request);
    return nullptr;
  }
  return system;
}

// ---- guarded engine calls (no C++ objects with destructors in __try) ----

// The params and the extra live in the caller's buffers; error is a CString.
bool CallCreate(void *system, InstanceId *out, std::uint64_t resource, std::uint64_t owner,
                std::uint64_t entry, std::uint8_t *params, std::uint8_t *extra,
                RED4ext::CString *error, std::uint32_t &faultAt) {
  faultAt = 0;
  __try {
    g_paramsCtor(params);
    faultAt = 1;
    if (entry) {
      const EntryName name{entry, 0, 0};
      g_entrySet(params + kEntryAt, &name);
    }
    *reinterpret_cast<std::uint32_t *>(params + kFlagsAt) |= 1;
    faultAt = 2;
    const auto create =
        *reinterpret_cast<const CreateFn *>(*static_cast<std::uintptr_t *>(system) + kCreateSlot);
    create(system, out, resource, owner, params, extra, 0, error);
    faultAt = 3;
    g_extraDtor(extra);
    g_paramsDtor(params);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool CallRequest(void *system, const InstanceId *id, std::uint64_t kind) {
  __try {
    const Request request{kind, kLabel, {0, 0}};
    const auto queue =
        *reinterpret_cast<const RequestFn *>(*static_cast<std::uintptr_t *>(system) + kRequestSlot);
    queue(system, id, &request, 0);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// The dispatcher's actor entry: its id and plan (false: unreadable).
bool ReadActor(const void *actor, std::uint32_t &id, std::int32_t &plan) {
  __try {
    id = *static_cast<const std::uint32_t *>(actor);
    plan = *reinterpret_cast<const std::int32_t *>(static_cast<const std::uint8_t *>(actor) + 0x28);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ReadBind(const std::int32_t *performer, std::int32_t *resolved, std::uint32_t &id,
              std::int32_t &kind, std::uint64_t &entity, std::uint64_t hand, bool &handed) {
  __try {
    id = static_cast<std::uint32_t>(*performer);
    kind = resolved[0];
    entity = *reinterpret_cast<const std::uint64_t *>(resolved + 2);
    handed = false;
    if (hand && kind == 2) {
      *reinterpret_cast<std::uint64_t *>(resolved + 2) = hand;
      handed = true;
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// ---- the casting hooks (any thread: they log and remember, nothing else) ----

std::uint8_t DispatchDetour(void *casting, void *actor, void *more) {
  if (!g_castLog.load(std::memory_order_relaxed) && !g_hand.load(std::memory_order_relaxed))
    return g_dispatchOriginal(casting, actor, more);
  std::uint32_t id = 0;
  std::int32_t plan = -1;
  if (!ReadActor(actor, id, plan))
    plan = -2;
  t_plan = plan;
  t_actor = id;
  const auto found = g_dispatchOriginal(casting, actor, more);
  t_plan = -1;
  if (g_castLog.load(std::memory_order_relaxed))
    log::Info("scene play: cast actor {} plan {} on {:#x}: {}", id, plan,
              reinterpret_cast<std::uintptr_t>(casting), found ? "acquired" : "not yet");
  return found;
}

void BindDetour(void *casting, std::int32_t *performer, std::int32_t *resolved, std::uint8_t found,
                void *spec, void *more) {
  const bool logging = g_castLog.load(std::memory_order_relaxed);
  const auto hand = g_hand.load(std::memory_order_relaxed);
  if (!logging && !hand) {
    g_bindOriginal(casting, performer, resolved, found, spec, more);
    return;
  }
  std::uint32_t id = 0;
  std::int32_t kind = -1;
  std::uint64_t entity = 0;
  bool handed = false;
  const bool read = ReadBind(performer, resolved, id, kind, entity, hand, handed);
  if (handed)
    g_hand.store(0);
  g_bindOriginal(casting, performer, resolved, found, spec, more);
  if (read && kind == 2) {
    std::lock_guard _(g_castLock);
    if (g_cast.size() < 64)
      g_cast.push_back(handed ? hand : entity);
  }
  if (logging)
    log::Info("scene play: bind actor {} (plan {}) on {:#x}: {}{}", id, t_plan,
              reinterpret_cast<std::uintptr_t>(casting),
              !read       ? std::string("unreadable")
              : kind == 2 ? std::format("entity {:#x}", entity)
                          : std::format("none (kind {})", kind),
              handed ? std::format(", handed {:#x} instead", hand) : "");
}

std::string Off() {
  if (!g_asked)
    return "scene play: off (tandem.ini scene_test = on)";
  return "scene play: unavailable (engine access, said at startup)";
}

std::string IdText(const InstanceId &id) {
  return std::format("{{scene {:#x}, owner {:#x}, internal {}, hash {:#x}}}", id.scene, id.owner,
                     id.internal, id.hash);
}

// The resource, if loaded: its facts (no destructors here: SEH).
bool ReadInfo(const RED4ext::scn::SceneResource *scene, char *out, std::size_t size) {
  __try {
    int n = std::snprintf(out, size,
                          "category %d, entry points:", static_cast<int>(scene->sceneCategoryTag));
    auto put = [&](const char *text) {
      if (n >= 0 && static_cast<std::size_t>(n) < size)
        n += std::snprintf(out + n, size - n, "%s", text);
    };
    char line[192];
    for (std::uint32_t i = 0; i < scene->entryPoints.size() && i < 12; ++i) {
      std::snprintf(line, sizeof(line), " %s", scene->entryPoints[i].name.ToString());
      put(line);
    }
    std::snprintf(line, sizeof(line), "; %u actor(s):", scene->actors.size());
    put(line);
    for (std::uint32_t i = 0; i < scene->actors.size() && i < 16; ++i) {
      const auto &actor = scene->actors[i];
      std::snprintf(line, sizeof(line), " [%u %s plan %d]", actor.actorId.id,
                    actor.actorName.c_str(), static_cast<int>(actor.acquisitionPlan));
      put(line);
    }
    std::snprintf(line, sizeof(line), "; %u player actor(s):", scene->playerActors.size());
    put(line);
    for (std::uint32_t i = 0; i < scene->playerActors.size() && i < 4; ++i) {
      const auto &actor = scene->playerActors[i];
      std::snprintf(line, sizeof(line), " [%u %s plan %d]", actor.actorId.id,
                    actor.playerName.c_str(), static_cast<int>(actor.acquisitionPlan));
      put(line);
    }
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
} // namespace

bool InstallSceneTest(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle, bool asked) {
  g_asked = asked;
  if (!asked) {
    FeatureNotAsked(kFeature, "tandem.ini scene_test = off");
    return false;
  }
  g_dispatchTarget = Find<void *>(kFeature, "the scene actor dispatcher", kDispatch);
  g_bindTarget = Find<void *>(kFeature, "the scene actor bind", kBind);
  if (!g_dispatchTarget || !g_bindTarget)
    return false;
  if (!sdk->hooking->Attach(handle, g_dispatchTarget, reinterpret_cast<void *>(&DispatchDetour),
                            reinterpret_cast<void **>(&g_dispatchOriginal))) {
    FeatureOff(kFeature, "could not hook the scene actor dispatcher");
    g_dispatchTarget = g_bindTarget = nullptr;
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_bindTarget, reinterpret_cast<void *>(&BindDetour),
                            reinterpret_cast<void **>(&g_bindOriginal))) {
    FeatureOff(kFeature, "could not hook the scene actor bind");
    g_bindTarget = nullptr;
    return false;
  }
  log::Info("scene play: casting hooks in (tandem.ini scene_test = on)");
  return true;
}

void RemoveSceneTest(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_castLog = false;
  g_hand = 0;
  if (g_bindTarget)
    sdk->hooking->Detach(handle, g_bindTarget);
  if (g_dispatchTarget)
    sdk->hooking->Detach(handle, g_dispatchTarget);
  g_bindTarget = g_dispatchTarget = nullptr;
}

bool CheckSceneTest() {
  if (!g_asked || FeatureIsOff(kFeature))
    return false;
  g_create = FindAddress(kFeature, "CreateSceneInstance", kCreate);
  g_request = FindAddress(kFeature, "the scene request queue", kRequest);
  g_paramsCtor = Find<ObjectFn>(kFeature, "the scene params' constructor", kParamsCtor);
  g_paramsDtor = Find<ObjectFn>(kFeature, "the scene params' destructor", kParamsDtor);
  g_entrySet = Find<EntrySetFn>(kFeature, "the scene params' entry point", kEntrySet);
  g_extraDtor = Find<ObjectFn>(kFeature, "the scene create's extra", kExtraDtor);
  g_load = Find<LoadFn>(kFeature, "the resource loader's load by path", kLoadByPath);
  bool ok =
      g_create && g_request && g_paramsCtor && g_paramsDtor && g_entrySet && g_extraDtor && g_load;
  ok = ClassSizeIs(kFeature, "scnSceneResource", 0x2D8) && ok;
  ok = ClassSizeIs(kFeature, "scnActorDef", 0x1E0) && ok;
  ok = ClassSizeIs(kFeature, "scnPlayerActorDef", 0x100) && ok;
  ok = ClassSizeIs(kFeature, "scnEntryPoint", 0x10) && ok;
  ok = ClassSizeIs(kFeature, "scnSceneInstanceId", 0x20) && ok;
  g_ok = ok;
  if (ok)
    FeatureOn(kFeature);
  return ok;
}

std::string ScenePlay(const std::string &path, const std::string &entry, bool again) {
  if (!g_ok)
    return Off();
  std::string why;
  void *system = SceneSystem(why);
  if (!system)
    return std::format("scene play: refused: {}", why);
  if (again && !g_lastOwner)
    return "scene play: refused: no play to repeat";
  const std::uint64_t resource = again ? g_lastResource : RED4ext::ResourcePath(path.c_str()).hash;
  const std::uint64_t owner = again ? g_lastOwner : kOwnerBase + ++g_plays;
  const std::uint64_t entryName = entry.empty() ? 0 : RED4ext::CName(entry.c_str()).hash;
  if (!again) {
    std::lock_guard _(g_castLock);
    g_cast.clear();
  }
  alignas(16) std::uint8_t params[0x200] = {};
  alignas(16) std::uint8_t extra[0x40] = {};
  RED4ext::CString error;
  InstanceId id;
  std::uint32_t faultAt = 0;
  if (!CallCreate(system, &id, resource, owner, entryName, params, extra, &error, faultAt))
    return std::format("scene play: refused: the create faulted (step {})", faultAt);
  const char *message = error.c_str();
  // Refused: the system's invalid id ({0, 0, 0xFF, 0} at 0x1439164c8) and an error.
  if (id.internal == 0xFF || (id.scene == 0 && id.hash == 0))
    return std::format("scene play: refused: {} (owner {:#x})",
                       message && *message ? message : "an empty instance id", owner);
  if (again)
    return std::format("scene play: again accepted (owner {:#x}): {}", owner, IdText(id));
  g_last = id;
  g_lastResource = resource;
  g_lastOwner = owner;
  const bool prepared = CallRequest(system, &id, 1);
  const bool started = prepared && CallRequest(system, &id, 2);
  return std::format("scene play: playing {} as {}{}{}", path, IdText(id),
                     entry.empty() ? "" : std::format(" from {}", entry),
                     started ? "" : ", a request faulted");
}

std::string SceneStop() {
  if (!g_ok)
    return Off();
  if (!g_lastOwner)
    return "scene play: nothing to stop";
  std::string why;
  void *system = SceneSystem(why);
  if (!system)
    return std::format("scene play: stop refused: {}", why);
  if (!CallRequest(system, &g_last, 6))
    return "scene play: the end request faulted";
  return std::format("scene play: end asked for {}", IdText(g_last));
}

std::string SceneInfo(const std::string &path) {
  if (!g_ok)
    return Off();
  const RED4ext::ResourcePath wanted(path.c_str());
  auto *loader = RED4ext::ResourceLoader::Get();
  if (!loader)
    return "scene play: info: no resource loader";
  if (g_infoPath != wanted.hash || !g_info) {
    g_info = {};
    g_load(loader, g_info, wanted);
    g_infoPath = wanted.hash;
  }
  if (!g_info)
    return std::format("scene play: info: {} could not be asked for", path);
  if (g_info->IsFailed())
    return std::format("scene play: info: {} failed to load", path);
  if (!g_info->IsLoaded() || !g_info->resource)
    return std::format("scene play: info: {} loading, ask again", path);
  auto *resource = g_info->resource.GetPtr();
  static auto *sceneClass = RED4ext::CRTTISystem::Get()->GetClass("scnSceneResource");
  if (!resource || resource->GetType() != sceneClass)
    return std::format("scene play: info: {} is not a scene", path);
  char text[2048] = {};
  if (!ReadInfo(reinterpret_cast<const RED4ext::scn::SceneResource *>(resource), text,
                sizeof(text)))
    return std::format("scene play: info: {} unreadable", path);
  return std::format("scene play: info: {}: {}", path, text);
}

std::string SceneCast(bool on) {
  if (!g_ok || !g_bindTarget)
    return Off();
  g_castLog = on;
  return std::format("scene play: cast log {}", on ? "on" : "off");
}

std::string SceneHand(std::uint64_t entity) {
  if (!g_ok || !g_bindTarget)
    return Off();
  g_hand = entity;
  return entity ? std::format("scene play: the next actor found is handed {:#x}", entity)
                : "scene play: no hand";
}

std::vector<std::uint64_t> SceneCastEntities() {
  std::lock_guard _(g_castLock);
  return g_cast;
}
} // namespace tandem::plugin::engine
