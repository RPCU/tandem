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
//
// A third, on every platform (§13.154): the SlideToSimple action's move
// controller casts the entity its move component refers to into a gamePuppet
// and asks whether that is the local player without checking the cast; an
// entity that is not a puppet (an NPC proxy mounted in a vehicle copy) makes
// it read through null. The guard answers "not the local player" for it, as
// the original does for any other puppet, through the controller's base.

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
// The SlideToSimple move controller's slot 15 (0x142C46940 on 2.50), "the
// move component's reference entity is set": its base (0x1408AAD70) keeps the
// entity's id; then the gamePuppet cast (0x1401BCA1C), unchecked, into "is it
// the local player" (0x142AE4A04), whose read of +0xC0 faults on null.
constexpr std::uint32_t kSlideSet = 2938119384;
constexpr std::uint32_t kSlideBase = 1165500739;
constexpr std::uint32_t kPuppetCast = 2632196524;
// Its start on 2.50, the three calls included (relative: independent of
// where the game is loaded); the first two must land on the two hashes'.
constexpr std::array<std::uint8_t, 0x36> kSlideStart{
    0x48, 0x89, 0x5C, 0x24, 0x08, // mov [rsp+8], rbx
    0x48, 0x89, 0x74, 0x24, 0x10, // mov [rsp+0x10], rsi
    0x57,                         // push rdi
    0x48, 0x83, 0xEC, 0x30,       // sub rsp, 0x30
    0x49, 0x8B, 0xD8,             // mov rbx, r8
    0x48, 0x8B, 0xF1,             // mov rsi, rcx
    0xE8, 0x16, 0x44, 0xC6, 0xFD, // call the base
    0x40, 0x8A, 0xF8,             // mov dil, al
    0x84, 0xC0,                   // test al, al
    0x74, 0x29,                   // je
    0x48, 0x8B, 0xD3,             // mov rdx, rbx
    0x48, 0x8D, 0x4C, 0x24, 0x20, // lea rcx, [rsp+0x20]
    0xE8, 0xAE, 0x60, 0x57, 0xFD, // call the gamePuppet cast
    0x48, 0x8B, 0xC8,             // mov rcx, rax
    0xE8, 0x8E, 0xE0, 0xE9, 0xFF, // call "is the local player"
};
constexpr std::size_t kSlideBaseCall = 0x15;
constexpr std::size_t kSlideCastCall = 0x29;

// A handle as the engine passes it: the instance first.
struct AnyHandle {
  RED4ext::ISerializable *instance;
  void *refCount;
};
using SlideSet = bool (*)(void *, void *, AnyHandle *);
SlideSet g_slideOriginal = nullptr;
SlideSet g_slideBase = nullptr;
void *g_slideTarget = nullptr;
// Set by the first frame's check (gamePuppet's class): until then the
// detour only calls the engine.
std::atomic<const RED4ext::CClass *> g_puppetClass{nullptr};
std::atomic<std::uint64_t> g_slideSkipped{0};

// On engine job threads: two reads (the instance, its class), one count.
bool SlideDetour(void *controller, void *moves, AnyHandle *entity) {
  const auto *puppet = g_puppetClass.load(std::memory_order_relaxed);
  if (puppet && (!entity || !entity->instance || !entity->instance->GetType()->IsA(puppet))) {
    g_slideSkipped.fetch_add(1, std::memory_order_relaxed);
    return g_slideBase(controller, moves, entity);
  }
  return g_slideOriginal(controller, moves, entity);
}

