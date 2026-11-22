// A guard around an engine crash seen under Proton only
// (docs/research/engine/re.md §13.98): the transform update job locks each
// entity's weak handle and, when the entity has expired, still calls the
// per-entity update with null + 0x2E0. That call's first write faults. The
// guard skips exactly that call; any other goes to the original.
//
// Raw code, so it is behind a byte check of the caller and the callee as
// measured on 2.50: any difference (another patch, another mod's hook on the
// same function) leaves it off, with the reason in the log.
//
// A second one, on every platform (§13.142): an entity being detached tells
// its components that its mask changed, the dismemberment component among
// them after its own detach, and that component reads through the pointer
// its detach zeroed. The guard skips the notification of a detached
// dismemberment component: there is no entry left for it to mark.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "guards.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>

namespace tandem::plugin::engine {
namespace {
// The per-entity transform update (0x1406107B0 on 2.50) and its only caller,
// the job that walks a list of weak entity handles (0x140D408A4).
constexpr std::uint32_t kTransformUpdate = 2451640480;
constexpr std::uint32_t kTransformJob = 469381152;
// What the job passes for an entity whose weak handle no longer locks.
constexpr std::uintptr_t kExpiredEntity = 0x2E0;
// Hits logged as warnings, one by one; after that, one line every kEvery.
constexpr std::uint64_t kLoggedHits = 5;
constexpr std::uint64_t kEvery = 1000;

// The callee's start on 2.50: its prologue, then the write that faults
// (`mov byte [rcx+0x39], 1` at +0x1A), the first use of its argument.
constexpr std::array<std::uint8_t, 0x1E> kUpdateStart{
    0x48, 0x8B, 0xC4,                         // mov rax, rsp
    0x48, 0x89, 0x58, 0x10,                   // mov [rax+0x10], rbx
    0x48, 0x89, 0x70, 0x18,                   // mov [rax+0x18], rsi
    0x55, 0x57, 0x41, 0x56,                   // push rbp; push rdi; push r14
    0x48, 0x8D, 0x68, 0x98,                   // lea rbp, [rax-0x68]
    0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, // sub rsp, 0x150
    0xC6, 0x41, 0x39, 0x01,                   // mov byte [rcx+0x39], 1
};
// In the caller: the locked handle starts null (+0x76), and the call passes
// its pointer + 0x2E0 whether the lock succeeded or not (+0xA9, then the
// call's rel32 at +0xB6, which must land on the callee).
constexpr std::size_t kZeroAt = 0x76;
// movdqu [rsp+0x20], xmm0 (xmm0 zeroed just before)
constexpr std::array<std::uint8_t, 6> kZero{0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20};
constexpr std::size_t kCallAt = 0xA9;
constexpr std::array<std::uint8_t, 13> kCall{
    0x48, 0x8B, 0x4C, 0x24, 0x20,             // mov rcx, [rsp+0x20]
    0x48, 0x81, 0xC1, 0xE0, 0x02, 0x00, 0x00, // add rcx, 0x2E0
    0xE8,                                     // call rel32
};

using Update = std::uintptr_t (*)(void *, void *, void *, void *);
Update g_original = nullptr;
void *g_target = nullptr;
std::atomic<std::uint64_t> g_skipped{0};

// On a job thread: compares one argument, logs, nothing else.
std::uintptr_t Detour(void *self, void *a2, void *a3, void *a4) {
  if (reinterpret_cast<std::uintptr_t>(self) == kExpiredEntity) {
    const auto n = g_skipped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kLoggedHits || n % kEvery == 0)
      log::Warn("transform guard: skipped the update of an expired entity ({} so far)", n);
    return 0;
  }
  return g_original(self, a2, a3, a4);
}

// gameDismembermentComponent's slot +0x198 (0x14025302C on 2.50), "the
// entity's mask changed": called by the entity for every component
// (0x1403D1BF8), its return value unused. The component is 0xB0 bytes; +0xA8
// is the dismemberment system's data, set when the component is initialized
// (slot +0x170) and zeroed by its detach (slot +0x190, which gives the
// component's entry back first) and by slot +0x178.
constexpr std::uint32_t kDismembermentNotify = 2784304484;
constexpr const char *kDismembermentClass = "gameDismembermentComponent";
constexpr std::uint32_t kDismembermentSize = 0xB0;
constexpr std::size_t kDismembermentData = 0xA8;
// The function's start on 2.50, up to the read that faults: nothing in it
// depends on where the game is loaded.
constexpr std::array<std::uint8_t, 0x20> kNotifyStart{
    0x48, 0x89, 0x5C, 0x24, 0x10,             // mov [rsp+0x10], rbx
    0x57,                                     // push rdi
    0x48, 0x83, 0xEC, 0x20,                   // sub rsp, 0x20
    0x48, 0x8B, 0xB9, 0xA8, 0x00, 0x00, 0x00, // mov rdi, [rcx+0xA8]
    0x8B, 0x99, 0x90, 0x00, 0x00, 0x00,       // mov ebx, [rcx+0x90]
    0x48, 0x8D, 0x4C, 0x24, 0x30,             // lea rcx, [rsp+0x30]
    0x48, 0x8B, 0x57, 0x68,                   // mov rdx, [rdi+0x68]: the fault
};
// Skips are said at most once per kReportFrames frames (game thread).
constexpr std::uint32_t kReportFrames = 600;

using Notify = void (*)(void *, std::uint16_t);
Notify g_notifyOriginal = nullptr;
void *g_notifyTarget = nullptr;
// Set by the first frame's check (the class's size): until then the detour
// only calls the engine.
std::atomic_bool g_notifyArmed{false};
std::atomic<std::uint64_t> g_notifySkipped{0};

// On engine job threads, for every dismemberment component of an entity
// whose mask changes: one read, one count. No lock, no allocation, no log.
void NotifyDetour(void *self, std::uint16_t mask) {
  if (g_notifyArmed.load(std::memory_order_relaxed) &&
      !*reinterpret_cast<void *const *>(reinterpret_cast<const std::uint8_t *>(self) +
                                        kDismembermentData)) {
    g_notifySkipped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_notifyOriginal(self, mask);
}

template <std::size_t N>
bool Matches(const char *guard, const char *what, std::uintptr_t at,
             const std::array<std::uint8_t, N> &expected) {
  if (std::memcmp(reinterpret_cast<const void *>(at), expected.data(), N) == 0)
    return true;
  std::string seen;
  for (std::size_t i = 0; i < N; ++i)
    seen += std::format("{:02x}", reinterpret_cast<const std::uint8_t *>(at)[i]);
  log::Error("{}: {} differs from 2.50 (exe+0x{:X}: {})", guard, what,
             at - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)), seen);
  return false;
}

