// tools/game.sh cmd test reset: one known state before every scenario
// (tools/scenario.sh runs it). The game loads the player's latest save as
// it is (in a car, mid-mission, wanted, indoors, downed) and each scenario
// leaves state for the next (revive cooldown, test worlds, holds, cars):
// this puts our V on foot, up, healed, at the test field, with the tests'
// leftovers gone, and logs "test reset: ready (...)" with whatever it could
// not clear, or "test reset: failed (...)" when V is not on foot at the
// field (the scenario then fails).

public class TandemTestReset {
  // The test field: "teleport home", an empty street corner.
  public static func Field() -> Vector4 {
    return new Vector4(-915.0, 1879.5, 36.19, 1.0);
  }

  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  // 0 idle, 1 out of vehicles and workspots, 2 at the field, settling.
  private let m_step: Int32;
  private let m_since: Float;
  private let m_tries: Int32;
  private let m_last: Vector4;
  private let m_stillSince: Float;
  private let m_left: array<String>;

  public func Start(game: GameInstance, system: ref<TandemSystem>, now: Float) -> Void {
    this.m_game = game;
    this.m_system = system;
    this.m_step = 1;
    this.m_since = now;
    this.m_tries = 0;
    ArrayClear(this.m_left);
    TandemNative.Log("test reset: start");
    this.Clear(now);
  }

  public func Busy() -> Bool {
    return this.m_step != 0;
  }