// The dispose gate (engine/vehicles.md §13.157): the entity scene's request
// (0x14074BC5C on 2.50, hash 3800374702) runs a removal at once on the
// caller's thread while the scene's async detach phase is on (its flag at
// scene+0x229F5C), and the same entity may then be detached on two threads
// (five crashes on 2026-11-23, +0x22CA324, our V arriving in another game's
// area while we disposed of our own cars there). A script's Dispose waits
// while the phase is on, or while the entity is not attached (status +0x156
// is not 4) or has a request pending (+0x68 & 0xF).
constexpr std::uint32_t kSceneRequest = 3800374702;
constexpr std::size_t kSceneLockAt = 0x16;
constexpr std::array<std::uint8_t, 7> kSceneLock{0x48, 0x8D, 0x91, 0x58, 0x9F, 0x22, 0x00};
constexpr std::size_t kScenePhaseAt = 0x2A;
constexpr std::array<std::uint8_t, 10> kScenePhase{0x8B, 0x87, 0x5C, 0x9F, 0x22,
                                                   0x00, 0x85, 0xC0, 0x74, 0x10};
constexpr std::size_t kScenePhaseFlag = 0x229F5C;
constexpr std::size_t kEntitySceneRef = 0xB8;
constexpr std::size_t kEntityStatus = 0x156;
constexpr std::size_t kEntityRequest = 0x68;
constexpr std::uint8_t kAttached = 4;
std::atomic_bool g_gateOn{false};
std::atomic<std::uint64_t> g_gateWaited{0};

// An entity the population system spawned (a parked car, a crowd car, its
// driver) has a stub that keeps a raw pointer to it: disposed behind that
// system's back, the stub's next pooling update read the freed entity
// (2026-11-23, +0x61DDD7, the fifth arrival: §13.157). It goes through that
// system instead: the stub system's FindStub (slot +0x1D0, 0x140219D60 on
// 2.50) and the population system's RemoveEntity (slot +0x1F8, hash
// 46405381; the vendored SDK header is a slot short, §13.101), which
// despawns it and lets its stub go.
constexpr std::size_t kFindStubSlot = 0x1D0;
constexpr std::uintptr_t kFindStubOffset = 0x219D60;
constexpr std::size_t kRemoveEntitySlot = 0x1F8;
constexpr std::uint32_t kRemoveEntity = 46405381;
constexpr std::size_t kEntityId = 0x48;
// RemoveEntity's start on 2.50: its prologue, then the stub system it asks.
constexpr std::array<std::uint8_t, 0x29> kRemoveStart{
    0x4C, 0x89, 0x44, 0x24, 0x18,             // mov [rsp+0x18], r8
    0x89, 0x54, 0x24, 0x10,                   // mov [rsp+0x10], edx
    0x55, 0x53, 0x56, 0x57,                   // push rbp, rbx, rsi, rdi
    0x41, 0x54, 0x41, 0x56, 0x41, 0x57,       // push r12, r14, r15
    0x48, 0x8B, 0xEC,                         // mov rbp, rsp
    0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, // sub rsp, 0x80
    0x48, 0x8B, 0xF9,                         // mov rdi, rcx
    0x8B, 0xF2,                               // mov esi, edx
    0x48, 0x8B, 0x89, 0xC0, 0x1F, 0x00, 0x00, // mov rcx, [rcx+0x1FC0]
};
// FindStub's start on 2.50.
constexpr std::array<std::uint8_t, 7> kFindStart{0x48, 0x8B, 0xC4, 0x48, 0x83, 0xEC, 0x28};
using FindStubFn = void *(*)(void *, std::uint64_t);
using RemoveEntityFn = void (*)(void *, std::uint32_t, std::uint64_t);
// 0 not checked yet, 1 on, 2 off (its reason logged once).
std::atomic<int> g_stubPath{0};
std::uintptr_t g_removeEntity = 0;
std::atomic<std::uint64_t> g_stubRemoved{0};

void *GameSystem(const char *name) {
  auto *engine = RED4ext::CGameEngine::Get();
  auto *type = RED4ext::CRTTISystem::Get()->GetClass(name);
  if (!engine || !engine->framework || !engine->framework->gameInstance || !type)
    return nullptr;
  return engine->framework->gameInstance->GetSystem(type);
}

std::uintptr_t Slot(void *object, std::size_t slot) {
  return (*reinterpret_cast<const std::uintptr_t *const *>(object))[slot / sizeof(void *)];
}

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

