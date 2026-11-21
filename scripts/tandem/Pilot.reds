// The test pilot's side in the game (docs/architecture/autopilot.md): what
// lets tools/pilot play our V without a human.
//
// - Its state, twenty times a second, as one line of key=value words in
//   pilot.state next to the DLL: where V is and looks, how it moves, what it
//   rides, its health, the NPCs around, the target, the prompts on screen.
// - Script-level actions, "pilot N ACTION ARGS" in tandem.cmd, each answered
//   in the state by ack=N and res=ok... or res=fail:REASON: what the game
//   lets a script make V do with its own gameplay path (look, draw, fire,
//   quickhack, take down, mount, autodrive, devices, the phone, loot, a
//   vendor). What no script can do (walk, jump, pick a dialogue choice)
//   goes through the pilot's virtual gamepad instead.
// - Safety: saving is locked from the first "pilot on" (the test never
//   writes the player's saves), and the pilot turns itself off, firing and
//   looking stopped, when its heartbeat ("pilot beat") stops for 3 s.
//
// Every engine name here is read from the 2.50 scripts and not yet run in
// game: docs/research/engine/autopilot.md has each one's source and tag.

public class TandemPilot {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_on: Bool;
  private let m_beatAt: Float;
  private let m_stateAt: Float;
  private let m_seq: Int32;
  private let m_ack: Int32;
  private let m_res: String;
  // The NPC or object the actions are about (pilot N target ...).
  private let m_target: wref<GameObject>;
  // Fire: the trigger held until this time.
  private let m_firing: Bool;
  private let m_fireUntil: Float;
  // Look: the view kept on a point, or on the target, until this time.
  private let m_lookUntil: Float;
  private let m_lookNext: Float;
  private let m_lookAt: Vector4;
  private let m_lookTarget: Bool;
  // V's speed, from its last two positions.
  private let m_lastAt: Vector4;
  private let m_lastTime: Float;
  private let m_speed: Float;
  // The world's clock when it last stood still (a loading screen): NPCs are
  // not listed for 2 s after it.
  private let m_simLast: Float;
  private let m_worldSince: Float;
  // The NPCs around, counted four times a second.
  private let m_scanAt: Float;
  private let m_npcs: Int32;
  private let m_hostile: Int32;
  private let m_lockAt: Float;

  public func Init(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    this.m_res = "none";
  }

  public func IsOn() -> Bool {
    return this.m_on;
  }