// Proton's ntdll is Wine's, which exports its version.
const char *WineVersion() {
  using GetVersion = const char *(__cdecl *)();
  auto *ntdll = GetModuleHandleW(L"ntdll.dll");
  auto get =
      ntdll ? reinterpret_cast<GetVersion>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
  if (!get)
    return nullptr;
  const char *version = get();
  return version ? version : "?";
}
} // namespace

std::string InstallCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  const auto f = feature::kTransformGuard;
  const char *wine = WineVersion();
  if (!wine) {
    FeatureNotAsked(f, "not Wine");
    return "off (not Wine)";
  }
  const auto update = FindAddress(f, "the transform update", kTransformUpdate);
  const auto job = FindAddress(f, "the transform job", kTransformJob);
  if (!update || !job)
    return "off (not found)";
  // Every check, so the log names each difference at once.
  constexpr const char *guard = "transform guard";
  bool ok = Matches(guard, "the update's start", update, kUpdateStart);
  ok = Matches(guard, "the job's handle", job + kZeroAt, kZero) && ok;
  ok = Matches(guard, "the job's call", job + kCallAt, kCall) && ok;
  if (ok) {
    std::int32_t rel = 0;
    std::memcpy(&rel, reinterpret_cast<const void *>(job + kCallAt + kCall.size()), sizeof rel);
    const auto next = job + kCallAt + kCall.size() + sizeof rel;
    if (next + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel)) != update) {
      log::Error("transform guard: the job's call goes elsewhere than the update");
      ok = false;
    }
  }
  if (!ok) {
    FeatureOff(f, "the transform update or its job differ from 2.50");
    return "off (code differs from 2.50)";
  }
  g_target = reinterpret_cast<void *>(update);
  if (!sdk->hooking->Attach(handle, g_target, reinterpret_cast<void *>(&Detour),
                            reinterpret_cast<void **>(&g_original))) {
    FeatureOff(f, "could not hook the transform update");
    g_target = nullptr;
    return "off (hook refused)";
  }
  FeatureOn(f);
  log::Info("transform guard: on (Wine {}): an expired entity's transform update is skipped", wine);
  return "on";
}

