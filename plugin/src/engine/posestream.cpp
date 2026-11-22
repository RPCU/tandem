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
//   sampled pose: +0x00 bone count, +0x30 transforms; +0x0C float track
//     count, +0x38 float tracks (read from the code: a face's tracks are
//     used only once the first sampled face passes its check, FaceTracksOk).
// Both functions run on animation job threads: nothing here touches the
// engine beyond these buffers, and every read of them is guarded.
//
// Scene replay (docs/architecture/scene-replay.md) uses the same two places
// for the actors of the host's story scene: on the host an actor NPC's pose
// is read after its entity job, as V's is; on a guest it is written into its
// proxy's root instance at the commit, as a remote player's is on a puppet.
// The humanoid NPC rigs carry the body rig's bones under the same names (the
// man and woman base rigs are the very rigs V's third-person body uses:
// docs/research/data/skeletons-2.50.txt), so the wire pose is the same; an
// NPC whose skeleton lacks one of them is not read, and its proxy keeps its
// own animation. Not yet verified in game on NPCs.
//
// Other rigs (tandem.ini scene_rigs, on by default; not yet run in game): a
// speaking actor's face (the entity's "face_rig" animated component, whose
// graph instance turns the facial tracks into its ~290 joints) and the body
// of an actor that is not on the humanoid rig are read and written at the
// commit of that component's own graph instance, whole rig, local space, by
// bone index: the host's NPC and its proxy are the same record and
// appearance, so the same rig resource (checked: the hash of its bone names
// and their count). What travels is the bones off the rig's reference pose
// (tandem/rigpose.hpp).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../degradations.hpp"
#include "../log.hpp"
#include "../perf.hpp"
#include "../runtime.hpp"
#include "access.hpp"
#include "posestream.hpp"

#include "tandem/pose.hpp"
#include "tandem/rigpose.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/animRig.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
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
constexpr std::size_t kSampledTrackCount = 0x0C, kSampledTracks = 0x38;
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

// Scene actors: NPCs read (host), proxies written (guest).
constexpr std::size_t kMaxActors = 16;
std::array<Binding, kMaxActors> g_actorBindings; // `player` holds the NPC's id

std::uint32_t BoundActor(std::uintptr_t instance) {
  for (const auto &binding : g_actorBindings)
    if (binding.instance.load(std::memory_order_acquire) == instance)
      return binding.player.load(std::memory_order_acquire);
  return 0;
}

std::uint32_t BoundPlayer(std::uintptr_t instance) {
  for (const auto &binding : g_bindings)
    if (binding.instance.load(std::memory_order_acquire) == instance)
      return binding.player.load(std::memory_order_acquire);
  return 0;
}

// ---- counters ----

std::atomic_uint64_t g_reads{0}, g_readSkips{0}, g_writes{0}, g_writeMisses{0};
std::atomic_uint64_t g_actorReads{0}, g_actorSkips{0}, g_actorWrites{0}, g_actorMisses{0};
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

