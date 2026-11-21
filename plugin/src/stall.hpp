#pragma once

// The stall report (docs/architecture/testing.md, "The stall report"). The
// net thread's watchdog (runtime.hpp) says when the game thread stops; when
// the net thread stops too, the whole plugin is stuck and nothing says so
// (2026-11-21: a guest's join froze the game, its net thread and its log at
// once, no watchdog line). A thread of its own watches the net thread, which
// never rests 5 s: it shares no lock with the game, the net thread or the
// logger (atomics only), and writes `stall.log` beside tandem.ini with Win32
// calls alone: how long each thread has been silent, the net thread's last
// step, then the net thread's and the game threads' stacks (module+offset:
// tandem.pdb, tools/re/addr.py).

#include <cstdint>
#include <filesystem>

namespace tandem::plugin::stall {
// Where the net thread was last (each a few lines of net_thread.cpp).
enum Step : std::uint8_t {
  kSession = 1, // a session starting (Endpoint::Host / Join)
  kPass,        // the top of a pass: the watchdog
  kUpdate,      // Endpoint::Update and its events (Runtime, Story, Comms… pushes)
  kTake,        // what the game queued: the Take* of Runtime, Story, Comms, ModRecords…
  kReport,      // the menu's status, the minute's and the 10 s reports
  kRetry,       // between sessions (a guest joining again)
};

// Net thread: alive, at `step`.
void NetBeat(Step step);
// Net thread: gone (its session stopped): nothing to watch.
void NetGone();

// At load and at unload (main.cpp).
void Start(const std::filesystem::path &file);
void Stop();
} // namespace tandem::plugin::stall
