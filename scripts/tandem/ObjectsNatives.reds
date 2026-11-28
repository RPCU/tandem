// The world's objects' natives (plugin/src/objects_natives.cpp). Keep the two
// in sync: tools/check-natives.py checks them with Natives.reds.

// Something of the world for our game to show (Objects.reds).
public native struct TandemObjectEvent {
  // 1 something broke in another game, 2 a device's fire, 3 a turret in
  // another player's hands, 4 our claim on a turret refused (another player
  // holds it)
  native let kind: Int32;
  // 1: whose game saw it break; 2 (seen, lost): whose V; 3: whose hands;
  // 4: who holds it
  native let player: Uint32;
  // 1: the placed entity it is a part of (none: the world's); 2 to 4: the device
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

// A physics prop for our game to show (Props.reds; docs/architecture/props.md).
public native struct TandemPropEvent {
  // 1 another game's prop starts to move, 2 a prop came to rest (another
  // game's word, or the record round our V), 3 another V pushed a prop our
  // game runs
  native let kind: Int32;
  // 1, 2: whose game; 3: whose V
  native let player: Uint32;
  native let entity: EntityID;
  // its body's index among the entity's physical components
  native let part: Int32;
  // 2: where it rests; 3: where the pusher's instance stood (plain floats:
  // the script VM aligns struct locals to 8)
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // 2: its pose
  native let qi: Float;
  native let qj: Float;
  native let qk: Float;
  native let qr: Float;
  // 3: the velocity the push gave it
  native let vx: Float;
  native let vy: Float;
  native let vz: Float;
}

// Where another game's moving prop is drawn now.
public native struct TandemPropPose {
  // false: no word for it lately (its game went quiet: let it go)
  native let found: Bool;
  native let owner: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let qi: Float;
  native let qj: Float;
  native let qk: Float;
  native let qr: Float;
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
  // A new session, or its end: nothing broken is remembered (the queue of
  // what the others said is kept: the net thread empties it between sessions).
  public static native func Reset()
  public static native func Report() -> String
  // (the parity dump) the session's record of what broke: its pieces and
  // digest, the same on every game that heard it all.
  public static native func Record() -> String
  // ---- physics props (Props.reds) ----
  // A prop our game runs moves: its state for this round.
  public static native func PropMoving(entity: EntityID, part: Int32, position: Vector4, orientation: Quaternion, velocity: Vector4)
  // The round's states to the others, nearest to our V first. True: some went.
  public static native func PropsRound(ours: Vector4) -> Bool
  // A prop our game runs came to rest: the session's record.
  public static native func PropRested(entity: EntityID, part: Int32, position: Vector4, orientation: Quaternion)
  // Our V pushed our instance of a prop another game runs.
  public static native func PropPushed(entity: EntityID, part: Int32, position: Vector4, velocity: Vector4)
  // What the others said of props since the last call.
  public static native func TakeProps() -> array<TandemPropEvent>
  // Where another game's moving prop is drawn now (not found: let it go).
  public static native func PropDrawn(entity: EntityID, part: Int32) -> TandemPropPose
  // Another game moves this prop now.
  public static native func PropTheirs(entity: EntityID, part: Int32) -> Bool
  // The record's rests within `radius` of `position` (at most 32).
  public static native func PropsAround(position: Vector4, radius: Float) -> array<TandemPropEvent>
  // The body's own velocity, by its RTTI setter. False: not there.
  public static native func SetBodyVelocity(body: ref<IScriptable>, velocity: Vector4) -> Bool
  // A placed entity's id from its hex (EntityHex's): the tests' commands.
  public static native func EntityFromHex(text: String) -> EntityID
  public static native func PropsReport() -> String
}
