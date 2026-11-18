// The pose scan (docs/research/pose-streaming.md step 1), a measuring tool:
// walks readable pointers from an entity's root animated component and
// reports every run of memory that looks like an array of bone transforms.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "posescan.hpp"

#include <RED4ext/RTTISystem.hpp>

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace tandem::plugin::engine {
namespace {
// QsTransform: translation (x y z w), rotation (i j k r), scale (x y z w).
struct Transform {
  float translation[4];
  float rotation[4];
  float scale[4];
};
static_assert(sizeof(Transform) == 48);

// How many bytes from `address` on are committed and readable (0: none).
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
  const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
  return end - address;
}

bool LooksLikeBone(const Transform &t) {
  float length = 0.f;
  for (float v : t.rotation) {
    if (!std::isfinite(v))
      return false;
    length += v * v;
  }
  if (std::fabs(length - 1.f) > 0.01f)
    return false;
  for (int k = 0; k < 3; ++k) {
    if (!std::isfinite(t.translation[k]) || std::fabs(t.translation[k]) > 50.f)
      return false;
    if (!std::isfinite(t.scale[k]) || t.scale[k] < 0.2f || t.scale[k] > 5.f)
      return false;
  }
  return true;
}

// The number of consecutive bone-like transforms at `address`.
std::size_t BoneRun(std::uintptr_t address, std::size_t readable) {
  const auto *bones = reinterpret_cast<const Transform *>(address);
  const std::size_t limit = std::min<std::size_t>(readable / sizeof(Transform), 600);
  std::size_t count = 0;
  while (count < limit && LooksLikeBone(bones[count]))
    ++count;
  return count;
}

struct Node {
  std::uintptr_t address;
  int depth;
  std::string path;
};
} // namespace

// ---- mark and diff: which buffer follows the body? ----

struct Marked {
  std::uintptr_t address; // of the hips-like transform
  std::string path;       // how the scan reached its array
  std::size_t index;      // its index in that array
  float height;
};
std::vector<Marked> g_marked;

// Every transform in the arrays reachable from the root component whose
// translation looks like a standing V's hips in model space.
void MarkHips(std::uintptr_t root) {
  g_marked.clear();
  std::deque<Node> queue{{root, 0, "root"}};
  std::unordered_set<std::uintptr_t> seen{root};
  std::size_t visited = 0;
  while (!queue.empty() && visited < 60000 && g_marked.size() < 400) {
    const auto node = queue.front();
    queue.pop_front();
    ++visited;
    const std::size_t readable = Readable(node.address);
    if (readable < 64)
      continue;
    const std::size_t span = std::min<std::size_t>(readable, node.depth == 0 ? 0x2B0 : 0x400);
    for (std::size_t offset = 0; offset + 8 <= span; offset += 8) {
      const auto value = *reinterpret_cast<const std::uintptr_t *>(node.address + offset);
      if ((value & 0xF) != 0 || !seen.insert(value).second)
        continue;
      if (value >= 0x140000000 && value < 0x150000000)
        continue;
      const std::size_t there = Readable(value);
      if (there < sizeof(Transform))
        continue;
      const auto path = std::format("{}+{:#x}", node.path, offset);
      if (const std::size_t run = BoneRun(value, there); run >= 20) {
        const auto *bones = reinterpret_cast<const Transform *>(value);
        for (std::size_t k = 0; k < run; ++k) {
          const auto &t = bones[k].translation;
          if (t[2] > 0.85f && t[2] < 1.05f && std::fabs(t[0]) < 0.2f && std::fabs(t[1]) < 0.2f)
            g_marked.push_back({value + k * sizeof(Transform), path, k, t[2]});
        }
        continue;
      }
      if (node.depth < 3)
        queue.push_back({value, node.depth + 1, path});
    }
  }
  log::Info("pose mark: {} hips-like transforms marked ({} objects visited)", g_marked.size(),
            visited);
}

