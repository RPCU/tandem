// The guest's quest freeze: a hook on QuestPhaseInstance::ExecuteNode
// (docs/research/engine-knowledge.md §13.15, read offline on the 2.50 exe).
//
//   ExecuteNode(phase, node, context, inputSocket, outputSockets) -> u8
//     hash 3227858325 (0x1402b3aa0 on 2.50). Its only direct caller
//     (0x1402b393c) runs one queued node of ExecuteGraph's work list
//     (0x140352984, under QuestPhaseInstance_ExecuteGraph 779107884):
//       0  the node finished; an output node (questOutputNodeDefinition)
//          ends the graph pass with its socket, other nodes queue their
//          output sockets;
//       2  the node finished: its output sockets are queued;
//       1, 3  nothing is queued: the signal stops at this node and the pass
//          goes on with the other queued nodes. ExecuteNode itself returns
//          1 for a node its phase has disabled (questDisableableNode in the
//          phase's set at +0xC8) and when the node's pre-check vfunc +0x150
//          says so: the engine's own "do nothing here".
//   So refusing a node = returning 1 without running it. The signal is not
//   kept anywhere by the engine: tandem keeps it (phase node path, node id,
//   input socket) and gives it back at the end of the freeze through the
//   root phase's ExecutePath (vfunc +0x120, Codeware's
//   QuestPhaseExecutor::ExecutePath and OpenWorldSystem, MIT), which runs
//   that node of that phase as if the signal had just arrived.
//
// Layouts (2.50, RTTI sizes checked before the first use; Codeware
// src/Red/QuestsSystem.hpp for the offsets):
//   questPhaseInstance (0x110): +0x30 Handle<questQuestPhaseResource>,
//     +0x70 NodePath (DynArray<u16>), +0x80 NodePathHash (u32)
//   CResource (0x40): +0x30 ResourcePath (u64)
//   questNodeDefinition (0x48): +0x40 id (u16)
//   QuestContext (0x340): +0xF8 phaseStack (DynArray<questPhaseInstance*>)
//   QuestNodeSocket: +0x00 name (CName)
//   questQuestsSystem (0x300): +0x60 SharedSpinLock, +0x68 Handle<root phase>
//
// Which phases freeze: every phase but the root and the open-world ones
// (Codeware's list, src/App/Quest/QuestPhaseRegistry.cpp:12-20, plus
// tandem.ini quest_freeze_allow), and any phase under an open-world one
// (by node path prefix, as Codeware's IsRelatedQuestNodePath). A phase is
// recognised as open-world from its own resource or from an ancestor in
// the context's phase stack (ExecuteGraph pushes each phase it runs).
//
// The hook may run on a job thread: it only reads engine memory (guarded)
// and touches its own state under a mutex.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "quests.hpp"

#include <RED4ext/DynArray.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/SharedSpinLock.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kExecuteNode = 3227858325;
constexpr std::uint32_t kCreateContext = 3144298192;

constexpr std::size_t kPhaseSize = 0x110, kPhaseResource = 0x30, kPhasePath = 0x70;
constexpr std::size_t kResourceSize = 0x40, kResourcePath = 0x30;
constexpr std::size_t kNodeSize = 0x48, kNodeId = 0x40;
constexpr std::size_t kContextSize = 0x340, kContextStack = 0xF8;
constexpr std::size_t kSystemSize = 0x300, kSystemLock = 0x60, kSystemRoot = 0x68;
constexpr std::size_t kExecutePathSlot = 0x120 / 8;
constexpr std::uint32_t kMaxPath = 64, kMaxStack = 32;
constexpr std::size_t kMaxHeld = 4096;
constexpr std::uint8_t kRefused = 1; // the engine's own "nothing here" (see above)

// Codeware's open-world phases (QuestPhaseRegistry.cpp:12-20).
constexpr const char *kOpenWorld[] = {
    R"(base\open_world\minor_activities\open_world_minor_activities.questphase)",
    R"(base\open_world\city_scenes\open_world_city_scenes.questphase)",
    R"(base\open_world\community\global_phase\open_world_global_commuinity_phase.questphase)",
    R"(base\open_world\fixers\open_world_fixers.questphase)",
    R"(base\open_world\phases\city_entities\city_entities.questphase)",
    R"(base\open_world\vendors\open_world_vendors.questphase)",
    R"(base\quest\bugfixing\open_world_bugfixing.questphase)",
};

