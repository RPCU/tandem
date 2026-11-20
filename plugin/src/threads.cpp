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
// The multiplication sign of "A×12" (UTF-8), kept out of the format strings.
constexpr const char *kTimes = "\xC3\x97";

struct Slot {
  std::atomic<std::uint64_t> name{0}; // 0: free
  std::atomic<const char *> text{nullptr};
  std::atomic<std::uint32_t> on{0};  // calls on the game thread
  std::atomic<std::uint32_t> off{0}; // calls on any other
};

std::array<Slot, kSlots> g_slots;
// Windows thread ids (0: not known yet).
std::atomic<std::uint32_t> g_frame{0};
std::atomic<std::uint32_t> g_tick{0};
std::atomic<std::uint32_t> g_tickMoves{0};
std::atomic<std::uint64_t> g_off{0};      // calls off the game thread, in all
std::atomic<std::uint64_t> g_early{0};    // calls before a game thread was known
std::atomic<std::uint64_t> g_unlisted{0}; // calls that found the table full

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

// What ran off the game thread so far; with `all`, what ran on it only too.
void Summary(bool all) {
  std::vector<Row> off, on;
  for (const auto &slot : g_slots) {
    if (slot.name.load(std::memory_order_acquire) == 0)
      continue;
    const auto offCount = slot.off.load(std::memory_order_relaxed);
    const auto onCount = slot.on.load(std::memory_order_relaxed);
    if (offCount > 0)
      off.push_back({NameOf(slot), offCount});
    else if (onCount > 0)
      on.push_back({NameOf(slot), onCount});
  }
  const auto offHooks = off.size();
  const auto onHooks = on.size();
  log::Always("threads: {} hooks off-thread{}", offHooks, List(std::move(off)));
  if (!all)
    return;
  log::Always("threads: {} hooks only on the game thread (thread {}){}", onHooks, GameThread(),
              List(std::move(on)));
  const auto moves = g_tickMoves.load(std::memory_order_relaxed);
  const auto early = g_early.load(std::memory_order_relaxed);
  const auto unlisted = g_unlisted.load(std::memory_order_relaxed);
  if (moves > 0)
    log::Always("threads: the script tick changed thread {} times: no thread is the game's", moves);
  if (early > 0)
    log::Always("threads: {} hook calls before a game thread was known, not judged", early);
  if (unlisted > 0)
    log::Always("threads: {} hook calls not counted, the table is full ({} hooks)", unlisted,
                kSlots);
}
} // namespace

void NoteFrame() { g_frame.store(Here(), std::memory_order_relaxed); }

void NoteTick() {
  const auto here = Here();
  const auto before = g_tick.exchange(here, std::memory_order_relaxed);
  if (before != here) {
    const auto frame = g_frame.load(std::memory_order_relaxed);
    if (before == 0)
      log::Info("threads: the script tick runs on thread {}, the frame update on {}{}", here, frame,
                frame != 0 && frame != here ? ": not the same" : "");
    else if (g_tickMoves.fetch_add(1, std::memory_order_relaxed) == 0)
      log::Warn("threads: the script tick moved from thread {} to {}", before, here);
  }
  // Only the tick reads and writes these two.
  static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
  static std::uint64_t reported = 0;
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(60))
    return;
  last = now;
  const auto off = g_off.load(std::memory_order_relaxed);
  if (off != reported) {
    reported = off;
    Summary(false);
  }
}

bool OnGameThread() {
  const auto game = GameThread();
  return game != 0 && Here() == game;
}

void HookRan(std::uint64_t name, const char *(*text)(std::uint64_t)) {
  if (name == 0)
    return;
  const auto game = GameThread();
  if (game == 0) {
    g_early.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  auto *slot = Find(name, text);
  if (!slot) {
    g_unlisted.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const auto here = Here();
  if (here == game) {
    slot->on.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_off.fetch_add(1, std::memory_order_relaxed);
  // Said at once, from this thread: the game may not live to the summary.
  if (slot->off.fetch_add(1, std::memory_order_relaxed) == 0)
    log::Warn("threads: hook {} ran off the game thread (thread {}, the game thread is {})",
              NameOf(*slot), here, game);
}

void Report() { Summary(true); }
} // namespace tandem::plugin::threads
