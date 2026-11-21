#include "threads.hpp"

#include "log.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace tandem::plugin::threads {
namespace {
// A power of two, several times the hooks there are (86 when this was
// written): a hook that finds no slot is counted, not named.
constexpr std::size_t kSlots = 512;
// A tick in progress for longer than this did not end (a script that stopped
// halfway): calls beside it are counted apart, not judged. The watchdog calls
// a game thread silent after as long (runtime.hpp).
constexpr std::uint64_t kStaleMs = 5000;
// The multiplication sign of "A×12" (UTF-8), kept out of the format strings.
constexpr const char *kTimes = "\xC3\x97";

struct Slot {
  std::atomic<std::uint64_t> name{0}; // 0: free
  std::atomic<const char *> text{nullptr};
  std::atomic<std::uint32_t> beside{0};  // on another thread, a tick in progress
  std::atomic<std::uint32_t> inside{0};  // on the tick's thread, during it (nested)
  std::atomic<std::uint32_t> between{0}; // no tick in progress
};

std::array<Slot, kSlots> g_slots;
// Windows thread ids (0: not known yet, or no tick in progress).
std::atomic<std::uint32_t> g_frame{0};
std::atomic<std::uint32_t> g_tick{0};      // the last tick's thread
std::atomic<std::uint32_t> g_ticking{0};   // the thread of the tick in progress
std::atomic<std::uint64_t> g_tickSince{0}; // GetTickCount64 when it began
std::atomic<std::uint32_t> g_tickMoves{0};
std::atomic<std::uint64_t> g_ticks{0};
std::atomic<std::uint64_t> g_beside{0};   // calls beside a tick, in all
std::atomic<std::uint64_t> g_stale{0};    // calls beside a tick that did not end
std::atomic<std::uint64_t> g_early{0};    // calls before the first tick
std::atomic<std::uint64_t> g_unlisted{0}; // calls that found the table full
std::atomic<std::uint64_t> g_unended{0};  // ticks that began before the last one ended
std::atomic<std::uint64_t> g_overlaps{0}; // ticks that began beside another

std::uint32_t Here() { return static_cast<std::uint32_t>(GetCurrentThreadId()); }

std::uint32_t GameThread() {
  const auto tick = g_tick.load(std::memory_order_relaxed);
  return tick != 0 ? tick : g_frame.load(std::memory_order_relaxed);
}

// The hook's slot, taken on its first call; null when the table is full.
Slot *Find(std::uint64_t name, const char *(*text)(std::uint64_t)) {
  for (std::size_t probe = 0; probe < kSlots; ++probe) {
    auto &slot = g_slots[(static_cast<std::size_t>(name) + probe) & (kSlots - 1)];
    auto held = slot.name.load(std::memory_order_acquire);
    if (held == 0 && slot.name.compare_exchange_strong(held, name, std::memory_order_acq_rel)) {
      slot.text.store(text ? text(name) : nullptr, std::memory_order_release);
      return &slot;
    }
    // (A lost exchange left the winner's name in `held`.)
    if (held == name)
      return &slot;
  }
  return nullptr;
}

std::string NameOf(const Slot &slot) {
  const char *text = slot.text.load(std::memory_order_acquire);
  if (text && *text)
    return text;
  return std::format("{:#x}", slot.name.load(std::memory_order_relaxed));
}

struct Row {
  std::string name;
  std::uint32_t count{};
};

std::string List(std::vector<Row> rows) {
  std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
    return a.count != b.count ? a.count > b.count : a.name < b.name;
  });
  std::string out;
  for (const auto &row : rows)
    out += std::format("{}{}{}{}", out.empty() ? ": " : ", ", row.name, kTimes, row.count);
  return out;
}

