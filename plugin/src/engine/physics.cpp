// The physics backlog watch (docs/research/engine/time.md §13.100, measured
// on 2.50 with tools/re, read-only).
//
// The physics system (a global, not an RTTI class) has 256 scene slots: a
// generation byte each at +0x38, a buffered-command queue pointer each at
// +0x140, the scene pointers at +0x948; +0x08 is a bitmap of free slots, +0x28
// how many slots were ever used. A queue counts its entries at +0x40 (0x30
// bytes each from +0x88, room for 0xFFFF) and fills a 1 MiB arena from
// +0x300060 (bytes used at +0x80, bounded). The append bumps the count with
// no bound check; the physics update drains it every frame.
//
// Nothing here is called by name: the functions whose code says so are
// compared byte for byte with what was measured, and any difference turns
// the watch off (PhysicsBacklog returns -1).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "physics.hpp"

#include <windows.h>

#include <cstddef>

namespace tandem::plugin::engine {
namespace {
// Address-library hashes (2.50).
constexpr std::uint32_t kPhysicsSystem = 3885377886; // the global holding the system
constexpr std::uint32_t kQueueAppend = 3646232200;   // queue, append one entry
constexpr std::uint32_t kQueueOfSlot = 3069123790;   // system, slot -> queue
constexpr std::uint32_t kSlotValid = 3811643879;     // system, slot id -> valid
constexpr std::uint32_t kSlotCreate = 2330137828;    // system, take a slot, clear its queue

constexpr std::uint32_t kSlots = 256;
constexpr std::uint32_t kFreeBitmap = 0x08, kSlotsUsed = 0x28, kQueues = 0x140;
constexpr std::uint32_t kEntryCount = 0x40;

// One stretch of a function's code as measured on 2.50 (no relative
// operands in it).
struct Code {
  std::uint32_t hash;
  std::uint32_t offset;
  const char *bytes; // hex
  const char *what;
};

constexpr Code kCode[] = {
    // sub rsp,28; mov r9,rcx; mov eax,1; lock xadd [rcx+40],eax; mov eax,eax;
    // lea r8,[rcx+88]; lea rcx,[rsp+30]; lea rdx,[rax+rax*2]; shl rdx,4;
    // add r8,rdx: the count at +0x40, entries of 0x30 bytes from +0x88.
    {kQueueAppend, 0x00,
     "4883ec284c8bc9b801000000f00fc141408bc04c8d8188000000488d4c2430488d1440"
     "48c1e2044c03c2",
     "queue append"},
    // movzx eax,r8w; mov rdx,[r9+rax*8+140]: the queue pointers at +0x140.
    {kQueueOfSlot, 0x0F, "33d284c0740c410fb7c0498b94c14001000048", "slot -> queue"},
    // cmp edx,-1; ...; cmp dx,100h; ...; movzx ecx,byte [rax+rcx+38]: 256
    // slots, generations at +0x38.
    {kSlotValid, 0x00,
     "83faff741db800010000663bd073130fb7c2c1ea10"
     "0fb64c0838663bd17503b001c332c0c3",
     "slot check"},
    // mov rbx,[rbx+rsi*8+140]; xor edx,edx; mov r8d,2FFFD0h; lea rcx,[rbx+88]:
    // a new queue clears 0xFFFF entries of 0x30 bytes.
    {kSlotCreate, 0x139, "488b9cf34001000033d241b8d0ff2f00488d8b88000000", "queue size"},
};

int Nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

bool CodeMatches(const Code &code) {
  const auto base = Resolve(code.hash);
  if (!base) {
    log::Error("physics watch: {} (hash {}) not in this game's address list", code.what, code.hash);
    return false;
  }
  const auto *at = reinterpret_cast<const std::uint8_t *>(base + code.offset);
  std::size_t i = 0;
  for (const char *p = code.bytes; *p;) {
    const int high = Nibble(p[0]), low = p[1] ? Nibble(p[1]) : -1;
    if (high < 0 || low < 0) {
      log::Error("physics watch: bad pattern for {}", code.what);
      return false;
    }
    if (at[i] != static_cast<std::uint8_t>(high << 4 | low)) {
      log::Error("physics watch: {} differs from 2.50 at +0x{:X}", code.what, code.offset + i);
      return false;
    }
    ++i;
    p += 2;
  }
  return true;
}

std::uintptr_t g_system = 0; // the global's address (0: watch off)

// The fullest live queue's count, or -1 when something could not be read.
// No C++ objects with destructors in a __try function.
std::int32_t ReadBacklog(std::uintptr_t global) {
  __try {
    const auto system = *reinterpret_cast<const std::uintptr_t *>(global);
    if (!system)
      return 0; // no world yet
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(system);
    auto used = *reinterpret_cast<const std::uint32_t *>(bytes + kSlotsUsed);
    if (used > kSlots)
      used = kSlots;
    std::uint32_t most = 0;
    for (std::uint32_t slot = 0; slot < used; ++slot) {
      const auto free =
          *reinterpret_cast<const std::uint64_t *>(bytes + kFreeBitmap + (slot >> 6) * 8);
      if (free >> (slot & 63) & 1)
        continue;
      const auto *queue =
          *reinterpret_cast<const std::uint8_t *const *>(bytes + kQueues + slot * 8);
      if (!queue)
        continue;
      const auto count = *reinterpret_cast<const volatile std::uint32_t *>(queue + kEntryCount);
      if (count > most)
        most = count;
    }
    return most > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<std::int32_t>(most);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return -1;
  }
}
} // namespace

std::int32_t PhysicsBacklog() {
  static const bool s_on = [] {
    bool ok = true;
    for (const auto &code : kCode)
      ok = CodeMatches(code) && ok; // every mismatch named at once
    g_system = ok ? Resolve(kPhysicsSystem) : 0;
    if (ok && !g_system) {
      log::Error("physics watch: the physics system's global (hash {}) not found", kPhysicsSystem);
      ok = false;
    }
    if (ok)
      log::Info("physics watch: on (2.50 layout: queues at +0x140, {} entries each)",
                kPhysicsQueueCapacity);
    else
      log::Warn("physics watch: off (layout check failed); a long pause is not watched");
    if (ok)
      FeatureOn(feature::kPhysics);
    else
      FeatureOff(feature::kPhysics, "the physics queues' code differs from 2.50");
    return ok;
  }();
  if (!s_on)
    return -1;
  const auto backlog = ReadBacklog(g_system);
  static bool s_unreadable = false;
  if (backlog < 0 && !s_unreadable) {
    s_unreadable = true;
    log::Warn("physics watch: the queues could not be read (logged once)");
  }
  return backlog;
}

bool CheckPhysics() { return PhysicsBacklog() >= 0; }
} // namespace tandem::plugin::engine
