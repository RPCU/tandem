// Police and wanted level (docs/architecture/police.md). Heat is per player:
// each game's own prevention system scores its own V's crimes and owns that
// V's wanted level, its stars and its chase timers, exactly as alone. What
// co-op adds:
// - every game says its V's level (TandemStreet.SendHeat), for the others;
// - only the owner of an area spawns police there (TandemArea): a game
//   that follows keeps its prevention system but not its spawns, and the
//   owner answers a remote V's heat with units, cars and (at five stars)
//   a MaxTac AV of its own, hostile to that V's puppet alone;
// - a crime only the owner's game can see (its NPCs calling the police on
//   a puppet) is never blamed on the owner's V: it is told to the
//   offender's game (TandemStreet.SendCrime), which scores it as its own;
// - police see a remote V through the owner's senses: the offender's game
//   feeds its prevention system with what the owner's NPCs detect.

// A unit the owner's game spawned for a remote player's heat: on foot, or
// the crew of one of its cars, or the MaxTac of its AV.
public class TandemPoliceUnit {
  public let id: EntityID;
  public let player: Uint32;
  public let since: Float;
  public let stoodDownAt: Float;
  // The car or AV it came in (none for a unit on foot).
  public let vehicle: EntityID;
  // When it was enlisted, on the engine clock our V's hits are stamped
  // with (GameObject.GetReceivedDamageByPlayerLastTimeStamp).
  public let enlistedAt: Float;
  // Our V hit it since: it may fight our V, as any NPC our V attacks.
  public let provoked: Bool;
  // It was kept off our V last tick (our V not wanted, not provoked).
  public let spared: Bool;
  // It has been taken off our V at least once (logged once).
  public let keptOff: Bool;
}

// A unit Police.reds sent or turned after a remote V's puppet that our V
// has not attacked: the AI's target selection keeps our V off it while our
// V is not wanted (Threats.reds, TandemPolice.SparesOurV). Written on the
// game thread, read on the AI's job threads.
@addField(NPCPuppet)
public let m_tandemSparesV: Bool;

// A police car or MaxTac AV the owner's game spawned for a remote player's
// heat.
public class TandemPoliceCar {
  public let id: EntityID;
  public let player: Uint32;
  public let record: TweakDBID;
  public let av: Bool;
  public let since: Float;
  public let stoodDownAt: Float;
  // When it was last told to chase the puppet.
  public let chasedAt: Float;
}