  // Real time: the world's clock slows under a Sandevistan.
  private func Clock() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }

  // ---- commands: pilot on | off | beat | probe | N ACTION ARGS ----

  public func Command(command: String) -> Void {
    let now = this.Clock();
    if Equals(command, "pilot on") {
      this.On(now);
      return;
    }
    if Equals(command, "pilot off") {
      this.Off("asked");
      return;
    }
    if Equals(command, "pilot beat") {
      if this.m_on {
        this.m_beatAt = now;
      }
      return;
    }
    if Equals(command, "pilot probe") {
      this.Probe();
      return;
    }
    let words = StrSplit(command, " ");
    if ArraySize(words) < 3 {
      TandemNative.Log(s"pilot: unknown command: \(command)");
      return;
    }
    if !this.m_on {
      TandemNative.Log(s"pilot: off, ignored: \(command)");
      return;
    }
    let number = words[1];
    let id = StringToInt(number, 0);
    if id <= 0 {
      TandemNative.Log(s"pilot: no action number: \(command)");
      return;
    }
    this.m_beatAt = now;
    // (One word: the state line is key=value words.)
    let res = StrReplaceAll(this.Act(words, now), " ", "_");
    this.m_ack = id;
    this.m_res = res;
    TandemNative.Log(s"pilot: \(StrAfterFirst(StrAfterFirst(command, " "), " ")) -> \(res)");
    this.WriteState(now);
  }

  private func On(now: Float) -> Void {
    this.m_beatAt = now;
    this.LockSaves();
    this.m_lockAt = now;
    if this.m_on {
      return;
    }
    this.m_on = true;
    this.m_worldSince = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    this.m_simLast = this.m_worldSince;
    TandemNative.Log("pilot: on (saving locked)");
    this.WriteState(now);
  }

  // Saving stays locked until the game ends: a test's world must never
  // reach the player's saves, also after the pilot is done.
  private func LockSaves() -> Void {
    TandemPilotNative.LockSaves();
    SaveLocksManager.RequestSaveLockAdd(this.m_game, n"TandemPilot");
    GameInstance.GetAutoSaveSystem(this.m_game).InvalidateAutoSaveRequests();
  }

  public func Off(why: String) -> Void {
    if !this.m_on {
      return;
    }
    this.m_on = false;
    this.StopFire();
    this.StopLook();
    this.m_target = null;
    TandemPilotNative.ClearState();
    TandemNative.Log(s"pilot: off (\(why)); saving stays locked");
  }

  public func Tick() -> Void {
    if !this.m_on {
      return;
    }
    let now = this.Clock();
    if now - this.m_beatAt > 3.0 {
      this.Off("no heartbeat for 3 s");
      return;
    }
    // A loading screen stops the world's clock; a loaded save forgets the
    // save lock (it is not persistent): asked again every 5 s.
    let sim = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    if sim <= this.m_simLast {
      this.m_worldSince = sim;
    }
    this.m_simLast = sim;
    if now - this.m_lockAt > 5.0 {
      this.m_lockAt = now;
      this.LockSaves();
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if this.m_firing {
      if now >= this.m_fireUntil || !IsDefined(GameObject.GetActiveWeapon(player)) {
        this.StopFire();
      } else {
        this.PullTrigger(player);
      }
    }
    if this.m_lookUntil > now && now >= this.m_lookNext {
      this.m_lookNext = now + 0.2;
      this.Look(player, 0.3);
    }
    if now - this.m_stateAt >= 0.05 {
      this.WriteState(now);
    }
  }

  // ---- the state line ----

  private static func F(value: Float) -> String {
    return FloatToStringPrec(value, 2);
  }

  private static func B(value: Bool) -> String {
    return value ? "1" : "0";
  }

  private func WriteState(now: Float) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let defs = GetAllBlackboardDefs();
    let boards = GameInstance.GetBlackboardSystem(this.m_game);
    let at = player.GetWorldPosition();
    if this.m_lastTime > 0.0 && now - this.m_lastTime > 0.02 {
      this.m_speed = Vector4.Distance(at, this.m_lastAt) / (now - this.m_lastTime);
    }
    this.m_lastAt = at;
    this.m_lastTime = now;
    this.m_stateAt = now;
    this.m_seq += 1;

    let line = s"seq=\(this.m_seq) t=\(TandemPilot.F(now)) pilot=1 ack=\(this.m_ack) res=\(this.m_res)";
    line += s" x=\(TandemPilot.F(at.X)) y=\(TandemPilot.F(at.Y)) z=\(TandemPilot.F(at.Z))";

    // Where the view looks, and from where.
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let view = cameras.GetActiveCameraForward();
    let eye: Transform;
    cameras.GetActiveCameraWorldTransform(eye);
    let from = Transform.GetPosition(eye);
    line += s" fx=\(FloatToStringPrec(view.X, 4)) fy=\(FloatToStringPrec(view.Y, 4)) fz=\(FloatToStringPrec(view.Z, 4))";
    line += s" cx=\(TandemPilot.F(from.X)) cy=\(TandemPilot.F(from.Y)) cz=\(TandemPilot.F(from.Z))";

    // The body's heading, or the vehicle's, and the speed of what moves.
    let car: wref<VehicleObject>;
    let riding = VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car);
    let heading = player.GetWorldForward();
    let speed = this.m_speed;
    let seat = "none";
    if riding {
      heading = car.GetWorldForward();
      speed = Vector4.Length(car.GetLinearVelocity());
      seat = VehicleComponent.IsDriver(this.m_game, player) ? "driver" : "passenger";
    }
    line += s" hx=\(FloatToStringPrec(heading.X, 4)) hy=\(FloatToStringPrec(heading.Y, 4)) speed=\(TandemPilot.F(speed))";
    line += s" veh=\(TandemPilot.B(riding)) seat=\(seat)";

    let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
    line += s" hp=\(TandemPilot.F(health))";

    // How V moves and what it does, from its state machine.
    let psm = boards.GetLocalInstanced(player.GetEntityID(), defs.PlayerStateMachine);
    if IsDefined(psm) {
      let locomotion = psm.GetInt(defs.PlayerStateMachine.Locomotion);
      let detailed = psm.GetInt(defs.PlayerStateMachine.LocomotionDetailed);
      line += s" loco=\(locomotion) detail=\(detailed)";
      line += s" crouch=\(TandemPilot.B(locomotion == EnumInt(gamePSMLocomotionStates.Crouch)))";
      line += s" sprint=\(TandemPilot.B(locomotion == EnumInt(gamePSMLocomotionStates.Sprint)))";
      // In the air: jumping, dodging in the air, or falling.
      let airborne = locomotion == EnumInt(gamePSMLocomotionStates.Jump) || locomotion == EnumInt(gamePSMLocomotionStates.DodgeAir)
        || locomotion == EnumInt(gamePSMLocomotionStates.SlideFall) || psm.GetInt(defs.PlayerStateMachine.Fall) != 0;
      line += s" air=\(TandemPilot.B(airborne && !riding))";
      line += s" swim=\(psm.GetInt(defs.PlayerStateMachine.Swimming))";
      line += s" combat=\(TandemPilot.B(psm.GetInt(defs.PlayerStateMachine.Combat) == EnumInt(gamePSMCombat.InCombat)))";
      line += s" ads=\(TandemPilot.B(psm.GetInt(defs.PlayerStateMachine.UpperBody) == EnumInt(gamePSMUpperBodyStates.Aim)))";
      line += s" tier=\(psm.GetInt(defs.PlayerStateMachine.HighLevel))";
      line += s" takedown=\(psm.GetInt(defs.PlayerStateMachine.Takedown))";
      line += s" carry=\(TandemPilot.B(psm.GetInt(defs.PlayerStateMachine.BodyCarrying) != 0))";
      line += s" dead=\(TandemPilot.B(psm.GetInt(defs.PlayerStateMachine.Vitals) != 0))";
      line += s" vstate=\(psm.GetInt(defs.PlayerStateMachine.Vehicle))";
    }
    let revive = TandemRevive.Of(this.m_game);
    line += s" down=\(TandemPilot.B(IsDefined(revive) && revive.IsDown()))";
    // The game's last input came from a pad (the pilot's wake waits for it).
    line += s" pad=\(TandemPilot.B(player.PlayerLastUsedPad()))";
    line += s" weapon=\(TandemPilot.B(IsDefined(GameObject.GetActiveWeapon(player))))";
    line += s" firing=\(TandemPilot.B(this.m_firing))";
    line += s" dilated=\(TandemPilot.B(GameInstance.GetTimeSystem(this.m_game).IsTimeDilationActive()))";

    // What is on screen: a menu, a call, dialogue choices, a prompt.
    let ui = boards.Get(defs.UI_System);
    line += s" menu=\(TandemPilot.B(IsDefined(ui) && ui.GetBool(defs.UI_System.IsInMenu)))";
    let phone = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PhoneSystem") as PhoneSystem;
    line += s" call=\(IsDefined(phone) ? phone.TandemPilotCall() : 0)";
    let prompts = boards.Get(defs.UIInteractions);
    if IsDefined(prompts) {
      let hubs = FromVariant<DialogChoiceHubs>(prompts.GetVariant(defs.UIInteractions.DialogChoiceHubs));
      let choices = 0;
      for hub in hubs.choiceHubs {
        choices += ArraySize(hub.choices);
      }
      let prompt = FromVariant<InteractionChoiceHubData>(prompts.GetVariant(defs.UIInteractions.InteractionChoiceHub));
      line += s" choices=\(choices) sel=\(prompts.GetInt(defs.UIInteractions.SelectedIndex))";
      line += s" prompt=\(prompt.active ? ArraySize(prompt.choices) : 0)";
    }
    let autodrive = boards.Get(defs.UI_AutodriveData);
    if IsDefined(autodrive) {
      line += s" auto=\(TandemPilot.B(autodrive.GetBool(defs.UI_AutodriveData.AutoDriveEnabled)))";
      line += s" autoavail=\(TandemPilot.B(autodrive.GetBool(defs.UI_AutodriveData.AutoDriveAvailable)))";
    }
    let locks: array<gameSaveLock>;
    line += s" savelock=\(TandemPilot.B(GameInstance.IsSavingLocked(this.m_game, locks)))";
    let session = TandemNative.IsStarted() && TandemNative.You() != 0u;
    line += s" session=\(TandemPilot.B(session)) role=\(session ? (TandemNative.IsHost() ? "host" : "guest") : "none")";
    // The car V called: how far it is (-1: none).
    let called = boards.Get(defs.VehicleSummonData).GetEntityID(defs.VehicleSummonData.SummonedVehicleEntityID);
    let calledCar = GameInstance.FindEntityByID(this.m_game, called) as VehicleObject;
    line += s" car=\(IsDefined(calledCar) ? TandemPilot.F(Vector4.Distance(at, calledCar.GetWorldPosition())) : "-1")";

    // The NPCs around, once the world has run for 2 s (listing them while
    // it streams in hung a load, 2026-11-18), four times a second.
    let sim = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    if sim - this.m_worldSince > 2.0 && now - this.m_scanAt >= 0.25 {
      this.m_scanAt = now;
      this.m_npcs = 0;
      this.m_hostile = 0;
      for npc in player.GetNPCsAroundObject(30.0) {
        if IsDefined(npc) && !npc.IsDead() {
          this.m_npcs += 1;
          if Equals(GameObject.GetAttitudeTowards(npc, player), EAIAttitude.AIA_Hostile) {
            this.m_hostile += 1;
          }
        }
      }
    }
    line += s" npcs=\(this.m_npcs) hostile=\(this.m_hostile)";

    // The target: where to aim at it, how far, how it is.
    if IsDefined(this.m_target) {
      let aim = this.AimPoint(this.m_target);
      let puppet = this.m_target as ScriptedPuppet;
      let targetHealth = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(this.m_target.GetEntityID()), gamedataStatPoolType.Health, true);
      line += s" tid=\(EntityID.GetHash(this.m_target.GetEntityID()) % 1000000u + 1u)";
      line += s" tx=\(TandemPilot.F(aim.X)) ty=\(TandemPilot.F(aim.Y)) tz=\(TandemPilot.F(aim.Z))";
      line += s" td=\(TandemPilot.F(Vector4.Distance(at, this.m_target.GetWorldPosition()))) thp=\(TandemPilot.F(targetHealth))";
      line += s" tdead=\(TandemPilot.B(IsDefined(puppet) && (puppet.IsDead() || ScriptedPuppet.IsDefeated(puppet))))";
    } else {
      line += " tid=0";
    }
    TandemPilotNative.WriteState(line);
  }

  // Where to aim at something: an NPC's chest (lower once it lies down).
  private func AimPoint(target: wref<GameObject>) -> Vector4 {
    let at = target.GetWorldPosition();
    let puppet = target as ScriptedPuppet;
    if IsDefined(puppet) {
      at.Z += puppet.IsDead() || ScriptedPuppet.IsDefeated(puppet) ? 0.3 : 1.3;
    }
    return at;
  }

  // ---- the actions ----

  private func Act(words: array<String>, now: Float) -> String {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return "fail:no_player";
    }
    let action = words[2];
    let arg = ArraySize(words) > 3 ? words[3] : "";
    let arg2 = ArraySize(words) > 4 ? words[4] : "";
    switch action {
      case "target":
        return this.Target(player, arg, arg2);
      case "look":
        return this.LookCommand(player, words, now);
      case "equip":
        return this.Equip(player, arg);
      case "fire":
        return this.Fire(player, arg, now);
      case "reload":
        return this.Reload(player);
      case "ability":
        return this.Ability(player, arg);
      case "heal":
        return this.Heal(player);
      case "hurt":
        return this.Hurt(player, arg);
      case "hack":
        return this.Hack(arg);
      case "takedown":
        return this.Takedown(player, arg);
      case "mount":
        return this.Mount(player, arg);
      case "exit":
        return this.Exit(player);
      case "summon":
        return GameInstance.GetVehicleSystem(this.m_game).SpawnActivePlayerVehicle() ? "ok:asked" : "fail:no_vehicle_to_call";
      case "autodrive":
        return this.Autodrive(player, arg);
      case "device":
        return this.Device(player, arg);
      case "lift":
        return this.Lift(player, arg);
      case "call":
        return this.Call(arg);
      case "loot":
        return this.Loot(player);
      case "vendor":
        return this.Vendor(player, arg);
      default:
        return "fail:unknown_action";
    }
  }

  // target nearest [RANGE] | hostile [RANGE] | proxy | view | none: the
  // nearest living NPC (or hostile one) within RANGE m (30), the nearest
  // shared NPC of the session, what V looks at, or nothing.
  private func Target(player: ref<PlayerPuppet>, kind: String, rangeText: String) -> String {
    this.m_target = null;
    if Equals(kind, "none") {
      return "ok";
    }
    if Equals(kind, "view") {
      this.m_target = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false);
    } else if Equals(kind, "proxy") {
      if IsDefined(this.m_system) && IsDefined(this.m_system.Npcs()) {
        this.m_target = this.m_system.Npcs().NearestProxy(player.GetWorldPosition());
      }
    } else if Equals(kind, "nearest") || Equals(kind, "hostile") {
      let range = StringToFloat(rangeText, 30.0);
      let best = range + 1.0;
      let at = player.GetWorldPosition();
      for npc in player.GetNPCsAroundObject(range > 0.0 ? range : 30.0) {
        if IsDefined(npc) && !npc.IsDead() && !ScriptedPuppet.IsDefeated(npc) {
          let hostile = Equals(GameObject.GetAttitudeTowards(npc, player), EAIAttitude.AIA_Hostile);
          let distance = Vector4.Distance(at, npc.GetWorldPosition());
          if distance < best && (hostile || Equals(kind, "nearest")) {
            best = distance;
            this.m_target = npc;
          }
        }
      }
    } else {
      return "fail:target_nearest|hostile|proxy|view|none";
    }
    if !IsDefined(this.m_target) {
      return "fail:nothing_found";
    }
    return s"ok:\(NameToString(this.m_target.GetClassName()))_\(TandemPilot.F(Vector4.Distance(player.GetWorldPosition(), this.m_target.GetWorldPosition())))m";
  }

  // look target [SECONDS] | look X Y Z [SECONDS] | look stop: the view
  // turned to a point by the game's own look-at (what quests use to turn V's
  // head), kept there for SECONDS (1).
  private func LookCommand(player: ref<PlayerPuppet>, words: array<String>, now: Float) -> String {
    let first = ArraySize(words) > 3 ? words[3] : "";
    let seconds = 1.0;
    if Equals(first, "stop") {
      this.StopLook();
      return "ok";
    }
    if Equals(first, "target") {
      if !IsDefined(this.m_target) {
        return "fail:no_target";
      }
      this.m_lookTarget = true;
      if ArraySize(words) > 4 {
        let text = words[4];
        seconds = StringToFloat(text, 1.0);
      }
    } else if ArraySize(words) >= 6 {
      let x = words[3];
      let y = words[4];
      let z = words[5];
      this.m_lookTarget = false;
      this.m_lookAt = new Vector4(StringToFloat(x), StringToFloat(y), StringToFloat(z), 1.0);
      if ArraySize(words) > 6 {
        let text = words[6];
        seconds = StringToFloat(text, 1.0);
      }
    } else {
      return "fail:look_target|X_Y_Z|stop";
    }
    this.m_lookUntil = now + MaxF(0.2, MinF(seconds, 60.0));
    this.m_lookNext = now + 0.2;
    this.Look(player, 0.3);
    return "ok";
  }

  private func Look(player: ref<PlayerPuppet>, duration: Float) -> Void {
    if this.m_lookTarget {
      if !IsDefined(this.m_target) {
        this.StopLook();
        return;
      }
      this.m_lookAt = this.AimPoint(this.m_target);
    }
    let request: AimRequest;
    request.duration = duration;
    request.adjustPitch = true;
    request.adjustYaw = true;
    request.endOnAimingStopped = false;
    request.endOnCameraInputApplied = false;
    request.precision = 0.01;
    request.easeIn = true;
    request.easeOut = true;
    request.checkRange = false;
    request.processAsInput = true;
    request.bodyPartsTracking = false;
    // Every field is set: the VM does not clear a struct local.
    request.endOnTargetReached = false;
    request.endOnTimeExceeded = true;
    request.maxDuration = duration + 0.2;
    request.cameraInputMagToBreak = 0.0;
    request.cameraMouseInputMagToBreak = 0.0;
    request.bptMaxDot = 0.0;
    request.bptMaxSwitches = 0.0;
    request.bptMinInputMag = 0.0;
    request.bptMinResetInputMag = 0.0;
    request.lookAtTarget = this.m_lookAt;
    let targeting = GameInstance.GetTargetingSystem(this.m_game);
    targeting.BreakAimSnap(player);
    targeting.LookAt(player, request);
  }

  private func StopLook() -> Void {
    if this.m_lookUntil > 0.0 {
      let player = GetPlayer(this.m_game);
      if IsDefined(player) {
        GameInstance.GetTargetingSystem(this.m_game).BreakLookAt(player);
      }
    }
    this.m_lookUntil = 0.0;
    this.m_lookTarget = false;
  }

  // equip weapon | slot1..slot4 | fists | holster: the equipment system's
  // own request (what the weapon keys send).
  private func Equip(player: ref<PlayerPuppet>, what: String) -> String {
    let request = new EquipmentSystemWeaponManipulationRequest();
    request.owner = player;
    switch what {
      case "weapon":
        request.requestType = EquipmentManipulationAction.RequestLastUsedOrFirstAvailableWeapon;
        break;
      case "slot1":
        request.requestType = EquipmentManipulationAction.RequestWeaponSlot1;
        break;
      case "slot2":
        request.requestType = EquipmentManipulationAction.RequestWeaponSlot2;
        break;
      case "slot3":
        request.requestType = EquipmentManipulationAction.RequestWeaponSlot3;
        break;
      case "slot4":
        request.requestType = EquipmentManipulationAction.RequestWeaponSlot4;
        break;
      case "fists":
        request.requestType = EquipmentManipulationAction.RequestFists;
        break;
      case "holster":
        request.requestType = EquipmentManipulationAction.UnequipWeapon;
        break;
      default:
        return "fail:equip_weapon|slot1..slot4|fists|holster";
    }
    GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"EquipmentSystem").QueueRequest(request);
    return "ok:asked";
  }

  // fire [SECONDS] | fire stop: the trigger of the weapon in V's hands held
  // for SECONDS (0.4), the way quests make V shoot (QuestForceShoot): the
  // weapon's own state machine fires, with its ammo, recoil and sound.
  private func Fire(player: ref<PlayerPuppet>, arg: String, now: Float) -> String {
    if Equals(arg, "stop") {
      this.StopFire();
      return "ok";
    }
    if !IsDefined(GameObject.GetActiveWeapon(player)) {
      return "fail:no_weapon_in_hand";
    }
    let seconds = StringToFloat(arg, 0.4);
    this.m_fireUntil = now + MaxF(0.05, MinF(seconds, 10.0));
    this.m_firing = true;
    player.GetPlayerStateMachineBlackboard().SetBool(GetAllBlackboardDefs().PlayerStateMachine.QuestForceShoot, true);
    this.PullTrigger(player);
    return "ok";
  }

  private func PullTrigger(player: ref<PlayerPuppet>) -> Void {
    let pull = new PSMPostponedParameterBool();
    pull.id = n"questForceShoot";
    pull.value = true;
    player.QueueEvent(pull);
  }

  private func StopFire() -> Void {
    if !this.m_firing {
      return;
    }
    this.m_firing = false;
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      player.GetPlayerStateMachineBlackboard().SetBool(GetAllBlackboardDefs().PlayerStateMachine.QuestForceShoot, false);
    }
  }

  // (a shortcut: no reload animation; the pad's X is the real one)
  private func Reload(player: ref<PlayerPuppet>) -> String {
    let weapon = GameObject.GetActiveWeapon(player);
    if !IsDefined(weapon) {
      return "fail:no_weapon_in_hand";
    }
    weapon.StartReload();
    return "ok";
  }

  // ability [sandevistan | off]: the operating system cyberware V wears
  // (Sandevistan, Berserk), used as its hotkey does; "sandevistan" asks the
  // state machine for it directly, "off" ends it.
  private func Ability(player: ref<PlayerPuppet>, which: String) -> String {
    if Equals(which, "off") {
      let stop = new PSMPostponedParameterBool();
      stop.id = n"requestSandevistanDeactivation";
      stop.value = true;
      player.QueueEvent(stop);
      return "ok";
    }
    if Equals(which, "sandevistan") {
      if !TimeDilationHelper.CanUseTimeDilation(player) {
        return "fail:time_dilation_not_allowed_now";
      }
      let start = new PSMPostponedParameterBool();
      start.id = n"requestSandevistanActivation";
      start.value = true;
      player.QueueEvent(start);
      return "ok:asked";
    }
    let item = EquipmentSystem.GetData(player).GetActiveItem(gamedataEquipmentArea.SystemReplacementCW);
    if !ItemID.IsValid(item) {
      return "fail:no_operating_system_cyberware";
    }
    return ItemActionsHelper.UseItem(player, item) ? s"ok:\(TDBID.ToStringDEBUG(ItemID.GetTDBID(item)))" : "fail:not_usable_now";
  }

  // (a shortcut: the heal with no animation; the pad's d-pad up is the real one)
  private func Heal(player: ref<PlayerPuppet>) -> String {
    let item = EquipmentSystem.GetData(player).GetItemIDFromHotkey(EHotkey.DPAD_UP);
    if !ItemID.IsValid(item) {
      return "fail:no_consumable_on_the_hotkey";
    }
    ItemActionsHelper.UseHealCharge(player, item);
    return "ok";
  }

  // hurt PERCENT | hurt all: V's health down by PERCENT of its maximum, or
  // to nothing (in a session with another V up, that is going down:
  // Revive.reds; alone it is death and the death menu).
  private func Hurt(player: ref<PlayerPuppet>, arg: String) -> String {
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let id = Cast<StatsObjectID>(player.GetEntityID());
    if Equals(arg, "all") {
      let revive = TandemRevive.Of(this.m_game);
      if !IsDefined(revive) || !revive.Protected() {
        return "fail:nobody_to_revive_V_it_would_die";
      }
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, 0.0, null, true);
      return "ok";
    }
    let percent = StringToFloat(arg, 0.0);
    if percent <= 0.0 || percent >= 100.0 {
      return "fail:hurt_PERCENT|all";
    }
    let health = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, true);
    pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, MaxF(1.0, health - percent), null, true);
    return "ok";
  }

  // hack [RECORD]: a quickhack from our V on the target, the game's own way
  // to run one (cost, upload, effect; Hacks.reds reports the upload).
  private func Hack(record: String) -> String {
    let puppet = this.m_target as ScriptedPuppet;
    if !IsDefined(puppet) {
      return "fail:no_npc_target";
    }
    if puppet.IsDead() {
      return "fail:target_dead";
    }
    let run = new ExecutePuppetActionEvent();
    run.actionID = TDBID.Create(Equals(record, "") ? "QuickHack.OverheatLvl1Hack" : record);
    puppet.QueueEvent(run);
    return "ok:asked";
  }

  // takedown grab | kill | nonlethal: what choosing the prompt on an NPC
  // queues on V (StartTakedownEvent): V slides to the target and its
  // takedown state machine plays the move. From within 3.5 m.
  private func Takedown(player: ref<PlayerPuppet>, how: String) -> String {
    let puppet = this.m_target as ScriptedPuppet;
    if !IsDefined(puppet) {
      return "fail:no_npc_target";
    }
    if puppet.IsDead() || ScriptedPuppet.IsDefeated(puppet) {
      return "fail:target_down";
    }
    if Vector4.Distance(player.GetWorldPosition(), puppet.GetWorldPosition()) > 3.5 {
      return "fail:too_far";
    }
    let start = new StartTakedownEvent();
    start.slideTime = 0.8;
    start.target = puppet;
    switch how {
      case "grab":
        start.actionName = n"GrappleTarget";
        break;
      case "kill":
        start.actionName = n"Takedown";
        break;
      case "nonlethal":
        start.actionName = n"TakedownNonLethal";
        break;
      default:
        return "fail:takedown_grab|kill|nonlethal";
    }
    player.QueueEvent(start);
    return "ok:asked";
  }

  // The vehicle an action is about: the one V looks at, else the car V
  // called, within 15 m.
  private func VehicleNear(player: ref<PlayerPuppet>) -> ref<VehicleObject> {
    let seen = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false) as VehicleObject;
    if IsDefined(seen) {
      return seen;
    }
    let defs = GetAllBlackboardDefs();
    let called = GameInstance.GetBlackboardSystem(this.m_game).Get(defs.VehicleSummonData).GetEntityID(defs.VehicleSummonData.SummonedVehicleEntityID);
    let car = GameInstance.FindEntityByID(this.m_game, called) as VehicleObject;
    if IsDefined(car) && Vector4.Distance(player.GetWorldPosition(), car.GetWorldPosition()) <= 15.0 {
      return car;
    }
    return null;
  }

  // mount [SLOT]: V into the vehicle it looks at (or its called car), the
  // driver's seat by default, with the entry animation (a mounting request
  // that is not instant: V's vehicle state machine plays it).
  private func Mount(player: ref<PlayerPuppet>, slotText: String) -> String {
    if GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(player.GetEntityID()) {
      return "fail:already_mounted";
    }
    let car = this.VehicleNear(player);
    if !IsDefined(car) {
      return "fail:no_vehicle_in_view_or_called";
    }
    let slot = Equals(slotText, "") ? n"seat_front_left" : StringToName(slotText);
    if !TandemVehicles.FreeFor(this.m_game, car, slot, player.GetEntityID()) {
      return "fail:seat_taken";
    }
    let info: MountingInfo;
    info.parentId = car.GetEntityID();
    info.childId = player.GetEntityID();
    info.slotId.id = slot;
    let request = new MountingRequest();
    request.lowLevelMountingInfo = info;
    request.preservePositionAfterMounting = false;
    request.mountData = new MountEventData();
    request.mountData.isInstant = false;
    request.mountData.slotName = slot;
    request.mountData.mountParentEntityId = car.GetEntityID();
    let options = new MountEventOptions();
    options.alive = true;
    options.occupiedByNonFriendly = false;
    request.mountData.mountEventOptions = options;
    GameInstance.GetMountingFacility(this.m_game).Mount(request);
    return s"ok:\(NameToString(slot))";
  }

  // exit: out of the vehicle, with the exit animation.
  private func Exit(player: ref<PlayerPuppet>) -> String {
    let car: wref<VehicleObject>;
    if !VehicleComponent.GetVehicle(this.m_game, player, car) || !IsDefined(car) {
      return "fail:not_in_a_vehicle";
    }
    let slot = n"None";
    VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
    let info: MountingInfo;
    info.parentId = car.GetEntityID();
    info.childId = player.GetEntityID();
    info.slotId.id = slot;
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = info;
    request.mountData = new MountEventData();
    request.mountData.isInstant = false;
    GameInstance.GetMountingFacility(this.m_game).Unmount(request);
    return "ok:asked";
  }

  // autodrive on | off | roam: patch 2.3's AutoDrive, as its key asks for
  // it. It drives to the tracked objective, or cruises when nothing is
  // tracked; the game refuses off the road, in combat, in a car not V's.
  private func Autodrive(player: ref<PlayerPuppet>, arg: String) -> String {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"AutoDriveSystem") as AutoDriveSystem;
    if !IsDefined(system) {
      return "fail:no_autodrive_system";
    }
    if Equals(arg, "off") {
      system.QueueRequest(new DisableAutoDriveRequest());
      return "ok:asked";
    }
    if Equals(arg, "roam") {
      system.QueueRequest(new ToggleFreeRoamRequest());
      return "ok:asked";
    }
    if !VehicleComponent.IsDriver(this.m_game, player) {
      return "fail:not_driving";
    }
    if !system.GetAutodriveAvailable() {
      return "fail:not_available_here";
    }
    system.QueueRequest(new EnableAutoDriveRequest());
    return "ok:asked";
  }

  // device [ACTION]: the device V looks at (a door, a terminal, a lift
  // panel) does ACTION as if V had chosen it on its prompt (the device's
  // own ExecuteAction: cost, skill checks, animation); with no ACTION, the
  // actions it offers V are listed.
  private func Device(player: ref<PlayerPuppet>, name: String) -> String {
    let seen = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false) as Device;
    if !IsDefined(seen) {
      return "fail:no_device_in_view";
    }
    let state = seen.GetDevicePS();
    let context = state.GenerateContext(gamedeviceRequestType.Direct, Device.GetInteractionClearance(), player, player.GetEntityID());
    let actions: array<ref<DeviceAction>>;
    state.GetActions(actions, context);
    let offered = "";
    for action in actions {
      if Equals(name, NameToString(action.actionName)) {
        return seen.TandemPilotExecute(action, player) ? s"ok:\(name)" : "fail:not_a_scriptable_action";
      }
      offered += (Equals(offered, "") ? "" : ",") + NameToString(action.actionName);
    }
    if Equals(name, "") {
      return s"ok:\(NameToString(seen.GetClassName())):\(offered)";
    }
    return s"fail:\(NameToString(seen.GetClassName()))_offers:\(offered)";
  }

  // lift FLOOR: the lift V looks at (its panel, from inside) goes to FLOOR
  // (0 is its lowest), as choosing the floor on the panel does.
  private func Lift(player: ref<PlayerPuppet>, floorText: String) -> String {
    let seen = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false) as LiftDevice;
    if !IsDefined(seen) {
      return "fail:no_lift_in_view";
    }
    let state = seen.GetDevicePS();
    let floorList = state.GetFloors();
    let floors = ArraySize(floorList);
    let floor = StringToInt(floorText, -1);
    if floor < 0 || floor >= floors {
      return s"fail:floors_0_to_\(floors - 1)_now_\(state.GetActiveFloor())";
    }
    let action = state.ActionGoToFloor(floors, state.GetActiveFloor(), floor, floorText, true);
    return seen.TandemPilotExecute(action, player) ? s"ok:from_\(state.GetActiveFloor())" : "fail:not_executed";
  }

  // call answer | reject: the incoming call, as the phone's HUD answers it.
  private func Call(arg: String) -> String {
    let phone = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PhoneSystem") as PhoneSystem;
    if !IsDefined(phone) {
      return "fail:no_phone_system";
    }
    if !Equals(arg, "answer") && !Equals(arg, "reject") {
      return "fail:call_answer|reject";
    }
    return phone.TandemPilotAnswer(Equals(arg, "reject")) ? "ok" : "fail:no_incoming_call";
  }

  // loot: everything the target (a body) or what V looks at (a container)
  // holds, to V: the game's own "take all".
  private func Loot(player: ref<PlayerPuppet>) -> String {
    let source: ref<GameObject> = this.m_target;
    if !IsDefined(source) {
      source = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false);
    }
    if !IsDefined(source) {
      return "fail:no_target_and_nothing_in_view";
    }
    if Vector4.Distance(player.GetWorldPosition(), source.GetWorldPosition()) > 4.0 {
      return "fail:too_far";
    }
    // A body, not somebody standing: their things are theirs.
    let owner = source as ScriptedPuppet;
    if IsDefined(owner) && !owner.IsDead() && !ScriptedPuppet.IsDefeated(owner) {
      return "fail:target_alive";
    }
    return GameInstance.GetTransactionSystem(this.m_game).TransferAllItems(source, player) ? "ok" : "fail:nothing_to_take";
  }

  // vendor list | buy: the target's (or the NPC V looks at) wares counted,
  // or its first item V can pay for bought, through the market system's
  // own request (price, money, stock).
  private func Vendor(player: ref<PlayerPuppet>, arg: String) -> String {
    let vendor: ref<GameObject> = this.m_target;
    if !IsDefined(vendor) {
      vendor = GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, false, false);
    }
    if !IsDefined(vendor) {
      return "fail:no_target_and_nothing_in_view";
    }
    let wares = MarketSystem.GetVendorItemsForSale(vendor, true);
    if Equals(arg, "list") {
      return s"ok:\(ArraySize(wares))_items";
    }
    if !Equals(arg, "buy") {
      return "fail:vendor_list|buy";
    }
    if ArraySize(wares) == 0 {
      return "fail:nothing_V_can_buy";
    }
    let line: TransactionRequestData;
    line.itemID = wares[0].itemID;
    line.quantity = 1;
    line.powerLevel = 0.0;
    let request = new BuyRequest();
    request.owner = vendor;
    ArrayPush(request.items, line);
    MarketSystem.GetInstance(this.m_game).QueueRequest(request);
    return s"ok:\(TDBID.ToStringDEBUG(ItemID.GetTDBID(line.itemID)))";
  }

  // pilot probe: what could replace the gamepad one day, in tandem's log.
  // The game's own functional-test system has FakeInput* functions (input
  // by action name, no device); whether a retail game has an instance of it
  // is what this reads. Nothing is called.
  private func Probe() -> Void {
    let player = GetPlayer(this.m_game);
    TandemNative.Log(s"pilot probe: functional tests: \(TandemPilotNative.ProbeFunctionalTests())");
    if IsDefined(player) {
      TandemNative.Log(s"pilot probe: the player has an AI component: \(IsDefined(player.GetAIControllerComponent()))");
      let locks: array<gameSaveLock>;
      TandemNative.Log(s"pilot probe: saving locked: \(GameInstance.IsSavingLocked(this.m_game, locks)) (\(ArraySize(locks)) locks)");
    }
  }
}

// A device does an action for an executor, as from its own prompt.
@addMethod(Device)
public final func TandemPilotExecute(action: ref<DeviceAction>, executor: ref<GameObject>) -> Bool {
  return this.ExecuteAction(action, executor);
}

// The call on V's phone: 1 ringing for V, 2 another phase, 0 none.
@addMethod(PhoneSystem)
public final func TandemPilotCall() -> Int32 {
  if Equals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.IncomingCall) {
    return 1;
  }
  if Equals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.Undefined) || Equals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.EndCall) {
    return 0;
  }
  return 2;
}

// V answers (or rejects) the call that rings, as the phone's HUD does.
@addMethod(PhoneSystem)
public final func TandemPilotAnswer(reject: Bool) -> Bool {
  if NotEquals(this.m_LastCallInformation.callPhase, questPhoneCallPhase.IncomingCall) {
    return false;
  }
  let request = new PickupPhoneRequest();
  request.CallInformation = this.m_LastCallInformation;
  request.shouldBeRejected = reject;
  this.QueueRequest(request);
  return true;
}