// Whether the pointer a graph instance holds at +0x40 is an animRig: its
// vtable lies in the game's image and its class says so. Job threads, once
// per rig address (the maps below remember the answer); guarded. (Called
// under the maps' locks: GetType only answers the class, it cannot wait.)
RED4ext::CClass *g_rigClass = nullptr; // CheckPoseStream
bool IsRig(std::uintptr_t address) {
  std::uintptr_t vtable = 0;
  if (!g_rigClass || !ReadAt(address, vtable) || !InGameImage(vtable))
    return false;
  __try {
    auto *type = reinterpret_cast<RED4ext::ISerializable *>(address)->GetType();
    return type && type->IsA(g_rigClass);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
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

// ---- the read side, scene actors: an NPC's skeleton, mapped the same way ----

struct ActorSource {
  std::atomic<std::uintptr_t> object{0};
  std::atomic<std::uint32_t> npc{0};
  // Its skeleton is not the humanoid body: read by its rig instead (below).
  std::atomic_bool otherRig{false};
  std::mutex lock; // the rest: job threads
  SourceMap map;
  std::chrono::steady_clock::time_point lastSend{};
};
std::array<ActorSource, kMaxActors> g_actorSources;
std::atomic_int g_actorSourceCount{0}; // 0: the job detour looks at no table

void ReadActor(ActorSource &source, std::uintptr_t object) {
  const auto npc = source.npc.load(std::memory_order_acquire);
  const auto now = std::chrono::steady_clock::now();
  std::uintptr_t skeleton = 0, poseObject = 0, transforms = 0;
  std::uint64_t signature = 0;
  if (npc == 0 || !ReadAt(object + kObjectSkeleton, skeleton) ||
      !ReadAt(object + kObjectPose, poseObject) || !ReadAt(poseObject, transforms) ||
      !ReadAt(skeleton + kSkeletonSignature, signature))
    return;

  std::array<std::uint32_t, pose::kBones> index{};
  float referenceHips = 1.f;
  {
    std::lock_guard _(source.lock);
    if (now - source.lastSend < kSendInterval)
      return;
    if (source.map.skeleton != skeleton || source.map.signature != signature) {
      source.map = {};
      source.map.skeleton = skeleton;
      source.map.signature = signature;
      source.map.usable = MapSource(skeleton, source.map);
      log::Info("pose stream: actor {}'s skeleton {:#x} ({}), {}", npc, skeleton, signature,
                source.map.usable ? "mapped" : "not a body the stream knows: not sent");
      source.otherRig.store(!source.map.usable, std::memory_order_release);
    }
    if (!source.map.usable) {
      g_actorSkips.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    source.lastSend = now;
    index = source.map.index;
    referenceHips = source.map.referenceHips;
  }

  std::array<proto::Quat, pose::kBones> model{};
  proto::Vec3 hips{};
  for (std::size_t bone = 0; bone < pose::kBones; ++bone) {
    Transform t{};
    if (!ReadAt(transforms + index[bone] * sizeof(Transform), t)) {
      g_actorSkips.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    model[bone] = {t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3]};
    if (bone == pose::kHips)
      hips = {t.translation[0], t.translation[1], t.translation[2]};
  }
  Runtime::Get().SubmitActorPose(npc, pose::Pack(pose::FromModel(model, hips, referenceHips)));
  g_actorReads.fetch_add(1, std::memory_order_relaxed);
}

void DetourJob(void *object, void *params) {
  g_originalJob(object, params);
  const auto address = reinterpret_cast<std::uintptr_t>(object);
  const auto source = g_sourceObject.load(std::memory_order_acquire);
  if (source != 0 && address == source) {
    const Perf::Scope timed(Work::PoseRead);
    ReadSource(source);
  } else if (g_actorSourceCount.load(std::memory_order_acquire) > 0)
    for (auto &actor : g_actorSources)
      if (actor.object.load(std::memory_order_acquire) == address) {
        ReadActor(actor, address);
        break;
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
  if (!IsRig(rigAddress))
    return false;
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

// `remote` written into the sampled pose of a body rig's root instance.
// False: not written (the rig does not take poses, or a lower level of detail).
bool WritePose(const pose::Pose &remote, std::uintptr_t instance, void *sampled) {
  std::uintptr_t rig = 0, bonesAddress = 0;
  std::uint32_t count = 0;
  const auto base = reinterpret_cast<std::uintptr_t>(sampled);
  if (!ReadAt(instance + kInstanceRig, rig) || !ReadAt(base + kSampledCount, count) ||
      !ReadAt(base + kSampledBones, bonesAddress) || count == 0 || count > kMaxBones ||
      (bonesAddress & 0xF) != 0)
    return false;
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
    return false;
  }
  if (!map.checked || count != map.count)
    return false; // a lower level of detail

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
  return true;
}

void WritePuppet(std::uint32_t player, std::uintptr_t instance, void *sampled) {
  pose::Pose remote;
  if (Runtime::Get().PoseOf(player, remote) && WritePose(remote, instance, sampled))
    g_writes.fetch_add(1, std::memory_order_relaxed);
  else // no pose for the player yet, or a reduced level of detail
    g_writeMisses.fetch_add(1, std::memory_order_relaxed);
}

// A scene actor's proxy: the host's pose of that NPC while its stream lasts.
bool WriteActor(std::uint32_t npc, std::uintptr_t instance, void *sampled) {
  pose::Pose remote;
  if (Runtime::Get().ActorPoseOf(npc, remote) && WritePose(remote, instance, sampled)) {
    g_actorWrites.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  g_actorMisses.fetch_add(1, std::memory_order_relaxed);
  return false;
}

// ---- other rigs: a face, a body that is not the humanoid one ----

constexpr std::size_t kMaxRigs = 40;
constexpr auto kRigReadInterval = std::chrono::microseconds(40'000); // 25 a second at most

// One graph instance read (host) or written (guest), whole rig.
struct RigSlot {
  std::atomic<std::uintptr_t> instance{0};
  std::atomic<std::uint32_t> npc{0};
  std::atomic<std::uint8_t> part{0};
  std::atomic_bool source{false};
  std::mutex lock;           // the rest: job threads; the game thread when it rebinds
  std::uintptr_t mapped = 0; // the anim::Rig the fields below are of
  bool usable = false;
  std::uint32_t hash = 0;
  std::shared_ptr<const std::vector<rig::Bone>> reference;
  std::vector<std::uint8_t> skip; // Root, Trajectory: the entity is placed by its NPC states
  std::vector<rig::Bone> scratch;
  std::chrono::steady_clock::time_point lastRead{};
  // Its float tracks (a face's): the rig's track names' hash and reference
  // values; none when the rig has none.
  std::uint32_t trackHash = 0;
  std::shared_ptr<const std::vector<float>> trackReference;
  std::vector<float> trackScratch;
};
std::array<RigSlot, kMaxRigs> g_rigSlots;
std::atomic_int g_rigCount{0}; // 0: the commit detour looks at no table
std::atomic_uint64_t g_rigReads{0}, g_rigWrites{0}, g_rigBones{0};

RigSlot *RigSlotOf(std::uintptr_t instance) {
  for (auto &slot : g_rigSlots)
    if (slot.instance.load(std::memory_order_acquire) == instance)
      return &slot;
  return nullptr;
}

// The rig's bones by name and its reference pose, local space. Under the
// slot's lock.
bool MapRig(std::uintptr_t rigAddress, RigSlot &slot) {
  if (!IsRig(rigAddress))
    return false;
  const auto *resource = reinterpret_cast<const RED4ext::anim::Rig *>(rigAddress);
  const auto &names = resource->boneNames;
  const auto count = names.Size();
  if (count == 0 || count > proto::kMaxRigBones || !resource->referencePoseLS)
    return false;
  std::vector<std::uint64_t> hashes(count);
  auto reference = std::make_shared<std::vector<rig::Bone>>(count);
  slot.skip.assign(count, 0);
  for (std::uint32_t i = 0; i < count; ++i) {
    hashes[i] = names[i].hash;
    const auto &t = resource->referencePoseLS[i];
    (*reference)[i] = {{t.Translation.X, t.Translation.Y, t.Translation.Z},
                       {t.Rotation.i, t.Rotation.j, t.Rotation.k, t.Rotation.r}};
    if (names[i] == RED4ext::CName("Root") || names[i] == RED4ext::CName("Trajectory"))
      slot.skip[i] = 1;
  }
  slot.hash = rig::RigHash(hashes);
  slot.reference = std::move(reference);
  slot.trackHash = 0;
  slot.trackReference.reset();
  const auto &trackNames = resource->trackNames;
  const auto &trackValues = resource->referenceTracks;
  if (trackNames.Size() > 0 && trackNames.Size() <= proto::kMaxRigTracks &&
      trackValues.Size() == trackNames.Size()) {
    std::vector<std::uint64_t> trackHashes(trackNames.Size());
    auto values = std::make_shared<std::vector<float>>(trackNames.Size());
    for (std::uint32_t i = 0; i < trackNames.Size(); ++i) {
      trackHashes[i] = trackNames[i].hash;
      (*values)[i] = trackValues[i];
    }
    slot.trackHash = rig::RigHash(trackHashes);
    slot.trackReference = std::move(values);
  }
  return true;
}

// A face's float tracks at the commit: 0 not checked yet, 1 used, 2 off (the
// first sampled face did not match its rig: logged once).
std::atomic_int g_faceTracks{0};
std::atomic_uint64_t g_trackReads{0}, g_trackWrites{0};

// The first full sampled face decides: its track count is its rig's, the
// pointer is one, and every value is a number of a track's size.
bool FaceTracksOk(const RigSlot &slot, std::uintptr_t tracks, std::uint32_t count) {
  const auto expected = slot.trackReference->size();
  std::vector<float> values(expected);
  bool ok = count == expected && tracks >= 0x10000 && (tracks & 0x3) == 0 &&
            CopyOut(reinterpret_cast<const void *>(tracks), values.data(), expected * 4);
  for (std::size_t i = 0; ok && i < expected; ++i)
    ok = std::isfinite(values[i]) && std::abs(values[i]) < 64.f;
  log::Info("pose stream: a sampled face has {} float tracks at {:#x} (its rig {}): {}", count,
            tracks, expected, ok ? "face tracks on" : "NOT the measured layout, face tracks off");
  return ok;
}

// The float tracks of a slot's sampled face, when they are its rig's.
float *FaceTracks(RigSlot &slot, void *sampled) {
  if (slot.part.load(std::memory_order_acquire) != proto::kRigFace || !slot.trackReference ||
      g_faceTracks.load(std::memory_order_acquire) == 2)
    return nullptr;
  std::uint32_t count = 0;
  std::uintptr_t tracks = 0;
  const auto base = reinterpret_cast<std::uintptr_t>(sampled);
  if (!ReadAt(base + kSampledTrackCount, count) || !ReadAt(base + kSampledTracks, tracks))
    return nullptr;
  if (g_faceTracks.load(std::memory_order_acquire) == 0) {
    int unchecked = 0;
    if (g_faceTracks.compare_exchange_strong(unchecked, 3)) { // 3: this thread checks
      g_faceTracks.store(FaceTracksOk(slot, tracks, count) ? 1 : 2, std::memory_order_release);
    }
  }
  if (g_faceTracks.load(std::memory_order_acquire) != 1 || count != slot.trackReference->size() ||
      tracks < 0x10000 || (tracks & 0x3) != 0)
    return nullptr;
  return reinterpret_cast<float *>(tracks);
}

// The sampled pose of a slot's instance: its bones, when it is the whole rig.
Transform *RigBones(RigSlot &slot, std::uintptr_t instance, void *sampled, std::uint32_t &count) {
  std::uintptr_t address = 0, bonesAddress = 0;
  const auto base = reinterpret_cast<std::uintptr_t>(sampled);
  if (!ReadAt(instance + kInstanceRig, address) || !ReadAt(base + kSampledCount, count) ||
      !ReadAt(base + kSampledBones, bonesAddress) || address == 0 || count == 0 ||
      count > proto::kMaxRigBones || bonesAddress < 0x10000 || (bonesAddress & 0xF) != 0)
    return nullptr;
  if (slot.mapped != address) {
    slot.mapped = address;
    slot.usable = MapRig(address, slot);
    log::Info("pose stream: npc {}'s {} rig {:#x}: {} bones, {}", slot.npc.load(),
              slot.part.load() == proto::kRigFace ? "face" : "body", address,
              slot.reference ? slot.reference->size() : 0, slot.usable ? "mapped" : "not usable");
  }
  // A far character is sampled with fewer bones: nothing then.
  if (!slot.usable || !slot.reference || count != slot.reference->size())
    return nullptr;
  return reinterpret_cast<Transform *>(bonesAddress);
}

bool ActorIsOtherRig(std::uint32_t npc);

// Host: the rig's pose as its graph hands it over, for the guests.
void ReadRig(RigSlot &slot, std::uintptr_t instance, void *sampled) {
  const auto npc = slot.npc.load(std::memory_order_acquire);
  const auto part = slot.part.load(std::memory_order_acquire);
  // A body is read this way only when it is not the humanoid one.
  if (npc == 0 || (part == proto::kRigBody && !ActorIsOtherRig(npc)))
    return;
  const std::unique_lock guard(slot.lock, std::try_to_lock);
  if (!guard.owns_lock())
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now - slot.lastRead < kRigReadInterval)
    return;
  std::uint32_t count = 0;
  const auto *bones = RigBones(slot, instance, sampled, count);
  if (!bones)
    return;
  slot.lastRead = now;
  std::vector<Transform> raw(count);
  if (!CopyOut(bones, raw.data(), count * sizeof(Transform)))
    return;
  std::vector<rig::Bone> pose(count);
  for (std::uint32_t i = 0; i < count; ++i)
    pose[i] = slot.skip[i]
                  ? (*slot.reference)[i]
                  : rig::Bone{{raw[i].translation[0], raw[i].translation[1], raw[i].translation[2]},
                              {raw[i].rotation[0], raw[i].rotation[1], raw[i].rotation[2],
                               raw[i].rotation[3]}};
  Runtime::RigSample sample{npc, part, slot.hash, slot.reference, std::move(pose)};
  if (const auto *tracks = FaceTracks(slot, sampled)) {
    std::vector<float> values(slot.trackReference->size());
    if (CopyOut(tracks, values.data(), values.size() * sizeof(float))) {
      sample.trackHash = slot.trackHash;
      sample.trackReference = slot.trackReference;
      sample.tracks = std::move(values);
      g_trackReads.fetch_add(1, std::memory_order_relaxed);
    }
  }
  Runtime::Get().SubmitRigPose(std::move(sample));
  g_rigReads.fetch_add(1, std::memory_order_relaxed);
}

// Guest: what the host said of the rig's bones, over the rig's own pose.
bool WriteRig(RigSlot &slot, std::uintptr_t instance, void *sampled) {
  const auto npc = slot.npc.load(std::memory_order_acquire);
  if (npc == 0)
    return false;
  const std::unique_lock guard(slot.lock, std::try_to_lock);
  if (!guard.owns_lock())
    return false;
  std::uint32_t count = 0;
  auto *bones = RigBones(slot, instance, sampled, count);
  if (!bones)
    return false;
  slot.scratch.resize(count);
  for (std::uint32_t i = 0; i < count; ++i)
    slot.scratch[i] = {
        {bones[i].translation[0], bones[i].translation[1], bones[i].translation[2]},
        {bones[i].rotation[0], bones[i].rotation[1], bones[i].rotation[2], bones[i].rotation[3]}};
  // Its float tracks first: the face may solve its joints from them.
  bool tracksWritten = false;
  if (auto *tracks = FaceTracks(slot, sampled)) {
    slot.trackScratch.resize(slot.trackReference->size());
    if (CopyOut(tracks, slot.trackScratch.data(), slot.trackScratch.size() * sizeof(float)) &&
        Runtime::Get().ApplyRigTracks(npc, slot.part.load(), slot.trackHash, *slot.trackReference,
                                      slot.trackScratch) > 0 &&
        CopyOut(slot.trackScratch.data(), tracks, slot.trackScratch.size() * sizeof(float))) {
      tracksWritten = true;
      g_trackWrites.fetch_add(1, std::memory_order_relaxed);
    }
  }
  const auto written =
      Runtime::Get().ApplyRigPose(npc, slot.part.load(), slot.hash, *slot.reference, slot.scratch);
  if (written == 0)
    return tracksWritten;
  for (std::uint32_t i = 0; i < count; ++i) {
    if (slot.skip[i])
      continue;
    const auto &bone = slot.scratch[i];
    bones[i].translation[0] = bone.translation.x;
    bones[i].translation[1] = bone.translation.y;
    bones[i].translation[2] = bone.translation.z;
    bones[i].rotation[0] = bone.rotation.i;
    bones[i].rotation[1] = bone.rotation.j;
    bones[i].rotation[2] = bone.rotation.k;
    bones[i].rotation[3] = bone.rotation.r;
  }
  g_rigWrites.fetch_add(1, std::memory_order_relaxed);
  g_rigBones.fetch_add(written, std::memory_order_relaxed);
  return true;
}

void CommitRig(std::uintptr_t address, void *sampled) {
  if (g_rigCount.load(std::memory_order_acquire) <= 0)
    return;
  if (auto *slot = RigSlotOf(address)) {
    if (slot->source.load(std::memory_order_acquire))
      ReadRig(*slot, address, sampled);
    else
      WriteRig(*slot, address, sampled);
  }
}

void DetourCommit(void *instance, void *params, void *sampled, std::uint32_t lod) {
  const auto address = reinterpret_cast<std::uintptr_t>(instance);
  if (const auto player = BoundPlayer(address)) {
    const Perf::Scope timed(Work::PoseWrite);
    WritePuppet(player, address, sampled);
  } else if (const auto npc = BoundActor(address)) {
    // No humanoid pose for it: a body on another rig, maybe.
    if (!WriteActor(npc, address, sampled))
      CommitRig(address, sampled);
  } else
    CommitRig(address, sampled);
  g_originalCommit(instance, params, sampled, lod);
}

bool ActorIsOtherRig(std::uint32_t npc) {
  for (const auto &source : g_actorSources)
    if (source.npc.load(std::memory_order_acquire) == npc &&
        source.object.load(std::memory_order_acquire) != 0)
      return source.otherRig.load(std::memory_order_acquire);
  return false;
}

// ---- game thread ----

// What the hooks read of the SDK's anim::Rig (2.50).
static_assert(__builtin_offsetof(RED4ext::anim::Rig, referencePoseLS) == 0x48);
static_assert(__builtin_offsetof(RED4ext::anim::Rig, boneNames) == 0x50);
static_assert(__builtin_offsetof(RED4ext::anim::Rig, referencePoseMS) == 0x60);
static_assert(__builtin_offsetof(RED4ext::anim::Rig, trackNames) == 0xE8);
static_assert(__builtin_offsetof(RED4ext::anim::Rig, referenceTracks) == 0xF8);

bool LayoutOk() {
  if (!g_layoutChecked.exchange(true)) {
    // Every check, so the log names each mismatch at once: class sizes and
    // property offsets from RTTI, measured on 2.50.
    const auto f = feature::kPoseStream;
    bool ok = ClassSizeIs(f, "entAnimatedComponent", kComponentSize);
    ok = ClassSizeIs(f, "animRig", sizeof(RED4ext::anim::Rig)) && ok;
    ok = PropertyAt(f, "entAnimatedComponent", "rig", 0x138) && ok;
    ok = PropertyAt(f, "entAnimatedComponent", "graph", 0x150) && ok;
    ok = PropertyAt(f, "animRig", "boneNames", 0x50) && ok;
    ok = PropertyAt(f, "animRig", "referencePoseMS", 0x60) && ok;
    ok = PropertyAt(f, "animRig", "trackNames", 0xE8) && ok;
    ok = PropertyAt(f, "animRig", "referenceTracks", 0xF8) && ok;
    g_rigClass = RED4ext::CRTTISystem::Get()->GetClass("animRig");
    g_layoutOk = ok;
    if (!ok)
      log::Error("pose stream: entAnimatedComponent or animRig differ from 2.50: poses are off");
  }
  return g_layoutOk && g_jobTarget && g_commitTarget;
}

// The entity's animated component of that name ("root", "face_rig").
std::uintptr_t AnimatedComponent(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
                                 const char *name) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity || !animatedClass)
    return 0;
  for (const auto &component : entity->components)
    if (component && component->name == RED4ext::CName(name) &&
        component->GetType()->IsA(animatedClass))
      return reinterpret_cast<std::uintptr_t>(component.GetPtr());
  return 0;
}

// The entity's root animated component.
std::uintptr_t RootComponent(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  return AnimatedComponent(entity, "root");
}
} // namespace

bool InstallPoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  const auto f = feature::kPoseStream;
  g_jobTarget = Find<void *>(f, "the entity animation job", kEntityJob);
  g_commitTarget = Find<void *>(f, "the pose commit", kCommit);
  if (!g_jobTarget || !g_commitTarget) {
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_jobTarget, reinterpret_cast<void *>(&DetourJob),
                            reinterpret_cast<void **>(&g_originalJob))) {
    FeatureOff(f, "could not hook the animation job");
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  if (!sdk->hooking->Attach(handle, g_commitTarget, reinterpret_cast<void *>(&DetourCommit),
                            reinterpret_cast<void **>(&g_originalCommit))) {
    FeatureOff(f, "could not hook the pose commit");
    sdk->hooking->Detach(handle, g_jobTarget);
    g_jobTarget = g_commitTarget = nullptr;
    return false;
  }
  log::Info("pose stream: hooks on");
  return true;
}

bool CheckPoseStream() {
  // Not installed (tandem.ini pose = off, or a hash missing): said already.
  if (!g_jobTarget || !g_commitTarget)
    return false;
  if (!LayoutOk())
    return false;
  FeatureOn(feature::kPoseStream);
  return true;
}

void RemovePoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_sourceObject = 0;
  for (auto &binding : g_bindings)
    binding.instance = 0;
  ClearPoseActors();
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
    if (instance != 0) {
      log::Warn("pose stream: no room to bind player {}", player);
      Degradations::Get().Fallback("no room", std::format("no pose slot for player {}", player));
    }
    return;
  }
  if (slot->instance.load() == instance && slot->player.load() == player)
    return;
  slot->player.store(player, std::memory_order_release);
  slot->instance.store(instance, std::memory_order_release);
  if (instance)
    log::Info("pose stream: player {} drawn on graph instance {:#x}", player, instance);
}

