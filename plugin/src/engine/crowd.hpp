#pragma once
// The crowd's own spawners, switched off where another game runs the street
// (docs/architecture/regions.md §6, "Deep inside"; engine facts:
// docs/research/engine/traffic-police.md §13.158).

#include <string>

namespace tandem::plugin::engine {
// The first frame (CheckAccess): whether the community system's crowd is the
// one measured on 2.50. Off (`crowd spawners`) otherwise, logged once.
void CheckCrowdSpawners();
// Any thread: whether our crowd's spawners should be off (our V deep inside a
// region another game populates). Applied on the game thread's frame.
void WantCrowdSpawnersOff(bool off);
// Game thread, every frame (main.cpp): applies the wanted state, and keeps it
// against the game's own enable (a quest's crowd node) while wanted off.
void RunCrowdSpawners();
// (test: `cmd overlap`) `crowd spawners: off (ours), 2 managers on 0, parked on 0`.
std::string CrowdSpawnersReport();
} // namespace tandem::plugin::engine