std::string InstallSlideGuard(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  const auto f = feature::kSlideGuard;
  const auto set = FindAddress(f, "the slide controller's reference", kSlideSet);
  const auto base = FindAddress(f, "the slide controller's base", kSlideBase);
  const auto cast = FindAddress(f, "the gamePuppet cast", kPuppetCast);
  if (!set || !base || !cast)
    return "off (not found)";
  bool ok = Matches("slide guard", "the slide controller's reference", set, kSlideStart);
  const auto target = [&](std::size_t at) {
    std::int32_t rel = 0;
    std::memcpy(&rel, reinterpret_cast<const void *>(set + at + 1), sizeof rel);
    return set + at + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
  };
  if (ok && (target(kSlideBaseCall) != base || target(kSlideCastCall) != cast)) {
    log::Error("slide guard: the slide controller's calls go elsewhere than 2.50's");
    ok = false;
  }
  if (!ok) {
    FeatureOff(f, "the slide controller's reference differs from 2.50");
    return "off (code differs from 2.50)";
  }
  g_slideBase = reinterpret_cast<SlideSet>(base);
  g_slideTarget = reinterpret_cast<void *>(set);
  if (!sdk->hooking->Attach(handle, g_slideTarget, reinterpret_cast<void *>(&SlideDetour),
                            reinterpret_cast<void **>(&g_slideOriginal))) {
    FeatureOff(f, "could not hook the slide controller's reference");
    g_slideTarget = nullptr;
    return "off (hook refused)";
  }
  return "hooked (armed by the first frame's check)";
}

void CheckSlideGuard() {
  if (!g_slideTarget)
    return;
  const auto *puppet = RED4ext::CRTTISystem::Get()->GetClass("gamePuppet");
  if (!puppet) {
    FeatureOff(feature::kSlideGuard, "no gamePuppet class");
    return; // the detour stays a pass-through
  }
  g_puppetClass.store(puppet, std::memory_order_relaxed);
  FeatureOn(feature::kSlideGuard);
  log::Info("slide guard: on: a slide whose reference entity is not a puppet is not asked "
            "whether it is the local player");
}

void CheckDisposeGate() {
  const auto f = feature::kDisposeGate;
  const auto request = FindAddress(f, "the entity scene's request", kSceneRequest);
  if (!request)
    return;
  bool ok = Matches("dispose gate", "the scene request's lock", request + kSceneLockAt, kSceneLock);
  ok = Matches("dispose gate", "the scene request's phase test", request + kScenePhaseAt,
               kScenePhase) &&
       ok;
  if (!ok) {
    FeatureOff(f, "the entity scene's request differs from 2.50");
    return;
  }
  g_gateOn.store(true, std::memory_order_relaxed);
  FeatureOn(f);
  log::Info("dispose gate: on: no entity is disposed of while the scene detaches");
}