using ExecuteNodeFn = std::uint8_t (*)(void *phase, void *node, void *context, const void *socket,
                                       void *outputs);
using CreateContextFn = void *(*)(void *system, void *context, std::int32_t, std::int8_t,
                                  std::int32_t, std::int32_t);
using ExecutePathFn = std::uint8_t (*)(void *phase, void *context,
                                       const RED4ext::DynArray<std::uint16_t> *path,
                                       std::uint16_t node, std::uint64_t socket, void *outputs);

ExecuteNodeFn g_original = nullptr;
void *g_target = nullptr;
std::atomic_bool g_active{false};
std::atomic_bool g_layoutChecked{false}, g_layoutOk{false};
thread_local bool t_replaying = false;

using Path = std::vector<std::uint16_t>;
struct Held {
  Path phase;
  std::uint16_t node{};
  std::uint64_t socket{};
  auto Key() const { return std::tie(phase, node, socket); }
};

std::mutex g_lock;                       // everything below
std::set<std::uint64_t> g_allowed;       // open-world resource path hashes
std::vector<Path> g_openPrefixes;        // node paths of open-world phases met
std::map<std::uint32_t, bool> g_byPhase; // NodePathHash -> allowed (cache)
std::vector<Held> g_held;
std::set<std::tuple<Path, std::uint16_t, std::uint64_t>> g_heldKeys;
bool g_overflow = false;
std::uint64_t g_refused = 0, g_passed = 0, g_repeats = 0;
std::set<std::uint32_t> g_frozenPhases;

// ---- guarded reads (no C++ objects with destructors in __try functions) ----

