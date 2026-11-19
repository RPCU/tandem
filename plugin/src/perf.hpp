#pragma once

// What tandem costs a frame (docs/architecture/netcode.md, "Frame cost"):
// the script tick section by section (TandemSystem.OnTick marks them with
// TandemNative.PerfMark), the native work on the engine's threads (puppet
// controllers, the pose stream), and the net thread's passes. Every thread
// adds to atomic counters; the net thread reports them once a minute, and
// `tools/game.sh cmd perf` asks for a report now. Free when nobody marks.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tandem::plugin {
// The script tick's sections, in TandemSystem.OnTick's order (System.reds
// passes these numbers: keep the two lists the same).
inline constexpr const char *kTickSections[] = {
    "sample", "world",     "npcs",    "stealth", "hacks",     "takedowns",  "revive", "bodies",
    "events", "puppets",   "drive",   "transit", "elsewhere", "visibility", "radio",  "travel",
    "time",   "attitudes", "leaving", "shots",   "tests",     "command",
};
inline constexpr std::size_t kTickSectionCount = std::size(kTickSections);

// Native work timed where it runs.
enum class Work : std::size_t {
  Controller = 0, // a puppet's movement controller (engine job thread), per call
  PoseRead,       // our V's pose read (animation job)
  PoseWrite,      // a puppet's pose written (animation job)
  NetPass,        // one pass of the net thread's loop
  Count,
};

class Perf {
public:
  static Perf &Get();

  // Game thread: the tick's section `section` begins (0: the tick begins,
  // -1: it ends); the time since the last mark goes to the section before.
  void Mark(int section);
  // Any thread: `us` of `work`.
  void Add(Work work, std::uint64_t us);
  // A summary of what was counted since the last report (and reset).
  std::string Report();

  class Scope {
  public:
    explicit Scope(Work work);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Work m_work;
    std::uint64_t m_startUs;
  };

private:
  struct Slot {
    std::atomic<std::uint64_t> us{}, count{}, maxUs{};
    void Add(std::uint64_t value);
  };
  std::array<Slot, kTickSectionCount> m_sections;
  std::array<Slot, static_cast<std::size_t>(Work::Count)> m_work;
  Slot m_tick; // the whole tick
  int m_section{-1};
  std::uint64_t m_sectionStartUs{}, m_tickStartUs{};
  std::atomic<std::uint64_t> m_reportedUs{};
};
} // namespace tandem::plugin
