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
// (Codeware's list, src/App/Quest/QuestPhaseRegistry.cpp:12-20, V's homes'
// interactions, plus tandem.ini quest_freeze_allow), and any phase under an
// open-world one
// (by node path prefix, as Codeware's IsRelatedQuestNodePath). A phase is
// recognised as open-world from its own resource or from an ancestor in
// the context's phase stack (ExecuteGraph pushes each phase it runs).
//
// The hook may run on a job thread: it only reads engine memory (guarded)
// and touches its own state under a mutex, and logs after releasing it.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../degradations.hpp"
#include "../log.hpp"
#include "../observer.hpp"
#include "access.hpp"
#include "quests.hpp"
#include "scenes.hpp"

#include <RED4ext/DynArray.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/ResourcePath.hpp>
#include <RED4ext/SharedSpinLock.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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
// The function in that slot on 2.50, by address-library hash: the root
// phase is a questRootInstance (its vtable 0x143699f88 holds 0x140bea3d0
// there), a plain questPhaseInstance (0x14308a948) holds 0x140bea4c4. A root
// whose slot holds neither is not called.
constexpr std::uint32_t kRootExecutePath = 2987275044;
constexpr std::uint32_t kPhaseExecutePath = 3749393265;
// The context is not an RTTI class: its 0x340 bytes (Codeware's
// QuestContext, 2.50) are followed by this many of ours, which the engine's
// CreateContext must leave untouched. A larger context is caught there
// instead of overrunning the stack, and turns the freeze off.
constexpr std::size_t kContextGuard = 0x200;
constexpr std::byte kGuardByte{0xA5};
constexpr std::uint32_t kMaxPath = 64, kMaxStack = 32;
constexpr std::size_t kMaxHeld = 4096;
constexpr std::uint8_t kRefused = 1; // the engine's own "nothing here" (see above)

// Codeware's open-world phases (QuestPhaseRegistry.cpp:12-20), but its
// minor activities (below).
constexpr const char *kOpenWorld[] = {
    R"(base\open_world\city_scenes\open_world_city_scenes.questphase)",
    R"(base\open_world\community\global_phase\open_world_global_commuinity_phase.questphase)",
    R"(base\open_world\fixers\open_world_fixers.questphase)",
    R"(base\open_world\phases\city_entities\city_entities.questphase)",
    R"(base\open_world\vendors\open_world_vendors.questphase)",
    R"(base\quest\bugfixing\open_world_bugfixing.questphase)",
};

// The NCPD scanner's activities (hustles, reported crimes, gang watches,
// outposts, hidden stashes: one child phase each of this one, Codeware's
// QuestPhaseRegistry.cpp:10). Whose world plays them where we stand is the
// group's leader's (docs/architecture/story.md, "Open-world activities"):
// ours apart or leading, the host's while grouped with it, when ours wait
// as our gigs do, and their held signals are given back when we are apart.
constexpr const char *kActivities[] = {
    R"(base\open_world\minor_activities\open_world_minor_activities.questphase)",
};

