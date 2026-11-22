#pragma once

// A slot added to an entity's slot component while its entity is built
// (docs/research/engine/stealth.md §13.46, game 2.50): vanilla mounts a
// grappled NPC on its holder's `grapple` slot, which neither V's templates
// nor an NPC's have. The ground for giving a puppet's body one; no script
// calls it yet (§13.46: where the mount attaches, and what is left).

#include <cstdint>
#include <string>

namespace RED4ext::ent {
struct IComponent;
}

namespace tandem::plugin::engine {
// A slot, as entSlot holds it: a name, the bone it follows and the
// transform relative to that bone.
struct BodySlot {
  std::uint64_t name = 0;
  std::uint64_t bone = 0;
  float x = 0.f, y = 0.f, z = 0.f;
  float i = 0.f, j = 0.f, k = 0.f, r = 1.f;
};

// The startup check (engine/access.cpp): entSlotComponent and entSlot laid
// out as measured; the feature "body slots" is on or off after it.
bool CheckBodySlots();

// Appends a slot to a slot component that is not attached yet (an entity
// being built: Codeware's Entity/Extract) and enters its name in the
// component's name index, which GetSlotTransform and the mounting facility
// look slots up by. "" when done (or the component has that slot already),
// else why not; nothing is changed then. The caller owns the component (its
// builder's thread).
std::string AddSlot(RED4ext::ent::IComponent *component, const BodySlot &slot);

// Gives a slot component that is not attached yet a name and an id (a
// script's Clone of another, added to the entity being built as a component
// of its own). "" when done, else why not.
std::string Rename(RED4ext::ent::IComponent *component, std::uint64_t name, std::uint64_t id);
} // namespace tandem::plugin::engine
