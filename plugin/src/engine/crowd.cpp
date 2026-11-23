// The crowd's own spawners, off where another game runs the street
// (docs/research/engine/traffic-police.md §13.158). The community system's
// crowd (gameCommunitySystem +0xD0 on 2.50) holds its crowd managers
// (+0x640, their count at +0x650: pedestrians and traffic vehicles), each
// spawning only while its byte +0x248 is set, and the parking-space spawner
// (+0x670), which rolls a parked car for a space streaming in only while its
// byte +0x9C is set. The game's own enable (2843676965) sets all of them; a
// quest's crowd node disables them through a request list the community
// update works through (its active count at +0x6D4).
//
// Density 0 thins the pedestrians and leaves the vehicles; so the game that
// does not run its street clears the same bytes the game's enable sets, and
// sets them again (the game's enable itself) when it runs its street again,
// unless a quest holds the crowd off then. Nothing already spawned is
// touched: our cars there still go out of sight (Traffic.reds Remove). Not
// the request list: its disable deletes every parked car at once, in sight
// too, and is written into the save.
//
// Behind a check of the measured code: the enable's own writes, the request
// functions in the crowd's vtable, the update's read of the active count.
// Any difference leaves the feature off (`crowd spawners`), logged once.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "crowd.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

namespace tandem::plugin::engine {
namespace {
constexpr std::size_t kCrowd = 0xD0;          // gameCommunitySystem: its crowd
constexpr std::size_t kManagers = 0x640;      // the crowd managers, inline
constexpr std::size_t kManagerCount = 0x650;  // u32
constexpr std::size_t kManagerOn = 0x248;     // byte, per manager
constexpr std::size_t kParked = 0x670;        // the parking-space spawner
constexpr std::size_t kParkedOn = 0x9C;       // byte
constexpr std::size_t kDisabledCount = 0x6D4; // u32: quests holding it off
constexpr std::size_t kDisableSlot = 0x18;    // the request functions
constexpr std::size_t kEnableSlot = 0x20;
constexpr std::uint32_t kMaxManagers = 4;

constexpr std::uint32_t kEnableAll = 2843676965;
constexpr std::uint32_t kUpdateRequests = 1426265232;
constexpr std::uint32_t kDisableRequest = 3834778093;
constexpr std::uint32_t kEnableRequest = 2891845843;

// The enable on 2.50 (0x142BD26CC): every manager's +0x248, the parked
// spawner's +0x9C, the distant crowd's +0x15, each set to 1.
constexpr std::array<std::uint8_t, 0x46> kEnableAllCode{
    0x48, 0x8D, 0x81, 0x40, 0x06, 0x00, 0x00, // lea rax, [rcx+0x640]
    0x8B, 0x50, 0x10,                         // mov edx, [rax+0x10]
    0x4C, 0x8D, 0x04, 0xD0,                   // lea r8, [rax+rdx*8]
    0x49, 0x3B, 0xC0, 0x74, 0x19,             // cmp rax, r8; je
    0x48, 0x8B, 0x10,                         // mov rdx, [rax]
    0x80, 0xBA, 0x48, 0x02, 0x00, 0x00, 0x01, // cmp byte [rdx+0x248], 1
    0x74, 0x07,                               // je
    0xC6, 0x82, 0x48, 0x02, 0x00, 0x00, 0x01, // mov byte [rdx+0x248], 1
    0x48, 0x83, 0xC0, 0x08, 0xEB, 0xE2,       // add rax, 8; jmp
    0x48, 0x8B, 0x81, 0x70, 0x06, 0x00, 0x00, // mov rax, [rcx+0x670]
    0xC6, 0x80, 0x9C, 0x00, 0x00, 0x00, 0x01, // mov byte [rax+0x9c], 1
    0x48, 0x8B, 0x81, 0x60, 0x06, 0x00, 0x00, // mov rax, [rcx+0x660]
    0xC6, 0x40, 0x15, 0x01, 0xC3,             // mov byte [rax+0x15], 1; ret
};
// The community update's read of the active count (0x140DC9E61 on 2.50).
constexpr std::size_t kUpdateCountAt = 0x61;
constexpr std::array<std::uint8_t, 7> kUpdateCount{0x83, 0xBF, 0xD4, 0x06, 0x00, 0x00, 0x00};

using EnableAllFn = void (*)(void *);

// 0 not checked, 1 on, 2 off.
std::atomic<int> g_state{0};
EnableAllFn g_enableAll = nullptr;
std::uintptr_t g_disable = 0;
std::uintptr_t g_enable = 0;
std::atomic_bool g_wantOff{false};
// Game thread.
bool g_turnedOff = false;
std::uint64_t g_kept = 0;

std::uintptr_t Follow(std::uintptr_t entry) {
  // A library address may be a jump to the function (`jmp rel32`).
  if (entry && *reinterpret_cast<const std::uint8_t *>(entry) == 0xE9) {
    std::int32_t rel = 0;
    std::memcpy(&rel, reinterpret_cast<const void *>(entry + 1), sizeof rel);
    return entry + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
  }
  return entry;
}

template <std::size_t N>
bool Same(const char *what, std::uintptr_t at, const std::array<std::uint8_t, N> &expected) {
  if (std::memcmp(reinterpret_cast<const void *>(at), expected.data(), N) == 0)
    return true;
  std::string seen;
  for (std::size_t i = 0; i < N; ++i)
    seen += std::format("{:02x}", reinterpret_cast<const std::uint8_t *>(at)[i]);
  log::Error("crowd spawners: {} differs from 2.50 (exe+0x{:X}: {})", what,
             at - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)), seen);
  return false;
}

