// Whom an NPC attacks: a guest's V is a player to it (docs/architecture/npcs.md,
// "Whom NPCs attack"; engine-knowledge §13.123). Vanilla weighs each hostile
// threat of an NPC in AIActionTarget, and only the engine's player-controlled
// flag makes it a player's weight: 1.0 instead of an NPC's 0.01
// (RegularThreatCalculation), 2.0 (BossThreatCalculation), and only then does
// a BlockTargetingPlayer status effect (a quest's "do not target the player")
// take it off the list (UpdateThreatsValue, SetTopThreatToCombatTarget). A
// remote player's puppet has the flag off, so the NPCs of the game that runs
// them weighed it as one of theirs and picked our V, or anyone, first.
//
// Here a puppet is weighed as a player wherever an NPC's threats are weighed:
// the same base value and the same terms after it, and the same block. The
// game that runs an NPC weighs its threats (the host, or a guest that owns
// it under regions): a proxy's brain is off, so nothing here runs for it,
// and the proxy tag is checked as well. MadnessThreatCalculation has no
// player term: a mad NPC weighs everyone alike.
//
// Every wrap here runs on the AI's job threads: it reads, calls the wrapped
// method and changes only its own out value or, as the wrapped method does,
// the NPC's own target tracker. A puppet is known by its spawn tag
// (Codeware's DynamicEntitySystem reads its tags under a lock), never by
// TandemSystem's puppet list, which the game thread changes.

public abstract class TandemThreats {
  // A remote player's puppet in our world (Body.reds spawns them tagged).
  public static func IsPuppet(object: wref<GameObject>) -> Bool {
    if !IsDefined(object) {
      return false;
    }
    let id = object.GetEntityID();
    return EntityID.IsDynamic(id) && GameInstance.GetDynamicEntitySystem().IsTagged(id, n"Tandem.Puppet");
  }

  // The NPC weighs this threat as a player though the engine's flag says
  // not: a puppet, in a session, for an NPC our game runs. Off while the
  // test command says "npc threats npc" (vanilla's weights, to compare).
  public static func AsPlayer(owner: wref<GameObject>, threat: wref<GameObject>) -> Bool {
    if !TandemNative.IsStarted() || !IsDefined(owner) || !IsDefined(threat) || threat.IsPlayerControlled() {
      return false;
    }
    if !TandemThreats.IsPuppet(threat) || GameInstance.GetDynamicEntitySystem().IsTagged(owner.GetEntityID(), n"Tandem.Proxy") {
      return false;
    }
    let system = GameInstance.GetScriptableSystemsContainer(owner.GetGame()).Get(n"TandemSystem") as TandemSystem;
    return !IsDefined(system) || !system.ThreatsAsNpcs();
  }

  // BlockTargetingPlayer as vanilla reads it, on the threat or on the NPC,
  // and on our V: a quest that spares the player on this game spares every
  // V (the puppet never gets the quest's status effects).
  public static func Blocked(owner: wref<GameObject>, threat: wref<GameObject>) -> Bool {
    let game = owner.GetGame();
    return StatusEffectSystem.ObjectHasStatusEffectOfType(threat, gamedataStatusEffectType.BlockTargetingPlayer)
      || StatusEffectSystem.ObjectHasStatusEffectOfType(owner, gamedataStatusEffectType.BlockTargetingPlayer)
      || StatusEffectSystem.ObjectHasStatusEffectOfType(GetPlayer(game), gamedataStatusEffectType.BlockTargetingPlayer);
  }

  // ---- tests (tools/game.sh cmd npc threats ...; scenario npcs-target-guest) ----

  // "npc threats": each NPC we run within 60 m of our V that has hostile
  // threats, its top one and what it holds on our V and on each puppet.
  // "npc threats npc" / "npc threats player": puppets weighed as vanilla
  // weighs an NPC, or as players (the default). "npc threats spawn": two
  // hostile gangers 4 m beyond the first puppet, as "npc hostile" spawns
  // them (hostile to our V and to every puppet).
  public static func Command(game: GameInstance, system: ref<TandemSystem>, command: String) -> Void {
    if Equals(command, "npc threats npc") || Equals(command, "npc threats player") {
      system.SetThreatsAsNpcs(Equals(command, "npc threats npc"));
      TandemNative.Log(s"threats: puppets weighed as \(system.ThreatsAsNpcs() ? "NPCs (vanilla)" : "players")");
    } else if Equals(command, "npc threats spawn") {
      TandemThreats.SpawnByPuppet(game, system);
    } else {
      TandemThreats.Report(game, system);
    }
  }

  // The largest threat value an NPC we run near our V holds on the first
  // remote player's puppet ("none": no NPC has it as a hostile threat).
  // test query threat puppet (TestQuery.reds).
  public static func PuppetThreat(game: GameInstance, system: ref<TandemSystem>) -> String {
    let players = system.PuppetPlayers();
    if ArraySize(players) == 0 {
      return "none";
    }
    let puppet = system.PuppetOf(players[0]);
    let best = -1000.0;
    let found = false;
    let npcs = TandemThreats.Weighing(game);
    for npc in npcs {
      let held = 0.0;
      if TandemThreats.Held(npc, puppet, held) && (!found || held > best) {
        best = held;
        found = true;
      }
    }
    return found ? FloatToStringPrec(best, 3) : "none";
  }

