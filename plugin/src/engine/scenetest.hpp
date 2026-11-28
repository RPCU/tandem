#pragma once
// (test) Starting a scene the way the engine starts the ones no quest owns
// (the TV, voicesets, chatter, the cut multiplayer's client): the scene
// system's CreateSceneInstance under an owner of ours, then its prepare and
// start requests. docs/research/engine/story.md §13.180, tests 1-3; run by
// `story scene play …` (scripts/tandem/SceneTest.reds), only with tandem.ini
// scene_test = on. Nothing of a session uses it.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
// At plugin load: the casting hooks (test 3), when tandem.ini asks.
bool InstallSceneTest(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle, bool asked);
void RemoveSceneTest(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
// At the first frame (access.hpp): every hash it calls.
bool CheckSceneTest();

// Game thread. Each answers one log-ready line.
// A new instance of the .scene at `path` under a new owner of ours (`again`:
// the last play's owner, which the system must refuse), from `entry` (empty:
// the params' own), then requests 1 and 2.
std::string ScenePlay(const std::string &path, const std::string &entry, bool again);
// Request 6 (end) for our last play.
std::string SceneStop();
// The resource loaded (asked again until it is): its category, entry points,
// actors with their acquisition plans, player actors.
std::string SceneInfo(const std::string &path);
// Test 3: the actor dispatcher and its bind logged (`scene play: cast …`
// lines); `hand` (0: none) the entity the next actor resolved to an entity
// is given instead.
std::string SceneCast(bool on);
std::string SceneHand(std::uint64_t entity);
// The entities bound to actors since our last play (EntityID hashes).
std::vector<std::uint64_t> SceneCastEntities();
} // namespace tandem::plugin::engine
