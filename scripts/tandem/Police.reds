// Police and wanted level (docs/architecture/police.md). Heat is per player:
// each game's own prevention system scores its own V's crimes and owns that
// V's wanted level, its stars and its chase timers, exactly as alone. What
// co-op adds:
// - every game says its V's level (TandemStreet.SendHeat), for the others;
// - only the owner of an area spawns police there (TandemArea): a game
//   that follows keeps its prevention system but not its spawns, and the
//   owner answers a remote V's heat with units of its own, hostile to that
//   V's puppet alone;
// - a crime only the owner's game can see (its NPCs calling the police on
//   a puppet) is never blamed on the owner's V: it is told to the
//   offender's game (TandemStreet.SendCrime), which scores it as its own;
// - police see a remote V through the owner's senses: the offender's game
//   feeds its prevention system with what the owner's NPCs detect.

// A unit the owner's game spawned for a remote player's heat.
public class TandemPoliceUnit {
  public let id: EntityID;
  public let player: Uint32;
  public let since: Float;
  public let stoodDownAt: Float;
}

public class TandemPolice {
  private let m_game: GameInstance;
  private let m_now: Float;
  private let m_you: Uint32;
  // Our V's level and star state as last sent (-1: not yet).
  private let m_sentStage: Int32;
  private let m_sentState: Int32;
  private let m_last: Float;
  // Following: the host's police that see our V now (their proxies' ids).
  private let m_viewers: array<Uint32>;
  // Owner: units out for remote players, spawn requests on their way (the
  // spawn system's tickets, and whom each is for), the request for spawn
  // points on its way (0: none) and when the next may go.
  private let m_units: array<ref<TandemPoliceUnit>>;
  private let m_tickets: array<Uint32>;
  private let m_ticketPlayers: array<Uint32>;
  private let m_pointsFor: Uint32;
  private let m_pointsAt: Float;
  private let m_nextDispatch: Float;
  // Owner: each remote player's level as last acted on.
  private let m_heatPlayers: array<Uint32>;
  private let m_heatStages: array<Int32>;

