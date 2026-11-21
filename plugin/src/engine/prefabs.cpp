// Quest prefabs (engine-knowledge §13.103, read offline on the 2.50 exe with
// tools/re, the handler's place checked live on 2.50).
//
//   questQuestsSystem (RTTI 0x300): +0x100 Handle<questQuestPrefabsHandler>
//     (made in the system's init, 0x140697564).
//   questQuestPrefabsHandler (RTTI 0xF0, a worldIQuestPrefabStateListener):
//     +0x30  SharedSpinLock, held by every change a phase makes
//     +0x60  SortedMap<GlobalNodeRef u64, i32>: keys (DynArray: +0x60
//            entries, +0x6C size), counts at +0x70: how many of our running
//            phases asked for each prefab (a phase's start 0x1406996c0 counts
//            up, its end 0x140698f24 down; at 0 the entry goes)
//     +0xB0  DynArray<{u64 GlobalNodeRef, u8 mode}> (16 bytes each: the
//            layout of questQuestPrefabsEntry, 0x10, which CDPR's cancelled
//            multiplayer replicated as questQuestsSystemReplicatedState.
//            replicatedQuestPrefabs): every prefab ever set and its mode now,
//            worldQuestPrefabLoadingMode {Disable 0, ForceLoad 1}
//     +0xC8  the world streaming system (worldRuntimeSystemWorldStreaming,
//            RTTI 0x3E0), which streams each prefab's sectors (sector
//            descriptors carry questPrefabNodeRef) in or out on its update.
//   SetMode(handler, GlobalNodeRef, mode): hash 660547284 (0x140699498):
//     the entry's mode (added when new; nothing when it already has that
//     mode), then the pair queued for the streaming system (its own lock).
//     A phase's count going 0→1 calls it with ForceLoad, 1→0 with Disable;
//     the replicated state's apply (0x142c9f548, MP leftover) calls it for
//     every pair it is given. Calling it ourselves is the same path.
//
// Game thread only. Reads are guarded (SEH) and sized; the layout check runs
// once: RTTI sizes, then the class of the object at +0x100 and at +0xC8
// (ISerializable::GetNativeType).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "prefabs.hpp"

#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/SharedSpinLock.hpp>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kSetMode = 660547284;
constexpr std::size_t kSystemSize = 0x300, kSystemHandler = 0x100;
constexpr std::size_t kHandlerSize = 0xF0, kHandlerLock = 0x30, kHandlerCounts = 0x60,
                      kHandlerModes = 0xB0, kHandlerStreaming = 0xC8;
constexpr std::size_t kEntrySize = 0x10, kStreamingSize = 0x3E0;
constexpr std::uint32_t kMaxEntries = 8192;
constexpr std::uint8_t kDisable = 0, kForceLoad = 1;

using SetModeFn = void (*)(void *handler, std::uint64_t prefab, std::uint8_t mode);

struct ArrayHead {
  std::uintptr_t entries;
  std::uint32_t capacity;
  std::uint32_t size;
};
struct ModeEntry {
  std::uint64_t prefab;
  std::uint8_t mode;
  std::uint8_t pad[7];
};
static_assert(sizeof(ModeEntry) == kEntrySize);

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

RED4ext::CClass *NativeTypeOf(void *object) {
  __try {
    return static_cast<RED4ext::ISerializable *>(object)->GetNativeType();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return nullptr;
  }
}

// What was found at the check: the handler, or why not.
bool g_checked = false;
bool g_ok = false;
std::string g_why = "not checked yet";

void *QuestsSystem() {
  auto *engine = RED4ext::CGameEngine::Get();
  auto *type = RED4ext::CRTTISystem::Get()->GetClass("questIQuestsSystem");
  if (!engine || !engine->framework || !engine->framework->gameInstance || !type)
    return nullptr;
  return engine->framework->gameInstance->GetSystem(type);
}

bool SizesMatch() {
  auto *rtti = RED4ext::CRTTISystem::Get();
  const auto size = [&](const char *name) {
    auto *type = rtti->GetClass(name);
    return type ? type->GetSize() : 0u;
  };
  return size("questQuestsSystem") == kSystemSize &&
         size("questQuestPrefabsHandler") == kHandlerSize &&
         size("questQuestPrefabsEntry") == kEntrySize &&
         size("worldRuntimeSystemWorldStreaming") == kStreamingSize;
}

