#pragma once

// The workspot an actor plays in (a bench, a wall to lean on, a stall), read
// from the game's own workspot system (engine-knowledge §13.134). Codeware's
// WorkspotGameSystem.GetWorkspotInstance never finds one on 2.50: it looks
// the instance up with RED4ext's FNV-1a hash of the key, and the game's map
// hashes an EntityID by its low 32 bits.

#include <cstdint>

namespace tandem::plugin::engine {
struct WorkspotRead {
  enum class State : std::int32_t {
    None = 0,       // not in a workspot (or no workspot system)
    Found = 1,      // in one: path, entry and animations read
    NoResource = 2, // in one whose instance holds no resource (a tree only)
    Refused = 3,    // the instance is not laid out as measured: not read
  };
  State state = State::None;
  std::uint64_t path = 0;  // the workspot resource's depot path hash
  std::uint32_t entry = 0; // its tree entry (WorkEntryId)
  std::uint64_t anim = 0;  // CName hashes: the animation playing, the idle one
  std::uint64_t idle = 0;
};

// At the first frame (access.hpp): the two classes' RTTI sizes and the
// system's instance lookup. Off: WorkspotOf answers None for everyone.
bool CheckWorkspots();

// Any thread: the instance is read under the system's shared lock, as the
// game's IsActorInWorkspot does.
WorkspotRead WorkspotOf(std::uint64_t entity);
} // namespace tandem::plugin::engine
