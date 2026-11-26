// Presence natives (plugin/src/presence_natives.cpp). Keep the two in sync:
// tools/check-natives.py checks them with Natives.reds.

public native struct TandemHeard {
  // presence::Kind: 1 step, 2 jump, 3 land, 4 draw, 5 holster, 6 hurt,
  // 7 down, 8 up, 9 ability on, 10 ability off, 11 climb, 12 vault, 13 dash,
  // 14 slide end
  native let kind: Int32;
  // step: gait (0 sneak, 1 walk, 2 run, 3 sprint) + 16 left foot + 32 ladder;
  // land and hurt: 0..2 how hard; abilities: proto::kTime* bits
  native let arg: Int32;
}

public native struct TandemCue {
  native let player: Uint32;
  // proto::CueKind: 1 a shot's end, 2 a melee swing landed
  native let kind: Int32;
  // 1 left hand, 2 it ended on a character, 4 it hit nothing
  native let arg: Int32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

// A hit another player's V took, as its game ran it (proto::HitTaken).
public native struct TandemHitTaken {
  native let player: Uint32;
  // proto::HitBy: 0 nobody we know, 1 a shared NPC, 2 a player
  native let by: Int32;
  native let attacker: Uint32;
  native let attack: TweakDBID;
  // gamedataAttackType
  native let attackType: Int32;
  // 1 melee, 2 it staggered the V, 4 it knocked it down, 8 critical
  native let flags: Int32;
  // EHitReactionZone and EHitShapeType of the hit shape, -1: none
  native let zone: Int32;
  native let shape: Int32;
  // the health it took, 0..1 of the V's whole
  native let share: Float;
  // where it landed, from the V's position; the way it went
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
}

public native struct TandemNpcReaction {
  native let npc: Uint32;
  native let by: Uint32;
  // animHitReactionType, or 0: its ragdoll ended
  native let type: Int32;
  native let direction: Int32;
  native let intensity: Int32;
  native let bodyPart: Int32;
  native let variation: Int32;
  native let source: Int32;
  native let stance: Int32;
  native let moveSpeed: Int32;
  native let moveDirection: Int32;
  native let turns: Bool;
  native let angle: Float;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

// A voice line the game started on one of our entities (NpcVoices.reds).
public native struct TandemLocalVoice {
  native let entity: EntityID;
  native let context: CName;
  // seconds
  native let delay: Float;
  native let inVehicle: Bool;
}

// A voice line of an NPC we show (proto::NpcVoice).
public native struct TandemNpcVoice {
  native let npc: Uint32;
  native let context: CName;
  // seconds
  native let delay: Float;
  native let inVehicle: Bool;
}

// A body at rest another game told (proto::CorpsePose).
public native struct TandemCorpse {
  native let npc: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // radians
  native let yaw: Float;
}

public native struct TandemBearing {
  native let distance: Float;
  // radians: 0 ahead, positive to the right
  native let angle: Float;
  native let pitch: Float;
  native let inView: Bool;
  // 0 ahead, 1 right, 2 behind, 3 left
  native let side: Int32;
}

public native struct TandemScreenPoint {
  // from the top left corner, 0..1
  native let x: Float;
  native let y: Float;
  native let visible: Bool;
}

public native class TandemPresence extends IScriptable {
  public static native func Send(health: Float, flags: Int32) -> Bool;
  public static native func Health(player: Uint32) -> Float;
  public static native func Flags(player: Uint32) -> Int32;
  public static native func SendCue(kind: Int32, arg: Int32, at: Vector4) -> Void;
  public static native func TakeCues() -> array<TandemCue>;
  public static native func SendHitTaken(by: Int32, attacker: Uint32, attack: TweakDBID, attackType: Int32, flags: Int32, zone: Int32, shape: Int32, share: Float, offset: Vector4, direction: Vector4) -> Void;
  public static native func TakeHitsTaken() -> array<TandemHitTaken>;
  public static native func Observe(player: Uint32, position: Vector4, dt: Float) -> array<TandemHeard>;
  public static native func Forget(player: Uint32) -> Void;
  public static native func Flinch(player: Uint32, strength: Float, direction: Vector4, forward: Vector4) -> Void;
  public static native func SendNpcReaction(npc: Uint32, by: Uint32, type: Int32, direction: Int32, intensity: Int32, bodyPart: Int32, variation: Int32, source: Int32, stance: Int32, moveSpeed: Int32, moveDirection: Int32, turns: Bool, angle: Float, way: Vector4) -> Void;
  public static native func TakeNpcReactions() -> array<TandemNpcReaction>;
  // NPC voices (NpcVoices.reds). Any thread: QueueNpcVoice only queues,
  // IsNpcProxy only reads; the others on the game thread.
  public static native func QueueNpcVoice(entity: EntityID, context: CName, delay: Float, inVehicle: Bool) -> Void;
  public static native func IsNpcProxy(entity: EntityID) -> Bool;
  public static native func TakeLocalNpcVoices() -> array<TandemLocalVoice>;
  public static native func SendNpcVoice(npc: Uint32, context: CName, delay: Float, inVehicle: Bool) -> Void;
  public static native func TakeNpcVoices() -> array<TandemNpcVoice>;
  // Bodies at rest (Corpses.reds). Owner: read a body's pose once, send it
  // with its place when read, or let the read go. Every other game: the
  // bodies told, and a copy drawn in its owner's pose (null: let go).
  public static native func ReadCorpse(npc: Uint32, entity: ref<Entity>) -> Void;
  public static native func SendCorpse(npc: Uint32, position: Vector4, yaw: Float) -> Bool;
  public static native func StopCorpseRead(npc: Uint32) -> Void;
  public static native func TakeCorpses() -> array<TandemCorpse>;
  public static native func BindCorpse(npc: Uint32, entity: ref<Entity>) -> Void;
  public static native func Status(life: Int32, place: Int32, tier: Int32, storyFlags: Int32, paused: Bool, flags: Int32) -> Int32;
  public static native func Bearing(camera: Vector4, forward: Vector4, halfFov: Float, target: Vector4) -> TandemBearing;
  public static native func Project(camera: Vector4, forward: Vector4, right: Vector4, halfFov: Float, vertical: Bool, aspect: Float, target: Vector4) -> TandemScreenPoint;
  public static native func RoundedMetres(distance: Float) -> Int32;
  // The nameplate at `distance`, by tandem.ini nameplates (0 near, 1 always,
  // 2 off): its opacity (0: not shown) and its scale.
  public static native func NameplateOpacity(distance: Float, mode: Int32) -> Float;
  public static native func NameplateScale(distance: Float, mode: Int32) -> Float;
  // Our V's status effects that show on it (any order): true when the set
  // changed and goes out. Another player's set as last heard (sorted), and a
  // count that moves on every change of anyone's.
  public static native func SendStatusVisuals(records: array<TweakDBID>) -> Bool;
  public static native func StatusVisualsOf(player: Uint32) -> array<TweakDBID>;
  public static native func StatusChanges() -> Uint32;
}
