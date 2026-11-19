// Body pose streaming: the two places of the engine's per-entity animation
// job that tandem uses (docs/research/engine-knowledge.md §13.2–13.3b,
// measured on game 2.50; functions by address-library hash).
//
//   EntityJob(animatedObject, params)      hash 2251364700
//     runs every graph instance of one entity (update, sample, commit), then
//     converts the entity's pose to model space. After it returns the pose is
//     complete: the local V's is read there.
//   Commit(graphInstance, params, sampled, lod)   hash 2074943978
//     copies one graph's sampled pose (local space, in its rig's bone order)
//     into the entity's pose. A puppet's root instance gets the remote
//     player's rotations written into `sampled` before the copy, so the
//     puppet's own deformation, shadow and face graphs follow them.
//
// Layouts (2.50; anything else turns the feature off, logged):
//   entAnimatedComponent (RTTI size 0x2B0): +0x178 animated object (root
//     component only), +0x270 graph instance.
//   animated object: +0x08 skeleton, +0x18 pose.
//   skeleton: +0x00 reference transforms (local), +0x08 count,
//     +0x20 bone names (CName), +0x60 signature (changes on rebuild).
//   pose: +0x00 transforms (48 bytes: translation, rotation, scale).
//   graph instance: +0x40 its anim::Rig.
//   sampled pose: +0x00 bone count, +0x30 transforms.
// Both functions run on animation job threads: nothing here touches the
// engine beyond these buffers, and every read of them is guarded.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "../perf.hpp"
#include "../runtime.hpp"
#include "posestream.hpp"

