// What a script may ask of a body being built (Body.reds' Entity/Extract;
// not called yet, §13.46): `TandemBody`
// static functions (engine/slots.hpp; docs/research/engine/stealth.md
// §13.46). Declared on the script side in scripts/tandem/BodyNatives.reds:
// keep the two in sync (tools/check-natives.py reads every pair). Called from
// Codeware's Entity/Extract, on the builder's job thread: it touches only the
// component passed, which that builder owns until the entity is attached.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "engine/slots.hpp"

#include <RED4ext/Scripting/Natives/Generated/ent/IComponent.hpp>
#include <RedLib.hpp>

namespace tandem::plugin {
struct TandemBody : Red::IScriptable {
  // Adds the slot `name`, on `bone` at `offset` from it (no rotation), to a
  // slot component of an entity being built, where GetSlotTransform and the
  // mounting facility find it. "" when done (or it had the slot already),
  // else why not (nothing changed).
  static Red::CString AddSlot(Red::Handle<Red::ent::IComponent> component, Red::CName name,
                              Red::CName bone, Red::Vector4 offset) {
    const engine::BodySlot slot{
        .name = name.hash, .bone = bone.hash, .x = offset.X, .y = offset.Y, .z = offset.Z};
    return Red::CString(engine::AddSlot(component.GetPtr(), slot).c_str());
  }

  // Names a slot component that is not attached yet and gives it an id of
  // its own (a Clone of another, to be added to the entity being built).
  // "" when done, else why not (nothing changed).
  static Red::CString RenameSlots(Red::Handle<Red::ent::IComponent> component, Red::CName name,
                                  std::uint64_t id) {
    return Red::CString(engine::Rename(component.GetPtr(), name.hash, id).c_str());
  }

  RTTI_IMPL_TYPEINFO(TandemBody);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemBody, "TandemBody", {
  RTTI_METHOD(AddSlot);
  RTTI_METHOD(RenameSlots);
});