void SetPoseActorSource(std::uint32_t npc, const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  std::uintptr_t object = 0;
  if (npc != 0 && entity && LayoutOk())
    if (const auto component = RootComponent(entity))
      ReadAt(component + kComponentObject, object);
  ActorSource *slot = nullptr;
  for (auto &source : g_actorSources)
    if (source.npc.load() == npc && source.object.load() != 0)
      slot = &source;
  if (!slot && object != 0)
    for (auto &source : g_actorSources)
      if (source.object.load() == 0) {
        slot = &source;
        break;
      }
  if (!slot) {
    if (object != 0) {
      log::Warn("pose stream: no room to read actor {}", npc);
      Degradations::Get().Fallback("no room", std::format("no pose slot to read actor {}", npc));
    }
    return;
  }
  if (slot->object.load() == object && slot->npc.load() == npc)
    return;
  // Off first: a job thread never reads a slot half changed.
  if (slot->object.exchange(0) != 0)
    g_actorSourceCount.fetch_sub(1);
  slot->npc.store(npc, std::memory_order_release);
  slot->otherRig.store(false, std::memory_order_release);
  if (object != 0) {
    slot->object.store(object, std::memory_order_release);
    g_actorSourceCount.fetch_add(1);
    log::Info("pose stream: reading actor {} (animated object {:#x})", npc, object);
  } else
    log::Info("pose stream: actor {} no longer read", npc);
}