void DiffHips() {
  std::size_t moved = 0;
  for (const auto &mark : g_marked) {
    if (Readable(mark.address) < sizeof(Transform))
      continue;
    const auto &now = reinterpret_cast<const Transform *>(mark.address)->translation;
    if (std::fabs(now[2] - mark.height) > 0.08f) {
      ++moved;
      log::Info("pose diff: {}[{}] at {:#x}: height {:.3f} -> {:.3f} (x {:.3f}, y {:.3f})",
                mark.path, mark.index, mark.address, mark.height, now[2], now[0], now[1]);
    }
  }
  log::Info("pose diff: {} of {} marked transforms moved", moved, g_marked.size());
}

// The rig's bone names with their indices (anim::Rig: boneNames at +0x50;
// the rig is the resource of the token at component +0x140, token +0x28).
void PoseBones(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity)
    return;
  for (const auto &component : entity->components) {
    if (!component || !component->GetType()->IsA(animatedClass) ||
        component->name != RED4ext::CName("root"))
      continue;
    const auto base = reinterpret_cast<std::uintptr_t>(component.GetPtr());
    const auto token = *reinterpret_cast<const std::uintptr_t *>(base + 0x140);
    if (Readable(token) < 0x30)
      return;
    const auto rig = *reinterpret_cast<const std::uintptr_t *>(token + 0x28);
    if (Readable(rig) < 0x70)
      return;
    const auto *names = *reinterpret_cast<const RED4ext::CName *const *>(rig + 0x50);
    const auto count = *reinterpret_cast<const std::uint32_t *>(rig + 0x5C);
    log::Info("pose bones: entity {:x}, rig {:#x}, {} bones", entity->entityID.hash, rig, count);
    if (count > 600 || Readable(reinterpret_cast<std::uintptr_t>(names)) < count * 8)
      return;
    std::string line;
    for (std::uint32_t k = 0; k < count; ++k) {
      const char *name = names[k].ToString();
      line += std::format(" {}={}", k, name ? name : "?");
      if (line.size() > 900 || k + 1 == count) {
        log::Info("pose bones:{}", line);
        line.clear();
      }
    }
    return;
  }
}

// The children of the object at component +0x168 (the graph instance?): for
// each, its vtable and every pose-like array it points at, with the heights
// of bones 2 (Hips) and 49 (a hand bone on the player rig).
void PoseTree(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity)
    return;
  for (const auto &component : entity->components) {
    if (!component || !component->GetType()->IsA(animatedClass) ||
        component->name != RED4ext::CName("root"))
      continue;
    const auto base = reinterpret_cast<std::uintptr_t>(component.GetPtr());
    for (std::size_t slot : {0x168, 0x170, 0x250, 0x288}) {
      const auto object = *reinterpret_cast<const std::uintptr_t *>(base + slot);
      const std::size_t size = std::min<std::size_t>(Readable(object), 0x400);
      if (size < 16)
        continue;
      log::Info("pose tree: entity {:x} +{:#x} -> {:#x}, vtable {:#x}", entity->entityID.hash, slot,
                object, *reinterpret_cast<const std::uintptr_t *>(object));
      // The object's own arrays, then its children's.
      for (int depth = 0; depth < 2; ++depth) {
        for (std::size_t offset = 0; offset + 8 <= size; offset += 8) {
          const auto child =
              depth == 0 ? object : *reinterpret_cast<const std::uintptr_t *>(object + offset);
          if (depth == 0 && offset != 0)
            break;
          const std::size_t childSize = std::min<std::size_t>(Readable(child), 0x400);
          if ((child & 0xF) != 0 || childSize < 64 || (child >= 0x140000000 && child < 0x150000000))
            continue;
          std::string arrays;
          for (std::size_t at = 0; at + 8 <= childSize; at += 8) {
            const auto array = *reinterpret_cast<const std::uintptr_t *>(child + at);
            if ((array & 0xF) != 0)
              continue;
            const std::size_t there = Readable(array);
            if (there < 60 * sizeof(Transform))
              continue;
            const std::size_t run = BoneRun(array, there);
            if (run < 60)
              continue;
            const auto *bones = reinterpret_cast<const Transform *>(array);
            arrays += std::format(" +{:#x}:{}x hips {:.2f} hand {:.2f}", at, run,
                                  bones[2].translation[2], bones[49].translation[2]);
          }
          if (!arrays.empty())
            log::Info("pose tree:   {}{:#x} vtable {:#x}:{}", depth == 0 ? "self " : "+", offset,
                      *reinterpret_cast<const std::uintptr_t *>(child), arrays);
        }
      }
    }
    return;
  }
}

