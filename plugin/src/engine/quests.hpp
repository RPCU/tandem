#pragma once
// The guest's quest freeze (docs/research/coop-quests-design.md §1.3, spike
// S1; engine-knowledge.md §13.15). A hook on QuestPhaseInstance::ExecuteNode
// refuses the nodes of the guest's own story phases while it is in a session,
// queues the signals it refused, and gives them back to their phases when the
// freeze ends, so the guest's story resumes where it stopped. Open-world
// phases (communities, vendors, NCPD hustles, city scenes, fixers) keep
// running: a guest away from the host runs its own surroundings
// (coop-regions.md §1.4 point 5).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
// At plugin load (tandem.ini quest_freeze = on): hook ExecuteNode. It passes
// every node through until SetQuestFreeze(true). `allow` are extra
// open-world phase resources (paths) that keep running.
bool InstallQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle,
                        const std::vector<std::string> &allow);
void RemoveQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);

// Game thread. The hook is in and the engine layouts are the measured ones.
bool QuestFreezeAvailable();
// Game thread. On: refuse story phases' nodes (true when the freeze is now
// on). Off: give the refused signals back to their phases (ExecutePath),
// in the order they came; answers how many were given back.
bool SetQuestFreeze(bool on);
bool QuestFrozen();
// How many signals are held now, and whether more came than could be held
// (then the guest's own story lost some: its leave must reload a save).
std::uint32_t QuestFreezeHeld();
bool QuestFreezeOverflowed();
// One line of counters (refused / allowed per class of phase).
std::string QuestFreezeReport();
} // namespace tandem::plugin::engine
