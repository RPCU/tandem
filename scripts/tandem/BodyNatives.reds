// What a body being built asks of the plugin (plugin/src/body_natives.cpp,
// engine/slots.hpp). Keep the two in sync: tools/check-natives.py checks
// them with Natives.reds. Called from Codeware's Entity/Extract (a job
// thread): it touches only the component it is given.

public native class TandemBody extends IScriptable {
  // Adds the slot `name`, on `bone` at `offset` from it (no rotation), to a
  // slot component of an entity being built, where GetSlotTransform and the
  // mounting facility find it: "" when done (or it had that slot already),
  // else why not (nothing changed; the feature "body slots" off says so).
  public static native func AddSlot(component: ref<IComponent>, name: CName, bone: CName, offset: Vector4) -> String
  // Names a slot component that is not attached yet and gives it an id of
  // its own (a Clone of another, to be added to the entity being built):
  // "" when done, else why not (nothing changed).
  public static native func RenameSlots(component: ref<IComponent>, name: CName, id: Uint64) -> String
}