  private static func Report(game: GameInstance, system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(game);
    let npcs = TandemThreats.Weighing(game);
    TandemNative.Log(s"threats: puppets weighed as \(system.ThreatsAsNpcs() ? "NPCs (vanilla)" : "players"), \(ArraySize(npcs)) NPC(s) we run with hostile threats");
    let players = system.PuppetPlayers();
    let shown = 0;
    for npc in npcs {
      if shown < 8 {
        let tracker = npc.GetTargetTrackerComponent();
        let top: TrackedLocation;
        let topName = "none";
        let topValue = "-";
        if tracker.GetTopHostileThreat(false, top) {
          topName = TandemThreats.Name(system, player, top.entity as GameObject);
          topValue = FloatToStringPrec(top.threat, 3);
        }
        let ours = 0.0;
        let line = s"threats: npc \(EntityID.ToDebugStringDecimal(npc.GetEntityID())) top \(topName) \(topValue); our V \(TandemThreats.Held(npc, player, ours) ? FloatToStringPrec(ours, 3) : "-")";
        for p in players {
          let theirs = 0.0;
          line += s"; player \(p) \(TandemThreats.Held(npc, system.PuppetOf(p), theirs) ? FloatToStringPrec(theirs, 3) : "-")";
        }
        TandemNative.Log(line);
        shown += 1;
      }
    }
  }

  // The NPCs within 60 m of our V that our game runs (no puppet, proxy or
  // corpse) and that have a hostile threat.
  private static func Weighing(game: GameInstance) -> array<ref<NPCPuppet>> {
    let out: array<ref<NPCPuppet>>;
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return out;
    }
    let around = player.GetNPCsAroundObject(60.0);
    for npc in around {
      if IsDefined(npc) && !npc.IsDead() && !TandemThreats.IsPuppet(npc) && !GameInstance.GetDynamicEntitySystem().IsTagged(npc.GetEntityID(), n"Tandem.Proxy") {
        let tracker = npc.GetTargetTrackerComponent();
        if IsDefined(tracker) && tracker.HasHostileThreat(false) {
          ArrayPush(out, npc);
        }
      }
    }
    return out;
  }

  // What the NPC's tracker holds on that entity (false: not a hostile
  // threat of it).
  private static func Held(npc: ref<ScriptedPuppet>, who: ref<GameObject>, out value: Float) -> Bool {
    if !IsDefined(npc) || !IsDefined(who) {
      return false;
    }
    let tracker = npc.GetTargetTrackerComponent();
    if !IsDefined(tracker) {
      return false;
    }
    let threats = tracker.GetHostileThreats(false);
    for t in threats {
      let held = t.entity as GameObject;
      if held == who {
        value = t.threat;
        return true;
      }
    }
    return false;
  }

  private static func Name(system: ref<TandemSystem>, player: ref<GameObject>, who: ref<GameObject>) -> String {
    if !IsDefined(who) {
      return "none";
    }
    if who == player {
      return "our V";
    }
    let p = system.PlayerOfPuppet(who.GetEntityID());
    return p != 0u ? s"player \(p)" : s"npc \(EntityID.ToDebugStringDecimal(who.GetEntityID()))";
  }

  private static func SpawnByPuppet(game: GameInstance, system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(game);
    let players = system.PuppetPlayers();
    let puppet: ref<GameObject>;
    if ArraySize(players) > 0 {
      puppet = system.PuppetOf(players[0]);
    }
    if !IsDefined(player) || !IsDefined(puppet) {
      TandemNative.Log("threats: no puppet to spawn hostiles by");
      return;
    }
    let from = player.GetWorldPosition();
    let at = puppet.GetWorldPosition();
    let away = Vector4.Normalize2D(new Vector4(at.X - from.X, at.Y - from.Y, 0.0, 0.0));
    if Vector4.IsZero(away) {
      away = player.GetWorldForward();
    }
    let side = new Vector4(-away.Y, away.X, 0.0, 0.0);
    let facing = Quaternion.BuildFromDirectionVector(new Vector4(-away.X, -away.Y, 0.0, 0.0), new Vector4(0.0, 0.0, 1.0, 0.0));
    let k = -1.0;
    while k <= 1.0 {
      let spot = new Vector4(at.X + away.X * 4.0 + side.X * 2.0 * k, at.Y + away.Y * 4.0 + side.Y * 2.0 * k, at.Z, 1.0);
      system.SpawnTestHostile(t"Character.kab_tyger_claws_biker1_ranged1_saratoga_ma", spot, facing, true);
      k += 2.0;
    }
    let mid = new Vector4(at.X + away.X * 4.0, at.Y + away.Y * 4.0, at.Z, 1.0);
    TandemNative.Log(s"threats: 2 hostiles by player \(players[0])'s puppet, \(FloatToStringPrec(Vector4.Distance(mid, at), 1)) m from it, \(FloatToStringPrec(Vector4.Distance(mid, from), 1)) m from our V");
  }
}