// The community system's crowd, checked; `why` says which step failed.
std::uint8_t *Crowd(std::string *why = nullptr) {
  const auto fail = [&](std::string reason) -> std::uint8_t * {
    if (why)
      *why = std::move(reason);
    return nullptr;
  };
  auto *engine = RED4ext::CGameEngine::Get();
  if (!engine || !engine->framework || !engine->framework->gameInstance)
    return fail("no game instance");
  void *system = nullptr;
  for (const char *name : {"gameICommunitySystem", "gameCommunitySystem"})
    if (auto *type = RED4ext::CRTTISystem::Get()->GetClass(name); type && !system)
      system = engine->framework->gameInstance->GetSystem(type);
  auto *community = reinterpret_cast<std::uint8_t *>(system);
  if (!community)
    return fail("no community system");
  auto *crowd = *reinterpret_cast<std::uint8_t **>(community + kCrowd);
  if (!crowd)
    return fail("no crowd at +0xD0");
  // The crowd's own class: its vtable carries the request functions.
  const auto vtable = *reinterpret_cast<const std::uintptr_t *>(crowd);
  const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  if (!InGameImage(vtable))
    return fail(std::format("its vtable 0x{:X} is not the game's", vtable));
  const auto *slots = reinterpret_cast<const std::uintptr_t *>(vtable);
  const auto disable = Follow(slots[kDisableSlot / 8]), enable = Follow(slots[kEnableSlot / 8]);
  if (disable != g_disable || enable != g_enable)
    return fail(std::format(
        "vtable exe+0x{:X}: slots exe+0x{:X} exe+0x{:X}, the requests exe+0x{:X} "
        "exe+0x{:X}",
        vtable - base, disable - base, enable - base, g_disable - base, g_enable - base));
  return crowd;
}

std::uint32_t Managers(const std::uint8_t *crowd) {
  const auto n = *reinterpret_cast<const std::uint32_t *>(crowd + kManagerCount);
  return n <= kMaxManagers ? n : 0;
}

std::uint8_t *Manager(std::uint8_t *crowd, std::uint32_t i) {
  return *reinterpret_cast<std::uint8_t **>(crowd + kManagers + i * sizeof(void *));
}
} // namespace

