#pragma once
// Quest prefabs (docs/architecture/story.md, "Quest prefabs";
// engine-knowledge §13.111, read offline on the 2.50 exe): the interiors and
// props a quest phase streams in. The quests system's prefab handler keeps
// one mode per prefab (ForceLoad or Disable) and hands each change to the
// world streaming system. The host reads which are on; a guest turns the
// host's on in its own world the engine's own way (the handler's SetMode,
// under the handler's lock, as a phase does) and off again.
//
// Everything here runs on the game thread, and only after the layout check
// (RTTI sizes, the handler's class, the streaming system's class) passed;
// otherwise each call answers "unavailable" and the feature is off.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
// At the first frame (access.hpp): the RTTI sizes and the handler's SetMode.
// The handler's and the streaming system's classes are checked at the first
// use, when a quests system exists. Off: every call answers "unavailable".
bool CheckPrefabs();
// The layouts are as measured on 2.50 (checked once, said once in the log).
bool QuestPrefabsAvailable();
// The prefabs our quests have on now (ForceLoad), sorted. False: unavailable.
bool QuestPrefabsOn(std::vector<std::uint64_t> &out);
// One prefab in our own game: its mode is ForceLoad now; one of our own
// quest phases asked for it (the handler's count). None: unavailable.
struct QuestPrefabHere {
  bool on{};
  bool held{};
};
std::optional<QuestPrefabHere> QuestPrefabState(std::uint64_t prefab);
// Turn one on (ForceLoad) or off (Disable), the engine's way. False:
// unavailable (nothing done).
bool SetQuestPrefab(std::uint64_t prefab, bool on);
// One line for the log and `cmd story prefabs`: available or why not, and
// the handler's counts.
std::string QuestPrefabsReport();
} // namespace tandem::plugin::engine
