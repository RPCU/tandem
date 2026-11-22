// A slot added to a slot component while its entity is built (see
// slots.hpp; docs/research/engine/stealth.md §13.46).
//
// entSlotComponent as read in the 2.50 exe (RTTI: 0x1A0 bytes, `slots`
// +0x120, `fallbackSlots` +0x130; the rest is not RTTI):
//   +0x140  one binding (0x28 bytes) per slot, a DynArray built when the
//           component attaches to its skeleton (vtable +0x180, 0x140BEED68:
//           0x140405B6C sizes it to `slots`), emptied at its detach;
//   +0x150  the name index, a HashMap<CName, uint32 slot index> (RED4ext's
//           HashMap, hash = the name's high ^ low 32 bits), built once from
//           `slots` after the component is loaded (vtable +0x28, 0x1403F7810).
// GetSlotTransform (native 0x140CA2200 → 0x140CA22E4) and the mounting
// facility's slot lookups (0x140202734, ~60 callers) find a slot by the index
// only, so a slot appended to `slots` is never found (what the scripts saw);
// 0x14032A4D4 then reads `slots[i]` and, once attached, binding i. A slot is
// therefore added only before the attach (no bindings yet), to `slots` and
// to the index together; the attach then binds it with the others.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "access.hpp"
#include "slots.hpp"

#include <RED4ext/HashMap.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/IComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/Slot.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/SlotComponent.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>