void CheckCrowdSpawners() {
  const auto f = feature::kCrowdSpawners;
  const auto enableAll = Follow(FindAddress(f, "the crowd's enable", kEnableAll));
  const auto update = Follow(FindAddress(f, "the crowd's request update", kUpdateRequests));
  g_disable = Follow(FindAddress(f, "the crowd's disable request", kDisableRequest));
  g_enable = Follow(FindAddress(f, "the crowd's enable request", kEnableRequest));
  if (!enableAll || !update || !g_disable || !g_enable) {
    g_state = 2;
    return;
  }
  bool ok = Same("the crowd's enable", enableAll, kEnableAllCode);
  ok = Same("the crowd's request update", update + kUpdateCountAt, kUpdateCount) && ok;
  if (!ok) {
    FeatureOff(f, "the community system's crowd differs from 2.50");
    g_state = 2;
    return;
  }
  g_enableAll = reinterpret_cast<EnableAllFn>(enableAll);
  g_state = 1;
  FeatureOn(f);
  log::Info("crowd spawners: on: where another game runs the street, our crowd spawns no "
            "pedestrian, traffic car or parked car");
}

void WantCrowdSpawnersOff(bool off) { g_wantOff.store(off, std::memory_order_relaxed); }

void RunCrowdSpawners() {
  if (g_state.load(std::memory_order_relaxed) != 1)
    return;
  const bool want = g_wantOff.load(std::memory_order_relaxed);
  if (!want && !g_turnedOff)
    return;
  std::string why;
  auto *crowd = Crowd(&why);
  if (!crowd) {
    static bool s_said = false;
    if (!s_said) {
      s_said = true;
      log::Warn("crowd spawners: the community system's crowd is not the one measured ({}): left "
                "alone",
                why);
    }
    return;
  }
  if (want) {
    int cleared = 0;
    for (std::uint32_t i = 0; i < Managers(crowd); ++i)
      if (auto *manager = Manager(crowd, i); manager && manager[kManagerOn] != 0) {
        manager[kManagerOn] = 0;
        ++cleared;
      }
    auto *parked = *reinterpret_cast<std::uint8_t **>(crowd + kParked);
    if (parked && parked[kParkedOn] != 0) {
      parked[kParkedOn] = 0;
      ++cleared;
    }
    if (cleared && g_turnedOff)
      ++g_kept; // the game turned them on again (a quest's crowd node)
    else if (cleared)
      log::Info("crowd spawners: off ({} managers, parked cars): another game runs our street",
                Managers(crowd));
    g_turnedOff = true;
    return;
  }
  g_turnedOff = false;
  if (*reinterpret_cast<const std::uint32_t *>(crowd + kDisabledCount) != 0) {
    log::Info("crowd spawners: our street again, the crowd left off: a quest holds it off");
    return;
  }
  g_enableAll(crowd);
  log::Info("crowd spawners: on again (our street), {} times turned off again meanwhile", g_kept);
  g_kept = 0;
}

std::string CrowdSpawnersReport() {
  const auto state = g_state.load(std::memory_order_relaxed);
  if (state != 1)
    return state == 0 ? "crowd spawners: not checked" : "crowd spawners: unavailable";
  std::string why;
  auto *crowd = Crowd(&why);
  if (!crowd)
    return "crowd spawners: no crowd (" + why + ")";
  int on = 0;
  for (std::uint32_t i = 0; i < Managers(crowd); ++i)
    if (const auto *manager = Manager(crowd, i); manager && manager[kManagerOn] != 0)
      ++on;
  const auto *parked = *reinterpret_cast<std::uint8_t *const *>(crowd + kParked);
  return std::format("crowd spawners: {}{}, {} of {} managers on, parked {}, quests holding the "
                     "crowd off {}, turned off again {}",
                     g_wantOff.load() ? "wanted off" : "ours", g_turnedOff ? " (off by us)" : "",
                     on, Managers(crowd), parked && parked[kParkedOn] ? "on" : "off",
                     *reinterpret_cast<const std::uint32_t *>(crowd + kDisabledCount), g_kept);
}
} // namespace tandem::plugin::engine
