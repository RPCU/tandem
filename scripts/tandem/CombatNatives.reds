// Hit outcomes' natives (plugin/src/combat_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native struct TandemFightEvent {
  // 1 (host) a guest's strike on our NPC (npc, player, damage, poise,
  // minHealth, flags: proto::kStrike*, attackType, zone, part, shape:
  // the hit shape's EHitReactionZone (255 none), EAIDismembermentBodyPart,
  // EHitShapeType; attack); 2 a host NPC's wound (npc, player: whose hit,
  // part: gameDismBodyPart, shape: gameDismWoundType, x y z); 3 a host NPC's
  // poise (npc, poise 0..1); 4 the session's difficulty (level:
  // gameDifficulty); 5 another V's hit on our V (player: whose,
  // physical..chemical, attackType, flags: proto::kHit*, attack, weapon);
  // 6 another V's throw (player, id, level: 0 grenade 1 knife 2 launcher
  // round 3 smart round, flags: proto::kThrow*, attack: the item, x y z the
  // origin, dx dy dz the velocity, npc / target: the NPC / V it homes on);
  // 7 its end (player, id, level: 0 exploded 1 stuck 2 gone, x y z); 8 the
  // session's rules (flags: proto::kRule*; level: the health percent per
  // extra player of enemy scaling); 9 (host) a guest blocked (level 1) or
  // parried (2) our NPC's attack (player, npc).
  // 10 the session's level (level: the mean of its players' levels; player:
  // how many they are); 11 (guest) a host NPC's power level (npc; damage:
  // the level). Levels.reds takes the last two. 12 an owner's word on one
  // of its NPCs: how many players it is scaled for (npc, player: the
  // count; Scaling.reds). 13 where another V's throw is now (player, id,
  // x y z, dx dy dz its velocity: zero when it lies still).
  native let kind: Int32;
  native let npc: Uint32;
  native let player: Uint32;
  native let damage: Float;
  native let poise: Float;
  native let minHealth: Float;
  native let flags: Int32;
  native let attackType: Int32;
  native let zone: Int32;
  native let part: Int32;
  native let shape: Int32;
  native let level: Int32;
  native let attack: TweakDBID;
  native let weapon: TweakDBID;
  native let physical: Float;
  native let thermal: Float;
  native let electric: Float;
  native let chemical: Float;
  // where it landed, and its direction (plain floats: no Vector4 in a native struct)
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
  native let id: Uint32;
  native let target: Uint32;
}

public native class TandemFight extends IScriptable {
  public static native func SendStrike(npc: Uint32, damage: Float, poise: Float, minHealth: Float, flags: Int32, attackType: Int32, zone: Int32, part: Int32, shape: Int32, attack: TweakDBID, position: Vector4, direction: Vector4)
  public static native func SendWound(npc: Uint32, player: Uint32, part: Int32, wound: Int32, position: Vector4)
  public static native func SendPoise(npc: Uint32, poise: Float)
  public static native func SendDifficulty(level: Int32)
  public static native func SendAreaHit(player: Uint32, physical: Float, thermal: Float, electric: Float, chemical: Float, attackType: Int32, flags: Int32, position: Vector4, direction: Vector4, attack: TweakDBID, weapon: TweakDBID)
  public static native func SendThrow(id: Uint32, kind: Int32, flags: Int32, item: TweakDBID, origin: Vector4, velocity: Vector4, npc: Uint32, at: Uint32)
  public static native func SendThrowEnd(id: Uint32, how: Int32, position: Vector4)
  // Where a throw of ours is now (a few times a second in flight); and what a copy at `copy` does about the real one's word: 0
  // nothing, 1 lie there, 2 fly on from there.
  public static native func SendThrowPath(id: Uint32, position: Vector4, velocity: Vector4)
  public static native func PathFix(copy: Vector4, position: Vector4, velocity: Vector4) -> Int32
  // ConfiguredRules' word: proto::kRule* in the low byte, the health percent
  // per extra player (enemy scaling) in the next.
  public static native func SendRules(rules: Int32)
  public static native func ConfiguredRules() -> Int32
  public static native func FriendlyFire() -> Bool
  public static native func SendDefended(npc: Uint32, how: Int32)
  public static native func MeleeHold(npc: Uint32) -> Float
  public static native func StaleThrows() -> Int32
  public static native func TakeEvents() -> array<TandemFightEvent>
  public static native func StrikeLoss(health: Float, maxHealth: Float, damage: Float, flags: Int32, minHealth: Float, immortal: Bool) -> Float
}