  public static func Of(game: GameInstance) -> ref<TandemPolice> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Police() : null;
  }

  // In a session where another game runs the world around our V: our own
  // prevention system keeps the score, the stars and the timers, and
  // spawns nothing.
  public static func Follows(game: GameInstance) -> Bool {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) && system.You() != 0u && !TandemArea.Owns();
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_sentStage = -1;
    this.m_sentState = -1;
  }

  private func Prevention() -> ref<PreventionSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PreventionSystem") as PreventionSystem;
  }

  public func Tick(now: Float, you: Uint32, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    this.m_you = you;
    if you == 0u || now - this.m_last < 0.25 {
      return;
    }
    this.m_last = now;
    this.SendOurs();
    if TandemArea.Owns() {
      this.Answer(system);
    } else {
      this.Watch(system);
    }
  }

  // The session is over: nobody is wanted but by their own game, and the
  // units we sent after the others go the game's way.
  public func End() -> Void {
    for unit in this.m_units {
      this.StandDown(unit);
    }
    ArrayClear(this.m_units);
    ArrayClear(this.m_tickets);
    ArrayClear(this.m_ticketPlayers);
    ArrayClear(this.m_viewers);
    ArrayClear(this.m_heatPlayers);
    ArrayClear(this.m_heatStages);
    this.m_pointsFor = 0u;
    this.m_sentStage = -1;
    this.m_sentState = -1;
  }

  // (test, tools/game.sh cmd police ...)
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let police = this.Prevention();
    if !IsDefined(police) {
      return;
    }
    if StrBeginsWith(command, "police heat") {
      // The quests' own request: our V is wanted at this level, here.
      let level = StrLen(command) > 12 ? StringToInt(StrAfterFirst(command, "heat ")) : 1;
      let request = new SetWantedLevel();
      request.m_wantedLevel = IntEnum<EPreventionHeatStage>(Clamp(level, 0, 5));
      request.m_forcePlayerPositionAsLastCrimePoint = true;
      police.QueueRequest(request);
      TandemNative.Log(s"police: our wanted level asked: \(level)");
    } else if Equals(command, "police calm") {
      let calm = new PreventionForceDeescalateRequest();
      calm.fakeBlinkingDuration = 0.5;
      calm.telemetryInfo = "tandem test";
      police.QueueRequest(calm);
      TandemNative.Log("police: asked to stand down");
    } else {
      let others = "";
      for player in system.PuppetPlayers() {
        others += s" player \(player): \(TandemStreet.HeatStage(player)) (state \(TandemStreet.HeatState(player)))";
      }
      let role = TandemArea.Owns() ? "we answer the others' heat" : "we follow: our own spawns are off";
      TandemNative.Log(s"police report: our wanted level \(police.GetHeatStageAsInt()) (state \(EnumInt(police.GetStarState()))), \(role);\(others); \(ArraySize(this.m_units)) unit(s) out for the others, \(ArraySize(this.m_tickets)) on their way, \(ArraySize(this.m_viewers)) of the host's police see our V");
    }
  }

  // ---- every game: our own V's heat ----

  private func SendOurs() -> Void {
    let police = this.Prevention();
    if !IsDefined(police) {
      return;
    }
    let stage = Cast<Int32>(police.GetHeatStageAsInt());
    let state = EnumInt(police.GetStarState());
    if stage == this.m_sentStage && state == this.m_sentState {
      return;
    }
    this.m_sentStage = stage;
    this.m_sentState = state;
    TandemStreet.SendHeat(stage, state);
    TandemNative.Log(s"police: our wanted level \(stage) (state \(state))");
  }

  // The owner of our area saw our puppet commit a crime: our own prevention
  // system scores it, as if its own NPCs had seen our V do it.
  public static func OnCrime(game: GameInstance, event: TandemStreetEvent) -> Void {
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    let victim: ref<GameObject>;
    if IsDefined(system) && event.npc != 0u {
      victim = GameInstance.GetDynamicEntitySystem().GetEntity(system.ProxyOfNpc(event.npc)) as GameObject;
    }
    TandemNative.Log(s"police: crime seen by the area's owner: kind \(event.reason), npc \(event.npc), flags \(event.flags)");
    if event.reason == 1 {
      PreventionSystem.CrimeWitnessRequestToPreventionSystem(game, player.GetWorldPosition());
    } else if event.reason == 2 {
      PreventionSystem.CombatStartedRequestToPreventionSystem(game, IsDefined(victim) ? victim : player);
    } else if IsDefined(victim) {
      PreventionSystem.CreateNewDamageRequest(game, victim, IntEnum<gamedataAttackType>(event.attack), event.amount, event.reason == 4);
    } else {
      PreventionSystem.CrimeWitnessRequestToPreventionSystem(game, player.GetWorldPosition());
    }
  }

  // ---- following: the owner's police and our V ----

  // The owner's police see our V when its NPCs have detected it
  // (NpcStates.detected): our prevention system hears it from their proxies
  // as it would from its own units' senses (active or grey stars, the
  // last known position).
  private func Watch(system: ref<TandemSystem>) -> Void {
    let police = this.Prevention();
    let npcs = system.Npcs();
    if !IsDefined(police) || !IsDefined(npcs) {
      return;
    }
    let chased = police.IsChasingPlayer();
    let bit = TandemPolice.BitOf(this.m_you);
    let seen: array<Uint32>;
    if chased {
      for npc in npcs.ProxyIds() {
        if (TandemNative.NpcDetected(npc) & bit) != 0 {
          let body = GameInstance.GetDynamicEntitySystem().GetEntity(system.ProxyOfNpc(npc)) as ScriptedPuppet;
          if IsDefined(body) && body.IsPrevention() {
            ArrayPush(seen, npc);
            if !ArrayContains(this.m_viewers, npc) {
              PreventionSystem.RegisterAsViewerToPreventionSystem(this.m_game, body);
            }
          }
        }
      }
    }
    for npc in this.m_viewers {
      if !ArrayContains(seen, npc) {
        let body = GameInstance.GetDynamicEntitySystem().GetEntity(system.ProxyOfNpc(npc)) as ScriptedPuppet;
        if IsDefined(body) {
          PreventionSystem.UnRegisterAsViewerToPreventionSystem(this.m_game, body);
        }
      }
    }
    this.m_viewers = seen;
  }

  public static func BitOf(player: Uint32) -> Int32 {
    let bit = 1;
    let p = 1u;
    while p < player && p < 8u {
      bit *= 2;
      p += 1u;
    }
    return bit;
  }

  // ---- owner: a remote V's heat ----

  // How many units are out for a level (on foot, around the puppet): the
  // game's own fallback when no police car can reach its V.
  private static func UnitsFor(stage: Int32) -> Int32 {
    if stage <= 0 {
      return 0;
    }
    if stage >= 5 {
      return 6;
    }
    return stage + 1;
  }

  private static func RecordFor(stage: Int32) -> TweakDBID {
    if stage >= 4 {
      return t"Character.prevention_unit_tier3";
    }
    if stage == 3 {
      return t"Character.prevention_unit_tier2";
    }
    return t"Character.prevention_unit_tier1";
  }

  private func Answer(system: ref<TandemSystem>) -> Void {
    // Units that died, or went with their part of the world.
    let alive: array<ref<TandemPoliceUnit>>;
    for unit in this.m_units {
      let body = GameInstance.FindEntityByID(this.m_game, unit.id) as ScriptedPuppet;
      if IsDefined(body) && !body.IsDead() && (unit.stoodDownAt == 0.0 || this.m_now - unit.stoodDownAt < 20.0) {
        ArrayPush(alive, unit);
      } else if IsDefined(body) && !body.IsDead() {
        // Stood down and walked off for a while: the game takes it away.
        GameInstance.GetPreventionSpawnSystem(this.m_game).RequestDespawn(unit.id);
      }
    }
    this.m_units = alive;
    // A request for spawn points that never came back.
    if this.m_pointsFor != 0u && this.m_now - this.m_pointsAt > 5.0 {
      this.m_pointsFor = 0u;
    }
    for player in system.PuppetPlayers() {
      if TandemArea.OwnsAround(player) {
        let stage = TandemStreet.HeatStage(player);
        let was = this.StageOf(player);
        let puppet = system.PuppetOf(player) as ScriptedPuppet;
        if stage != was {
          this.SetStage(player, stage);
          TandemNative.Log(s"police: player \(player) wanted level \(was) -> \(stage)");
          if stage == 0 {
            for unit in this.m_units {
              if unit.player == player {
                this.StandDown(unit);
              }
            }
          }
        }
        if IsDefined(puppet) {
          this.Face(player, puppet, stage, system);
          if stage > 0 {
            this.Dispatch(player, puppet, stage);
          }
        }
      }
    }
    // A player that left takes its heat with it.
    let i = 0;
    while i < ArraySize(this.m_heatPlayers) {
      if !ArrayContains(system.PuppetPlayers(), this.m_heatPlayers[i]) {
        for unit in this.m_units {
          if unit.player == this.m_heatPlayers[i] {
            this.StandDown(unit);
          }
        }
        ArrayErase(this.m_heatPlayers, i);
        ArrayErase(this.m_heatStages, i);
      } else {
        i += 1;
      }
    }
  }

  private func StageOf(player: Uint32) -> Int32 {
    let i = ArrayFindFirst(this.m_heatPlayers, player);
    return i < 0 ? 0 : this.m_heatStages[i];
  }

  private func SetStage(player: Uint32, stage: Int32) -> Void {
    let i = ArrayFindFirst(this.m_heatPlayers, player);
    if i < 0 {
      ArrayPush(this.m_heatPlayers, player);
      ArrayPush(this.m_heatStages, stage);
    } else {
      this.m_heatStages[i] = stage;
    }
  }

  // The police around a remote V's puppet look at it as its own heat says:
  // hostile while it is wanted, neutral again when it is not. Never as our
  // V's heat says (Attitudes.reds leaves the police out of what it copies).
  private func Face(player: Uint32, puppet: ref<ScriptedPuppet>, stage: Int32, system: ref<TandemSystem>) -> Void {
    let mine = puppet.GetAttitudeAgent();
    if !IsDefined(mine) {
      return;
    }
    let wanted = stage > 0;
    for npc in puppet.GetNPCsAroundObject(60.0) {
      let theirs = npc.GetAttitudeAgent();
      if npc.IsPrevention() && IsDefined(theirs) && system.PlayerOfPuppet(npc.GetEntityID()) == 0u {
        let now = theirs.GetAttitudeTowards(mine);
        if wanted && NotEquals(now, EAIAttitude.AIA_Hostile) {
          theirs.SetAttitudeTowards(mine, EAIAttitude.AIA_Hostile);
          mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Hostile);
        } else if !wanted && Equals(now, EAIAttitude.AIA_Hostile) {
          theirs.SetAttitudeTowards(mine, EAIAttitude.AIA_Neutral);
          mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Neutral);
          let tracker = npc.GetTargetTrackerComponent();
          if IsDefined(tracker) {
            tracker.RemoveThreat(tracker.MapThreat(puppet));
          }
        }
      }
    }
  }

  // More units for a wanted remote V, a few at a time, at points the
  // game's own spawn system finds around its puppet.
  private func Dispatch(player: Uint32, puppet: ref<ScriptedPuppet>, stage: Int32) -> Void {
    if this.m_now < this.m_nextDispatch || this.m_pointsFor != 0u {
      return;
    }
    let out = ArraySize(this.m_tickets);
    for unit in this.m_units {
      if unit.player == player && unit.stoodDownAt == 0.0 {
        out += 1;
      }
    }
    let wanted = TandemPolice.UnitsFor(stage) - out;
    if wanted <= 0 {
      return;
    }
    if wanted > 2 {
      wanted = 2;
    }
    let origins: array<Vector4>;
    ArrayPush(origins, puppet.GetWorldPosition());
    let records: array<TweakDBID>;
    ArrayPush(records, TandemPolice.RecordFor(stage));
    this.m_pointsFor = player;
    this.m_pointsAt = this.m_now;
    this.m_nextDispatch = this.m_now + 6.0;
    GameInstance.GetPreventionSpawnSystem(this.m_game).FindPursuitPointsRangeAsync(origins, 25.0, 45.0, Cast<Uint32>(wanted), records, true, NavGenAgentSize.Human, this, "OnPursuitPoints");
    TandemNative.Log(s"police: \(wanted) unit(s) asked for player \(player) (level \(stage))");
  }

  // The spawn system's answer (called by the engine, by name).
  public final func OnPursuitPoints(const spawnPoints: array<Vector3>, const characterRecords: array<TweakDBID>, unitCount: Uint32) -> Int32 {
    let player = this.m_pointsFor;
    this.m_pointsFor = 0u;
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let puppet = IsDefined(system) ? system.PuppetOf(player) : null;
    if player == 0u || !IsDefined(puppet) || ArraySize(characterRecords) == 0 || TandemStreet.HeatStage(player) == 0 {
      return 0;
    }
    let sent = 0;
    let spawner = GameInstance.GetPreventionSpawnSystem(this.m_game);
    for point in spawnPoints {
      if sent < Cast<Int32>(unitCount) {
        let transform: WorldTransform; // assigned on every path: position and orientation set below
        let at = Cast<Vector4>(point);
        WorldTransform.SetPosition(transform, at);
        WorldTransform.SetOrientationFromDir(transform, Vector4.Normalize2D(puppet.GetWorldPosition() - at));
        let ticket = spawner.RequestUnitSpawn(characterRecords[0], transform);
        ArrayPush(this.m_tickets, ticket);
        ArrayPush(this.m_ticketPlayers, player);
        sent += 1;
      }
    }
    TandemNative.Log(s"police: \(sent) unit(s) on their way to player \(player)");
    return sent;
  }

  // A spawn request finished: ours (true: the game's own system has no
  // ticket for it and must not look), or the game's.
  public func TakeSpawn(result: SpawnRequestResult) -> Bool {
    let i = ArrayFindFirst(this.m_tickets, result.requestID);
    if i < 0 {
      return false;
    }
    let player = this.m_ticketPlayers[i];
    ArrayErase(this.m_tickets, i);
    ArrayErase(this.m_ticketPlayers, i);
    if !result.success {
      TandemNative.Log(s"police: a unit for player \(player) could not spawn");
      return true;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let puppet = IsDefined(system) ? system.PuppetOf(player) as ScriptedPuppet : null;
    for spawned in result.spawnedObjects {
      let body = spawned as ScriptedPuppet;
      if IsDefined(body) {
        let unit = new TandemPoliceUnit();
        unit.id = body.GetEntityID();
        unit.player = player;
        unit.since = this.m_now;
        ArrayPush(this.m_units, unit);
        if IsDefined(puppet) {
          // After that V, and nobody else.
          GameObject.ChangeAttitudeToHostile(body, puppet);
          GameObject.ChangeAttitudeToHostile(puppet, body);
          TargetTrackingExtension.InjectThreat(body, puppet);
          NPCPuppet.ChangeHighLevelState(body, gamedataNPCHighLevelState.Combat);
        }
        TandemNative.Log(s"police: unit \(TDBID.ToStringDEBUG(body.GetRecordID())) out for player \(player)");
      }
    }
    return true;
  }

  // The chase is over for this unit: it forgets the V and walks off, as
  // the game's own units do when the stars are gone.
  private func StandDown(unit: ref<TandemPoliceUnit>) -> Void {
    if unit.stoodDownAt != 0.0 {
      return;
    }
    unit.stoodDownAt = this.m_now;
    let body = GameInstance.FindEntityByID(this.m_game, unit.id) as ScriptedPuppet;
    if !IsDefined(body) || body.IsDead() {
      return;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let puppet = IsDefined(system) ? system.PuppetOf(unit.player) : null;
    body.StopPoliceBehaviour();
    let tracker = body.GetTargetTrackerComponent();
    if IsDefined(puppet) {
      GameObject.ChangeAttitudeToNeutral(body, puppet);
      GameObject.ChangeAttitudeToNeutral(puppet, body);
      if IsDefined(tracker) {
        tracker.RemoveThreat(tracker.MapThreat(puppet));
      }
    }
    if IsDefined(tracker) {
      tracker.ResetRecentlyDroppedThreat();
    }
    NPCPuppet.ChangeHighLevelState(body, gamedataNPCHighLevelState.Relaxed);
    StatusEffectHelper.ApplyStatusEffect(body, t"PreventionStatusEffect.OnStarDropToZero");
    TandemNative.Log(s"police: unit for player \(unit.player) stands down");
  }

  // ---- owner: whose crime it was ----

  // One of our NPCs calls the police. True when it saw a remote V's puppet
  // do it: that player's game is told, and ours must not blame our V.
  public func Witnessed(witness: ref<GameObject>) -> Bool {
    if this.m_you == 0u || !TandemArea.Owns() {
      return false;
    }
    let npc = witness as ScriptedPuppet;
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(npc) || !IsDefined(system) {
      return false;
    }
    let culprit = TandemPolice.CulpritOf(npc);
    if !IsDefined(culprit) {
      return false;
    }
    let player = system.PlayerOfPuppet(culprit.GetEntityID());
    if player == 0u {
      return false;
    }
    TandemStreet.SendCrime(player, 1, 0, 0, 0.0, system.SharedNpcOf(npc.GetEntityID()), 0u, culprit.GetWorldPosition());
    TandemNative.Log(s"police: \(TDBID.ToStringDEBUG(npc.GetRecordID())) calls the police on player \(player)");
    return true;
  }

  // What an NPC reacts to: the source of the stimulus it is acting on.
  private static func CulpritOf(npc: ref<ScriptedPuppet>) -> ref<GameObject> {
    let reactions = npc.GetStimReactionComponent();
    if !IsDefined(reactions) {
      return null;
    }
    let data = reactions.GetActiveReactionData();
    if !IsDefined(data) || !IsDefined(data.stimTarget) {
      data = reactions.GetDesiredReactionData();
    }
    return IsDefined(data) ? data.stimTarget : null;
  }

  // An officer or a turret of ours opens on someone. True when that is a
  // remote V's puppet: its game hears of the fight, ours is not in it.
  public func Engaged(officer: ref<GameObject>, target: ref<GameObject>) -> Bool {
    if this.m_you == 0u || !TandemArea.Owns() || !IsDefined(target) {
      return false;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let player = IsDefined(system) ? system.PlayerOfPuppet(target.GetEntityID()) : 0u;
    if player == 0u {
      return false;
    }
    let npc = IsDefined(officer) ? system.SharedNpcOf(officer.GetEntityID()) : 0u;
    TandemStreet.SendCrime(player, 2, 1, 0, 0.0, npc, 0u, target.GetWorldPosition());
    return true;
  }
}

// ---- the game's prevention system ----

// A game that follows spawns no police of its own: the area's owner does.
@wrapMethod(PreventionSystem)
private final func RequestUnitSpawn(recordID: TweakDBID, spawnTransform: WorldTransform) -> Void {
  if TandemPolice.Follows(this.GetGameInstance()) {
    return;
  }
  wrappedMethod(recordID, spawnTransform);
}

@wrapMethod(PreventionSystem)
private final func SpawnPoliceVehicle() -> Uint32 {
  if TandemPolice.Follows(this.GetGameInstance()) {
    return 0u;
  }
  return wrappedMethod();
}

@wrapMethod(PreventionSystem)
private final func SpawnRoadblockade(heatStage: EPreventionHeatStage) -> Void {
  if TandemPolice.Follows(this.GetGameInstance()) {
    return;
  }
  wrappedMethod(heatStage);
}

@wrapMethod(PreventionSystem)
private final func RequestAVSpawnAtPosition(position: Vector3) -> Void {
  if TandemPolice.Follows(this.GetGameInstance()) {
    return;
  }
  wrappedMethod(position);
}

// The owner's units for a remote V come back through the game's own
// spawn callback: they are ours to register, not the game's.
@wrapMethod(PreventionSystem)
private final func OnPreventionUnitSpawnedRequest(request: ref<PreventionUnitSpawnedRequest>) -> Void {
  let police = TandemPolice.Of(this.GetGameInstance());
  if IsDefined(police) && police.TakeSpawn(request.requestResult) {
    return;
  }
  wrappedMethod(request);
}

// A prevention turret that opens on its target reports a fight with "the
// player", whatever the target. When it is a remote V, that V's game is
// told instead.
@wrapMethod(PreventionSystem)
public final static func CombatStartedRequestToPreventionSystem(context: GameInstance, requester: wref<GameObject>) -> Void {
  let turret = requester as SensorDevice;
  let police = TandemPolice.Of(context);
  if IsDefined(turret) && IsDefined(police) && police.Engaged(turret, turret.GetCurrentlyFollowedTarget()) {
    return;
  }
  wrappedMethod(context, requester);
}

// An NPC that calls the police names "the player": the local V, whoever it
// saw. When it saw a remote V, that V's game is told instead.
@wrapMethod(CallPolice)
protected func Activate(context: ScriptExecutionContext) -> Void {
  let owner = ScriptExecutionContext.GetOwner(context);
  let police = IsDefined(owner) ? TandemPolice.Of(owner.GetGame()) : null;
  if IsDefined(police) && police.Witnessed(owner) {
    return;
  }
  wrappedMethod(context);
}
