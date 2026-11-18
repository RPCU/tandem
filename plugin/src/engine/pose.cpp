// The pose hook: where an animation graph hands over its final pose.
//
// Every graph ends in an animAnimNode_Output; its "sample" virtual function
// runs once per graph pass, on animation job threads, and leaves the pose in
// its last argument. Hooking it gives the local V's final pose to read and a
// puppet's final pose to overwrite (docs/research/pose-streaming.md).
//
// Facts used (game 2.50, read from how the CTO mod finds the same point, see
// docs/research/third-person-overhaul.md; no code of it is used):
//   - the sample function is the vtable slot at +0x128 of animAnimNode_Output;
//   - void Sample(node, a2, a3, out): *out is the pose;
//   - pose +0x00: bone count (u32), +0x30: the bones, 48-byte transforms in
//     parent space (translation, rotation, scale).
// The class is found by name and the function through an instance's vtable;
// the hook refuses to install when either lies outside the game's image.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "pose.hpp"

#include <RED4ext/RTTISystem.hpp>

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace tandem::plugin::engine {
namespace {
constexpr std::size_t kSampleSlot = 0x128;
constexpr std::size_t kPoseBones = 0x30;
constexpr std::uint32_t kMaxBones = 512;

struct Transform {
  float translation[4];
  float rotation[4];
  float scale[4];
};
static_assert(sizeof(Transform) == 48);

using Sample = void (*)(void *node, void *a2, void *a3, void *out);

const RED4ext::v1::Sdk *g_sdk = nullptr;
RED4ext::v1::PluginHandle g_handle{};
Sample g_original = nullptr;
void *g_target = nullptr;
std::uintptr_t g_imageBegin = 0;
std::uintptr_t g_imageEnd = 0;

// ---- guarded reads (job threads, pointers of unknown layouts) ----

bool SafeRead(std::uintptr_t address, std::uintptr_t &value) {
  if (address < 0x10000 || address > 0x00007FFFFFFFFFF0 || (address & 7) != 0)
    return false;
  __try {
    value = *reinterpret_cast<const std::uintptr_t *>(address);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// The pose behind a sample call's last argument; false when it does not look
// like one.
bool ReadPose(void *out, std::uint32_t &count, Transform *&bones) {
  __try {
    const auto pose = *reinterpret_cast<const std::uintptr_t *>(out);
    if (pose < 0x10000 || (pose & 7) != 0)
      return false;
    count = *reinterpret_cast<const std::uint32_t *>(pose);
    bones = *reinterpret_cast<Transform *const *>(pose + kPoseBones);
    if (count == 0 || count > kMaxBones || reinterpret_cast<std::uintptr_t>(bones) < 0x10000 ||
        (reinterpret_cast<std::uintptr_t>(bones) & 0xF) != 0)
      return false;
    // Touch both ends: the copy below must not fault.
    volatile float first = bones[0].rotation[3];
    volatile float last = bones[count - 1].rotation[3];
    (void)first;
    (void)last;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// ---- what the hook saw ----

struct Seen {
  std::uint64_t calls = 0;
  std::set<void *> nodes, a2, a3, outs, poses;
  std::set<DWORD> threads;
  float hipsMin = 1e9f, hipsMax = -1e9f;
};
std::mutex g_statsLock;
std::map<std::uint32_t, Seen> g_seen;
std::atomic_bool g_stats{false};

// ---- watch: which argument leads back to an entity ----

struct Needle {
  std::uintptr_t address;
  std::string label;
};
std::mutex g_watchLock;
std::vector<Needle> g_needles;
std::map<std::uint32_t, int> g_watchLeft; // bone count -> calls still to search
std::set<std::string> g_hits;
int g_watchDepth = 2;
std::atomic_bool g_watching{false};

const std::string *MatchNeedle(std::uintptr_t value) {
  for (const auto &needle : g_needles)
    if (needle.address == value)
      return &needle.label;
  return nullptr;
}

void Search(std::uintptr_t base, const std::string &path, int depth, std::uint32_t count) {
  constexpr std::size_t kWindow = 0x200;
  for (std::size_t offset = 0; offset < kWindow; offset += 8) {
    std::uintptr_t value = 0;
    if (!SafeRead(base + offset, value))
      break; // the object ends here
    if (const auto *label = MatchNeedle(value)) {
      auto hit = std::format("{} bones: {}+0x{:X} = {}", count, path, offset, *label);
      if (g_hits.insert(hit).second)
        log::Info("pose watch: {}", hit);
    } else if (depth > 1 && value >= 0x10000 && (value < g_imageBegin || value >= g_imageEnd)) {
      Search(value, std::format("{}+0x{:X}->", path, offset), depth - 1, count);
    }
  }
}

// ---- copy: the source's pose onto the target rig's poses ----

std::mutex g_copyLock;
std::array<Transform, kMaxBones> g_sourcePose{};
bool g_sourceValid = false;
std::uint32_t g_sourceCount = 0, g_targetCount = 0;
std::vector<int> g_sourceOfTarget; // target bone -> source bone, -1 none
int g_hipsTarget = -1;
std::atomic_bool g_copying{false};
std::atomic_uint64_t g_reads{0}, g_writes{0};

void Observe(void *node, void *a2, void *a3, void *out) {
  std::uint32_t count = 0;
  Transform *bones = nullptr;
  if (!ReadPose(out, count, bones))
    return;

  if (g_stats.load(std::memory_order_relaxed)) {
    std::lock_guard _(g_statsLock);
    auto &seen = g_seen[count];
    ++seen.calls;
    if (seen.nodes.size() < 64) {
      seen.nodes.insert(node);
      seen.a2.insert(a2);
      seen.a3.insert(a3);
      seen.outs.insert(out);
      seen.poses.insert(bones);
      seen.threads.insert(GetCurrentThreadId());
    }
    if (count > 2) {
      seen.hipsMin = std::min(seen.hipsMin, bones[2].translation[2]);
      seen.hipsMax = std::max(seen.hipsMax, bones[2].translation[2]);
    }
  }

  if (g_watching.load(std::memory_order_relaxed)) {
    std::lock_guard _(g_watchLock);
    auto left = g_watchLeft.find(count);
    if (left != g_watchLeft.end() && left->second > 0) {
      --left->second;
      Search(reinterpret_cast<std::uintptr_t>(node), "node", g_watchDepth, count);
      Search(reinterpret_cast<std::uintptr_t>(a2), "a2", g_watchDepth, count);
      Search(reinterpret_cast<std::uintptr_t>(a3), "a3", g_watchDepth, count);
      Search(reinterpret_cast<std::uintptr_t>(out), "out", g_watchDepth, count);
      if (left->second == 0)
        log::Info("pose watch: done for {} bones ({} distinct hits so far)", count, g_hits.size());
    }
  }

  if (g_copying.load(std::memory_order_relaxed)) {
    std::lock_guard _(g_copyLock);
    if (count == g_sourceCount) {
      std::memcpy(g_sourcePose.data(), bones, count * sizeof(Transform));
      g_sourceValid = true;
      g_reads.fetch_add(1, std::memory_order_relaxed);
    } else if (count == g_targetCount && g_sourceValid) {
      for (std::uint32_t bone = 0; bone < count; ++bone) {
        const int source = g_sourceOfTarget[bone];
        if (source < 0)
          continue;
        std::memcpy(bones[bone].rotation, g_sourcePose[source].rotation, sizeof(float) * 4);
        // Bone lengths stay the body's own; the hips carry the height.
        if (static_cast<int>(bone) == g_hipsTarget)
          std::memcpy(bones[bone].translation, g_sourcePose[source].translation, sizeof(float) * 4);
      }
      g_writes.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

void Detour(void *node, void *a2, void *a3, void *out) {
  g_original(node, a2, a3, out);
  if (g_stats.load(std::memory_order_relaxed) || g_watching.load(std::memory_order_relaxed) ||
      g_copying.load(std::memory_order_relaxed))
    Observe(node, a2, a3, out);
}

// ---- game thread helpers ----

std::size_t Readable(std::uintptr_t address) {
  if (address < 0x10000 || address > 0x00007FFFFFFFFFFF)
    return 0;
  MEMORY_BASIC_INFORMATION info{};
  if (VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) == 0)
    return 0;
  constexpr DWORD kRead =
      PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY;
  if (info.State != MEM_COMMIT || (info.Protect & kRead) == 0 || (info.Protect & PAGE_GUARD))
    return 0;
  return reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize - address;
}

// The bone names of an entity's root rig (anim::Rig::boneNames; the rig is
// the resource of the token at entAnimatedComponent +0x140, measured on 2.50
// and checked for readability at each step). Empty: not found.
std::vector<RED4ext::CName> RigBones(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity || !animatedClass)
    return {};
  for (const auto &component : entity->components) {
    if (!component || !component->GetType()->IsA(animatedClass) ||
        component->name != RED4ext::CName("root"))
      continue;
    const auto base = reinterpret_cast<std::uintptr_t>(component.GetPtr());
    const auto token = *reinterpret_cast<const std::uintptr_t *>(base + 0x140);
    if (Readable(token) < 0x30)
      return {};
    const auto rig = *reinterpret_cast<const std::uintptr_t *>(token + 0x28);
    if (Readable(rig) < 0x70)
      return {};
    const auto *names = *reinterpret_cast<const RED4ext::CName *const *>(rig + 0x50);
    const auto count = *reinterpret_cast<const std::uint32_t *>(rig + 0x5C);
    if (count == 0 || count > kMaxBones ||
        Readable(reinterpret_cast<std::uintptr_t>(names)) < count * sizeof(RED4ext::CName))
      return {};
    return {names, names + count};
  }
  return {};
}
} // namespace

void SetPoseHookContext(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle) {
  g_sdk = sdk;
  g_handle = handle;
}

bool InstallPoseHook() {
  if (g_target)
    return true;
  if (!g_sdk)
    return false;
  auto *rtti = RED4ext::CRTTISystem::Get();
  auto *outputClass = rtti ? rtti->GetClass("animAnimNode_Output") : nullptr;
  if (!outputClass) {
    log::Error("pose hook: class animAnimNode_Output not found");
    return false;
  }
  // The layout the slot was measured on.
  if (outputClass->size != 0x60) {
    log::Error("pose hook: animAnimNode_Output is 0x{:X} bytes, measured as 0x60 on 2.50",
               outputClass->size);
    return false;
  }

  const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(image + dos->e_lfanew);
  g_imageBegin = image;
  g_imageEnd = image + nt->OptionalHeader.SizeOfImage;
  const auto inImage = [](std::uintptr_t address) {
    return address >= g_imageBegin && address < g_imageEnd;
  };

  void *instance = outputClass->CreateInstance(true);
  if (!instance) {
    log::Error("pose hook: could not create an animAnimNode_Output");
    return false;
  }
  const auto vtable = *reinterpret_cast<const std::uintptr_t *>(instance);
  outputClass->DestructCls(instance);
  outputClass->GetAllocator()->Free(instance);
  if (!inImage(vtable) || !inImage(vtable + kSampleSlot)) {
    log::Error("pose hook: the node's vtable is outside the game image");
    return false;
  }
  const auto sample = *reinterpret_cast<const std::uintptr_t *>(vtable + kSampleSlot);
  if (!inImage(sample)) {
    log::Error("pose hook: the sample slot points outside the game image");
    return false;
  }

  g_target = reinterpret_cast<void *>(sample);
  if (!g_sdk->hooking->Attach(g_handle, g_target, reinterpret_cast<void *>(&Detour),
                              reinterpret_cast<void **>(&g_original))) {
    log::Error("pose hook: could not hook the output node's sample function");
    g_target = nullptr;
    return false;
  }
  g_stats = true;
  log::Info("pose hook: on (vtable exe+0x{:X}, sample exe+0x{:X})", vtable - image, sample - image);
  return true;
}

void RemovePoseHook() {
  g_stats = false;
  g_watching = false;
  g_copying = false;
  if (g_target && g_sdk)
    g_sdk->hooking->Detach(g_handle, g_target);
  g_target = nullptr;
}

void PoseWatch(const RED4ext::Handle<RED4ext::ent::Entity> &entity, const std::string &label,
               int depth) {
  if (!entity || !InstallPoseHook())
    return;
  const auto bones = RigBones(entity);
  std::lock_guard _(g_watchLock);
  g_needles.push_back({reinterpret_cast<std::uintptr_t>(entity.GetPtr()), label + " entity"});
  for (const auto &component : entity->components) {
    if (!component)
      continue;
    const char *type = component->GetType()->GetName().ToString();
    const char *name = component->name.ToString();
    g_needles.push_back({reinterpret_cast<std::uintptr_t>(component.GetPtr()),
                         std::format("{} {} '{}'", label, type ? type : "?", name ? name : "?")});
  }
  g_watchDepth = depth < 1 ? 1 : depth > 3 ? 3 : depth;
  g_watchLeft[static_cast<std::uint32_t>(bones.size())] = g_watchDepth >= 3 ? 12 : 200;
  g_watching = true;
  log::Info("pose watch: {} ({:x}): {} bones, {} components, depth {}", label,
            entity->entityID.hash, bones.size(), entity->components.Size(), g_watchDepth);
}

void PoseCopy(const RED4ext::Handle<RED4ext::ent::Entity> &source,
              const RED4ext::Handle<RED4ext::ent::Entity> &target) {
  if (!source || !target) {
    g_copying = false;
    log::Info("pose copy: off ({} reads, {} writes)", g_reads.load(), g_writes.load());
    return;
  }
  if (!InstallPoseHook())
    return;
  const auto from = RigBones(source);
  const auto to = RigBones(target);
  if (from.empty() || to.empty() || from.size() == to.size()) {
    log::Warn("pose copy: rigs not usable ({} and {} bones)", from.size(), to.size());
    return;
  }
  std::vector<int> map(to.size(), -1);
  int mapped = 0, hips = -1;
  for (std::size_t bone = 0; bone < to.size(); ++bone) {
    // The entity is placed by its movement controller, not by its pose.
    if (to[bone] == RED4ext::CName("Root") || to[bone] == RED4ext::CName("Trajectory"))
      continue;
    for (std::size_t other = 0; other < from.size(); ++other) {
      if (from[other] == to[bone]) {
        map[bone] = static_cast<int>(other);
        ++mapped;
        break;
      }
    }
    if (to[bone] == RED4ext::CName("Hips"))
      hips = static_cast<int>(bone);
  }
  {
    std::lock_guard _(g_copyLock);
    g_sourceCount = static_cast<std::uint32_t>(from.size());
    g_targetCount = static_cast<std::uint32_t>(to.size());
    g_sourceOfTarget = std::move(map);
    g_hipsTarget = hips;
    g_sourceValid = false;
  }
  g_reads = 0;
  g_writes = 0;
  g_copying = true;
  log::Info("pose copy: on, {} of {} bones mapped from the {}-bone rig", mapped, to.size(),
            from.size());
}

void PoseReport() {
  if (!g_target) {
    log::Info("pose report: the hook is not installed");
    return;
  }
  std::map<std::uint32_t, Seen> seen;
  {
    std::lock_guard _(g_statsLock);
    seen.swap(g_seen);
  }
  for (const auto &[count, entry] : seen) {
    log::Info("pose report: {} bones: {} calls, {} nodes, a2 {} a3 {} out {} distinct, {} pose "
              "buffers, {} threads, hips z {:.3f}..{:.3f}",
              count, entry.calls, entry.nodes.size(), entry.a2.size(), entry.a3.size(),
              entry.outs.size(), entry.poses.size(), entry.threads.size(), entry.hipsMin,
              entry.hipsMax);
  }
  log::Info("pose report: copy {} ({} reads, {} writes)", g_copying.load() ? "on" : "off",
            g_reads.load(), g_writes.load());
}
} // namespace tandem::plugin::engine