namespace tandem::plugin::engine {
namespace {
// The game's hash of a CName key in its HashMaps (0x14038B480, 0x1401A7990).
struct NameHash {
  std::uint32_t operator()(const RED4ext::CName &name) const noexcept {
    return static_cast<std::uint32_t>(name.hash >> 32) ^ static_cast<std::uint32_t>(name.hash);
  }
};
using NameIndex = RED4ext::HashMap<RED4ext::CName, std::uint32_t, NameHash>;
// The game's nodes (0x1402B2BC8 allocates capacity * 0x1C: 0x18 per node,
// then 4 per bucket) and the map's size (+0x150 to the handle at +0x180).
static_assert(sizeof(NameIndex::Node) == 0x18);
static_assert(sizeof(NameIndex) == 0x30);

struct Bindings {
  void *entries;
  std::uint32_t capacity;
  std::uint32_t size;
};

constexpr std::uint32_t kComponentSize = 0x1A0;
constexpr std::uint32_t kSlotsAt = 0x120;
constexpr std::uint32_t kFallbackSlotsAt = 0x130;
constexpr std::size_t kBindingsAt = 0x140;
constexpr std::size_t kNameIndexAt = 0x150;
static_assert(__builtin_offsetof(RED4ext::ent::SlotComponent, slots) == kSlotsAt);
static_assert(__builtin_offsetof(RED4ext::ent::SlotComponent, fallbackSlots) == kFallbackSlotsAt);
static_assert(sizeof(RED4ext::ent::SlotComponent) == kComponentSize);
static_assert(sizeof(RED4ext::ent::Slot) == 0x30);

RED4ext::CClass *SlotComponentClass() {
  static RED4ext::CClass *const cls = [] {
    auto *rtti = RED4ext::CRTTISystem::Get();
    return rtti ? rtti->GetClass("entSlotComponent") : nullptr;
  }();
  return cls;
}

RED4ext::ent::SlotComponent *AsSlotComponent(RED4ext::ent::IComponent *component) {
  auto *cls = SlotComponentClass();
  if (!component || !cls || !FeatureIsOn(feature::kBodySlots))
    return nullptr;
  auto *type = component->GetType();
  if (!type || !type->IsA(cls))
    return nullptr;
  return reinterpret_cast<RED4ext::ent::SlotComponent *>(component);
}

NameIndex &IndexOf(RED4ext::ent::SlotComponent *component) {
  return *reinterpret_cast<NameIndex *>(reinterpret_cast<std::uint8_t *>(component) + kNameIndexAt);
}

const Bindings &BindingsOf(RED4ext::ent::SlotComponent *component) {
  return *reinterpret_cast<const Bindings *>(reinterpret_cast<std::uint8_t *>(component) +
                                             kBindingsAt);
}

// Why the component's name index is not the one measured ("": it is): every
// slot's name must lead to a slot of that name, the map's own fields must
// agree, and its allocator must be the game's. A component never loaded (a
// script's Clone: its properties copied, nothing built) has an empty index,
// with its allocator: `empty` says so, and the caller enters every slot.
std::string IndexFault(RED4ext::ent::SlotComponent *component, bool &empty) {
  const auto &slots = component->slots;
  auto &index = IndexOf(component);
  empty = false;
  if (!InGameImage(index.allocator))
    return "its name index has no allocator of the game's";
  if (index.capacity == 0 && index.size == 0 && !index.indexTable) {
    empty = true;
    return {};
  }
  if (slots.Size() == 0)
    return "it has no slots of its own";
  if (!index.indexTable || !index.nodeList.nodes || index.size > index.capacity ||
      index.size > slots.Size() || index.nodeList.stride != sizeof(NameIndex::Node) ||
      index.nodeList.capacity < index.capacity)
    return std::format("its name index is not laid out as measured (size {}, capacity {}, stride "
                       "{}, {} slots)",
                       index.size, index.capacity, index.nodeList.stride, slots.Size());
  for (std::uint32_t i = 0; i < slots.Size(); ++i) {
    const auto *found = index.Get(slots.Data()[i].slotName);
    if (!found || *found >= slots.Size() ||
        slots.Data()[*found].slotName != slots.Data()[i].slotName)
      return std::format("its name index does not lead to its slot {}",
                         slots.Data()[i].slotName.ToString());
  }
  return {};
}

bool Finite(const BodySlot &slot) {
  for (const float value : {slot.x, slot.y, slot.z, slot.i, slot.j, slot.k, slot.r})
    if (!std::isfinite(value))
      return false;
  return true;
}
} // namespace

bool CheckBodySlots() {
  bool ok = ClassSizeIs(feature::kBodySlots, "entSlotComponent", kComponentSize);
  ok = PropertyAt(feature::kBodySlots, "entSlotComponent", "slots", kSlotsAt) && ok;
  ok = PropertyAt(feature::kBodySlots, "entSlotComponent", "fallbackSlots", kFallbackSlotsAt) && ok;
  ok = ClassSizeIs(feature::kBodySlots, "entSlot", 0x30) && ok;
  ok = PropertyAt(feature::kBodySlots, "entSlot", "slotName", 0x00) && ok;
  ok = PropertyAt(feature::kBodySlots, "entSlot", "boneName", 0x08) && ok;
  ok = PropertyAt(feature::kBodySlots, "entSlot", "relativePosition", 0x10) && ok;
  ok = PropertyAt(feature::kBodySlots, "entSlot", "relativeRotation", 0x20) && ok;
  if (ok)
    FeatureOn(feature::kBodySlots);
  return ok;
}

std::string AddSlot(RED4ext::ent::IComponent *component, const BodySlot &slot) {
  if (!FeatureIsOn(feature::kBodySlots))
    return "the body slots feature is off";
  auto *slots = AsSlotComponent(component);
  if (!slots)
    return "not a slot component";
  if (slot.name == 0 || slot.bone == 0 || !Finite(slot))
    return "no slot to add (no name, no bone or a transform that is not finite)";
  // Attached: each slot has its binding, and one past them would be read.
  if (BindingsOf(slots).size != 0)
    return "its component is attached already";
  bool empty = false;
  if (auto fault = IndexFault(slots, empty); !fault.empty())
    return fault;
  const RED4ext::CName name(slot.name);
  auto &index = IndexOf(slots);
  // Never built: its slots first, as the game builds it (0x1403F7810: each
  // name once, the first slot of that name).
  if (empty)
    for (std::uint32_t i = 0; i < slots->slots.Size(); ++i)
      index.Insert(slots->slots.Data()[i].slotName, i);
  if (index.Get(name))
    return {};
  if (slots->slots.Size() >= 0xFFFF)
    return "it has too many slots";
  RED4ext::ent::Slot added{};
  added.slotName = name;
  added.boneName = RED4ext::CName(slot.bone);
  added.relativePosition.X = slot.x;
  added.relativePosition.Y = slot.y;
  added.relativePosition.Z = slot.z;
  added.relativeRotation.i = slot.i;
  added.relativeRotation.j = slot.j;
  added.relativeRotation.k = slot.k;
  added.relativeRotation.r = slot.r;
  const std::uint32_t at = slots->slots.Size();
  slots->slots.EmplaceBack(added);
  index.Insert(name, at);
  log::Debug("body slots: {} added on {} (slot {}, {} in its index{})", name.ToString(),
             added.boneName.ToString(), at, index.size, empty ? ", built" : "");
  return {};
}

std::string Rename(RED4ext::ent::IComponent *component, std::uint64_t name, std::uint64_t id) {
  if (!FeatureIsOn(feature::kBodySlots))
    return "the body slots feature is off";
  auto *slots = AsSlotComponent(component);
  if (!slots)
    return "not a slot component";
  if (name == 0 || id == 0)
    return "no name or no id";
  if (BindingsOf(slots).size != 0)
    return "its component is attached already";
  auto *type = component->GetType();
  auto *nameProperty = type ? type->GetProperty("name") : nullptr;
  auto *idProperty = type ? type->GetProperty("id") : nullptr;
  if (!nameProperty || !idProperty || !nameProperty->type || !idProperty->type ||
      nameProperty->type->GetSize() != sizeof(RED4ext::CName) || idProperty->type->GetSize() != 8)
    return "entIComponent's name or id is not as measured";
  const RED4ext::CName newName(name);
  nameProperty->SetValue(component, newName);
  idProperty->SetValue<std::uint64_t>(component, id);
  return {};
}
} // namespace tandem::plugin::engine