std::string InstallDismembermentGuard(const RED4ext::v1::Sdk *sdk,
                                      RED4ext::v1::PluginHandle handle) {
  const auto f = feature::kDismembermentGuard;
  const auto notify =
      FindAddress(f, "the dismemberment component's mask notification", kDismembermentNotify);
  if (!notify)
    return "off (not found)";
  // Another game version, another patch or another mod's hook on it: off,
  // rather than a hook on something else.
  if (!Matches("dismemberment guard", "the notification's start", notify, kNotifyStart)) {
    FeatureOff(f, "the dismemberment component's mask notification differs from 2.50");
    return "off (code differs from 2.50)";
  }
  g_notifyTarget = reinterpret_cast<void *>(notify);
  if (!sdk->hooking->Attach(handle, g_notifyTarget, reinterpret_cast<void *>(&NotifyDetour),
                            reinterpret_cast<void **>(&g_notifyOriginal))) {
    FeatureOff(f, "could not hook the dismemberment component's mask notification");
    g_notifyTarget = nullptr;
    return "off (hook refused)";
  }
  return "hooked (armed by the first frame's check)";
}

void CheckDismembermentGuard() {
  // Off already, with its reason (the hash, the bytes, the hook).
  if (!g_notifyTarget)
    return;
  if (!ClassSizeIs(feature::kDismembermentGuard, kDismembermentClass, kDismembermentSize))
    return; // the detour stays a pass-through
  g_notifyArmed.store(true, std::memory_order_relaxed);
  FeatureOn(feature::kDismembermentGuard);
  log::Info("dismemberment guard: on: a detached dismemberment component's mask notification "
            "is skipped");
}

void ReportCrashGuards() {
  static std::uint64_t s_said = 0;
  static std::uint32_t s_frames = kReportFrames;
  if (s_frames < kReportFrames)
    ++s_frames;
  const auto n = g_notifySkipped.load(std::memory_order_relaxed);
  if (n == s_said || s_frames < kReportFrames)
    return;
  s_said = n;
  s_frames = 0;
  log::Warn("dismemberment guard: {} detached notifications skipped", n);
}

void RemoveCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  if (g_notifyTarget) {
    g_notifyArmed.store(false, std::memory_order_relaxed);
    sdk->hooking->Detach(handle, g_notifyTarget);
    g_notifyTarget = nullptr;
    if (const auto n = g_notifySkipped.load())
      log::Info("dismemberment guard: {} detached notifications skipped this run", n);
  }
  if (!g_target)
    return;
  sdk->hooking->Detach(handle, g_target);
  g_target = nullptr;
  if (const auto n = g_skipped.load())
    log::Info("transform guard: {} expired entities skipped this run", n);
}
} // namespace tandem::plugin::engine
