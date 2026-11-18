#pragma once
// Which scene a game is playing, and whether it is private (romance,
// intimate, one-to-one chats, minor side scenes): docs/research/
// coop-quests-design.md §2.12, engine-knowledge.md §13.17.
//
// The quest hook (quests.cpp) reports every scene node it sees run; the game
// thread reads the scene resource the scene system has loaded for it (its
// category tag and its actors) and classifies it.

#include <cstdint>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
enum class SceneClass : std::int32_t {
  None = 0,      // no scene node seen lately, or its resource is not loaded
  Important = 1, // main and side quest scenes: everyone may watch
  Minor = 2,     // chats, open-world and minor one-to-one scenes
  Romance = 3,   // a one-to-one scene with a romance partner
};

struct ScenePrivacy {
  std::vector<std::string> actors;   // romance partners, by scene actor name
  std::vector<std::string> privates; // scene resource paths always private
  std::vector<std::string> publics;  // and never
};

// Plugin load.
void SetScenePrivacy(const ScenePrivacy &privacy);
// Any thread (the quest hook): a node is about to run.
void ObserveSceneNode(void *node);
// Game thread: the scene whose node ran within the last `maxAgeMs`.
SceneClass ClassifyRecentScene(std::uint32_t maxAgeMs, std::string &why);
std::uint64_t RecentScenePath();
} // namespace tandem::plugin::engine
