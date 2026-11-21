#pragma once

// The thread probe: does a script hook run beside the script tick
// (docs/architecture/testing.md, "The thread probe")? The tick says when it
// begins and ends (TandemNative.ScriptTickBegin / ScriptTickEnd, around
// TandemSystem.OnTick); the hooks listed in tools/scripts-threads.known call
// TandemNative.HookRan first. A call on another thread while a tick is in
// progress ran beside it (logged at once: `threads: hook NAME ran beside the
// script tick`), one on the tick's own thread inside it (nested, safe), any
// other between ticks. In 2.50 the tick runs on the job workers and changes
// thread at any time, so the thread alone says nothing.
//
// HookRan is called from any thread: a fixed table of atomics, no lock, no
// allocation. The rest runs on the script tick.

#include <cstdint>

namespace tandem::plugin::threads {
// The frame update's thread (main.cpp, every frame of the running game): the
// game thread until the script tick has run.
void NoteFrame();
// The script tick begins on this thread (the game thread from then on), and
// ends on it. The end also writes the summary, every 60 s when a hook ran
// beside a tick since the last one.
void TickBegin();
void TickEnd();
// False on any other thread than the game thread (the last script tick's),
// and while none is known.
bool OnGameThread();
// Any thread. `text` names a hook the first time it is seen (the CName's
// string, which the engine keeps).
void HookRan(std::uint64_t name, const char *(*text)(std::uint64_t));
// `threads: N hooks beside the script tick: A×12, B×3`, then the hooks seen
// inside it only and between ticks.
void Report();
} // namespace tandem::plugin::threads
