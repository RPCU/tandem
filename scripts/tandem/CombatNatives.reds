// Hit outcomes' natives (plugin/src/combat_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native struct TandemFightEvent {
  // 1 (host) a guest's strike on our NPC (npc, player, damage, poise,
  // minHealth, flags: proto::kStrike*, attackType, zone, part, shape:
  // the hit shape's EHitReactionZone (255 none), EAIDismembermentBodyPart,
  // EHitShapeType; attack); 2 a host NPC's wound (npc, player: whose hit,
  // part: gameDismBodyPart, shape: gameDismWoundType, x y z); 3 a host NPC's
  // poise (npc, poise 0..1); 4 the session's difficulty (level:
  // gameDifficulty); 5 another V's area effect on our V (player: whose,
  // physical..chemical, attackType, flags: proto::kHit*, attack, weapon);
  // 6 the session's level (level: the mean of its players' levels; player:
  // how many they are); 7 (guest) a host NPC's power level (npc; damage:
  // the level). Levels.reds takes the last two.
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
}

public native class TandemFight extends IScriptable {
  public static native func SendStrike(npc: Uint32, damage: Float, poise: Float, minHealth: Float, flags: Int32, attackType: Int32, zone: Int32, part: Int32, shape: Int32, attack: TweakDBID, position: Vector4, direction: Vector4)
  public static native func SendWound(npc: Uint32, player: Uint32, part: Int32, wound: Int32, position: Vector4)
  public static native func SendPoise(npc: Uint32, poise: Float)
  public static native func SendDifficulty(level: Int32)
  public static native func SendAreaHit(player: Uint32, physical: Float, thermal: Float, electric: Float, chemical: Float, attackType: Int32, flags: Int32, position: Vector4, direction: Vector4, attack: TweakDBID, weapon: TweakDBID)
  public static native func TakeEvents() -> array<TandemFightEvent>
  public static native func StrikeLoss(health: Float, maxHealth: Float, damage: Float, flags: Int32, minHealth: Float, immortal: Bool) -> Float
}