#include "tandem/pose.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/Scripting/Natives/animRig.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
namespace {
constexpr std::uint32_t kEntityJob = 2251364700;
constexpr std::uint32_t kCommit = 2074943978;

constexpr std::size_t kComponentSize = 0x2B0;
constexpr std::size_t kComponentObject = 0x178;
constexpr std::size_t kComponentInstance = 0x270;
constexpr std::size_t kObjectSkeleton = 0x08, kObjectPose = 0x18;
constexpr std::size_t kSkeletonReference = 0x00, kSkeletonCount = 0x08, kSkeletonNames = 0x20,
                      kSkeletonSignature = 0x60;
constexpr std::size_t kInstanceRig = 0x40;
constexpr std::size_t kSampledCount = 0x00, kSampledBones = 0x30;
constexpr std::uint32_t kMaxBones = 1024;
constexpr auto kSendInterval = std::chrono::microseconds(33'333); // 30 Hz

struct Transform {
  float translation[4];
  float rotation[4];
  float scale[4];
};
static_assert(sizeof(Transform) == 48);

using EntityJob = void (*)(void *object, void *params);
using Commit = void (*)(void *instance, void *params, void *sampled, std::uint32_t lod);
EntityJob g_originalJob = nullptr;
Commit g_originalCommit = nullptr;
void *g_jobTarget = nullptr;
void *g_commitTarget = nullptr;
std::atomic_bool g_layoutChecked{false}, g_layoutOk{false};

// ---- bindings (game thread writes, job threads read) ----

std::atomic<std::uintptr_t> g_sourceObject{0};
struct Binding {
  std::atomic<std::uintptr_t> instance{0};
  std::atomic<std::uint32_t> player{0};
};
std::array<Binding, Runtime::kMaxPuppets> g_bindings;

std::uint32_t BoundPlayer(std::uintptr_t instance) {
  for (const auto &binding : g_bindings)
    if (binding.instance.load(std::memory_order_acquire) == instance)
      return binding.player.load(std::memory_order_acquire);
  return 0;
}

// ---- counters ----

std::atomic_uint64_t g_reads{0}, g_readSkips{0}, g_writes{0}, g_writeMisses{0};
// (g_writeMisses: no pose for the player yet, or a reduced level of detail)

template <class... Args>
void Once(std::atomic_bool &flag, std::format_string<Args...> f, Args &&...a) {
  if (!flag.exchange(true))
    log::Warn(f, std::forward<Args>(a)...);
}

// ---- guarded copies (no C++ objects with destructors in __try functions) ----

bool CopyOut(const void *from, void *to, std::size_t size) {
  __try {
    std::memcpy(to, from, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

template <class T> bool ReadAt(std::uintptr_t address, T &value) {
  if (address < 0x10000 || address > 0x00007FFFFFFFFFF0)
    return false;
  return CopyOut(reinterpret_cast<const void *>(address), &value, sizeof(T));
}

// ---- the read side: the local V's skeleton, mapped to the body rig ----

struct SourceMap {
  std::uintptr_t skeleton = 0;
  std::uint64_t signature = 0;
  bool usable = false;
  std::array<std::uint32_t, pose::kBones> index{}; // body bone -> skeleton entry
  float referenceHips = 1.f;
};
SourceMap g_source; // job threads, under g_sourceLock
std::mutex g_sourceLock;
std::chrono::steady_clock::time_point g_lastSend{};
std::atomic_bool g_warnedSource{false};

// The body bone's entry in V's skeleton: its shadow_ copy when there is one
// (the same pose without the first-person camera pull), else the bone itself.
bool MapSource(std::uintptr_t skeleton, SourceMap &map) {
  std::uint32_t count = 0;
  std::uintptr_t names = 0, reference = 0;
  if (!ReadAt(skeleton + kSkeletonCount, count) || !ReadAt(skeleton + kSkeletonNames, names) ||
      !ReadAt(skeleton + kSkeletonReference, reference) || count == 0 || count > kMaxBones)
    return false;
  std::vector<std::uint64_t> hashes(count);
  if (!CopyOut(reinterpret_cast<const void *>(names), hashes.data(), count * 8))
    return false;
  for (std::size_t bone = 0; bone < pose::kBones; ++bone) {
    const std::string name(pose::kBoneNames[bone]);
    const auto plain = RED4ext::CName(name.c_str()).hash;
    const auto shadow = RED4ext::CName(("shadow_" + name).c_str()).hash;
    int found = -1, foundPlain = -1;
    for (std::uint32_t i = 0; i < count; ++i) {
      if (hashes[i] == shadow)
        found = static_cast<int>(i);
      else if (hashes[i] == plain && foundPlain < 0)
        foundPlain = static_cast<int>(i);
    }
    if (found < 0)
      found = foundPlain;
    if (found < 0) {
      Once(g_warnedSource, "pose stream: V's skeleton has no {} ({} entries): not sent", name,
           count);
      return false;
    }
    map.index[bone] = static_cast<std::uint32_t>(found);
  }
  Transform hips{};
  if (!ReadAt(reference + map.index[pose::kHips] * sizeof(Transform), hips) ||
      !(hips.translation[2] > 0.3f && hips.translation[2] < 2.f))
    return false;
  map.referenceHips = hips.translation[2];
  return true;
}

void ReadSource(std::uintptr_t object) {
  const auto now = std::chrono::steady_clock::now();
  std::uintptr_t skeleton = 0, poseObject = 0, transforms = 0;
  std::uint64_t signature = 0;
  if (!ReadAt(object + kObjectSkeleton, skeleton) || !ReadAt(object + kObjectPose, poseObject) ||
      !ReadAt(poseObject, transforms) || !ReadAt(skeleton + kSkeletonSignature, signature))
    return;

  std::array<std::uint32_t, pose::kBones> index{};
  float referenceHips = 1.f;
  {
    std::lock_guard _(g_sourceLock);
    if (now - g_lastSend < kSendInterval)
      return;
    if (g_source.skeleton != skeleton || g_source.signature != signature) {
      g_source = {};
      g_source.skeleton = skeleton;
      g_source.signature = signature;
      g_source.usable = MapSource(skeleton, g_source);
      log::Info("pose stream: V's skeleton {:#x} ({}), {}", skeleton, signature,
                g_source.usable ? "mapped" : "not usable");
    }
    if (!g_source.usable) {
      g_readSkips.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    g_lastSend = now;
    index = g_source.index;
    referenceHips = g_source.referenceHips;
  }

  std::array<proto::Quat, pose::kBones> model{};
  proto::Vec3 hips{};
  for (std::size_t bone = 0; bone < pose::kBones; ++bone) {
    Transform t{};
    if (!ReadAt(transforms + index[bone] * sizeof(Transform), t)) {
      g_readSkips.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    model[bone] = {t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3]};
    if (bone == pose::kHips)
      hips = {t.translation[0], t.translation[1], t.translation[2]};
  }
  Runtime::Get().SubmitLocalPose(pose::Pack(pose::FromModel(model, hips, referenceHips)));
  g_reads.fetch_add(1, std::memory_order_relaxed);
}

void DetourJob(void *object, void *params) {
  g_originalJob(object, params);
  const auto source = g_sourceObject.load(std::memory_order_acquire);
  if (source != 0 && reinterpret_cast<std::uintptr_t>(object) == source) {
    const Perf::Scope timed(Work::PoseRead);
    ReadSource(source);
  }
}

// ---- the write side: a puppet's rig ----

struct TargetMap {
  std::uintptr_t rig = 0;
  bool checked = false, usable = false;
  std::uint32_t count = 0;
  std::array<int, pose::kBones> index{}; // body bone -> rig bone, -1 none
  float referenceHips = 1.f;
  float forearm = 0.f; // LeftHand's distance from LeftForeArm in the reference
};
std::mutex g_targetLock;
std::vector<TargetMap> g_targets;

bool MapTarget(std::uintptr_t rigAddress, TargetMap &map) {
  const auto *rig = reinterpret_cast<const RED4ext::anim::Rig *>(rigAddress);
  const auto &names = rig->boneNames;
  const auto &reference = rig->referencePoseMS;
  if (names.Size() == 0 || names.Size() > kMaxBones || reference.Size() != names.Size())
    return false;
  map.count = names.Size();
  int mapped = 0;
  for (std::size_t bone = 0; bone < pose::kBones; ++bone) {
    const std::string name(pose::kBoneNames[bone]);
    const RED4ext::CName wanted(name.c_str());
    map.index[bone] = -1;
    for (std::uint32_t i = 0; i < names.Size(); ++i)
      if (names[i] == wanted) {
        map.index[bone] = static_cast<int>(i);
        ++mapped;
        break;
      }
  }
  const int hips = map.index[pose::kHips];
  const int forearm = map.index[pose::BoneIndex("LeftForeArm")];
  const int hand = map.index[pose::BoneIndex("LeftHand")];
  if (hips < 0 || forearm < 0 || hand < 0)
    return false;
  map.referenceHips = reference[hips].Translation.Z;
  const auto &a = reference[forearm].Translation, &b = reference[hand].Translation;
  map.forearm =
      std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
  log::Debug("pose stream: puppet rig {:#x}: {} bones, {} of {} body bones, hips {:.3f} m",
             rigAddress, names.Size(), mapped, pose::kBones, map.referenceHips);
  return mapped == static_cast<int>(pose::kBones) && map.referenceHips > 0.3f;
}

// The sampled pose must be the rig's, in local space: its bone count is the
// rig's, and LeftHand sits a forearm's length from its parent (in model space
// it would be a metre from the root).
bool CheckSampled(const TargetMap &map, const Transform *bones, std::uint32_t count) {
  if (count != map.count)
    return false;
  const auto &hand = bones[map.index[pose::BoneIndex("LeftHand")]].translation;
  const float length = std::sqrt(hand[0] * hand[0] + hand[1] * hand[1] + hand[2] * hand[2]);
  const bool local = std::abs(length - map.forearm) < 0.25f * map.forearm;
  log::Info("pose stream: sampled pose of {} bones, LeftHand {:.3f} m from its parent (forearm "
            "{:.3f}): {}",
            count, length, map.forearm, local ? "local space, writes on" : "NOT local space, off");
  return local;
}

std::atomic_bool g_warnedTarget{false};

void WritePuppet(std::uint32_t player, std::uintptr_t instance, void *sampled) {
  pose::Pose remote;
  if (!Runtime::Get().PoseOf(player, remote)) {
    g_writeMisses.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  std::uintptr_t rig = 0, bonesAddress = 0;
  std::uint32_t count = 0;
  const auto base = reinterpret_cast<std::uintptr_t>(sampled);
  if (!ReadAt(instance + kInstanceRig, rig) || !ReadAt(base + kSampledCount, count) ||
      !ReadAt(base + kSampledBones, bonesAddress) || count == 0 || count > kMaxBones ||
      (bonesAddress & 0xF) != 0)
    return;
  auto *bones = reinterpret_cast<Transform *>(bonesAddress);

  TargetMap map;
  {
    std::lock_guard _(g_targetLock);
    TargetMap *found = nullptr;
    for (auto &target : g_targets)
      if (target.rig == rig)
        found = &target;
    if (!found) {
      g_targets.push_back({});
      found = &g_targets.back();
      found->rig = rig;
      found->usable = MapTarget(rig, *found);
    }
    // Checked on a full pose: a far puppet may be sampled with fewer bones.
    if (found->usable && !found->checked && count == found->count) {
      found->checked = true;
      found->usable = CheckSampled(*found, bones, count);
    }
    map = *found;
  }
  if (!map.usable) {
    Once(g_warnedTarget, "pose stream: a puppet's rig does not take poses (see above)");
    return;
  }
  if (!map.checked || count != map.count) {
    g_writeMisses.fetch_add(1, std::memory_order_relaxed); // a lower level of detail
    return;
  }

  for (std::size_t bone = 0; bone < pose::kBones; ++bone) {
    if (!pose::IsSent(bone))
      continue;
    const auto &q = remote.local[bone];
    auto &out = bones[map.index[bone]];
    out.rotation[0] = q.i;
    out.rotation[1] = q.j;
    out.rotation[2] = q.k;
    out.rotation[3] = q.r;
  }
  auto &hips = bones[map.index[pose::kHips]].translation;
  hips[0] = remote.hips.x * map.referenceHips;
  hips[1] = remote.hips.y * map.referenceHips;
  hips[2] = remote.hips.z * map.referenceHips;
  g_writes.fetch_add(1, std::memory_order_relaxed);
}

void DetourCommit(void *instance, void *params, void *sampled, std::uint32_t lod) {
  if (const auto player = BoundPlayer(reinterpret_cast<std::uintptr_t>(instance))) {
    const Perf::Scope timed(Work::PoseWrite);
    WritePuppet(player, reinterpret_cast<std::uintptr_t>(instance), sampled);
  }
  g_originalCommit(instance, params, sampled, lod);
}

// ---- game thread ----

bool LayoutOk() {
  if (!g_layoutChecked.exchange(true)) {
    auto *animated = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
    auto *rig = RED4ext::CRTTISystem::Get()->GetClass("animRig");
    const bool ok = animated && animated->GetSize() == kComponentSize && rig &&
                    rig->GetSize() == sizeof(RED4ext::anim::Rig);
    g_layoutOk = ok;
    if (!ok)
      log::Error("pose stream: entAnimatedComponent or animRig differ from 2.50: poses are off");
  }
  return g_layoutOk && g_jobTarget && g_commitTarget;
}

// The entity's root animated component.
std::uintptr_t RootComponent(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity || !animatedClass)
    return 0;
  for (const auto &component : entity->components)
    if (component && component->name == RED4ext::CName("root") &&
        component->GetType()->IsA(animatedClass))
      return reinterpret_cast<std::uintptr_t>(component.GetPtr());
  return 0;
}
} // namespace

bool InstallPoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_jobTarget = reinterpret_cast<void *>(RED4ext::UniversalRelocBase::Resolve(kEntityJob));
  g_commitTarget = reinterpret_cast<void *>(RED4ext::UniversalRelocBase::Resolve(kCommit));
  if (!g_jobTarget || !g_commitTarget) {
    log::Error("pose stream: the animation job or the pose commit is not in this game version");
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_jobTarget, reinterpret_cast<void *>(&DetourJob),
                            reinterpret_cast<void **>(&g_originalJob))) {
    log::Error("pose stream: could not hook the animation job");
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_commitTarget, reinterpret_cast<void *>(&DetourCommit),
                            reinterpret_cast<void **>(&g_originalCommit))) {
    log::Error("pose stream: could not hook the pose commit");
    sdk->hooking->Detach(handle, g_jobTarget);
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  log::Info("pose stream: hooks on");
  return true;
}

void RemovePoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_sourceObject = 0;
  for (auto &binding : g_bindings)
    binding.instance = 0;
  if (g_commitTarget)
    sdk->hooking->Detach(handle, g_commitTarget);
  if (g_jobTarget)
    sdk->hooking->Detach(handle, g_jobTarget);
  g_jobTarget = g_commitTarget = nullptr;
}

void SetPoseSource(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  if (!entity) {
    g_sourceObject = 0;
    return;
  }
  if (!LayoutOk())
    return;
  const auto component = RootComponent(entity);
  std::uintptr_t object = 0;
  if (component)
    ReadAt(component + kComponentObject, object);
  if (g_sourceObject.exchange(object) != object)
    log::Info("pose stream: reading V (animated object {:#x})", object);
}

void BindPosePuppet(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  std::uintptr_t instance = 0;
  if (entity && LayoutOk()) {
    if (const auto component = RootComponent(entity))
      ReadAt(component + kComponentInstance, instance);
  }
  Binding *slot = nullptr;
  for (auto &binding : g_bindings)
    if (binding.player.load() == player && binding.instance.load() != 0)
      slot = &binding;
  if (!slot && instance != 0)
    for (auto &binding : g_bindings)
      if (binding.instance.load() == 0) {
        slot = &binding;
        break;
      }
  if (!slot) {
    if (instance != 0)
      log::Warn("pose stream: no room to bind player {}", player);
    return;
  }
  if (slot->instance.load() == instance && slot->player.load() == player)
    return;
  slot->player.store(player, std::memory_order_release);
  slot->instance.store(instance, std::memory_order_release);
  if (instance)
    log::Info("pose stream: player {} drawn on graph instance {:#x}", player, instance);
}

void PoseStreamReport() {
  log::Info("pose stream: {} poses read ({} skipped), {} written ({} without a pose yet), "
            "source {:#x}, loopback player {}",
            g_reads.exchange(0), g_readSkips.exchange(0), g_writes.exchange(0),
            g_writeMisses.exchange(0), g_sourceObject.load(), Runtime::Get().PoseLoopback());
}
} // namespace tandem::plugin::engine
