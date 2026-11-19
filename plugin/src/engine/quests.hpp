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
// At plugin load (tandem.ini quest_freeze = on, or scene_privacy = on):
// hook ExecuteNode. It passes every node through (reporting scene nodes to
// scenes.cpp) until SetQuestFreeze(true), which only `freeze` allows.
// `allow` are extra open-world phase resources (paths) that keep running.
bool InstallQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle,
                        const std::vector<std::string> &allow, const std::vector<std::string> &gigs,
                        bool freeze);
void RemoveQuestFreeze(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);

// Game thread. The hook is in and the engine layouts are the measured ones.
bool QuestFreezeAvailable();
// Game thread. On: refuse story phases' nodes (true when the freeze is now
// on). Off: give the refused signals back to their phases (ExecutePath),
// in the order they came; answers how many were given back.
bool SetQuestFreeze(bool on);
bool QuestFrozen();
// Game thread. Ownership ("Whose gig is it", quest-carryover.md): grouped
// with the host (the default) our gig phases wait like the story; apart
// they run, and the gig signals held meanwhile are given back. A gig phase
// is one under a resource of tandem.ini quest_freeze_gigs (until spike C2
// settles a better test).
void SetQuestGrouped(bool grouped);
bool QuestGrouped();
// Spike C2: log every phase met (its chain of resources) and every journal
// node in it (the journal entry it names), once each.
void SetQuestTrace(bool on);
// How many signals are held now, and whether more came than could be held
// (then the guest's own story lost some: its leave must reload a save).
std::uint32_t QuestFreezeHeld();
bool QuestFreezeOverflowed();
// One line of counters (refused / allowed per class of phase).
std::string QuestFreezeReport();
} // namespace tandem::plugin::engine