// What ran beside the tick so far; with `all`, what ran inside it only and
// between ticks too.
void Summary(bool all) {
  std::vector<Row> beside, inside, between;
  for (const auto &slot : g_slots) {
    if (slot.name.load(std::memory_order_acquire) == 0)
      continue;
    const auto besideCount = slot.beside.load(std::memory_order_relaxed);
    const auto insideCount = slot.inside.load(std::memory_order_relaxed);
    const auto betweenCount = slot.between.load(std::memory_order_relaxed);
    if (besideCount > 0)
      beside.push_back({NameOf(slot), besideCount});
    else if (betweenCount > 0)
      between.push_back({NameOf(slot), betweenCount});
    else if (insideCount > 0)
      inside.push_back({NameOf(slot), insideCount});
  }
  const auto besideHooks = beside.size();
  const auto insideHooks = inside.size();
  const auto betweenHooks = between.size();
  log::Always("threads: {} hooks beside the script tick{}", besideHooks, List(std::move(beside)));
  if (!all)
    return;
  log::Always("threads: {} hooks only inside the script tick{}", insideHooks,
              List(std::move(inside)));
  log::Always("threads: {} hooks between ticks, never beside one{}", betweenHooks,
              List(std::move(between)));
  log::Always("threads: {} script ticks, on thread {} last, it changed thread {} times",
              g_ticks.load(std::memory_order_relaxed), GameThread(),
              g_tickMoves.load(std::memory_order_relaxed));
  const auto early = g_early.load(std::memory_order_relaxed);
  const auto stale = g_stale.load(std::memory_order_relaxed);
  const auto unended = g_unended.load(std::memory_order_relaxed);
  const auto overlaps = g_overlaps.load(std::memory_order_relaxed);
  const auto unlisted = g_unlisted.load(std::memory_order_relaxed);
  if (early > 0)
    log::Always("threads: {} hook calls before the first script tick (between ticks)", early);
  if (stale > 0 || unended > 0)
    log::Always("threads: {} hook calls beside a tick that never ended, not judged ({} ticks "
                "did not end)",
                stale, unended);
  if (overlaps > 0)
    log::Always("threads: {} script ticks began while another ran", overlaps);
  if (unlisted > 0)
    log::Always("threads: {} hook calls not counted, the table is full ({} hooks)", unlisted,
                kSlots);
}
} // namespace

void NoteFrame() { g_frame.store(Here(), std::memory_order_relaxed); }

void TickBegin() {
  const auto here = Here();
  const auto now = GetTickCount64();
  g_ticks.fetch_add(1, std::memory_order_relaxed);
  // The tick before did not end (its thread still holds the mark).
  const auto running = g_ticking.load(std::memory_order_acquire);
  if (running == here) {
    g_unended.fetch_add(1, std::memory_order_relaxed);
  } else if (running != 0 && now - g_tickSince.load(std::memory_order_relaxed) < kStaleMs) {
    if (g_overlaps.fetch_add(1, std::memory_order_relaxed) == 0)
      log::Warn("threads: a script tick began on thread {} while one ran on thread {}", here,
                running);
  } else if (running != 0) {
    g_unended.fetch_add(1, std::memory_order_relaxed);
  }
  // The start first: a hook that sees the thread sees when it began.
  g_tickSince.store(now, std::memory_order_relaxed);
  g_ticking.store(here, std::memory_order_release);

  const auto before = g_tick.exchange(here, std::memory_order_relaxed);
  if (before != here) {
    const auto frame = g_frame.load(std::memory_order_relaxed);
    if (before == 0)
      log::Info("threads: the script tick runs on thread {}, the frame update on {}{}", here, frame,
                frame != 0 && frame != here ? ": not the same" : "");
    else if (g_tickMoves.fetch_add(1, std::memory_order_relaxed) == 0)
      log::Info("threads: the script tick moved from thread {} to {}", before, here);
  }
}

void TickEnd() {
  // Only the thread that began it ends it (another tick may have begun since).
  auto here = Here();
  g_ticking.compare_exchange_strong(here, 0, std::memory_order_release, std::memory_order_relaxed);
  // Only the tick reads and writes these two.
  static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
  static std::uint64_t reported = 0;
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(60))
    return;
  last = now;
  const auto beside = g_beside.load(std::memory_order_relaxed);
  if (beside != reported) {
    reported = beside;
    Summary(false);
  }
}

std::uint32_t FrameThread() { return g_frame.load(std::memory_order_relaxed); }
std::uint32_t TickThread() { return g_tick.load(std::memory_order_relaxed); }

bool OnGameThread() {
  const auto game = GameThread();
  return game != 0 && Here() == game;
}

void HookRan(std::uint64_t name, const char *(*text)(std::uint64_t)) {
  if (name == 0)
    return;
  auto *slot = Find(name, text);
  if (!slot) {
    g_unlisted.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const auto ticking = g_ticking.load(std::memory_order_acquire);
  if (ticking == 0) {
    if (g_tick.load(std::memory_order_relaxed) == 0)
      g_early.fetch_add(1, std::memory_order_relaxed);
    slot->between.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const auto here = Here();
  if (ticking == here) {
    slot->inside.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (GetTickCount64() - g_tickSince.load(std::memory_order_relaxed) >= kStaleMs) {
    g_stale.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_beside.fetch_add(1, std::memory_order_relaxed);
  // Said at once, from this thread: the game may not live to the summary.
  if (slot->beside.fetch_add(1, std::memory_order_relaxed) == 0)
    log::Warn("threads: hook {} ran beside the script tick (thread {}, the tick on {})",
              NameOf(*slot), here, ticking);
}

void Report() { Summary(true); }
} // namespace tandem::plugin::threads
