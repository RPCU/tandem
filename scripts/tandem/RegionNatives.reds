// Per-player regions' natives (plugin/src/region_natives.cpp). Keep the two
// in sync: tools/check-natives.py checks them with Natives.reds.

// One thing to do about who runs an NPC (docs/architecture/regions.md):
// 1 give `npc` to `player` (answer Give or Refuse); 2 `npc` is ours now, from
// `player`, with the state below (answer Adopt); 3 `npc` is `player`'s now
// (0: it left the session), `was` before; 4 the players linked with us changed.
public native struct TandemRegionEvent {
  native let kind: Int32;
  native let npc: Uint32;
  native let player: Uint32;
  native let was: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
  native let health: Float;
  native let poise: Float;
  native let flags: Int32;
  native let target: Uint32;
  native let hostileTo: Int32;
  native let highLevel: Int32;
  native let effects: Int32;
}

// One status effect of an NPC taken over.
public native struct TandemRegionEffect {
  native let record: TweakDBID;
  native let remaining: Float;
  native let stacks: Int32;
}

// The region of a linked player that outranks us (regions.md §6): where its
// V is. Its game populates the world there; ours holds its own back.
public native struct TandemRegionCover {
  native let player: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

// One change to the crowd's null areas: put `player`'s at (x, y, z), its old
// one going (place), or take it away.
public native struct TandemNullStep {
  native let player: Uint32;
  native let place: Bool;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

// A placed NPC that died in the session (the ledger, regions.md §14): its
// static entity, what it was, where it fell, whose game ran it, whether it
// is knocked out (not dead) and how long ago (seconds).
public native struct TandemDeath {
  native let entity: EntityID;
  native let record: TweakDBID;
  native let appearance: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
  native let by: Uint32;
  native let knockedOut: Bool;
  native let age: Float;
}

public native class TandemRegions extends IScriptable {
  public static native func TakeEvents() -> array<TandemRegionEvent>
  // The status effects that came with the last event 2 for this NPC.
  public static native func EffectsOf(npc: Uint32) -> array<TandemRegionEffect>
  // Who runs an NPC of the session (0: not one of them).
  public static native func OwnerOf(npc: Uint32) -> Uint32
  // The players whose region meets ours: we show their NPCs, they show ours.
  public static native func IsLinked(player: Uint32) -> Bool
  public static native func Linked() -> array<Uint32>
  // Grouped or apart, for quests: apart, our world is our own (linked with
  // nobody); grouped, the leader's world is the one played (0: no session).
  public static native func IsApart() -> Bool
  public static native func Leader() -> Uint32
  // A player that holds its region (a gig of its own world runs where its V
  // stands) leads its group, ranks first there and keeps its NPCs. Hold says
  // it of our own world.
  public static native func Holds(player: Uint32) -> Bool
  public static native func Hold(held: Bool)
  // The game that runs the world around `player`: its new NPCs, its crowd,
  // its traffic and police (0: no session yet); OwnsArea: ours, around our
  // own V. Its own game's, unless that V is deep in a higher-ranked linked
  // player's region (docs/architecture/regions.md, the area owner).
  public static native func AreaOwner(player: Uint32) -> Uint32
  public static native func OwnsArea() -> Bool
  // The linked player that outranks us and whose V is within `radius` of
  // this point (0: none). 2147483648 + player: one that outranks us has not
  // been placed yet: wait before deciding.
  public static native func CoveredBy(x: Float, y: Float, z: Float, radius: Float) -> Uint32
  // The overlap (regions.md §6). Covers: the regions of the linked players
  // that outrank us. CrowdShare: the share of our crowd we keep with our V
  // there (1 all, 0 none; `quiet`: at none now). NullSteps: what to change
  // about the crowd's null areas now (`on` false: every area goes).
  public static native func Covers() -> array<TandemRegionCover>
  public static native func CrowdShare(x: Float, y: Float, z: Float, quiet: Bool) -> Float
  public static native func NullSteps(now: Float, on: Bool) -> array<TandemNullStep>
  // Placed NPCs, the death ledger, twins (regions.md §3, §14). Placed: one
  // of our NPCs is placed in the world (its static entity id). PlacedOf: the
  // entity an NPC's owner named (undefined: none). DiedBy: the session says
  // that entity's NPC died, in that player's game (0: it lives). TakeDeaths: the deaths said since the last call.
  // DeathsNear: how many lie within `radius`. Bodies: the deaths whose body
  // we show with our V there (`shown`: the ones we show now).
  public static native func Placed(npc: Uint32, entity: EntityID)
  public static native func PlacedOf(npc: Uint32) -> EntityID
  public static native func DiedBy(entity: EntityID) -> Uint32
  public static native func TakeDeaths() -> array<TandemDeath>
  public static native func DeathsNear(x: Float, y: Float, z: Float, radius: Float) -> Int32
  public static native func Bodies(x: Float, y: Float, z: Float, shown: array<EntityID>) -> array<TandemDeath>
  // For each proxy that came, the index of our own NPC that is the same
  // person (-1: none), one to one (tandem/twins.hpp).
  public static native func MatchTwins(theirRecords: array<TweakDBID>, theirPlaces: array<Vector4>, theirEntities: array<EntityID>, ourRecords: array<TweakDBID>, ourPlaces: array<Vector4>, ourEntities: array<EntityID>, ourBusy: array<Bool>) -> array<Int32>
  // One of our NPCs never changes owner.
  public static native func Pin(npc: Uint32)
  // Our answers to event 1 and to event 2.
  public static native func Give(npc: Uint32, to: Uint32, x: Float, y: Float, z: Float, yaw: Float, health: Float, poise: Float, flags: Int32, target: Uint32, hostileTo: Int32, highLevel: Int32, effects: array<TweakDBID>, remaining: array<Float>, stacks: array<Int32>)
  public static native func Refuse(npc: Uint32)
  public static native func Adopt(npc: Uint32, accepted: Bool)
}