  // Everything that needs no waiting: abilities, test worlds, holds, life,
  // health, effects, police, the tests' NPCs and cars.
  private func Clear(now: Float) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if IsDefined(this.m_system) {
      this.m_system.ClearTestState();
    }
    // Story: no test private scene, no test quest freeze.
    TandemStorySystem.Get(this.m_game).Command("story private off");
    TandemStorySystem.Get(this.m_game).Command("story hub off");
    TandemStorySystem.Get(this.m_game).Command("quest freeze off");
    // No test of interiors we cannot load (story spectate missing on).
    let spectate = TandemStorySystem.Get(this.m_game).Spectate();
    if IsDefined(spectate) {
      spectate.ClearTest();
    }
    // Back in our V's body: no camera, turret or car a scenario left it in
    // (RemoteView.reds; the game's own way out, as a quest ends one).
    let remote = TandemRemoteView.Ours(this.m_game);
    if remote.on {
      TakeOverControlSystem.ReleaseControlOfRemoteControlledVehicle(this.m_game, player.GetEntityID());
      TakeOverControlSystem.ReleaseControl(this.m_game);
      TandemNative.Log(s"test reset: out of our V's remote view (\(TandemRemoteView.SortName(remote.sort)))");
    }
    // Our V's own god mode from "god on" (Revive.reds keeps its own).
    let gods = GameInstance.GetGodModeSystem(this.m_game);
    gods.RemoveGodMode(player.GetEntityID(), gameGodModeType.Invulnerable, n"Default");
    gods.RemoveGodMode(player.GetEntityID(), gameGodModeType.Immortal, n"Default");
    // Healed, and what keeps hurting or slowing it gone.
    GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, 100.0, null, true);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Bleeding);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Burning);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Crippled);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Poisoned);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Electrocuted);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.EMP);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Blind);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Stunned);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Wounded);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Pain);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Exhausted);
    StatusEffectHelper.RemoveAllStatusEffectsByType(player, gamedataStatusEffectType.Deafened);
    // Police: the game's own de-escalation (as the mask cyberware asks it).
    let police = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PreventionSystem") as PreventionSystem;
    if IsDefined(police) && police.IsChasingPlayer() {
      let calm = new PreventionForceDeescalateRequest();
      calm.fakeBlinkingDuration = 0.5;
      calm.telemetryInfo = "TandemTestReset";
      police.QueueRequest(calm);
      TandemNative.Log("test reset: police asked to stand down");
    }
  }

  public func Tick(now: Float) -> Void {
    if this.m_step == 0 {
      return;
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let mounting = GameInstance.GetMountingFacility(this.m_game);
    let workspots = GameInstance.GetWorkspotSystem(this.m_game);
    if this.m_step == 1 {
      // Out of any vehicle (an instant unmount, then the workspot's own
      // exit) and any workspot (sitting, a device).
      let car: wref<VehicleObject>;
      let mounted = mounting.IsMountedToAnything(player.GetEntityID());
      if mounted && now - this.m_since >= 1.0 * Cast<Float>(this.m_tries) {
        if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
          let slot = n"None";
          VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
          if this.m_tries == 0 {
            TandemVehicles.Unseat(this.m_game, player, car.GetEntityID(), slot);
          } else {
            workspots.UnmountFromVehicle(car, player, true);
          }
          TandemNative.Log(s"test reset: out of the vehicle (\(NameToString(slot)), try \(this.m_tries + 1))");
        }
        this.m_tries += 1;
      }
      if workspots.IsActorInWorkspot(player) && this.m_tries < 3 {
        workspots.StopInDevice(player);
        this.m_tries += 1;
        TandemNative.Log("test reset: out of the workspot");
      }
      let free = !mounted && !workspots.IsActorInWorkspot(player);
      if free || now - this.m_since > 5.0 {
        if !free {
          ArrayPush(this.m_left, mounted ? "still mounted" : "still in a workspot");
        }
        // Cars are deleted once nobody sits in them (deleting a car with a
        // body in it froze the game, System.reds ProcessLeaving).
        if IsDefined(this.m_system) {
          this.m_system.ClearTestCars();
        }
        TandemTravel.ExpectMove(this.m_game);
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, TandemTestReset.Field(), TandemYawAngles(0.0));
        this.m_step = 2;
        this.m_since = now;
        this.m_stillSince = now;
        this.m_last = player.GetWorldPosition();
      }
      return;
    }
    // At the field: standing still on the ground for a while, the world
    // around streamed (no engine signal for that: time and a body that has
    // stopped falling).
    let at = player.GetWorldPosition();
    if Vector4.Distance(at, this.m_last) > 0.05 {
      this.m_stillSince = now;
    }
    this.m_last = at;
    let near = Vector4.Distance(at, TandemTestReset.Field()) < 3.0;
    let settled = now - this.m_since >= 3.0 && now - this.m_stillSince >= 1.5 && near;
    if !settled && now - this.m_since < 20.0 {
      return;
    }
    this.Finish(player, near && settled, at);
  }

  private func Finish(player: ref<PlayerPuppet>, placed: Bool, at: Vector4) -> Void {
    this.m_step = 0;
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let onFoot = !GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(player.GetEntityID());
    if IsDefined(psm) {
      let defs = GetAllBlackboardDefs();
      // HighLevel 1 is exploration, free play (docs/research/coop-scenes.md
      // §1); a scene holds V from tier 2. At tier 1 the scene system may
      // still count V in a quest's background scene: said apart.
      let level = psm.GetInt(defs.PlayerStateMachine.HighLevel);
      let scenes = GameInstance.GetSceneSystem(this.m_game).GetScriptInterface();
      if level >= 2 && level <= 5 {
        ArrayPush(this.m_left, s"in a scene (tier \(level))");
      } else if IsDefined(scenes) && scenes.IsEntityInScene(player.GetEntityID()) {
        ArrayPush(this.m_left, s"in a background scene (tier \(level))");
      }
      if psm.GetInt(defs.PlayerStateMachine.Combat) == 1 {
        ArrayPush(this.m_left, "in combat");
      }
      if psm.GetInt(defs.PlayerStateMachine.Takedown) != 0 {
        ArrayPush(this.m_left, "grappling");
      }
      if psm.GetInt(defs.PlayerStateMachine.BodyCarrying) != 0 {
        ArrayPush(this.m_left, "carrying a body");
      }
      if psm.GetInt(defs.PlayerStateMachine.Vitals) != 0 {
        ArrayPush(this.m_left, "not alive");
      }
    }
    let police = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PreventionSystem") as PreventionSystem;
    if IsDefined(police) && police.IsChasingPlayer() {
      ArrayPush(this.m_left, s"police heat \(police.GetHeatStageAsInt())");
    }
    let ui = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_System);
    if IsDefined(ui) && ui.GetBool(GetAllBlackboardDefs().UI_System.IsInMenu) {
      ArrayPush(this.m_left, "a menu open");
    }
    let phone = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice);
    if IsDefined(phone) && (phone.GetBool(GetAllBlackboardDefs().UI_ComDevice.isDisplayingMessage) || phone.GetBool(GetAllBlackboardDefs().UI_ComDevice.ContactsActive)) {
      ArrayPush(this.m_left, "the phone open");
    }
    if GameInstance.GetTimeSystem(this.m_game).IsTimeDilationActive() {
      ArrayPush(this.m_left, "time dilated");
    }
    let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
    if health < 99.0 {
      ArrayPush(this.m_left, s"health \(RoundF(health)) %");
    }
    let leftover = "";
    for item in this.m_left {
      leftover += ", " + item;
    }
    if placed && onFoot {
      // A scenario answers for the fallbacks taken from here on
      // (tools/scenario.sh: none it did not declare).
      TandemDegraded.ResetFallbacks();
      TandemNative.Log(s"test reset: ready (on foot, at the test field\(leftover))");
    } else {
      let foot = onFoot ? "on foot" : "not on foot";
      let spot = s"at \(at.X) \(at.Y) \(at.Z), not settled at the test field";
      if placed {
        spot = "at the test field";
      }
      TandemNative.Log(s"test reset: failed (\(foot), \(spot)\(leftover))");
    }
  }
}
