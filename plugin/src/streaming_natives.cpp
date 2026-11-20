// What the scripts' per-entity wraps call: `TandemStreaming` static
// functions and their struct (docs/architecture/devices.md, Streaming).
// Declared on the script side in scripts/tandem/StreamingNatives.reds: keep
// the two in sync (tools/check-natives.py reads every pair).
//
// QueueStreamed runs wherever the engine calls the wrap (Device and
// VehicleObject attach/detach, a device state's Notify: job threads while a
// save loads): it only appends plain values under a lock, with our own CRT's
// allocator. TakeStreamed runs on the game thread's tick.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"

#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace tandem::plugin {
// One entity that came, went or was touched. Plain fields only (the script
// VM aligns struct locals to 8).
struct TandemStreamed {
  Red::EntityID entity;
  Red::CName cls;
  bool attached{};
};
static_assert(alignof(TandemStreamed) <= 8, "the script VM aligns struct locals to 8");

namespace {
// Kinds (scripts/tandem/StreamingNatives.reds TandemStreaming.Kind*).
constexpr std::size_t kKinds = 5;
// A kind nobody drains (no tick yet: the main menu's world) stops growing
// here; what is past it is dropped, and counted.
constexpr std::size_t kMaxQueued = 65536;

struct Queue {
  std::vector<TandemStreamed> events;
  std::uint64_t dropped{};
};

std::mutex g_lock;
std::array<Queue, kKinds> g_queues;
} // namespace

struct TandemStreaming : Red::IScriptable {
  static void QueueStreamed(std::int32_t kind, Red::EntityID entity, Red::CName cls,
                            bool attached) {
    if (kind < 0 || static_cast<std::size_t>(kind) >= kKinds)
      return;
    std::lock_guard lock(g_lock);
    auto &queue = g_queues[static_cast<std::size_t>(kind)];
    if (queue.events.size() >= kMaxQueued) {
      ++queue.dropped;
      return;
    }
    queue.events.push_back(TandemStreamed{.entity = entity, .cls = cls, .attached = attached});
  }

  // Everything queued of `kind` since the last call, in order.
  static Red::DynArray<TandemStreamed> TakeStreamed(std::int32_t kind) {
    Red::DynArray<TandemStreamed> out;
    if (kind < 0 || static_cast<std::size_t>(kind) >= kKinds)
      return out;
    std::vector<TandemStreamed> taken;
    std::uint64_t dropped = 0;
    {
      std::lock_guard lock(g_lock);
      auto &queue = g_queues[static_cast<std::size_t>(kind)];
      taken.swap(queue.events);
      dropped = queue.dropped;
      queue.dropped = 0;
    }
    if (dropped > 0)
      log::Warn("streaming: kind {} overflowed, {} event(s) dropped", kind, dropped);
    out.Reserve(static_cast<std::uint32_t>(taken.size()));
    for (const auto &event : taken)
      out.EmplaceBack(event);
    return out;
  }

  RTTI_IMPL_TYPEINFO(TandemStreaming);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreamed, "TandemStreamed", {
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(cls);
  RTTI_PROPERTY(attached);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreaming, "TandemStreaming", {
  RTTI_METHOD(QueueStreamed);
  RTTI_METHOD(TakeStreamed);
});
