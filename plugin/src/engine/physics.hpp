#pragma once

// The physics system's buffered-command queues, read while the world is
// paused (docs/research/engine/time.md §13.100, measured on 2.50): every
// game-side write to a physics body is queued and applied when the physics
// update drains the queue. Its 65 535 entries are appended without a bound
// check, so a queue that is never drained overwrites the heap past them.

#include <cstdint>

namespace tandem::plugin::engine {
// The queue's capacity in entries (2.50).
constexpr std::int32_t kPhysicsQueueCapacity = 0xFFFF;

// The fullest scene queue's entry count now; -1 when the watch is off (the
// startup layout check failed: logged once, at the first call). Game thread
// only; reads, never writes.
std::int32_t PhysicsBacklog();
// At the first frame (access.hpp): runs that layout check (the bytes of the
// four functions that give the queues' offsets, and the system's global).
bool CheckPhysics();
} // namespace tandem::plugin::engine