// The handler of the quests system now (it lives as long as the game
// instance: looked up at each call, its class checked once).
void *Handler() {
  auto *system = QuestsSystem();
  std::uintptr_t handler = 0;
  if (!system || !ReadAt(reinterpret_cast<std::uintptr_t>(system) + kSystemHandler, handler) ||
      !handler)
    return nullptr;
  if (!g_checked) {
    g_checked = true;
    auto *rtti = RED4ext::CRTTISystem::Get();
    std::uintptr_t streaming = 0;
    if (!SizesMatch())
      g_why = "questQuestsSystem, questQuestPrefabsHandler, questQuestPrefabsEntry or "
              "worldRuntimeSystemWorldStreaming differ from 2.50";
    else if (NativeTypeOf(reinterpret_cast<void *>(handler)) !=
             rtti->GetClass("questQuestPrefabsHandler"))
      g_why = "the quests system holds no questQuestPrefabsHandler where 2.50 had it";
    else if (!ReadAt(handler + kHandlerStreaming, streaming) || !streaming ||
             NativeTypeOf(reinterpret_cast<void *>(streaming)) !=
                 rtti->GetClass("worldRuntimeSystemWorldStreaming"))
      g_why = "the prefab handler does not lead to the world streaming system as on 2.50";
    else {
      g_ok = true;
      g_why = "as on 2.50";
    }
    if (g_ok)
      log::Info("prefabs: the quests system's prefab handler is as measured on 2.50");
    else
      log::Error("prefabs: {}: quest prefabs are not shared (a guest cannot see the host's "
                 "quest interiors)",
                 g_why);
  }
  return g_ok ? reinterpret_cast<void *>(handler) : nullptr;
}

RED4ext::SharedSpinLock *LockOf(void *handler) {
  return reinterpret_cast<RED4ext::SharedSpinLock *>(reinterpret_cast<std::uintptr_t>(handler) +
                                                     kHandlerLock);
}

// The handler's arrays, copied (under its lock).
bool ReadModes(void *handler, std::vector<ModeEntry> &out) {
  ArrayHead head{};
  if (!ReadAt(reinterpret_cast<std::uintptr_t>(handler) + kHandlerModes, head) ||
      head.size > kMaxEntries || head.size > head.capacity)
    return false;
  out.resize(head.size);
  return head.size == 0 ||
         CopyOut(reinterpret_cast<const void *>(head.entries), out.data(), head.size * kEntrySize);
}
bool ReadHeld(void *handler, std::vector<std::uint64_t> &out) {
  ArrayHead head{};
  if (!ReadAt(reinterpret_cast<std::uintptr_t>(handler) + kHandlerCounts, head) ||
      head.size > kMaxEntries || head.size > head.capacity)
    return false;
  out.resize(head.size);
  return head.size == 0 || CopyOut(reinterpret_cast<const void *>(head.entries), out.data(),
                                   head.size * sizeof(std::uint64_t));
}
} // namespace

bool QuestPrefabsAvailable() { return Handler() != nullptr; }

bool QuestPrefabsOn(std::vector<std::uint64_t> &out) {
  out.clear();
  auto *handler = Handler();
  if (!handler)
    return false;
  std::vector<ModeEntry> modes;
  auto *lock = LockOf(handler);
  lock->Lock();
  const bool read = ReadModes(handler, modes);
  lock->Unlock();
  if (!read)
    return false;
  for (const auto &entry : modes)
    if (entry.mode == kForceLoad && entry.prefab != 0)
      out.push_back(entry.prefab);
  std::ranges::sort(out);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return true;
}

std::optional<QuestPrefabHere> QuestPrefabState(std::uint64_t prefab) {
  auto *handler = Handler();
  if (!handler)
    return std::nullopt;
  std::vector<ModeEntry> modes;
  std::vector<std::uint64_t> held;
  auto *lock = LockOf(handler);
  lock->Lock();
  const bool read = ReadModes(handler, modes) && ReadHeld(handler, held);
  lock->Unlock();
  if (!read)
    return std::nullopt;
  QuestPrefabHere here;
  for (const auto &entry : modes)
    if (entry.prefab == prefab)
      here.on = entry.mode == kForceLoad;
  here.held = std::ranges::find(held, prefab) != held.end();
  return here;
}

bool SetQuestPrefab(std::uint64_t prefab, bool on) {
  auto *handler = Handler();
  if (!handler || prefab == 0)
    return false;
  static RED4ext::UniversalRelocFunc<SetModeFn> setMode(kSetMode);
  auto *lock = LockOf(handler);
  lock->Lock();
  setMode(handler, prefab, on ? kForceLoad : kDisable);
  lock->Unlock();
  return true;
}

std::string QuestPrefabsReport() {
  auto *handler = Handler();
  if (!handler)
    return std::format("prefabs: unavailable ({})", g_why);
  std::vector<ModeEntry> modes;
  std::vector<std::uint64_t> held;
  auto *lock = LockOf(handler);
  lock->Lock();
  const bool read = ReadModes(handler, modes) && ReadHeld(handler, held);
  lock->Unlock();
  if (!read)
    return "prefabs: the handler's arrays could not be read";
  const auto on =
      std::ranges::count_if(modes, [](const ModeEntry &entry) { return entry.mode == kForceLoad; });
  return std::format("prefabs: {} known to our quests, {} on, {} asked for by our own phases",
                     modes.size(), on, held.size());
}
} // namespace tandem::plugin::engine