bool CopyOut(const void *from, void *to, std::size_t size) {
  __try {
    std::memcpy(to, from, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

template <class T> bool ReadAt(std::uintptr_t address, T &value) {
  if (address < 0x10000 || address > 0x00007FFFFFFFFFF0)
    return false;
  return CopyOut(reinterpret_cast<const void *>(address), &value, sizeof(T));
}

struct ArrayHead {
  std::uintptr_t entries;
  std::uint32_t capacity;
  std::uint32_t size;
};

bool ReadPath(std::uintptr_t phase, Path &out) {
  ArrayHead head{};
  if (!ReadAt(phase + kPhasePath, head) || head.size > kMaxPath)
    return false;
  out.resize(head.size);
  return head.size == 0 || CopyOut(reinterpret_cast<const void *>(head.entries), out.data(),
                                   head.size * sizeof(std::uint16_t));
}

std::uint64_t ResourceOf(std::uintptr_t phase) {
  std::uintptr_t resource = 0;
  std::uint64_t path = 0;
  if (!ReadAt(phase + kPhaseResource, resource) || !resource ||
      !ReadAt(resource + kResourcePath, path))
    return 0;
  return path;
}

bool IsPrefix(const Path &prefix, const Path &path) {
  return path.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

std::uint32_t HashOf(const Path &path) {
  std::uint32_t hash = 0x811C9DC5; // FNV-1a over the ids, as Codeware's
  for (const auto id : path)
    for (const auto byte : {static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(id >> 8)}) {
      hash ^= byte;
      hash *= 0x01000193;
    }
  return hash;
}

// Under g_lock. Whether the phase keeps running in the freeze.
bool Allowed(std::uintptr_t phase, const Path &path, std::uintptr_t context) {
  if (path.empty())
    return true; // the root graph: it only starts phases
  const auto hash = HashOf(path);
  if (const auto it = g_byPhase.find(hash); it != g_byPhase.end())
    return it->second;

  bool allowed = false;
  const auto learn = [&](std::uintptr_t candidate, const Path &candidatePath) {
    if (!g_allowed.contains(ResourceOf(candidate)))
      return false;
    if (std::ranges::find(g_openPrefixes, candidatePath) == g_openPrefixes.end()) {
      g_openPrefixes.push_back(candidatePath);
      g_byPhase.clear(); // earlier refusals may have been its children
      log::Info("quest: open-world phase {:#x} keeps running (resource {:#x})",
                HashOf(candidatePath), ResourceOf(candidate));
    }
    return true;
  };
  allowed = learn(phase, path);
  if (!allowed) {
    ArrayHead stack{};
    if (ReadAt(context + kContextStack, stack) && stack.size <= kMaxStack)
      for (std::uint32_t i = 0; i < stack.size && !allowed; ++i) {
        std::uintptr_t ancestor = 0;
        Path ancestorPath;
        if (ReadAt(stack.entries + i * sizeof(void *), ancestor) && ancestor &&
            ReadPath(ancestor, ancestorPath))
          allowed = learn(ancestor, ancestorPath);
      }
  }
  if (!allowed)
    for (const auto &prefix : g_openPrefixes)
      if (IsPrefix(prefix, path)) {
        allowed = true;
        break;
      }
  g_byPhase[hash] = allowed;
  return allowed;
}

std::uint8_t Detour(void *phase, void *node, void *context, const void *socket, void *outputs) {
  if (!g_active.load(std::memory_order_relaxed) || t_replaying)
    return g_original(phase, node, context, socket, outputs);

  const auto phaseAddress = reinterpret_cast<std::uintptr_t>(phase);
  Path path;
  std::uint16_t nodeId = 0;
  std::uint64_t socketName = 0;
  if (!ReadPath(phaseAddress, path) ||
      !ReadAt(reinterpret_cast<std::uintptr_t>(node) + kNodeId, nodeId) ||
      !ReadAt(reinterpret_cast<std::uintptr_t>(socket), socketName))
    return g_original(phase, node, context, socket, outputs); // unreadable: never guess

  // Decided under the lock, run outside it: nodes run nested graphs, which
  // come back here on the same thread.
  bool allowed = false;
  {
    std::lock_guard _(g_lock);
    allowed = Allowed(phaseAddress, path, reinterpret_cast<std::uintptr_t>(context));
    if (allowed)
      ++g_passed;
  }
  if (allowed)
    return g_original(phase, node, context, socket, outputs);
  {
    std::lock_guard _(g_lock);
    ++g_refused;
    if (g_frozenPhases.insert(HashOf(path)).second)
      log::Info("quest: freezing phase {:#x} (resource {:#x}, depth {})", HashOf(path),
                ResourceOf(phaseAddress), path.size());
    // Kept once per signal: the engine wakes some nodes (scenes, phones)
    // again and again while they wait.
    auto key = std::make_tuple(path, nodeId, socketName);
    if (g_heldKeys.contains(key)) {
      ++g_repeats;
    } else if (g_held.size() >= kMaxHeld) {
      if (!g_overflow)
        log::Error("quest: more than {} signals held: the guest's own story lost some, its "
                   "leave must reload its last save",
                   kMaxHeld);
      g_overflow = true;
    } else {
      g_heldKeys.insert(key);
      g_held.push_back({std::move(path), nodeId, socketName});
    }
  }
  return kRefused;
}

bool LayoutOk() {
  if (!g_layoutChecked.exchange(true)) {
    auto *rtti = RED4ext::CRTTISystem::Get();
    const auto size = [&](const char *name) {
      auto *type = rtti->GetClass(name);
      return type ? type->GetSize() : 0u;
    };
    const bool ok =
        size("questPhaseInstance") == kPhaseSize && size("CResource") == kResourceSize &&
        size("questNodeDefinition") == kNodeSize && size("questQuestsSystem") == kSystemSize;
    g_layoutOk = ok;
    if (!ok)
      log::Error("quest: questPhaseInstance, CResource, questNodeDefinition or questQuestsSystem "
                 "differ from 2.50: the quest freeze is off (no story sync for a guest)");
  }
  return g_layoutOk;
}

// The game's quests system (RTTI: questQuestsSystem).
void *QuestsSystem() {
  auto *engine = RED4ext::CGameEngine::Get();
  auto *type = RED4ext::CRTTISystem::Get()->GetClass("questIQuestsSystem");
  if (!engine || !engine->framework || !engine->framework->gameInstance || !type)
    return nullptr;
  return engine->framework->gameInstance->GetSystem(type);
}

// Gives one held signal back to its phase. Game thread.
std::uint8_t Replay(void *system, void *root, const Held &held) {
  alignas(16) std::array<std::byte, kContextSize> context{};
  static RED4ext::UniversalRelocFunc<CreateContextFn> createContext(kCreateContext);
  createContext(system, context.data(), 1, 0, -1, -1);
  RED4ext::DynArray<std::uint16_t> path;
  for (const auto id : held.phase)
    path.PushBack(id);
  RED4ext::DynArray<std::array<std::byte, 16>> outputs; // QuestNodeSocket[]
  const auto vtable = *reinterpret_cast<void ***>(root);
  const auto executePath = reinterpret_cast<ExecutePathFn>(vtable[kExecutePathSlot]);
  return executePath(root, context.data(), &path, held.node, held.socket, &outputs);
}
} // namespace

bool InstallQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle,
                        const std::vector<std::string> &allow) {
  {
    std::lock_guard _(g_lock);
    for (const auto *path : kOpenWorld)
      g_allowed.insert(RED4ext::ResourcePath(path).hash);
    for (const auto &path : allow)
      g_allowed.insert(RED4ext::ResourcePath(path.c_str()).hash);
  }
  g_target = reinterpret_cast<void *>(RED4ext::UniversalRelocBase::Resolve(kExecuteNode));
  if (!g_target) {
    log::Error("quest: ExecuteNode is not in this game version: no quest freeze");
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_target, reinterpret_cast<void *>(&Detour),
                            reinterpret_cast<void **>(&g_original))) {
    log::Error("quest: could not hook ExecuteNode: no quest freeze");
    g_target = nullptr;
    return false;
  }
  log::Info("quest: freeze hook in ({} open-world phases keep running)", g_allowed.size());
  return true;
}

void RemoveQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_active = false;
  if (g_target)
    sdk->hooking->Detach(handle, g_target);
  g_target = nullptr;
}

