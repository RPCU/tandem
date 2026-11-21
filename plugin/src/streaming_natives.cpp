// What the scripts' per-entity wraps call: `TandemStreaming` static
// functions and their struct (docs/architecture/devices.md, Streaming).
// Declared on the script side in scripts/tandem/StreamingNatives.reds: keep
// the two in sync (tools/check-natives.py reads every pair).
//
// QueueStreamed runs wherever the engine calls the wrap (Device and
// VehicleObject attach/detach, a device state's Notify, a breach's outcome:
// job threads while a save loads): it only appends plain values under a lock, with our own CRT's
// allocator. TakeStreamed runs on the game thread's tick.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"

#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RedLib.hpp>

#include <array>
#include <atomic>
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

// Something that happened at a place, to an entity or between two (a piece
// of the world breaking, a device's shot). Plain fields only, as above.
struct TandemStreamedPoint {
  Red::EntityID entity;
  Red::EntityID other;
  Red::CName name;
  float x{}, y{}, z{};
  std::int32_t value{};
};
static_assert(alignof(TandemStreamedPoint) <= 8, "the script VM aligns struct locals to 8");

namespace {
// Kinds (scripts/tandem/StreamingNatives.reds TandemStreaming.Kind*).
constexpr std::size_t kKinds = 8;
// A kind nobody drains (no tick yet: the main menu's world) stops growing
// here; what is past it is dropped, and counted.
constexpr std::size_t kMaxQueued = 65536;

struct Queue {
  std::vector<TandemStreamed> events;
  std::uint64_t dropped{};
};

std::mutex g_lock;
std::array<Queue, kKinds> g_queues;
// The point queues (StreamingNatives.reds TandemPointKind), as the others.
constexpr std::size_t kPointKinds = 4;
constexpr std::size_t kMaxPoints = 4096;
struct PointQueue {
  std::vector<TandemStreamedPoint> events;
  std::uint64_t dropped{};
};
std::array<PointQueue, kPointKinds> g_points;
// The entity whose wrapped callback the game thread is calling itself right
// now (0: none): its wrap lets that call through instead of queueing it.
std::atomic<std::uint64_t> g_passing{0};
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

  // From any thread: append to `kind`'s point queue.
  static void QueuePoint(std::int32_t kind, Red::EntityID entity, Red::EntityID other,
                         Red::CName name, Red::Vector4 position, std::int32_t value) {
    if (kind < 0 || static_cast<std::size_t>(kind) >= kPointKinds)
      return;
    std::lock_guard lock(g_lock);
    auto &queue = g_points[static_cast<std::size_t>(kind)];
    if (queue.events.size() >= kMaxPoints) {
      ++queue.dropped;
      return;
    }
    queue.events.push_back(TandemStreamedPoint{.entity = entity,
                                               .other = other,
                                               .name = name,
                                               .x = position.X,
                                               .y = position.Y,
                                               .z = position.Z,
                                               .value = value});
  }

  // Game thread: everything queued of `kind` since the last call, in order.
  static Red::DynArray<TandemStreamedPoint> TakePoints(std::int32_t kind) {
    Red::DynArray<TandemStreamedPoint> out;
    if (kind < 0 || static_cast<std::size_t>(kind) >= kPointKinds)
      return out;
    std::vector<TandemStreamedPoint> taken;
    std::uint64_t dropped = 0;
    {
      std::lock_guard lock(g_lock);
      auto &queue = g_points[static_cast<std::size_t>(kind)];
      taken.swap(queue.events);
      dropped = queue.dropped;
      queue.dropped = 0;
    }
    if (dropped > 0)
      log::Warn("streaming: point kind {} overflowed, {} event(s) dropped", kind, dropped);
    out.Reserve(static_cast<std::uint32_t>(taken.size()));
    for (const auto &event : taken)
      out.EmplaceBack(event);
    return out;
  }

  // Game thread: we call `entity`'s wrapped callback ourselves until
  // SetPassing(none). From any thread: is that call the one running?
  static void SetPassing(Red::EntityID entity) { g_passing.store(entity.hash); }
  static bool IsPassing(Red::EntityID entity) {
    return entity.hash != 0 && g_passing.load() == entity.hash;
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreamedPoint, "TandemStreamedPoint", {
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(other);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(value);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStreaming, "TandemStreaming", {
  RTTI_METHOD(QueueStreamed);
  RTTI_METHOD(TakeStreamed);
  RTTI_METHOD(QueuePoint);
  RTTI_METHOD(TakePoints);
  RTTI_METHOD(SetPassing);
  RTTI_METHOD(IsPassing);
});