void BindPoseActor(std::uint32_t npc, const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  std::uintptr_t instance = 0;
  if (npc != 0 && entity && LayoutOk())
    if (const auto component = RootComponent(entity))
      ReadAt(component + kComponentInstance, instance);
  Binding *slot = nullptr;
  for (auto &binding : g_actorBindings)
    if (binding.player.load() == npc && binding.instance.load() != 0)
      slot = &binding;
  if (!slot && instance != 0)
    for (auto &binding : g_actorBindings)
      if (binding.instance.load() == 0) {
        slot = &binding;
        break;
      }
  if (!slot) {
    if (instance != 0) {
      log::Warn("pose stream: no room to bind actor {}", npc);
      Degradations::Get().Fallback("no room", std::format("no pose slot for actor {}", npc));
    }
    return;
  }
  if (slot->instance.load() == instance && slot->player.load() == npc)
    return;
  slot->instance.store(0, std::memory_order_release);
  slot->player.store(npc, std::memory_order_release);
  slot->instance.store(instance, std::memory_order_release);
  log::Info("pose stream: actor {} {}", npc,
            instance ? std::format("drawn on graph instance {:#x}", instance)
                     : std::string("released: its own animation again"));
}

void BindPoseRig(std::uint32_t npc, std::uint8_t part, bool source,
                 const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  std::uintptr_t instance = 0;
  if (npc != 0 && entity && LayoutOk())
    if (const auto component =
            AnimatedComponent(entity, part == proto::kRigFace ? "face_rig" : "root"))
      ReadAt(component + kComponentInstance, instance);
  RigSlot *slot = nullptr;
  for (auto &candidate : g_rigSlots)
    if (candidate.npc.load() == npc && candidate.part.load() == part &&
        candidate.source.load() == source && candidate.instance.load() != 0)
      slot = &candidate;
  if (!slot && instance != 0)
    for (auto &candidate : g_rigSlots)
      if (candidate.instance.load() == 0) {
        slot = &candidate;
        break;
      }
  if (!slot) {
    if (instance != 0) {
      log::Warn("pose stream: no room for npc {}'s {} rig", npc,
                part == proto::kRigFace ? "face" : "body");
      Degradations::Get().Fallback("no room", std::format("no rig slot for npc {}", npc));
    }
    return;
  }
  if (slot->instance.load() == instance && slot->npc.load() == npc)
    return;
  // Off first, then under the lock: a job thread never sees it half changed.
  if (slot->instance.exchange(0) != 0)
    g_rigCount.fetch_sub(1);
  {
    std::lock_guard guard(slot->lock);
    slot->npc.store(npc, std::memory_order_release);
    slot->part.store(part, std::memory_order_release);
    slot->source.store(source, std::memory_order_release);
    slot->mapped = 0;
    slot->usable = false;
    slot->reference.reset();
    slot->trackHash = 0;
    slot->trackReference.reset();
    slot->lastRead = {};
  }
  if (instance != 0) {
    slot->instance.store(instance, std::memory_order_release);
    g_rigCount.fetch_add(1);
  }
  log::Info("pose stream: npc {}'s {} rig {}", npc, part == proto::kRigFace ? "face" : "body",
            instance == 0
                ? std::string("let go")
                : std::format("{} on graph instance {:#x}", source ? "read" : "written", instance));
}

