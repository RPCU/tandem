#pragma once

// The thread probe: which thread each script hook runs on
// (docs/architecture/testing.md, "The thread probe"). The hooks listed in
// tools/scripts-threads.known call TandemNative.HookRan first; one that
// ran on another thread than the script tick's is logged at once
// (`threads: hook NAME ran off the game thread`) and counted.
//
// HookRan is called from any thread: a fixed table of atomics, no lock, no
// allocation. The rest runs on the game thread.

#include <cstdint>

namespace tandem::plugin::threads {
// The frame update's thread (main.cpp, every frame of the running game): the
// game thread until the script tick has run.
void NoteFrame();
// The script tick's thread (TandemNative.TakeEvents, every tick): the game
// thread from then on. Also writes the summary, every 60 s when a hook ran
// off it since the last one.
void NoteTick();
// False on any other thread than the game thread, and while none is known.
bool OnGameThread();
// Any thread. `text` names a hook the first time it is seen (the CName's
// string, which the engine keeps).
void HookRan(std::uint64_t name, const char *(*text)(std::uint64_t));
// Game thread: `threads: N hooks off-thread: A×12, B×3`, then the hooks seen
// on the game thread only.
void Report();
} // namespace tandem::plugin::threads