// The game thread: whether the entity has a stub, and if so its removal by
// the population system. 0: no stub (the caller disposes of it); 1: removed;
// 2: it has one and cannot be removed now (the path off, or its id wider
// than the population system's): left alone.
int RemoveIfStubbed(void *entity) {
  if (!entity)
    return 2;
  auto *stubs = GameSystem("gameIEntityStubSystem");
  auto *population = GameSystem("gameIPopulationSystem");
  if (!stubs || !population)
    return 2;
  if (g_stubPath.load(std::memory_order_relaxed) == 0) {
    const auto f = feature::kDisposeGate;
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    // The library's address is a jump to the function (`jmp rel32`).
    const auto entry = FindAddress(f, "the population system's RemoveEntity", kRemoveEntity);
    g_removeEntity = entry;
    if (entry && *reinterpret_cast<const std::uint8_t *>(entry) == 0xE9) {
      std::int32_t rel = 0;
      std::memcpy(&rel, reinterpret_cast<const void *>(entry + 1), sizeof rel);
      g_removeEntity = entry + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
    }
    bool ok = g_removeEntity != 0;
    ok = ok && Matches("dispose gate", "RemoveEntity's start", g_removeEntity, kRemoveStart);
    ok = ok && Matches("dispose gate", "FindStub's start", base + kFindStubOffset, kFindStart);
    const auto slot = ok ? Slot(population, kRemoveEntitySlot) : 0;
    if (ok && (Slot(stubs, kFindStubSlot) != base + kFindStubOffset ||
               (slot != g_removeEntity && slot != entry))) {
      log::Error("dispose gate: the stub or population system's slots differ from 2.50");
      ok = false;
    }
    if (!ok)
      FeatureOff(f, "the stub and population systems differ from 2.50: their entities are left");
    g_stubPath.store(ok ? 1 : 2, std::memory_order_relaxed);
  }
  const auto id = *reinterpret_cast<const std::uint64_t *>(
      static_cast<const std::uint8_t *>(entity) + kEntityId);
  const auto find = reinterpret_cast<FindStubFn>(Slot(stubs, kFindStubSlot));
  if (g_stubPath.load(std::memory_order_relaxed) != 1)
    return 2; // nothing we can tell: never disposed behind its back
  if (!find(stubs, id))
    return 0;
  // RemoveEntity looks its stub up by 32 bits and reads it unchecked.
  if (id > 0xFFFFFFFFull || !find(stubs, id & 0xFFFFFFFFull))
    return 2;
  reinterpret_cast<RemoveEntityFn>(g_removeEntity)(population, static_cast<std::uint32_t>(id), 0);
  g_stubRemoved.fetch_add(1, std::memory_order_relaxed);
  return 1;
}

bool DisposeAllowed(void *entity) {
  if (!entity)
    return false;
  if (!g_gateOn.load(std::memory_order_relaxed))
    return true;
  const auto *bytes = static_cast<const std::uint8_t *>(entity);
  const auto *ref = *reinterpret_cast<const std::uint8_t *const *>(bytes + kEntitySceneRef);
  const auto *scene = ref ? *reinterpret_cast<const std::uint8_t *const *>(ref) : nullptr;
  const bool busy = !scene ||
                    *reinterpret_cast<const std::uint32_t *>(scene + kScenePhaseFlag) != 0 ||
                    bytes[kEntityStatus] != kAttached ||
                    (*reinterpret_cast<const std::uint32_t *>(bytes + kEntityRequest) & 0xF) != 0;
  if (busy)
    g_gateWaited.fetch_add(1, std::memory_order_relaxed);
  return !busy;
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
  static std::uint64_t s_slideSaid = 0;
  static std::uint64_t s_gateSaid = 0;
  const auto waited = g_gateWaited.load(std::memory_order_relaxed);
  if (waited != s_gateSaid && s_frames >= kReportFrames) {
    s_gateSaid = waited;
    log::Info(
        "dispose gate: {} disposals put off (the scene detaching, or the entity not attached), "
        "{} removed by the population system",
        waited, g_stubRemoved.load(std::memory_order_relaxed));
  }
  const auto n = g_notifySkipped.load(std::memory_order_relaxed);
  const auto slides = g_slideSkipped.load(std::memory_order_relaxed);
  if ((n == s_said && slides == s_slideSaid) || s_frames < kReportFrames)
    return;
  s_frames = 0;
  if (n != s_said)
    log::Warn("dismemberment guard: {} detached notifications skipped", n);
  if (slides != s_slideSaid)
    log::Warn("slide guard: {} slides of a non-puppet reference answered by the base", slides);
  s_said = n;
  s_slideSaid = slides;
}

void RemoveCrashGuards(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  if (g_slideTarget) {
    g_puppetClass.store(nullptr, std::memory_order_relaxed);
    sdk->hooking->Detach(handle, g_slideTarget);
    g_slideTarget = nullptr;
    if (const auto n = g_slideSkipped.load())
      log::Info("slide guard: {} slides answered by the base this run", n);
  }
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