// V's homes: each player's own apartment interactions (the shower, the bed,
// the coffee, the cat, the decorations) keep running on a frozen guest, as
// its own surroundings do (docs/architecture/activities.md, "Apartments";
// engine-knowledge §13.85). The four bought apartments' phases are not named
// in the resource list: `quest trace on` logs them, quest_freeze_allow adds
// them.
constexpr const char *kPersonal[] = {
    R"(base\quest\minor_quests\mq000\phases\mq000_vs_apartment_interactions.questphase)",
    R"(base\quest\minor_quests\mq000\phases\mq000_vs_apartment_decorations.questphase)",
    R"(dlc\dlc6_apart\phases\dlc6_apart_judy_apartment.questphase)",
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
// CheckQuests: the context's constructor and the two functions the root
// phase's slot may hold (0: not in this game).
CreateContextFn g_createContext = nullptr;
std::uintptr_t g_rootExecutePath = 0, g_phaseExecutePath = 0;
std::atomic_bool g_replayOk{false};
std::atomic_bool g_active{false};
bool g_freezeAllowed = false; // tandem.ini quest_freeze = on (set at load)
std::atomic_bool g_layoutChecked{false}, g_layoutOk{false};
thread_local bool t_replaying = false;
// Ownership (docs/architecture/quest-carryover.md, "Whose gig is it"): a
// phase this game does not own waits. Story phases are the host's for the
// whole session; a gig's phase is ours while we are apart from the host and
// waits while we are grouped with it. Grouped until the regions work says
// otherwise: then gigs wait like the story, as before.
std::atomic_bool g_grouped{true};
// The same for the open-world activities: the host's world leads where we
// stand (the story script says so from the regions every second).
std::atomic_bool g_activitiesGrouped{true};
std::atomic_bool g_trace{false};

using Path = std::vector<std::uint16_t>;
struct Held {
  Path phase;
  std::uint16_t node{};
  std::uint64_t socket{};
  bool gig{};
  bool activity{};
  auto Key() const { return std::tie(phase, node, socket); }
};

// What a phase is to the freeze.
enum class Owner : std::uint8_t { World, Gig, Activity, Story }; // World: always runs

std::mutex g_lock;                        // everything below
std::set<std::uint64_t> g_allowed;        // open-world resource path hashes
std::set<std::uint64_t> g_gigs;           // gig resource path hashes (tandem.ini)
std::set<std::uint64_t> g_activityPhases; // open-world activity resource path hashes
std::vector<Path> g_openPrefixes;         // node paths of open-world phases met
std::vector<Path> g_gigPrefixes;          // and of gig phases
std::vector<Path> g_activityPrefixes;     // and of open-world activity phases
std::map<std::uint32_t, Owner> g_byPhase; // NodePathHash -> what it is (cache)
std::set<std::uint32_t> g_tracedPhases;   // spike C2: lines already logged
std::set<std::pair<std::uint32_t, std::uint16_t>> g_tracedNodes;
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

// Under g_lock. What the phase is: by its own resource, an ancestor's on
// the context's phase stack, or a known phase's node path it extends. The
// open world wins over a gig list entry. A phase met for the first time
// adds its line to `lines`, logged by the caller once g_lock is released.
Owner OwnerOf(std::uintptr_t phase, const Path &path, std::uintptr_t context,
              std::vector<std::string> &lines) {
  if (path.empty())
    return Owner::World; // the root graph: it only starts phases
  const auto hash = HashOf(path);
  if (const auto it = g_byPhase.find(hash); it != g_byPhase.end())
    return it->second;

  bool open = false, gig = false, activity = false;
  const auto learn = [&](std::uintptr_t candidate, const Path &candidatePath) {
    const auto resource = ResourceOf(candidate);
    const bool isOpen = g_allowed.contains(resource);
    const bool isActivity = !isOpen && g_activityPhases.contains(resource);
    const bool isGig = !isOpen && !isActivity && g_gigs.contains(resource);
    if (!isOpen && !isActivity && !isGig)
      return;
    auto &prefixes = isOpen ? g_openPrefixes : isActivity ? g_activityPrefixes : g_gigPrefixes;
    if (std::ranges::find(prefixes, candidatePath) == prefixes.end()) {
      prefixes.push_back(candidatePath);
      g_byPhase.clear(); // earlier verdicts may have been its children
      lines.push_back(std::format("quest: {} phase {:#x} (resource {:#x})",
                                  isOpen       ? "open-world phase keeps running:"
                                  : isActivity ? "open-world activity"
                                               : "gig",
                                  HashOf(candidatePath), resource));
    }
    (isOpen ? open : isActivity ? activity : gig) = true;
  };
  learn(phase, path);
  ArrayHead stack{};
  if (!open && !activity && ReadAt(context + kContextStack, stack) && stack.size <= kMaxStack)
    for (std::uint32_t i = 0; i < stack.size && !open && !activity; ++i) {
      std::uintptr_t ancestor = 0;
      Path ancestorPath;
      if (ReadAt(stack.entries + i * sizeof(void *), ancestor) && ancestor &&
          ReadPath(ancestor, ancestorPath))
        learn(ancestor, ancestorPath);
    }
  for (const auto &prefix : g_openPrefixes)
    open = open || IsPrefix(prefix, path);
  for (const auto &prefix : g_activityPrefixes)
    activity = activity || IsPrefix(prefix, path);
  for (const auto &prefix : g_gigPrefixes)
    gig = gig || IsPrefix(prefix, path);
  const auto owner = open       ? Owner::World
                     : activity ? Owner::Activity
                     : gig      ? Owner::Gig
                                : Owner::Story;
  g_byPhase[hash] = owner;
  return owner;
}

// ---- spike C2: what a phase is made of (quest trace on) ----
// One line per phase (its chain of resources, root first) and one per
// journal node met in it (the journal entry it names): enough to tell, from
// one solo run past a few gigs, how a gig's phase can be recognised.

struct JournalOffsets {
  RED4ext::CClass *node = nullptr;
  std::uint32_t type = 0, path = 0, realPath = 0, className = 0;
};
JournalOffsets g_journal; // set once, before the trace is on

// The journal path a journal node names, into `out` (guarded).
bool JournalPathOf(void *node, char *out, std::size_t size, std::uint64_t &className) {
  __try {
    auto *type = static_cast<RED4ext::ISerializable *>(node)->GetType();
    if (!type || !type->IsA(g_journal.node))
      return false;
    const auto *bytes = static_cast<const std::uint8_t *>(node);
    const auto *kind = *reinterpret_cast<const std::uint8_t *const *>(bytes + g_journal.type);
    if (!kind)
      return false;
    const auto *path = *reinterpret_cast<const std::uint8_t *const *>(kind + g_journal.path);
    if (!path)
      return false;
    const auto *text =
        reinterpret_cast<const RED4ext::CString *>(path + g_journal.realPath)->c_str();
    className = *reinterpret_cast<const std::uint64_t *>(path + g_journal.className);
    std::size_t i = 0;
    for (; text && i + 1 < size && text[i]; ++i)
      out[i] = text[i];
    out[i] = 0;
    return i > 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void Trace(std::uintptr_t phase, const Path &path, void *node, std::uint16_t nodeId,
           std::uintptr_t context) {
  const auto hash = HashOf(path);
  std::string chain;
  bool firstOfPhase = false;
  {
    std::lock_guard _(g_lock);
    if (g_tracedPhases.size() + g_tracedNodes.size() > 20000)
      return;
    firstOfPhase = g_tracedPhases.insert(hash).second;
  }
  if (firstOfPhase) {
    ArrayHead stack{};
    if (ReadAt(context + kContextStack, stack) && stack.size <= kMaxStack)
      for (std::uint32_t i = 0; i < stack.size; ++i) {
        std::uintptr_t ancestor = 0;
        Path ancestorPath;
        if (ReadAt(stack.entries + i * sizeof(void *), ancestor) && ancestor &&
            ReadPath(ancestor, ancestorPath))
          chain += std::format(" {:#x}@{}", ResourceOf(ancestor), ancestorPath.size());
      }
    std::string ids;
    for (const auto id : path)
      ids += std::format("{}{}", ids.empty() ? "" : ".", id);
    log::Info("quest trace: phase {:#x} path {} resource {:#x} chain{}", hash, ids,
              ResourceOf(phase), chain);
  }
  char journal[200];
  std::uint64_t className = 0;
  if (g_journal.node && JournalPathOf(node, journal, sizeof(journal), className)) {
    {
      std::lock_guard _(g_lock);
      if (!g_tracedNodes.insert({hash, nodeId}).second)
        return;
    }
    log::Info("quest trace: phase {:#x} node {} journal {} ({})", hash, nodeId, journal,
              RED4ext::CName(className).ToString());
  }
}

// ---- the moment list's phases (SetPhaseNotes) ----

std::atomic_bool g_phaseNotes{false};
std::mutex g_phaseLock;                             // g_phaseSeen
std::map<std::uint64_t, std::uint64_t> g_phaseSeen; // resource path hash -> ms

std::uint64_t NowMs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

// The phase's own resource and its ancestors' (guarded reads), noted as
// running now. A few reads and one short lock per node, only while the list
// has a phase.
void NotePhase(std::uintptr_t phase, std::uintptr_t context) {
  std::array<std::uint64_t, 17> resources{};
  std::size_t count = 0;
  if (const auto own = ResourceOf(phase))
    resources[count++] = own;
  ArrayHead stack{};
  if (ReadAt(context + kContextStack, stack) && stack.size <= kMaxStack)
    for (std::uint32_t i = 0; i < stack.size && count < resources.size(); ++i) {
      std::uintptr_t ancestor = 0;
      if (ReadAt(stack.entries + i * sizeof(void *), ancestor) && ancestor)
        if (const auto resource = ResourceOf(ancestor))
          resources[count++] = resource;
    }
  if (count == 0)
    return;
  const auto now = NowMs();
  std::lock_guard _(g_phaseLock);
  for (std::size_t i = 0; i < count; ++i)
    g_phaseSeen[resources[i]] = now;
  if (g_phaseSeen.size() > 512)
    std::erase_if(g_phaseSeen, [&](const auto &seen) { return now - seen.second > 30'000; });
}

// The observer's story trail (observer.hpp): a phase resource the first
// time it runs. The phase last told, per thread: one guarded read and the
// observer's lock only when a thread moves to another phase.
thread_local std::uintptr_t t_toldPhase = 0;
void TellObserver(std::uintptr_t phase) {
  if (phase == t_toldPhase)
    return;
  t_toldPhase = phase;
  Observer::Get().Phase(ResourceOf(phase));
}

std::uint8_t Detour(void *phase, void *node, void *context, const void *socket, void *outputs) {
  // Scene privacy (scenes.cpp): which scene runs, on every game.
  ObserveSceneNode(node);
  if (Observer::On())
    TellObserver(reinterpret_cast<std::uintptr_t>(phase));
  if (g_phaseNotes.load(std::memory_order_relaxed))
    NotePhase(reinterpret_cast<std::uintptr_t>(phase), reinterpret_cast<std::uintptr_t>(context));
  const bool tracing = g_trace.load(std::memory_order_relaxed);
  if ((!g_active.load(std::memory_order_relaxed) && !tracing) || t_replaying)
    return g_original(phase, node, context, socket, outputs);

  const auto phaseAddress = reinterpret_cast<std::uintptr_t>(phase);
  Path path;
  std::uint16_t nodeId = 0;
  std::uint64_t socketName = 0;
  if (!ReadPath(phaseAddress, path) ||
      !ReadAt(reinterpret_cast<std::uintptr_t>(node) + kNodeId, nodeId) ||
      !ReadAt(reinterpret_cast<std::uintptr_t>(socket), socketName))
    return g_original(phase, node, context, socket, outputs); // unreadable: never guess
  if (tracing)
    Trace(phaseAddress, path, node, nodeId, reinterpret_cast<std::uintptr_t>(context));
  if (!g_active.load(std::memory_order_relaxed))
    return g_original(phase, node, context, socket, outputs);

  // Decided under the lock, run outside it: nodes run nested graphs, which
  // come back here on the same thread. Nothing is logged under the lock
  // either: every quest node of every thread waits on it, and a log line
  // is a file write (and the logger's own lock).
  bool allowed = false, gig = false, activity = false;
  std::vector<std::string> lines;
  {
    std::lock_guard _(g_lock);
    const auto owner =
        OwnerOf(phaseAddress, path, reinterpret_cast<std::uintptr_t>(context), lines);
    gig = owner == Owner::Gig;
    activity = owner == Owner::Activity;
    // Ours to run: the open world, our gigs while we are apart, and our
    // open-world activities while our world leads where we stand.
    allowed = owner == Owner::World || (gig && !g_grouped.load(std::memory_order_relaxed)) ||
              (activity && !g_activitiesGrouped.load(std::memory_order_relaxed));
    if (allowed)
      ++g_passed;
  }
  for (const auto &line : lines)
    log::Debug("{}", line);
  if (allowed)
    return g_original(phase, node, context, socket, outputs);
  const auto hash = HashOf(path);
  const auto depth = path.size();
  bool firstOfPhase = false, overflowed = false;
  {
    std::lock_guard _(g_lock);
    ++g_refused;
    firstOfPhase = g_frozenPhases.insert(hash).second;
    // Kept once per signal: the engine wakes some nodes (scenes, phones)
    // again and again while they wait.
    auto key = std::make_tuple(path, nodeId, socketName);
    if (g_heldKeys.contains(key)) {
      ++g_repeats;
    } else if (g_held.size() >= kMaxHeld) {
      overflowed = !std::exchange(g_overflow, true);
    } else {
      g_heldKeys.insert(key);
      g_held.push_back({std::move(path), nodeId, socketName, gig, activity});
    }
  }
  if (firstOfPhase)
    log::Debug("quest: freezing {} phase {:#x} (resource {:#x}, depth {})",
               gig        ? "gig"
               : activity ? "open-world activity"
                          : "story",
               hash, ResourceOf(phaseAddress), depth);
  if (overflowed) {
    log::Error("quest: more than {} signals held: the guest's own story lost some, its leave "
               "must reload its last save",
               kMaxHeld);
    Degradations::Get().Fallback("quest signals dropped",
                                 "more signals held than there is room for: the guest's own "
                                 "story lost some");
  }
  return kRefused;
}

bool LayoutOk() {
  if (!g_layoutChecked.exchange(true)) {
    // Every check, so the log names each mismatch at once (sizes from RTTI,
    // measured on 2.50).
    const auto f = feature::kQuestNodes;
    bool ok = ClassSizeIs(f, "questPhaseInstance", kPhaseSize);
    ok = ClassSizeIs(f, "CResource", kResourceSize) && ok;
    ok = ClassSizeIs(f, "questNodeDefinition", kNodeSize) && ok;
    ok = ClassSizeIs(f, "questQuestsSystem", kSystemSize) && ok;
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

// The root phase's ExecutePath, when its vtable holds one of the two
// functions measured on 2.50 in that slot; null (and the freeze off) when not.
ExecutePathFn ExecutePathOf(void *root) {
  std::uintptr_t vtable = 0, target = 0;
  if (ReadAt(reinterpret_cast<std::uintptr_t>(root), vtable) && InGameImage(vtable) &&
      ReadAt(vtable + kExecutePathSlot * sizeof(void *), target) && target &&
      (target == g_rootExecutePath || target == g_phaseExecutePath))
    return reinterpret_cast<ExecutePathFn>(target);
  g_replayOk = false;
  FeatureOff(feature::kQuestFreeze,
             std::format("the root phase's vtable slot +0x{:X} holds {:#x}, not the function "
                         "measured on 2.50",
                         kExecutePathSlot * sizeof(void *), target));
  return nullptr;
}

// Gives one held signal back to its phase. Game thread, under the quests
// system's lock. 0 when given; when the engine's context is not the one
// measured, the offset it wrote at past its end (nothing given then).
std::size_t Replay(void *system, void *root, ExecutePathFn executePath, const Held &held) {
  alignas(16) std::array<std::byte, kContextSize + kContextGuard> context{};
  std::memset(context.data() + kContextSize, static_cast<int>(kGuardByte), kContextGuard);
  g_createContext(system, context.data(), 1, 0, -1, -1);
  for (std::size_t i = kContextSize; i < context.size(); ++i)
    if (context[i] != kGuardByte)
      return i;
  RED4ext::DynArray<std::uint16_t> path;
  for (const auto id : held.phase)
    path.PushBack(id);
  RED4ext::DynArray<std::array<std::byte, 16>> outputs; // QuestNodeSocket[]
  executePath(root, context.data(), &path, held.node, held.socket, &outputs);
  return 0;
}
// Gives held signals back to their phases, in the order they came. Game
// thread. Answers how many (-1: no quests system).
int GiveBack(const std::vector<Held> &held) {
  auto *system = QuestsSystem();
  void *root = nullptr;
  if (system)
    ReadAt(reinterpret_cast<std::uintptr_t>(system) + kSystemRoot, root);
  if (!root) {
    log::Error("quest: no quests system: {} held signals dropped", held.size());
    Degradations::Get().Fallback("quest signals dropped", "no quests system to give them back to");
    return -1;
  }
  const auto executePath = g_replayOk.load() ? ExecutePathOf(root) : nullptr;
  if (!executePath) {
    log::Error("quest: this game's quest phases are not laid out as on 2.50: {} held signals "
               "dropped, the guest's own story must reload its last save",
               held.size());
    Degradations::Get().Fallback("quest signals dropped",
                                 "the quest phases are not laid out as on 2.50");
    return -1;
  }
  auto *lock = reinterpret_cast<RED4ext::SharedSpinLock *>(
      reinterpret_cast<std::uintptr_t>(system) + kSystemLock);
  int given = 0;
  t_replaying = true;
  // (The engine's own lock, as its callers hold it; none of ours.)
  lock->Lock();
  std::size_t overrun = 0;
  for (const auto &signal : held) {
    overrun = Replay(system, root, executePath, signal);
    if (overrun)
      break;
    ++given;
  }
  lock->Unlock();
  t_replaying = false;
  if (overrun) {
    g_replayOk = false;
    FeatureOff(feature::kQuestFreeze,
               std::format("the quest context is larger than the 0x{:X} bytes measured on 2.50 "
                           "(written at +0x{:X})",
                           kContextSize, overrun));
    log::Error("quest: {} of {} held signals not given back: the guest's own story must reload "
               "its last save",
               held.size() - given, held.size());
  }
  return given;
}
} // namespace

bool InstallQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle,
                        const std::vector<std::string> &allow, const std::vector<std::string> &gigs,
                        bool freeze) {
  g_freezeAllowed = freeze;
  {
    std::lock_guard _(g_lock);
    for (const auto *path : kOpenWorld)
      g_allowed.insert(RED4ext::ResourcePath(path).hash);
    for (const auto *path : kPersonal)
      g_allowed.insert(RED4ext::ResourcePath(path).hash);
    for (const auto &path : allow)
      g_allowed.insert(RED4ext::ResourcePath(path.c_str()).hash);
    for (const auto &path : gigs)
      g_gigs.insert(RED4ext::ResourcePath(path.c_str()).hash);
    for (const auto *path : kActivities)
      g_activityPhases.insert(RED4ext::ResourcePath(path).hash);
  }
  g_target = Find<void *>(feature::kQuestNodes, "the quest phase's ExecuteNode", kExecuteNode);
  if (!g_target) {
    FeatureOff(feature::kQuestFreeze, "no quest node hook");
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_target, reinterpret_cast<void *>(&Detour),
                            reinterpret_cast<void **>(&g_original))) {
    FeatureOff(feature::kQuestNodes, "could not hook ExecuteNode");
    FeatureOff(feature::kQuestFreeze, "no quest node hook");
    g_target = nullptr;
    return false;
  }
  log::Info(
      "quest: node hook in ({})",
      freeze ? std::format("freeze available, {} open-world phases keep running", g_allowed.size())
             : "observing scenes only: tandem.ini quest_freeze = on for the freeze");
  return true;
}

void RemoveQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_active = false;
  if (g_target)
    sdk->hooking->Detach(handle, g_target);
  g_target = nullptr;
}

bool CheckQuests() {
  // Not installed (tandem.ini): said by the plugin's startup, nothing to check.
  if (!g_target)
    return false;
  const bool nodes = LayoutOk();
  if (nodes)
    FeatureOn(feature::kQuestNodes);
  if (!g_freezeAllowed) {
    FeatureSwitchedOff(feature::kQuestFreeze, "tandem.ini quest_freeze = off");
    return nodes;
  }
  // The freeze gives its held signals back through these: without them it
  // must not start.
  const auto f = feature::kQuestFreeze;
  g_createContext = Find<CreateContextFn>(f, "QuestsSystem::CreateContext", kCreateContext);
  g_rootExecutePath = FindAddress(f, "the root phase's ExecutePath", kRootExecutePath);
  g_phaseExecutePath = FindAddress(f, "the quest phase's ExecutePath", kPhaseExecutePath);
  const bool replay = g_createContext && g_rootExecutePath && g_phaseExecutePath;
  if (!nodes)
    FeatureOff(f, "the quest node hook's layouts differ from 2.50");
  else if (replay)
    FeatureOn(f);
  g_replayOk = nodes && replay;
  return g_replayOk;
}

bool QuestFreezeAvailable() {
  return g_target && g_freezeAllowed && LayoutOk() && g_replayOk.load();
}

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
  const int given = GiveBack(held);
  if (given < 0)
    return false;
  log::Info("quest: own quests resumed ({} held signals given back{})", given,
            overflow ? ", some were lost" : "");
  return true;
}

void SetQuestGrouped(bool grouped) {
  if (g_grouped.exchange(grouped) == grouped)
    return;
  log::Info("quest: {} the host: our gigs {}", grouped ? "grouped with" : "apart from",
            grouped ? "wait" : "run");
  if (grouped || !g_active.load())
    return;
  // Apart again: the gig signals held while grouped go back to their phases.
  std::vector<Held> gigs;
  {
    std::lock_guard _(g_lock);
    std::vector<Held> kept;
    for (auto &held : g_held) {
      if (held.gig)
        g_heldKeys.erase(std::make_tuple(held.phase, held.node, held.socket));
      (held.gig ? gigs : kept).push_back(std::move(held));
    }
    g_held = std::move(kept);
  }
  if (!gigs.empty())
    log::Info("quest: {} held gig signals given back", GiveBack(gigs));
}

bool QuestGrouped() { return g_grouped.load(); }

void SetActivitiesGrouped(bool grouped) {
  if (g_activitiesGrouped.exchange(grouped) == grouped)
    return;
  log::Info("quest: our open-world activities {}",
            grouped ? "wait (the host's world leads where we stand)" : "run (ours lead here)");
  if (grouped || !g_active.load())
    return;
  // Ours lead again: the activity signals held meanwhile go back.
  std::vector<Held> activities;
  {
    std::lock_guard _(g_lock);
    std::vector<Held> kept;
    for (auto &held : g_held) {
      if (held.activity)
        g_heldKeys.erase(std::make_tuple(held.phase, held.node, held.socket));
      (held.activity ? activities : kept).push_back(std::move(held));
    }
    g_held = std::move(kept);
  }
  if (!activities.empty())
    log::Info("quest: {} held open-world activity signals given back", GiveBack(activities));
}

bool ActivitiesGrouped() { return g_activitiesGrouped.load(); }

void SetPhaseNotes(bool on) {
  // (The notes read the phase by offset: only on the measured layout.)
  on = on && LayoutOk();
  g_phaseNotes = on;
  if (!on) {
    std::lock_guard _(g_phaseLock);
    g_phaseSeen.clear();
  }
}

std::vector<std::uint64_t> RecentPhases(std::uint32_t maxAgeMs) {
  std::vector<std::uint64_t> out;
  const auto now = NowMs();
  std::lock_guard _(g_phaseLock);
  for (const auto &[resource, at] : g_phaseSeen)
    if (now - at <= maxAgeMs)
      out.push_back(resource);
  return out;
}

void SetQuestTrace(bool on) {
  if (on && !g_journal.node) {
    auto *rtti = RED4ext::CRTTISystem::Get();
    const auto offset = [&](const char *type, const char *name) -> std::uint32_t {
      auto *found = rtti->GetClass(type);
      const auto *property = found ? found->GetProperty(name) : nullptr;
      return property ? property->valueOffset : 0;
    };
    JournalOffsets journal{.node = rtti->GetClass("questJournalNodeDefinition"),
                           .type = offset("questJournalNodeDefinition", "type"),
                           .path = offset("questIJournal_NodeType", "path"),
                           .realPath = offset("gameJournalPath", "realPath"),
                           .className = offset("gameJournalPath", "className")};
    if (journal.node && journal.type && journal.path && journal.realPath && journal.className)
      g_journal = journal;
    else
      log::Warn("quest trace: journal node layout not found by RTTI: phases only");
  }
  {
    std::lock_guard _(g_lock);
    g_tracedPhases.clear();
    g_tracedNodes.clear();
  }
  g_trace = on && g_target != nullptr && LayoutOk();
  log::Info("quest trace: {}", g_trace ? "on" : g_target ? "off" : "no hook (scene_privacy off?)");
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
  return std::format(
      "quest freeze {} ({}): {} nodes refused ({} repeats), {} passed, {} signals held, "
      "{} phases frozen, {} open-world phases met, {} open-world activity phases ({}){}",
      g_active ? "on" : "off", g_grouped ? "grouped" : "apart", g_refused, g_repeats, g_passed,
      g_held.size(), g_frozenPhases.size(), g_openPrefixes.size(), g_activityPrefixes.size(),
      g_activitiesGrouped ? "waiting" : "running", g_overflow ? ", OVERFLOW" : "");
}
} // namespace tandem::plugin::engine