public class TandemPolice {
  private let m_game: GameInstance;
  // Police cars and the AV for a remote V (`police cars off`: a test's switch).
  private let m_carsOn: Bool = true;
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
  // What each ticket is (0 a unit on foot, 1 a car, 2 an AV) and, for a
  // car, the point out of its V's sight it goes to (zero: none).
  private let m_ticketKinds: array<Int32>;
  private let m_ticketPoints: array<Vector4>;
  private let m_pointsFor: Uint32;
  private let m_pointsAt: Float;
  private let m_nextDispatch: Float;
  // What the request for points is for (0 units, 1 a car, 2 an AV), and the
  // car's record and crew.
  private let m_pointsKind: Int32;
  private let m_pointsVehicle: TweakDBID;
  private let m_pointsCrew: array<TweakDBID>;
  // Which search a car's place is on (TandemPolice.CarSearch): the next one
  // after a search that found nothing, back to the first after a car.
  private let m_carSearch: Int32;
  // Owner: police cars and AVs out for remote players, when the next car
  // may be asked for, the players an AV came for at this level 5 (one
  // each), whether we asked the spawn system to look for roads (it only
  // does while someone asks, and the prevention system asks only while our
  // own V is chased), and whether our own V was chased last time.
  private let m_cars: array<ref<TandemPoliceCar>>;
  private let m_nextCar: Float;
  private let m_avFor: array<Uint32>;
  private let m_roads: Bool;
  private let m_wasChased: Bool;
  // Owner: each remote player's level as last acted on.
  private let m_heatPlayers: array<Uint32>;
  private let m_heatStages: array<Int32>;
  // Owner: when a remote V was last wanted in our area, or units or cars of
  // ours were out for one (Answering).
  private let m_answeredAt: Float;

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
    this.m_answeredAt = -1000.0;
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
    for car in this.m_cars {
      this.StandDownCar(car);
    }
    this.Roads(false);
    ArrayClear(this.m_units);
    ArrayClear(this.m_cars);
    ArrayClear(this.m_avFor);
    ArrayClear(this.m_tickets);
    ArrayClear(this.m_ticketPlayers);
    ArrayClear(this.m_ticketKinds);
    ArrayClear(this.m_ticketPoints);
    ArrayClear(this.m_viewers);
    ArrayClear(this.m_heatPlayers);
    ArrayClear(this.m_heatStages);
    this.m_pointsFor = 0u;
    this.m_sentStage = -1;
    this.m_sentState = -1;
    this.m_answeredAt = -1000.0;
  }

  // (test, tools/game.sh cmd police ...)
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let police = this.Prevention();
    if !IsDefined(police) {
      return;
    }
    if StrBeginsWith(command, "police cars ") {
      this.m_carsOn = Equals(command, "police cars on");
      TandemNative.Log(s"police: cars and the AV for the others \(this.m_carsOn ? "on" : "off")");
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
      let hostile = this.HostileToOurV();
      let cars = 0;
      let avs = 0;
      for car in this.m_cars {
        if car.av {
          avs += 1;
        } else {
          cars += 1;
        }
      }
      TandemNative.Log(s"police report: our wanted level \(police.GetHeatStageAsInt()) (state \(EnumInt(police.GetStarState()))), \(role);\(others); \(ArraySize(this.m_units)) unit(s) out for the others (\(hostile) on our V), \(cars) car(s), \(avs) AV(s), \(ArraySize(this.m_tickets)) on their way, \(ArraySize(this.m_viewers)) of the host's police see our V");
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
    // Ours, whoever else is chased here (PreventionSystem.TandemNotOurs).
    let prevention = GameInstance.GetScriptableSystemsContainer(game).Get(n"PreventionSystem") as PreventionSystem;
    if IsDefined(prevention) {
      prevention.TandemOwnAct();
    }
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

  // What the game spawns on foot at a level here: the district's heat data
  // (the prevention system's preset, PreventionData.NCPD before it has
  // one). The script's failsafe records, Character.prevention_unit_tier1/2/3,
  // are not in 2.50's TweakDB: a spawn of one always fails.
  private func HeatData(stage: Int32) -> wref<PreventionHeatData_Record> {
    let police = this.Prevention();
    let preset: wref<DistrictPreventionData_Record>;
    if IsDefined(police) {
      preset = police.TandemPreset();
    }
    let heat = TandemPolice.HeatOf(preset, stage);
    if !IsDefined(heat) || heat.GetUnitRecordsPoolCount() == 0 {
      heat = TandemPolice.HeatOf(TweakDBInterface.GetDistrictPreventionDataRecord(t"PreventionData.NCPD"), stage);
    }
    return heat;
  }

  private static func HeatOf(preset: wref<DistrictPreventionData_Record>, stage: Int32) -> wref<PreventionHeatData_Record> {
    if !IsDefined(preset) || stage <= 0 {
      return null;
    }
    if stage == 1 {
      return preset.Heat1();
    }
    if stage == 2 {
      return preset.Heat2();
    }
    if stage == 3 {
      return preset.Heat3();
    }
    if stage == 4 {
      return preset.Heat4();
    }
    return preset.Heat5();
  }

  // As many characters from the level's pool, drawn by weight as the
  // game's own fallback does (PreventionSystem.GetCharacterRecordFromPool).
  private static func DrawRecords(heat: wref<PreventionHeatData_Record>, count: Int32) -> array<TweakDBID> {
    let out: array<TweakDBID>;
    let pool: array<wref<PreventionUnitPoolData_Record>>;
    heat.UnitRecordsPool(pool);
    let sum = 0.0;
    for entry in pool {
      if IsDefined(entry) {
        sum += MaxF(entry.Weight(), 0.0);
      }
    }
    let n = 0;
    while n < count {
      let pick = RandRangeF(0.0, sum);
      let seen = 0.0;
      let chosen = TDBID.None();
      let last = TDBID.None();
      for entry in pool {
        let character = IsDefined(entry) ? entry.CharacterRecord() : null;
        if IsDefined(character) && !TDBID.IsValid(chosen) {
          last = character.GetID();
          seen += MaxF(entry.Weight(), 0.0);
          if pick < seen {
            chosen = last;
          }
        }
      }
      if !TDBID.IsValid(chosen) {
        // Weights of zero, or the draw at the very end: the last one.
        chosen = last;
      }
      if TDBID.IsValid(chosen) {
        ArrayPush(out, chosen);
      }
      n += 1;
    }
    return out;
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
    this.Spare();
    // Cars and AVs: wrecked, gone, or stood down and driven off for a
    // while (the game takes them away with whoever is still aboard).
    let cars: array<ref<TandemPoliceCar>>;
    for car in this.m_cars {
      let body = GameInstance.FindEntityByID(this.m_game, car.id) as VehicleObject;
      if !IsDefined(body) {
        TandemNative.Log(s"police: \(TandemPolice.KindOf(car)) for player \(car.player) gone");
      } else if body.IsDestroyed() {
        TandemNative.Log(s"police: \(TandemPolice.KindOf(car)) for player \(car.player) destroyed");
      } else if car.stoodDownAt == 0.0 || this.m_now - car.stoodDownAt < 20.0 {
        ArrayPush(cars, car);
      } else {
        GameInstance.GetPreventionSpawnSystem(this.m_game).RequestDespawnVehicleAndPassengers(body);
        TandemNative.Log(s"police: \(TandemPolice.KindOf(car)) for player \(car.player) taken away");
      }
    }
    this.m_cars = cars;
    // A request for spawn points that never came back.
    if this.m_pointsFor != 0u && this.m_now - this.m_pointsAt > 5.0 {
      this.m_pointsFor = 0u;
    }
    // Our own V's chase ending clears the roads the spawn system looks for
    // (PreventionSystem.UpdateStrategyPreCheckRequests): ours too.
    let prevention = this.Prevention();
    let chased = IsDefined(prevention) && prevention.IsChasingPlayer();
    if this.m_wasChased && !chased {
      this.m_roads = false;
    }
    this.m_wasChased = chased;
    let carsWanted = false;
    let answering = ArraySize(this.m_units) > 0 || ArraySize(this.m_cars) > 0 || ArraySize(this.m_tickets) > 0;
    // In a local: ArrayContains on the array a call returns reads some
    // other array (tools/scripts-temporaries.py), so every level was
    // forgotten on the tick it was learnt.
    let here = system.PuppetPlayers();
    for player in here {
      if TandemArea.OwnsAround(player) {
        let stage = TandemStreet.HeatStage(player);
        let was = this.StageOf(player);
        let puppet = system.PuppetOf(player) as ScriptedPuppet;
        if stage != was {
          this.SetStage(player, stage);
          TandemNative.Log(s"police: player \(player) wanted level \(was) -> \(stage)");
          if stage == 0 {
            this.StandDownFor(player);
          }
        }
        if stage < 5 {
          ArrayRemove(this.m_avFor, player);
        }
        if stage > 0 {
          answering = true;
        }
        if IsDefined(puppet) {
          this.Face(player, puppet, stage, system);
          if stage > 0 {
            this.Crews(player, puppet, system);
            this.Dispatch(player, puppet, stage);
            // Cars and the AV: off until proven in game (police-host crashed
            // a job thread 30 ms after the roads were checked for them,
            // 2026-11-21); `police cars on` turns them on for a test.
            if this.m_carsOn && TandemPolice.CarsFor(stage) > 0 {
              carsWanted = true;
              this.SendCar(player, puppet, stage, chased);
            }
            if this.m_carsOn && stage >= 5 {
              this.SendAV(player, puppet);
            }
          }
        }
      }
    }
    if !carsWanted {
      this.Roads(false);
    }
    if answering {
      this.m_answeredAt = this.m_now;
    }
    // A player that left takes its heat with it.
    let i = 0;
    while i < ArraySize(this.m_heatPlayers) {
      if !ArrayContains(here, this.m_heatPlayers[i]) {
        this.StandDownFor(this.m_heatPlayers[i]);
        ArrayRemove(this.m_avFor, this.m_heatPlayers[i]);
        ArrayErase(this.m_heatPlayers, i);
        ArrayErase(this.m_heatStages, i);
      } else {
        i += 1;
      }
    }
  }

  // Our game answers another player's heat: one is wanted in our area, or
  // was within 10 s, or units or cars of ours are still out for one. What
  // the game's prevention system hears then is mostly their chase, not our
  // V's doing (PreventionSystem.TandemNotOurs). Reads only.
  public func Answering() -> Bool {
    return this.m_you != 0u && this.m_now - this.m_answeredAt < 10.0;
  }

  // Everything out for that player stands down: units, cars, the AV.
  private func StandDownFor(player: Uint32) -> Void {
    for unit in this.m_units {
      if unit.player == player {
        this.StandDown(unit);
      }
    }
    for car in this.m_cars {
      if car.player == player {
        this.StandDownCar(car);
      }
    }
  }

  private static func KindOf(car: ref<TandemPoliceCar>) -> String {
    return car.av ? s"AV \(TDBID.ToStringDEBUG(car.record))" : s"car \(TDBID.ToStringDEBUG(car.record))";
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
  // One turned on a puppet spares our V as our own units do (Spare: the
  // MaxTac an AV set down, the game's own police near the puppet), until
  // our V hits it.
  private func Face(player: Uint32, puppet: ref<ScriptedPuppet>, stage: Int32, system: ref<TandemSystem>) -> Void {
    let mine = puppet.GetAttitudeAgent();
    let ourV = GetPlayer(this.m_game);
    if !IsDefined(mine) || !IsDefined(ourV) {
      return;
    }
    let wanted = stage > 0;
    let oursWanted = TandemPolice.OursWanted(this.m_game);
    for npc in puppet.GetNPCsAroundObject(60.0) {
      let theirs = npc.GetAttitudeAgent();
      if npc.IsPrevention() && IsDefined(theirs) && system.PlayerOfPuppet(npc.GetEntityID()) == 0u {
        let now = theirs.GetAttitudeTowards(mine);
        let ours = this.IsUnit(npc.GetEntityID());
        if wanted && NotEquals(now, EAIAttitude.AIA_Hostile) {
          theirs.SetAttitudeTowards(mine, EAIAttitude.AIA_Hostile);
          mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Hostile);
          if !ours {
            npc.m_tandemSparesV = npc.GetReceivedDamageByPlayerLastTimeStamp() <= 0.0;
            // Kept off our V in the same pass, not the next: in between, an
            // officer that saw our V beside the wanted puppet went hostile
            // to it and gave our V a star (police-host-cars, 2026-11-22).
            if npc.m_tandemSparesV && !oursWanted {
              TandemPolice.KeepOff(npc, ourV);
            }
          }
        } else if wanted && !ours && npc.m_tandemSparesV {
          if npc.GetReceivedDamageByPlayerLastTimeStamp() > 0.0 {
            npc.m_tandemSparesV = false;
            TandemNative.Log(s"police: our V attacked an officer on player \(player): it may fight our V too");
          } else if !oursWanted && TandemPolice.KeepOff(npc, ourV) {
            TandemNative.LogDebug(s"police: an officer on player \(player) turned on our V: kept off it");
          }
        } else if !wanted && Equals(now, EAIAttitude.AIA_Hostile) {
          theirs.SetAttitudeTowards(mine, EAIAttitude.AIA_Neutral);
          mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Neutral);
          let tracker = npc.GetTargetTrackerComponent();
          if IsDefined(tracker) {
            tracker.RemoveThreat(tracker.MapThreat(puppet));
          }
          if !this.IsUnit(npc.GetEntityID()) {
            npc.m_tandemSparesV = false;
          }
        }
      }
    }
  }

  // Our V is wanted by our own game (its own heat: our units and the game's
  // fight it by vanilla's rules then). Reads only: the AI's job threads ask.
  public static func OursWanted(game: GameInstance) -> Bool {
    let police = GameInstance.GetScriptableSystemsContainer(game).Get(n"PreventionSystem") as PreventionSystem;
    return IsDefined(police) && police.GetHeatStageAsInt() > 0u;
  }

  // An NPC sent or turned after a remote V must not take our V for its
  // target: our V is not wanted and has not attacked it. Asked by the AI's
  // target selection on job threads (Threats.reds): reads only.
  public static func SparesOurV(owner: wref<GameObject>, target: wref<GameObject>) -> Bool {
    let unit = owner as NPCPuppet;
    if !IsDefined(unit) || !unit.m_tandemSparesV || !IsDefined(target) || !target.IsPlayerControlled() {
      return false;
    }
    return !TandemPolice.OursWanted(owner.GetGame());
  }

  // Police sent for one V chase that V only. Vanilla's own target selection
  // turns an NPC hostile to whatever tops its threats
  // (SetTopThreatToCombatTarget.SetCombatTarget), and its helping-an-ally
  // and hostile-stimulus paths turn a fighting NPC on whoever is near
  // (AIActionHelper.TryChangingAttitudeToHostile): nothing kept our V out of
  // that. Each tick, each unit of ours, while our V is not wanted: neutral
  // to our V and our V off its threats and its squad's (as vanilla's combat
  // target check drops a V the police may not fight, final.reds:456663). Our V
  // hitting it is the provocation that ends it, as for any NPC; our V's own
  // heat (that hit's, or any) gives it to vanilla's rules. Threats.reds
  // keeps our V off its targets between ticks.
  private func Spare() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let wanted = TandemPolice.OursWanted(this.m_game);
    for unit in this.m_units {
      let body = GameInstance.FindEntityByID(this.m_game, unit.id) as NPCPuppet;
      if IsDefined(body) && unit.stoodDownAt == 0.0 {
        if !unit.provoked && body.GetReceivedDamageByPlayerLastTimeStamp() > unit.enlistedAt {
          unit.provoked = true;
          unit.spared = false;
          body.m_tandemSparesV = false;
          GameObject.ChangeAttitudeToHostile(body, player);
          TargetTrackingExtension.InjectThreat(body, player);
          TandemNative.Log(s"police: our V attacked a unit for player \(unit.player): it fights our V too");
        } else if !unit.provoked {
          body.m_tandemSparesV = true;
          if !wanted {
            if TandemPolice.KeepOff(body, player) && !unit.keptOff {
              unit.keptOff = true;
              TandemNative.Log(s"police: a unit for player \(unit.player) turned on our V: kept off it (our V is not wanted)");
            }
          } else if unit.spared {
            // Our V's own heat: the groups' relation again (the police's
            // to our V's, which vanilla sets by our V's stars).
            TandemPolice.ByGroups(body, player);
            TandemNative.Log(s"police: our V is wanted: a unit for player \(unit.player) may fight it too");
          }
          unit.spared = !wanted;
        }
      }
    }
  }

  // Neutral to our V, our V off its threats and its squad's. True when it
  // had turned on our V.
  private static func KeepOff(body: ref<NPCPuppet>, player: ref<GameObject>) -> Bool {
    let theirs = body.GetAttitudeAgent();
    let ours = player.GetAttitudeAgent();
    let turned = false;
    if IsDefined(theirs) && IsDefined(ours) {
      if Equals(theirs.GetAttitudeTowards(ours), EAIAttitude.AIA_Hostile) {
        theirs.SetAttitudeTowards(ours, EAIAttitude.AIA_Neutral);
        turned = true;
      }
      if Equals(ours.GetAttitudeTowards(theirs), EAIAttitude.AIA_Hostile) {
        ours.SetAttitudeTowards(theirs, EAIAttitude.AIA_Neutral);
      }
    }
    let tracker = body.GetTargetTrackerComponent();
    let threat: TrackedLocation; // assigned on every path: read only after ThreatFromEntity fills it
    if IsDefined(tracker) && tracker.ThreatFromEntity(player, threat) {
      AISquadHelper.RemoveThreatFromSquad(body, threat);
      tracker.RemoveThreat(tracker.MapThreat(player));
      turned = true;
    }
    return turned;
  }

  // The unit looks at our V as its group looks at our V's again.
  private static func ByGroups(body: ref<NPCPuppet>, player: ref<GameObject>) -> Void {
    let theirs = body.GetAttitudeAgent();
    let ours = player.GetAttitudeAgent();
    if !IsDefined(theirs) || !IsDefined(ours) {
      return;
    }
    let relation = GameInstance.GetAttitudeSystem(body.GetGame()).GetAttitudeRelation(theirs.GetAttitudeGroup(), ours.GetAttitudeGroup());
    theirs.SetAttitudeTowards(ours, relation);
  }

  // How many of our units are on our V now (hostile to it, or holding it
  // as a threat): 0 while our V is not wanted and has hit none.
  private func HostileToOurV() -> Int32 {
    let player = GetPlayer(this.m_game);
    let count = 0;
    if !IsDefined(player) {
      return count;
    }
    for unit in this.m_units {
      let body = GameInstance.FindEntityByID(this.m_game, unit.id) as NPCPuppet;
      if IsDefined(body) && !body.IsDead() && unit.stoodDownAt == 0.0 {
        let tracker = body.GetTargetTrackerComponent();
        let threat: TrackedLocation; // assigned on every path: only ThreatFromEntity's answer is read
        if Equals(GameObject.GetAttitudeBetween(body, player), EAIAttitude.AIA_Hostile) || (IsDefined(tracker) && tracker.ThreatFromEntity(player, threat)) {
          count += 1;
        }
      }
    }
    return count;
  }

  // More units for a wanted remote V, a few at a time, at points the
  // game's own spawn system finds around its puppet. A car's crew counts:
  // units on foot fill in for the cars that could not come.
  private func Dispatch(player: Uint32, puppet: ref<ScriptedPuppet>, stage: Int32) -> Void {
    if this.m_now < this.m_nextDispatch || this.m_pointsFor != 0u {
      return;
    }
    let out = this.TicketsFor(player, 0);
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
    this.m_nextDispatch = this.m_now + 6.0;
    let heat = this.HeatData(stage);
    let records: array<TweakDBID>;
    if IsDefined(heat) {
      records = TandemPolice.DrawRecords(heat, wanted);
    }
    if ArraySize(records) == 0 {
      TandemNative.Log(s"police: no unit records for level \(stage) here: none sent for player \(player)");
      return;
    }
    // At the distances the game's own units on foot come from.
    let range = heat.SpawnRange();
    let near = range.X > 0.0 ? range.X : 25.0;
    let far = range.Y > near ? range.Y : near + 20.0;
    let origins: array<Vector4>;
    ArrayPush(origins, puppet.GetWorldPosition());
    this.m_pointsFor = player;
    this.m_pointsKind = 0;
    this.m_pointsAt = this.m_now;
    GameInstance.GetPreventionSpawnSystem(this.m_game).FindPursuitPointsRangeAsync(origins, near, far, Cast<Uint32>(wanted), records, true, NavGenAgentSize.Human, this, "OnPursuitPoints");
    TandemNative.Log(s"police: \(wanted) unit(s) asked for player \(player) (level \(stage), \(TDBID.ToStringDEBUG(records[0])), \(near)-\(far) m)");
  }

  // Our tickets of that kind (0 units, 1 cars, 2 AVs) on their way to that
  // player.
  private func TicketsFor(player: Uint32, kind: Int32) -> Int32 {
    let count = 0;
    let i = 0;
    while i < ArraySize(this.m_tickets) {
      if this.m_ticketPlayers[i] == player && this.m_ticketKinds[i] == kind {
        count += 1;
      }
      i += 1;
    }
    return count;
  }

  private func AddTicket(ticket: Uint32, player: Uint32, kind: Int32, point: Vector4) -> Void {
    ArrayPush(this.m_tickets, ticket);
    ArrayPush(this.m_ticketPlayers, player);
    ArrayPush(this.m_ticketKinds, kind);
    ArrayPush(this.m_ticketPoints, point);
  }

  // The spawn system's answer (called by the engine, by name).
  public final func OnPursuitPoints(const spawnPoints: array<Vector3>, const characterRecords: array<TweakDBID>, unitCount: Uint32) -> Int32 {
    let player = this.m_pointsFor;
    let kind = this.m_pointsKind;
    this.m_pointsFor = 0u;
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let puppet = IsDefined(system) ? system.PuppetOf(player) : null;
    if player == 0u || !IsDefined(puppet) || TandemStreet.HeatStage(player) == 0 {
      return 0;
    }
    let points: array<Vector4>;
    for point in spawnPoints {
      ArrayPush(points, Cast<Vector4>(point));
    }
    if kind == 1 {
      return this.PlaceCar(player, puppet, points);
    }
    if kind == 2 {
      return this.PlaceAV(player, puppet, points);
    }
    if ArraySize(characterRecords) == 0 {
      return 0;
    }
    let sent = 0;
    let spawner = GameInstance.GetPreventionSpawnSystem(this.m_game);
    for at in points {
      if sent < Cast<Int32>(unitCount) {
        let transform: WorldTransform; // assigned on every path: position and orientation set below
        WorldTransform.SetPosition(transform, at);
        WorldTransform.SetOrientationFromDir(transform, Vector4.Normalize2D(puppet.GetWorldPosition() - at));
        let ticket = spawner.RequestUnitSpawn(characterRecords[sent % ArraySize(characterRecords)], transform);
        this.AddTicket(ticket, player, 0, new Vector4(0.0, 0.0, 0.0, 0.0));
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
    let kind = this.m_ticketKinds[i];
    let point = this.m_ticketPoints[i];
    ArrayErase(this.m_tickets, i);
    ArrayErase(this.m_ticketPlayers, i);
    ArrayErase(this.m_ticketKinds, i);
    ArrayErase(this.m_ticketPoints, i);
    let what = kind == 1 ? "a car" : kind == 2 ? "an AV" : "a unit";
    if !result.success {
      TandemNative.Log(s"police: \(what) for player \(player) could not spawn");
      TandemDegraded.Fallback("police not spawned", s"\(what) for player \(player) could not spawn");
      if kind == 1 {
        // Units on foot fill in meanwhile (Dispatch counts the crews).
        this.m_nextCar = this.m_now + 15.0;
      }
      return true;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let puppet = IsDefined(system) ? system.PuppetOf(player) as ScriptedPuppet : null;
    let vehicle: wref<VehicleObject>;
    for spawned in result.spawnedObjects {
      let car = spawned as VehicleObject;
      if IsDefined(car) {
        vehicle = car;
      }
    }
    let vehicleId = IsDefined(vehicle) ? vehicle.GetEntityID() : EntityID.FromHash(0ul);
    let aboard = 0;
    for spawned in result.spawnedObjects {
      let body = spawned as NPCPuppet;
      if IsDefined(body) {
        this.Enlist(body, player, puppet, vehicleId);
        aboard += 1;
      }
    }
    if kind == 0 || !IsDefined(vehicle) {
      return true;
    }
    let car = new TandemPoliceCar();
    car.id = vehicleId;
    car.player = player;
    car.record = vehicle.GetRecordID();
    car.av = kind == 2;
    car.since = this.m_now;
    ArrayPush(this.m_cars, car);
    if car.av {
      TandemNative.Log(s"police: AV \(TDBID.ToStringDEBUG(car.record)) out for player \(player) (\(aboard) aboard)");
      return true;
    }
    if IsDefined(puppet) {
      // Where the spawn system put it is about our V: the point found
      // around the puppet, out of its sight, is about that V.
      let target = puppet.GetWorldPosition();
      if !Vector4.IsXYZZero(point) {
        let at = new Vector4(point.X, point.Y, point.Z + 0.3, 1.0);
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(vehicle, at, Vector4.ToRotation(Vector4.Normalize2D(target - at)));
        TandemNative.Log(s"police: car placed out of player \(player)'s sight, \(Vector4.Distance(at, target)) m away");
      }
      TandemPolice.Chase(vehicle, puppet, 10.0);
      car.chasedAt = this.m_now;
    }
    TandemNative.Log(s"police: car \(TDBID.ToStringDEBUG(car.record)) chasing player \(player) (\(aboard) aboard)");
    return true;
  }

  // A unit of ours, after that V and nobody else. On foot it starts the
  // fight at once; a crew starts it from its seat, and the game's own
  // behaviour gets it out within 15 m of a target that is not the player
  // (AITargetInUnmountingRangeCond).
  private func Enlist(body: ref<NPCPuppet>, player: Uint32, puppet: ref<ScriptedPuppet>, vehicle: EntityID) -> Void {
    let unit = new TandemPoliceUnit();
    unit.id = body.GetEntityID();
    unit.player = player;
    unit.since = this.m_now;
    unit.vehicle = vehicle;
    unit.enlistedAt = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    ArrayPush(this.m_units, unit);
    // Not on our V, which it did not come for (Spare).
    body.m_tandemSparesV = true;
    if IsDefined(puppet) {
      GameObject.ChangeAttitudeToHostile(body, puppet);
      GameObject.ChangeAttitudeToHostile(puppet, body);
      TargetTrackingExtension.InjectThreat(body, puppet);
      if !EntityID.IsDefined(vehicle) {
        NPCPuppet.ChangeHighLevelState(body, gamedataNPCHighLevelState.Combat);
      }
    }
    TandemNative.Log(s"police: unit \(TDBID.ToStringDEBUG(body.GetRecordID())) out for player \(player)");
  }

  private func IsUnit(id: EntityID) -> Bool {
    for unit in this.m_units {
      if unit.id == id {
        return true;
      }
    }
    return false;
  }

  // ---- owner: cars and the AV for a remote V ----

  // Police cars out at a level, at most: none before two stars.
  private static func CarsFor(stage: Int32) -> Int32 {
    if stage < 2 {
      return 0;
    }
    return stage >= 4 ? 2 : 1;
  }

  // The heat table of a level here (its strategies' ranges, the cars'
  // cooldown).
  private func TableFor(stage: Int32) -> wref<PreventionHeatTable_Record> {
    let police = this.Prevention();
    return IsDefined(police) ? police.TandemHeatTable(stage) : null;
  }

  // The spawn system only finds roads for police cars from the strategies
  // it is asked to check, and the prevention system asks only while our
  // own V is chased (UpdateStrategyPreCheckRequests). While a remote V is
  // wanted and ours is not, we ask, from that V's level; when nobody we
  // answer is, we stop.
  private func Roads(wanted: Bool, opt stage: Int32) -> Void {
    let spawner = GameInstance.GetPreventionSpawnSystem(this.m_game);
    if !wanted {
      if this.m_roads && !this.m_wasChased {
        spawner.ClearStrategyPreCheckRequests();
        TandemNative.Log("police: no more roads to check for police cars");
      }
      this.m_roads = false;
      return;
    }
    if this.m_roads {
      return;
    }
    let table = this.TableFor(stage);
    // All seven, as the game asks: the checker queues every strategy's slot
    // and calls its callback, and a slot never set has none (three asked:
    // a job thread crashed 30 ms later, engine/traffic-police.md §13.102).
    let requests: array<ref<BaseStrategyRequest>>;
    let strategy = 1;
    while strategy <= 7 {
      ArrayPush(requests, TandemPolice.StrategyRequest(table, IntEnum<vehiclePoliceStrategy>(strategy)));
      strategy += 1;
    }
    spawner.SetStrategyPreCheckRequests(requests);
    this.m_roads = true;
    TandemNative.Log(s"police: roads checked for police cars (level \(stage))");
  }

  // As the prevention system makes them (CreateStrategyRequest), from a
  // level's table: strategy N reads StrategyN.
  private static func StrategyRequest(table: wref<PreventionHeatTable_Record>, strategy: vehiclePoliceStrategy) -> ref<BaseStrategyRequest> {
    let data: wref<StrategyData_Record>;
    if IsDefined(table) {
      switch strategy {
        case vehiclePoliceStrategy.DriveTowardsPlayer:
          data = table.Strategy1();
          break;
        case vehiclePoliceStrategy.DriveAwayFromPlayer:
          data = table.Strategy2();
          break;
        case vehiclePoliceStrategy.PatrolNearby:
          data = table.Strategy3();
          break;
        case vehiclePoliceStrategy.InterceptAtNextIntersection:
          data = table.Strategy4();
          break;
        case vehiclePoliceStrategy.GetToPlayerFromAnywhere:
          data = table.Strategy5();
          break;
        case vehiclePoliceStrategy.InitialSearch:
          data = table.Strategy6();
          break;
        default:
          data = table.Strategy7();
      }
    }
    let range = new Vector2(80.0, 200.0);
    let angles = new Vector2(0.0, 360.0);
    let direct = 50.0;
    if IsDefined(data) {
      range = data.VehicleSpawnDistanceRange();
      angles = data.VehicleSpawnAngleRange();
      direct = data.MinDirectDistance();
    }
    switch strategy {
      case vehiclePoliceStrategy.DriveTowardsPlayer:
        return DriveTowardsPlayerStrategyRequest.Create(range, direct);
      case vehiclePoliceStrategy.DriveAwayFromPlayer:
        return DriveAwayFromPlayerStrategyRequest.Create(range, direct);
      case vehiclePoliceStrategy.PatrolNearby:
        return PatrolNearbyStrategyRequest.Create(range, angles);
      case vehiclePoliceStrategy.InterceptAtNextIntersection:
        return InterceptAtNextIntersectionStrategyRequest.Create(range, direct);
      case vehiclePoliceStrategy.GetToPlayerFromAnywhere:
        return GetToPlayerFromAnywhereStrategyRequest.Create(range, direct);
      case vehiclePoliceStrategy.InitialSearch:
        return InitialSearchStrategyRequest.Create(range, direct);
      default:
        return SearchFromAnywhereStrategyRequest.Create(range, angles);
    }
  }

  // A police car from the level's pool, drawn by weight as the game does
  // (GetVehicleRecordFromPool), and its crew from the car's own passengers
  // (TryGetUnitDataFromVehicleRecord), else from the level's units.
  private func DrawCar(stage: Int32, out crew: array<TweakDBID>) -> wref<Vehicle_Record> {
    let heat = this.HeatData(stage);
    let pool: array<wref<PreventionVehiclePoolData_Record>>;
    if IsDefined(heat) {
      heat.VehicleRecordPool(pool);
    }
    if ArraySize(pool) == 0 {
      heat = TandemPolice.HeatOf(TweakDBInterface.GetDistrictPreventionDataRecord(t"PreventionData.NCPD"), stage);
      if IsDefined(heat) {
        heat.VehicleRecordPool(pool);
      }
    }
    let sum = 0.0;
    for entry in pool {
      if IsDefined(entry) {
        sum += MaxF(entry.Weight(), 0.0);
      }
    }
    let pick = RandRangeF(0.0, sum);
    let seen = 0.0;
    let chosen: wref<Vehicle_Record>;
    for entry in pool {
      let record = IsDefined(entry) ? entry.VehicleRecord() : null;
      if IsDefined(record) && !IsDefined(chosen) {
        seen += MaxF(entry.Weight(), 0.0);
        if pick <= seen {
          chosen = record;
        }
      }
    }
    if !IsDefined(chosen) {
      return null;
    }
    let count = RandRange(chosen.MinVehiclePassengersCount(), chosen.MaxVehiclePassengersCount() + 1);
    let passengers: array<wref<Character_Record>>;
    chosen.PreventionPassengers(passengers);
    let n = 0;
    while n < count {
      if ArraySize(passengers) > 0 {
        let passenger = passengers[RandRange(0, ArraySize(passengers))];
        if IsDefined(passenger) {
          ArrayPush(crew, passenger.GetID());
        }
      }
      n += 1;
    }
    if ArraySize(crew) == 0 && IsDefined(heat) && count > 0 {
      crew = TandemPolice.DrawRecords(heat, count);
    }
    return chosen;
  }

  // A police car for a wanted remote V at two stars and more: first a
  // place for it on the roads around the puppet, out of that V's sight.
  private func SendCar(player: Uint32, puppet: ref<ScriptedPuppet>, stage: Int32, chased: Bool) -> Void {
    if this.m_now < this.m_nextCar || this.m_pointsFor != 0u || this.TicketsFor(player, 1) > 0 {
      return;
    }
    let out = 0;
    for car in this.m_cars {
      if car.player == player && !car.av && car.stoodDownAt == 0.0 {
        out += 1;
      }
    }
    if out >= TandemPolice.CarsFor(stage) {
      return;
    }
    let table = this.TableFor(stage);
    let cooldown = IsDefined(table) ? table.VehicleSpawnCooldown() : 0.0;
    this.m_nextCar = this.m_now + (cooldown > 5.0 ? cooldown : 15.0);
    if !chased && !this.m_roads {
      // The spawn system needs a moment to look for roads.
      this.Roads(true, stage);
      this.m_nextCar = this.m_now + 3.0;
      return;
    }
    let crew: array<TweakDBID>;
    let record = this.DrawCar(stage, crew);
    if !IsDefined(record) || ArraySize(crew) == 0 {
      TandemNative.Log(s"police: no police car or crew records for level \(stage) here: on foot only for player \(player)");
      return;
    }
    let heat = this.HeatData(stage);
    let range = IsDefined(heat) ? heat.VehicleSpawnDistanceRange() : new Vector2(0.0, 0.0);
    let near = range.X > 0.0 ? range.X : 40.0;
    let far = range.Y > near ? range.Y : near + 60.0;
    let search = this.m_carSearch;
    if search > 0 {
      // The level's range found nothing (drake-ave, 150-250 m: 0 points,
      // 2026-11-21): nearer, still out of that V's sight.
      near = TandemPolice.CarNear();
      far = TandemPolice.CarFar();
    }
    let origins: array<Vector4>;
    ArrayPush(origins, puppet.GetWorldPosition());
    this.m_pointsFor = player;
    this.m_pointsKind = 1;
    this.m_pointsAt = this.m_now;
    this.m_pointsVehicle = record.GetID();
    this.m_pointsCrew = crew;
    // NavGenAgentSize 1 is Vehicle (the RTTI enum; the scripts declare
    // Human alone): points a car fits on. The last search takes a person's
    // (0, the one the game's own units on foot are found with), whose
    // navmesh has the streets too.
    let size = search >= 2 ? 0 : 1;
    GameInstance.GetPreventionSpawnSystem(this.m_game).FindPursuitPointsRangeAsync(origins, near, far, 4u, crew, true, IntEnum<NavGenAgentSize>(size), this, "OnPursuitPoints");
    TandemNative.Log(s"police: a place for a car asked for player \(player) (level \(stage), \(TDBID.ToStringDEBUG(record.GetID())), \(near)-\(far) m, agent size \(size))");
  }

  // A car's place when the level's range has none: this near and far.
  public static func CarNear() -> Float { return 40.0; }
  public static func CarFar() -> Float { return 120.0; }
  // The searches: the level's range, nearer, nearer for a person's size.
  public static func CarSearches() -> Int32 { return 3; }

  // Out of that V's sight: more than 60° off where it looks (the game's
  // forward is +Y turned by yaw: (-sin, cos)).
  private static func OutOfSight(player: Uint32, from: Vector4, point: Vector4) -> Bool {
    let yaw = TandemNative.RemoteYaw(player);
    let dx = point.X - from.X;
    let dy = point.Y - from.Y;
    let length = SqrtF(dx * dx + dy * dy);
    if length < 1.0 {
      return false;
    }
    return (dy * CosF(yaw) - dx * SinF(yaw)) / length < 0.5;
  }

  // The places found: the car is asked for (the game's own chase vehicle,
  // its crew aboard) and goes to the first one out of the V's sight. None:
  // units on foot fill in.
  private func PlaceCar(player: Uint32, puppet: ref<GameObject>, points: array<Vector4>) -> Int32 {
    let from = puppet.GetWorldPosition();
    let place = new Vector4(0.0, 0.0, 0.0, 0.0);
    for point in points {
      if Vector4.IsXYZZero(place) && TandemPolice.OutOfSight(player, from, point) {
        place = point;
      }
    }
    if Vector4.IsXYZZero(place) {
      // The next search soon (another range, another size), else the first
      // again after the level's cooldown.
      this.m_carSearch = (this.m_carSearch + 1) % TandemPolice.CarSearches();
      if this.m_carSearch != 0 {
        this.m_nextCar = this.m_now + 2.0;
      }
      TandemNative.Log(s"police: no place for a car out of player \(player)'s sight (\(ArraySize(points)) found): on foot only for now");
      return 0;
    }
    this.m_carSearch = 0;
    let spawner = GameInstance.GetPreventionSpawnSystem(this.m_game);
    let wanted: array<vehiclePoliceStrategy>;
    ArrayPush(wanted, vehiclePoliceStrategy.GetToPlayerFromAnywhere);
    ArrayPush(wanted, vehiclePoliceStrategy.SearchFromAnywhere);
    ArrayPush(wanted, vehiclePoliceStrategy.DriveTowardsPlayer);
    let strategy = spawner.GetRandomAvailableStrategy(wanted);
    if Equals(strategy, vehiclePoliceStrategy.None) {
      TandemNative.Log(s"police: no road the spawn system can bring a car on for player \(player): on foot only for now");
      return 0;
    }
    let stage = TandemStreet.HeatStage(player);
    let request = TandemPolice.StrategyRequest(this.TableFor(stage), strategy);
    let ticket = spawner.RequestChaseVehicle(this.m_pointsVehicle, this.m_pointsCrew, request);
    this.AddTicket(ticket, player, 1, place);
    TandemNative.Log(s"police: car \(TDBID.ToStringDEBUG(this.m_pointsVehicle)) asked for player \(player) (level \(stage), strategy \(EnumInt(strategy)), \(ArraySize(this.m_pointsCrew)) aboard)");
    return 1;
  }

  // The game's own chase (PreventionSystem.ChasePlayer) aimed at the
  // puppet, as the dynamic spawn system sends its chase cars
  // (DynamicSpawnSystem.SpawnCallback): no limit on chasing vehicles,
  // driving stats boosted, and its init command so it starts with it.
  private static func Chase(car: ref<VehicleObject>, target: ref<GameObject>, startSpeed: Float) -> Void {
    let command = new AIVehicleChaseCommand();
    command.target = target;
    command.distanceMin = 3.0;
    command.distanceMax = 10.0;
    command.forcedStartSpeed = startSpeed;
    command.ignoreChaseVehiclesLimit = true;
    command.boostDrivingStats = true;
    let evt = new AICommandEvent();
    evt.command = command;
    car.QueueEvent(evt);
    let ai = car.GetAIComponent();
    if IsDefined(ai) {
      ai.SetInitCmd(command);
    }
  }

  // Each tick, for the cars out for that V: whoever sits in them is ours
  // (a crew the spawn result did not list), and a car whose driver is in
  // and has lost the chase is given it again.
  private func Crews(player: Uint32, puppet: ref<ScriptedPuppet>, system: ref<TandemSystem>) -> Void {
    let target = puppet.GetWorldPosition();
    for car in this.m_cars {
      if car.player == player && car.stoodDownAt == 0.0 {
        let body = GameInstance.FindEntityByID(this.m_game, car.id) as VehicleObject;
        if IsDefined(body) {
          let passengers: array<wref<GameObject>>;
          VehicleComponent.GetAllPassengers(this.m_game, car.id, false, passengers);
          for passenger in passengers {
            let npc = passenger as NPCPuppet;
            // (Not a V's puppet that took the car.)
            if IsDefined(npc) && !npc.IsPlayer() && !npc.IsDead() && system.PlayerOfPuppet(npc.GetEntityID()) == 0u && !this.IsUnit(npc.GetEntityID()) {
              this.Enlist(npc, player, puppet, car.id);
            }
          }
          let position = body.GetWorldPosition();
          if !car.av && this.m_now - car.chasedAt > 8.0 && !body.IsChasingTarget() && VehicleComponent.HasActiveDriverMounted(this.m_game, car.id) && Vector4.Distance(position, target) > 30.0 {
            TandemPolice.Chase(body, puppet, -1.0);
            car.chasedAt = this.m_now;
            TandemNative.Log(s"police: car \(TDBID.ToStringDEBUG(car.record)) chasing player \(player) again");
          }
        }
      }
    }
  }

  // The MaxTac AV at five stars, one for each wanted V: landed near its
  // body (RequestAVSpawnAtLocation), at a point found around it.
  private func SendAV(player: Uint32, puppet: ref<ScriptedPuppet>) -> Void {
    if ArrayContains(this.m_avFor, player) || this.m_pointsFor != 0u || this.TicketsFor(player, 2) > 0 {
      return;
    }
    let heat = this.HeatData(5);
    let records: array<TweakDBID>;
    if IsDefined(heat) {
      records = TandemPolice.DrawRecords(heat, 1);
    }
    let origins: array<Vector4>;
    ArrayPush(origins, puppet.GetWorldPosition());
    this.m_pointsFor = player;
    this.m_pointsKind = 2;
    this.m_pointsAt = this.m_now;
    GameInstance.GetPreventionSpawnSystem(this.m_game).FindPursuitPointsRangeAsync(origins, 15.0, 40.0, 4u, records, false, NavGenAgentSize.Human, this, "OnPursuitPoints");
    TandemNative.Log(s"police: a place for an AV asked for player \(player)");
  }

  private func PlaceAV(player: Uint32, puppet: ref<GameObject>, points: array<Vector4>) -> Int32 {
    let from = puppet.GetWorldPosition();
    let place = from;
    let found = false;
    for point in points {
      if !found && TandemPolice.OutOfSight(player, from, point) {
        place = point;
        found = true;
      }
    }
    if !found && ArraySize(points) > 0 {
      place = points[0];
    }
    // The first wave's records, as the prevention system picks them
    // (RequestAVSpawnAtPosition).
    let pick = RandRange(1, 4);
    let record = pick == 1 ? t"Vehicle.max_tac_av1" : pick == 2 ? t"Vehicle.max_tac_av2" : t"Vehicle.max_tac_av3";
    let ticket = GameInstance.GetPreventionSpawnSystem(this.m_game).RequestAVSpawnAtLocation(record, Cast<Vector3>(place));
    ArrayPush(this.m_avFor, player);
    this.AddTicket(ticket, player, 2, new Vector4(0.0, 0.0, 0.0, 0.0));
    TandemNative.Log(s"police: AV \(TDBID.ToStringDEBUG(record)) asked for player \(player), \(Vector4.Distance(place, from)) m from it");
    return 1;
  }

  // The chase is over for this car: it stops what it was told and drives
  // off as the game's own do (StartVehicleDeescalationBehavior); its crew
  // stands down as units. The AV leaves on its own.
  private func StandDownCar(car: ref<TandemPoliceCar>) -> Void {
    if car.stoodDownAt != 0.0 {
      return;
    }
    car.stoodDownAt = this.m_now;
    let body = GameInstance.FindEntityByID(this.m_game, car.id) as VehicleObject;
    if IsDefined(body) && !car.av && !body.IsDestroyed() {
      GameInstance.GetPreventionSpawnSystem(this.m_game).InterruptAllActionAndCommands(body);
      let panic = new AIVehiclePanicCommand();
      panic.allowSimplifiedMovement = true;
      panic.ignoreTickets = true;
      panic.useSpeedBasedLookupRange = true;
      let evt = new AICommandEvent();
      evt.command = panic;
      body.QueueEvent(evt);
      let ai = body.GetAIComponent();
      if IsDefined(ai) {
        ai.SetInitCmd(panic);
      }
    }
    let what = car.av ? "AV" : "car";
    TandemNative.Log(s"police: \(what) for player \(car.player) stands down");
  }

  // The chase is over for this unit: it forgets the V and walks off, as
  // the game's own units do when the stars are gone.
  private func StandDown(unit: ref<TandemPoliceUnit>) -> Void {
    if unit.stoodDownAt != 0.0 {
      return;
    }
    unit.stoodDownAt = this.m_now;
    let body = GameInstance.FindEntityByID(this.m_game, unit.id) as NPCPuppet;
    if !IsDefined(body) || body.IsDead() {
      return;
    }
    // Back to vanilla's rules with our V.
    body.m_tandemSparesV = false;
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

// The district's police data it spawns from (set as V changes district).
@addMethod(PreventionSystem)
public final func TandemPreset() -> wref<DistrictPreventionData_Record> {
  return this.m_preventionPreset;
}

// The heat table of a level in the district's matrix (the strategies'
// ranges, the cooldowns), for a remote V's level.
@addMethod(PreventionSystem)
public final func TandemHeatTable(stage: Int32) -> wref<PreventionHeatTable_Record> {
  if !IsDefined(this.m_preventionDataMatrix) || stage < 0 || stage > 5 {
    return null;
  }
  return this.GetDataTableForHeat(IntEnum<EPreventionHeatStage>(stage));
}

// ---- another player's chase is not our V's heat ----
//
// Vanilla's prevention system knows one culprit, "the player". Around a
// chase our game runs for another V it hears inputs that name nobody (a
// witness of the fight calling it in: CallPolice and the native
// CrowdCallingPoliceEvent name the local V; a prevention turret opening on
// whatever it follows; the native stolen-vehicle request), and each raises
// a V at no star to one (OnCrimeWitnessRequest, OnCombatStartedRequest,
// OnVehicleStolenRequest: final.reds:141470-141500). Seen in game: units
// and MaxTac out for a guest's puppet 3 m from the host's V, and "our
// wanted level 1" for a V that did nothing (police-host-cars, 2026-11-21;
// which input it was is not in that log: every first star is logged with
// its reason since).
//
// While our game answers another player's heat (TandemPolice.Answering)
// and our V has no star, a first star needs our V to have done something:
// an input vanilla attributes by the hit's instigator (damage, a kill, an
// officer hit, a car it blew up: these mark it, TandemOwnAct), a crime the
// area's owner saw (TandemPolice.OnCrime), or damage it dealt in the last
// 10 s (PlayerPuppet.GetLastDamageInflictedTime: what a witness would have
// seen). A quest's level (SetWantedLevel) always counts. With a star,
// vanilla's rules: the fight is our V's too.

// When our V last did what the prevention system scores (simulation time).
@addField(PreventionSystem)
private let m_tandemOwnActAt: Float;

@addField(PreventionSystem)
private let m_tandemOwnAct: Bool;

@addMethod(PreventionSystem)
public final func TandemOwnAct() -> Void {
  this.m_tandemOwnAct = true;
  this.m_tandemOwnActAt = EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
}

// A first star for this reason would be another player's chase heard as
// our V's crime. Reads only.
@addMethod(PreventionSystem)
public final func TandemNotOurs(reason: String) -> Bool {
  let game = this.GetGameInstance();
  if NotEquals(this.m_heatStage, EPreventionHeatStage.Heat_0) || Equals(reason, "QuestEvent") || Equals(reason, "DEBUG") || Equals(reason, "KillPrevention") {
    return false;
  }
  let police = TandemPolice.Of(game);
  if !IsDefined(police) || !police.Answering() {
    return false;
  }
  let now = EngineTime.ToFloat(GameInstance.GetSimTime(game));
  if this.m_tandemOwnAct && now - this.m_tandemOwnActAt < 10.0 {
    return false;
  }
  let player = GetPlayer(game);
  if IsDefined(player) {
    let dealt = EngineTime.ToFloat(player.GetLastDamageInflictedTime());
    if dealt > 0.0 && now - dealt < 10.0 {
      return false;
    }
  }
  return true;
}

// The one place the level changes: nothing of another player's chase gets
// our V its first star, whatever path it came by.
@wrapMethod(PreventionSystem)
private final func ChangeHeatStage(newHeatStage: EPreventionHeatStage, heatChangeReason: String) -> Void {
  if EnumInt(newHeatStage) > 0 && Equals(this.m_heatStage, EPreventionHeatStage.Heat_0) && TandemNative.IsStarted() {
    if this.TandemNotOurs(heatChangeReason) {
      TandemNative.Log(s"police: heat refused for our V (\(heatChangeReason)): it did nothing, the police here are after another player");
      return;
    }
    let police = TandemPolice.Of(this.GetGameInstance());
    let v = GetPlayer(this.GetGameInstance());
    let now = EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
    let dealt = IsDefined(v) ? now - EngineTime.ToFloat(v.GetLastDamageInflictedTime()) : -1.0;
    TandemNative.Log(s"police: our V gets heat (\(heatChangeReason)): answering another's chase \(IsDefined(police) && police.Answering()), our own act \(this.m_tandemOwnAct ? now - this.m_tandemOwnActAt : -1.0) s ago, our V's last hit \(dealt) s ago");
  }
  wrappedMethod(newHeatStage, heatChangeReason);
}

// Damage and kills, sent when the instigator is our V: its own.
@wrapMethod(PreventionSystem)
protected final func OnPreventionDamage(evt: ref<PreventionDamage>) -> Void {
  this.TandemOwnAct();
  wrappedMethod(evt);
}

// A car our V shot that blows up (VehicleComponent.OnDeath, m_hitByPlayer).
@wrapMethod(PreventionSystem)
private final func OnVehicleExplosion() -> Void {
  this.TandemOwnAct();
  wrappedMethod();
}

// An officer our V hit (sent by the hit's instigator), or a prevention
// turret opening on its target: the turret names nobody.
@wrapMethod(PreventionSystem)
protected final func OnCombatStartedRequest(request: ref<PreventionCombatStartedRequest>) -> Void {
  if !IsDefined(request.requester as SensorDevice) {
    this.TandemOwnAct();
  } else if this.TandemNotOurs("EnterCombat") {
    TandemNative.Log("police: a turret's fight not counted against our V: it did nothing, the police here are after another player");
    return;
  }
  wrappedMethod(request);
}

// A witness's call names the local V whoever it saw.
@wrapMethod(PreventionSystem)
protected final func OnCrimeWitnessRequest(request: ref<PreventionCrimeWitnessRequest>) -> Void {
  if this.TandemNotOurs("CrimeWitness") {
    TandemNative.Log("police: a witness's call not counted against our V: it did nothing, the police here are after another player");
    return;
  }
  wrappedMethod(request);
}

// A police vehicle taken: ours when our V sits in one.
@wrapMethod(PreventionSystem)
protected final func OnVehicleStolenRequest(request: ref<PreventionVehicleStolenRequest>) -> Void {
  let game = this.GetGameInstance();
  if VehicleComponent.IsMountedToVehicle(game, GetPlayer(game)) {
    this.TandemOwnAct();
  } else if this.TandemNotOurs("PlayerStoleVehicle") {
    TandemNative.Log("police: a stolen police vehicle not counted against our V: it sits in none, the police here are after another player");
    return;
  }
  wrappedMethod(request);
}

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