bool QuestFreezeAvailable() { return g_target && LayoutOk(); }

bool QuestFrozen() { return g_active.load(); }

bool SetQuestFreeze(bool on) {
  if (on) {
    if (!QuestFreezeAvailable())
      return false;
    if (!g_active.exchange(true))
      log::Info("quest: own quests frozen");
    return true;
  }
  if (!g_active.exchange(false))
    return false;

  std::vector<Held> held;
  bool overflow = false;
  {
    std::lock_guard _(g_lock);
    held = std::exchange(g_held, {});
    g_heldKeys.clear();
    overflow = std::exchange(g_overflow, false);
    g_frozenPhases.clear();
  }
  auto *system = QuestsSystem();
  void *root = nullptr;
  if (system)
    ReadAt(reinterpret_cast<std::uintptr_t>(system) + kSystemRoot, root);
  if (!root) {
    log::Error("quest: no quests system: {} held signals dropped", held.size());
    return false;
  }
  auto *lock = reinterpret_cast<RED4ext::SharedSpinLock *>(
      reinterpret_cast<std::uintptr_t>(system) + kSystemLock);
  std::size_t given = 0;
  t_replaying = true;
  lock->Lock();
  for (const auto &signal : held) {
    Replay(system, root, signal);
    ++given;
  }
  lock->Unlock();
  t_replaying = false;
  log::Info("quest: own quests resumed ({} held signals given back{})", given,
            overflow ? ", some were lost" : "");
  return true;
}

std::uint32_t QuestFreezeHeld() {
  std::lock_guard _(g_lock);
  return static_cast<std::uint32_t>(g_held.size());
}

bool QuestFreezeOverflowed() {
  std::lock_guard _(g_lock);
  return g_overflow;
}

std::string QuestFreezeReport() {
  std::lock_guard _(g_lock);
  return std::format("quest freeze {}: {} nodes refused ({} repeats), {} passed, {} signals held, "
                     "{} phases frozen, {} open-world phases met{}",
                     g_active ? "on" : "off", g_refused, g_repeats, g_passed, g_held.size(),
                     g_frozenPhases.size(), g_openPrefixes.size(), g_overflow ? ", OVERFLOW" : "");
}
} // namespace tandem::plugin::engine