void ClearPoseActors() {
  for (auto &source : g_actorSources)
    if (source.object.exchange(0) != 0)
      g_actorSourceCount.fetch_sub(1);
  for (auto &binding : g_actorBindings)
    binding.instance.store(0, std::memory_order_release);
  for (auto &slot : g_rigSlots)
    if (slot.instance.exchange(0) != 0)
      g_rigCount.fetch_sub(1);
}

std::string PoseActorsReport() {
  int sources = 0, bound = 0;
  for (const auto &source : g_actorSources)
    sources += source.object.load() != 0;
  for (const auto &binding : g_actorBindings)
    bound += binding.instance.load() != 0;
  int rigs = 0;
  for (const auto &slot : g_rigSlots)
    rigs += slot.instance.load() != 0;
  return std::format(
      "{} actor(s) read ({} poses, {} skipped), {} proxy(ies) posed ({} writes, {} "
      "without a pose), {} poses heard; {} other rig(s) bound ({} reads, {} writes "
      "of {} bones, {} rig poses heard); face tracks {} ({} reads, {} writes)",
      sources, g_actorReads.exchange(0), g_actorSkips.exchange(0), bound, g_actorWrites.exchange(0),
      g_actorMisses.exchange(0), Runtime::Get().ActorPosesReceived(), rigs, g_rigReads.exchange(0),
      g_rigWrites.exchange(0), g_rigBones.exchange(0), Runtime::Get().RigPosesReceived(),
      g_faceTracks.load() == 1   ? "on"
      : g_faceTracks.load() == 2 ? "off"
                                 : "not checked",
      g_trackReads.exchange(0), g_trackWrites.exchange(0));
}

void PoseStreamReport() {
  log::Info("pose stream: {} poses read ({} skipped), {} written ({} without a pose yet), "
            "source {:#x}, loopback player {}",
            g_reads.exchange(0), g_readSkips.exchange(0), g_writes.exchange(0),
            g_writeMisses.exchange(0), g_sourceObject.load(), Runtime::Get().PoseLoopback());
}
} // namespace tandem::plugin::engine
