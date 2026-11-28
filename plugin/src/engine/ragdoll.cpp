// A ragdoll's bodies, the cut multiplayer's way (docs/research/engine/
// presence.md §13.162, measured on 2.50 with tools/re, read-only).
//
// entRagdollComponent (0x220) keeps its physics ragdoll's id at +0x1F4.
// Its vtable slot 52 fills an entRagdollComponentReplicatedState: the
// bodies' transforms (physics, hash 1828463752) and whether it sleeps; slot
// 53 applies one: physics hash 646325763 (id, {first, end} of the
// transforms, at rest) sets every body when the count matches the
// ragdoll's, its velocities cleared when at rest. The read dereferences the
// physics object it looks up without a check when it is no ragdoll (type
// byte +0x34 is not 0x0C): that check is made here first, with the same
// lookup.
//
// The code of each function is compared with what was measured, and each
// component's slot 53 with the measured function before its +0x1F4 is read:
// any difference turns the feature off.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "ragdoll.hpp"

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/Transform.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>
#include <RED4ext/Scripting/Natives/entIComponent.hpp>

#include <windows.h>

#include <format>

namespace tandem::plugin::engine {
namespace {
constexpr auto kFeature = feature::kRagdoll;

// Address-library hashes (2.50).
constexpr std::uint32_t kApplyState = 352523151;  // entRagdollComponent slot 53
constexpr std::uint32_t kSetBodies = 646325763;   // physics: id, bodies, at rest
constexpr std::uint32_t kReadBodies = 1828463752; // physics: out array, id
constexpr std::uint32_t kLookup = 1570510966;     // physics: the object of an id

constexpr std::uint32_t kRagdollId = 0x1F4;  // entRagdollComponent
constexpr std::uint32_t kApplySlot = 53 * 8; // its vtable
constexpr std::uint32_t kObjectType = 0x34;  // physics object
constexpr std::uint8_t kRagdollType = 0x0C;
constexpr std::uint32_t kMaxBodies = 64;

struct Code {
  std::uint32_t hash;
  std::uint32_t offset;
  const char *bytes; // hex, no relative operands
  const char *what;
};

constexpr Code kCode[] = {
    // sub rsp,38h; mov r8,[rcx+58h]; mov ecx,[rcx+1F4h]: the state at +0x58,
    // the ragdoll's id at +0x1F4.
    {kApplyState, 0x00, "4883ec384c8b41588b89f4010000", "apply state"},
    // ...mov r15b,r8b; mov rdi,rdx; mov ebx,ecx: (id, bodies, at rest).
    {kSetBodies, 0x00,
     "48895c2408488974241048897c24185541564157488bec4881ec80000000458af8488bfa8bd9", "set bodies"},
    // xor edi,edi; test rax,rax; je; cmp byte [rax+34h],0Ch; cmove rdi,rax:
    // a ragdoll is type 0x0C (and is dereferenced unchecked after).
    {kReadBodies, 0x1E, "33ff4885c074088078340c480f44f8", "read bodies"},
};

int Nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

bool CodeMatches(const Code &code) {
  const auto base = FindAddress(kFeature, code.what, code.hash);
  if (!base)
    return false;
  const auto *at = reinterpret_cast<const std::uint8_t *>(base + code.offset);
  std::size_t i = 0;
  for (const char *p = code.bytes; *p; p += 2, ++i) {
    const int high = Nibble(p[0]), low = p[1] ? Nibble(p[1]) : -1;
    if (high < 0 || low < 0 || at[i] != static_cast<std::uint8_t>(high << 4 | low)) {
      FeatureOff(kFeature,
                 std::format("{} differs from 2.50 at +0x{:X}", code.what, code.offset + i));
      return false;
    }
  }
  return true;
}

using SetBodiesFn = bool (*)(std::uint32_t, const RED4ext::Transform *const (*)[2], bool);
using ReadBodiesFn = void (*)(RED4ext::DynArray<RED4ext::Transform> *, std::uint32_t);
using LookupFn = std::uintptr_t (*)(std::uint32_t);

struct Functions {
  std::uintptr_t applyState = 0;
  SetBodiesFn set = nullptr;
  ReadBodiesFn read = nullptr;
  LookupFn lookup = nullptr;
};

const Functions *Get() {
  static const Functions s_functions = [] {
    Functions f;
    bool ok = true;
    for (const auto &code : kCode)
      ok = CodeMatches(code) && ok; // every mismatch named
    const auto lookup = FindAddress(kFeature, "physics object", kLookup);
    // The read's own lookup (a call at +0x19) is the one called here.
    const auto read = Resolve(kReadBodies);
    if (ok && lookup && read) {
      const auto rel = *reinterpret_cast<const std::int32_t *>(read + 0x1A);
      if (*reinterpret_cast<const std::uint8_t *>(read + 0x19) != 0xE8 ||
          read + 0x1E + rel != lookup) {
        FeatureOff(kFeature, "the bodies' read looks its object up elsewhere than in 2.50");
        ok = false;
      }
    }
    if (!ok || !lookup)
      return f;
    f.applyState = Resolve(kApplyState);
    f.set = reinterpret_cast<SetBodiesFn>(Resolve(kSetBodies));
    f.read = reinterpret_cast<ReadBodiesFn>(read);
    f.lookup = reinterpret_cast<LookupFn>(lookup);
    FeatureOn(kFeature);
    log::Info("ragdoll state: on (2.50: the ragdoll's id at entRagdollComponent +0x{:X})",
              kRagdollId);
    return f;
  }();
  return s_functions.set ? &s_functions : nullptr;
}

// The entity's ragdoll's physics id: 0 when it has no entRagdollComponent
// laid out as measured (its slot 53 the measured function).
std::uint32_t RagdollOf(RED4ext::ent::Entity *entity, const Functions &f, std::string &why) {
  static auto *ragdollClass = RED4ext::CRTTISystem::Get()->GetClass("entRagdollComponent");
  if (!entity || !ragdollClass) {
    why = "no entity";
    return 0;
  }
  for (const auto &component : entity->components) {
    if (!component || !component->GetType()->IsA(ragdollClass))
      continue;
    const auto base = reinterpret_cast<std::uintptr_t>(component.GetPtr());
    const auto vtable = *reinterpret_cast<const std::uintptr_t *>(base);
    if (!InGameImage(vtable) ||
        *reinterpret_cast<const std::uintptr_t *>(vtable + kApplySlot) != f.applyState) {
      why = "its ragdoll component is not laid out as on 2.50";
      return 0;
    }
    const auto id = *reinterpret_cast<const std::uint32_t *>(base + kRagdollId);
    if (id == 0 || id == 0xFFFFFFFF) {
      why = "its ragdoll has no physics body";
      return 0;
    }
    return id;
  }
  why = "it has no ragdoll component";
  return 0;
}

// No C++ objects with destructors in a __try function.
bool IsRagdoll(const Functions &f, std::uint32_t id) {
  __try {
    const auto object = f.lookup(id);
    return object && *reinterpret_cast<const std::uint8_t *>(object + kObjectType) == kRagdollType;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ReadInto(const Functions &f, std::uint32_t id, RED4ext::DynArray<RED4ext::Transform> *out) {
  __try {
    f.read(out, id);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool SetFrom(const Functions &f, std::uint32_t id, const RED4ext::Transform *const (*span)[2]) {
  __try {
    return f.set(id, span, true);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
} // namespace

bool CheckRagdoll() { return Get() != nullptr; }

std::vector<RagdollBody> ReadRagdoll(RED4ext::ent::Entity *entity, std::string &why) {
  const auto *f = Get();
  if (!f) {
    why = "the ragdoll's code differs from 2.50";
    return {};
  }
  const auto id = RagdollOf(entity, *f, why);
  if (!id)
    return {};
  if (!IsRagdoll(*f, id)) {
    why = "its physics body is no ragdoll";
    return {};
  }
  RED4ext::DynArray<RED4ext::Transform> transforms;
  transforms.Reserve(kMaxBodies); // the read then fills it without growing it
  if (!ReadInto(*f, id, &transforms)) {
    why = "its bodies could not be read";
    return {};
  }
  if (transforms.Size() == 0 || transforms.Size() > kMaxBodies) {
    why = std::format("its ragdoll has {} bodies", transforms.Size());
    return {};
  }
  std::vector<RagdollBody> out;
  out.reserve(transforms.Size());
  for (const auto &t : transforms)
    out.push_back({t.position.X, t.position.Y, t.position.Z, t.orientation.i, t.orientation.j,
                   t.orientation.k, t.orientation.r});
  return out;
}

std::string WriteRagdoll(RED4ext::ent::Entity *entity, const std::vector<RagdollBody> &bodies) {
  const auto *f = Get();
  if (!f)
    return "the ragdoll's code differs from 2.50";
  std::string why;
  const auto id = RagdollOf(entity, *f, why);
  if (!id)
    return why;
  if (!IsRagdoll(*f, id))
    return "its physics body is no ragdoll";
  if (bodies.empty() || bodies.size() > kMaxBodies)
    return std::format("{} bodies told", bodies.size());
  std::vector<RED4ext::Transform> transforms(bodies.size());
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const auto &b = bodies[i];
    transforms[i].position = {b.x, b.y, b.z, 1.f};
    transforms[i].orientation = {b.i, b.j, b.k, b.r};
  }
  const RED4ext::Transform *const span[2] = {transforms.data(),
                                             transforms.data() + transforms.size()};
  // False: the ragdoll has another number of bodies (another rig).
  if (!SetFrom(*f, id, &span))
    return std::format("{} bodies told, the copy's ragdoll has others", bodies.size());
  return "";
}
} // namespace tandem::plugin::engine
