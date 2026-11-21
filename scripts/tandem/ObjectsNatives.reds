// The world's objects' natives (plugin/src/objects_natives.cpp). Keep the two
// in sync: tools/check-natives.py checks them with Natives.reds.

// Something of the world for our game to show (Objects.reds).
public native struct TandemObjectEvent {
  // 1 something broke in another game, 2 a device's fire, 3 a turret in
  // another player's hands
  native let kind: Int32;
  // 1: whose game saw it break; 2 (seen, lost): whose V; 3: whose hands
  native let player: Uint32;
  // 1: the placed entity it is a part of (none: the world's); 2 and 3: the device
  native let entity: EntityID;
  // 1: its destruction component
  native let name: CName;
  // where (3: where it points; plain floats: the script VM aligns struct
  // locals to 8)
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // 1: the level of destruction; 2: 0 idle, 1 aim, 2 shot, 3 seen, 4 lost;
  // 3: 1 firing, 2 let go
  native let value: Int32;
  // 2: 0 none, 1 a shared NPC, 2 a placed entity, 3 a player
  native let targetKind: Int32;
  // 2: the NPC, or the player
  native let target: Uint32;
  // 2: the placed entity
  native let targetEntity: EntityID;
}

public native class TandemObjects extends IScriptable {
  // Something broke in our game: a destruction component of a placed entity
  // (entity and name), or a piece of the world at a place. True: the others
  // are told (false: the echo of a break we replayed, or told already).
  public static native func Broke(entity: EntityID, name: CName, position: Vector4, level: Int32) -> Bool
  // Another game's break that waited for our V, replayed now: its echo is
  // not told.
  public static native func Replaying(entity: EntityID, name: CName, position: Vector4)
  // A device's fire. Host: our turret turned to a target (kind 1), shot at
  // it (2), follows nobody (0); the target a shared NPC, a player, a placed
  // entity, else the place. Guest: our copy locked our own V (3), lost it (4).
  public static native func Fire(entity: EntityID, kind: Int32, npc: Uint32, player: Uint32, target: EntityID, position: Vector4)
  // A turret in our V's hands: where it points, whether it fires (flags 1),
  // or that we let go of it (2).
  public static native func Hands(entity: EntityID, position: Vector4, flags: Int32)
  // What the others' games say since the last call.
  public static native func TakeEvents() -> array<TandemObjectEvent>
  // A new session, or its end: nothing broken is remembered.
  public static native func Reset()
  public static native func Report() -> String
}