// The base a threat starts from: 0.01 for an NPC, 1.0 for a player, then the
// same distance, height, hysteresis, damage, attackers, visibility and cover
// terms, then x0.1 while grappled or x0.5 unarmed (final.reds:165479). A
// puppet gets the player's base: the 0.99 between them, scaled as vanilla
// scales the whole.
// job thread safe: it changes only its own out value, read by the caller (UpdateThreatsValue) as the wrapped method's.
@wrapMethod(AIActionTarget)
private final static func RegularThreatCalculation(
  owner: wref<ScriptedPuppet>,
  ownerPos: Vector4,
  targetTrackerComponent: ref<TargetTrackerComponent>,
  newTargetObject: wref<GameObject>,
  threat: wref<GameObject>,
  timeSinceTargetChange: Float,
  currentTime: Float,
  out threatValue: Float
) {
  wrappedMethod(owner, ownerPos, targetTrackerComponent, newTargetObject, threat, timeSinceTargetChange, currentTime, threatValue);
  if ScriptedPuppet.IsActive(threat) && threat.IsPuppet() && TandemThreats.AsPlayer(owner, threat) {
    let scale = 1.0;
    if ScriptedPuppet.IsBeingGrappled(threat) {
      scale = 0.1;
    } else if !AIActionTarget.HasWeaponInInventory(threat) {
      scale = 0.5;
    }
    threatValue += 0.99 * scale;
  }
}

// A boss: 0.01 for an NPC, 2.0 for a player, the same terms after it
// (final.reds:165521); a companion that has been its target for 3 s is
// dropped to 0.01 first, as vanilla does.
// job thread safe: it changes only its own out value, read by the caller (UpdateThreatsValue) as the wrapped method's.
@wrapMethod(AIActionTarget)
private final static func BossThreatCalculation(
  owner: wref<ScriptedPuppet>,
  ownerPos: Vector4,
  targetTrackerComponent: ref<TargetTrackerComponent>,
  newTargetObject: wref<GameObject>,
  threat: wref<GameObject>,
  timeSinceTargetChange: Float,
  currentTime: Float,
  out threatValue: Float
) {
  wrappedMethod(owner, ownerPos, targetTrackerComponent, newTargetObject, threat, timeSinceTargetChange, currentTime, threatValue);
  if ScriptedPuppet.IsActive(threat) && threat.IsPuppet() && TandemThreats.AsPlayer(owner, threat) {
    let puppet = threat as ScriptedPuppet;
    let dropped = threat == newTargetObject && IsDefined(puppet) && puppet.IsPlayerCompanion() && timeSinceTargetChange >= 3.0;
    if IsDefined(puppet) && !dropped {
      threatValue += 1.99;
    }
  }
}

// A quest's BlockTargetingPlayer takes a player off an NPC's threats (base
// -1, final.reds:165419): a puppet too, after vanilla has weighed it. Police
// sent after another V's puppet take our V off the same way while it is not
// wanted and has not attacked them (Police.reds, TandemPolice.SparesOurV).
// job thread safe: it writes only the NPC's own target tracker, as the wrapped method itself does from the same task.
@wrapMethod(AIActionTarget)
public final static func UpdateThreatsValue(puppet: ref<NPCPuppet>, newTargetObject: ref<GameObject>, timeSinceTargetChange: Float) {
  wrappedMethod(puppet, newTargetObject, timeSinceTargetChange);
  if !TandemNative.IsStarted() || !IsDefined(puppet) || !GameInstance.GetDynamicEntitySystem().IsPopulated(n"Tandem.Puppet") {
    return;
  }
  let tracker = puppet.GetTargetTrackerComponent();
  if !IsDefined(tracker) {
    return;
  }
  let threats = tracker.GetHostileThreats(false);
  for t in threats {
    let threat = t.entity as GameObject;
    if TandemPolice.SparesOurV(puppet, threat) || (TandemThreats.AsPlayer(puppet, threat) && TandemThreats.Blocked(puppet, threat)) {
      tracker.SetThreatBaseMul(threat, -1.0);
    }
  }
}

// The combat target the top threat becomes: vanilla refuses a player under
// BlockTargetingPlayer (final.reds:456609); a puppet the same. Vanilla turns
// the NPC hostile to any other target it is given: police sent after another
// V's puppet are refused our V (TandemPolice.SparesOurV).
// job thread safe: it only reads and calls the wrapped method.
@wrapMethod(SetTopThreatToCombatTarget)
private final func SetCombatTarget(context: ScriptExecutionContext, target: ref<GameObject>) {
  let owner = ScriptExecutionContext.GetOwner(context);
  if IsDefined(target) && (TandemPolice.SparesOurV(owner, target) || (TandemThreats.AsPlayer(owner, target) && TandemThreats.Blocked(owner, target))) {
    wrappedMethod(context, null);
    return;
  }
  wrappedMethod(context, target);
}
