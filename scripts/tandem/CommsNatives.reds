// The comms natives (plugin/src/comms_natives.cpp). Keep the two in sync:
// tools/check-natives.py checks them with Natives.reds.

// One change to make on screen (kind 1 show a ping, 2 hide it, 3 outline a
// linked player's tag or reveal, 4 stop, 5 our link with `player` changed,
// 6 `player`'s waypoint).
public native struct TandemCommsEvent {
  native let kind: Int32;
  native let player: Uint32;
  // pings: the sender's id for it
  native let id: Uint32;
  // pings: 1 spot, 2 enemy, 3 device, 4 loot, 5 vehicle, 6 danger, 7 team
  // target; tags: 0
  // scan, 1 reveal; links: 0 none, 1 we asked, 2 they ask, 3 linked (before:
  // the state it was); waypoints: 0 none, 1 a map pin, 2 a tracked point
  native let type: Int32;
  native let before: Int32;
  // 0 none, 1 a shared NPC, 2 a placed entity, 3 a player
  native let targetKind: Int32;
  // 1: its NpcId; 3: the player
  native let npc: Uint32;
  // 2: the entity (the same in every game)
  native let entity: EntityID;
  // Where (plain floats: the script VM aligns struct locals to 8).
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // pings: how long it shows; reveals: how long (0: until off)
  native let seconds: Float;
}

public native class TandemCommsNative extends IScriptable {
  public static native func Update() -> array<TandemCommsEvent>
  // Our ping at a shared NPC, a player or a static entity (all 0: a place):
  // 0 not sent (too fast, pings off, no session), 1 marked, 2 made a danger,
  // 3 taken back.
  public static native func Ping(kind: Int32, npc: Uint32, player: Uint32, entity: EntityID, position: Vector4) -> Int32
  // Our team target ("Mark target"): 0 not sent, 1 marked, 3 cleared (the
  // same target again); ClearTarget: it died (false: none).
  public static native func MarkTarget(npc: Uint32, player: Uint32, entity: EntityID, position: Vector4) -> Int32
  public static native func ClearTarget() -> Bool
  // Our scan tag (reason 0) or reveal (1) of a shared NPC or a static
  // entity, to linked players.
  public static native func Tag(reason: Int32, on: Bool, seconds: Int32, npc: Uint32, entity: EntityID)
  public static native func SetLink(player: Uint32, on: Bool) -> Bool
  public static native func LinkState(player: Uint32) -> Int32
  public static native func AnyLinked() -> Bool
  // tandem.ini: link (0 auto, 1 ask, 2 off), pings
  public static native func LinkSetting() -> Int32
  public static native func PingsOn() -> Bool
  public static native func SetWaypoint(kind: Int32, position: Vector4)
  public static native func Report() -> String
}