void PoseMark(const RED4ext::Handle<RED4ext::ent::Entity> &entity, bool diff) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (diff) {
    DiffHips();
    return;
  }
  if (!entity)
    return;
  for (const auto &component : entity->components) {
    if (component && component->GetType()->IsA(animatedClass) &&
        component->name == RED4ext::CName("root")) {
      MarkHips(reinterpret_cast<std::uintptr_t>(component.GetPtr()));
      return;
    }
  }
}

void PoseScan(const RED4ext::Handle<RED4ext::ent::Entity> &entity) {
  static auto *animatedClass = RED4ext::CRTTISystem::Get()->GetClass("entAnimatedComponent");
  if (!entity)
    return;
  for (const auto &component : entity->components) {
    if (!component || !component->GetType()->IsA(animatedClass) ||
        component->name != RED4ext::CName("root"))
      continue;

    const auto root = reinterpret_cast<std::uintptr_t>(component.GetPtr());
    log::Info("pose scan: entity {:x}, root component {:#x}", entity->entityID.hash, root);
    std::deque<Node> queue{{root, 0, "root"}};
    std::unordered_set<std::uintptr_t> seen{root};
    std::size_t visited = 0, found = 0;
    while (!queue.empty() && visited < 40000 && found < 60) {
      const auto node = queue.front();
      queue.pop_front();
      ++visited;
      const std::size_t readable = Readable(node.address);
      if (readable < 64)
        continue;
      // The component itself is 0x2B0 long; other objects: look at 0x400.
      const std::size_t span = std::min<std::size_t>(readable, node.depth == 0 ? 0x2B0 : 0x400);
      for (std::size_t offset = 0; offset + 8 <= span; offset += 8) {
        const auto value = *reinterpret_cast<const std::uintptr_t *>(node.address + offset);
        if ((value & 0xF) != 0 || !seen.insert(value).second)
          continue;
        // Not into the game image (code, vtables, static data).
        if (value >= 0x140000000 && value < 0x150000000)
          continue;
        const std::size_t there = Readable(value);
        if (there < sizeof(Transform))
          continue;
        const auto path = std::format("{}+{:#x}", node.path, offset);
        if (const std::size_t run = BoneRun(value, there); run >= 20) {
          const auto *bones = reinterpret_cast<const Transform *>(value);
          log::Info("pose scan: {} -> {:#x}: {} transforms; [1] t ({:.3f} {:.3f} {:.3f}) "
                    "r ({:.3f} {:.3f} {:.3f} {:.3f}); [5] t ({:.3f} {:.3f} {:.3f})",
                    path, value, run, bones[1].translation[0], bones[1].translation[1],
                    bones[1].translation[2], bones[1].rotation[0], bones[1].rotation[1],
                    bones[1].rotation[2], bones[1].rotation[3], bones[5].translation[0],
                    bones[5].translation[1], bones[5].translation[2]);
          ++found;
          continue;
        }
        if (node.depth < 3)
          queue.push_back({value, node.depth + 1, path});
      }
    }
    log::Info("pose scan: {} objects visited, {} transform arrays", visited, found);
    return;
  }
  log::Info("pose scan: entity {:x} has no root animated component", entity->entityID.hash);
}
} // namespace tandem::plugin::engine
