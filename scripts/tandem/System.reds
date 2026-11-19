// The game-thread side of a session: samples the local V, spawns and removes
// the other players' puppets, and hands each puppet to the plugin's movement
// controller. Movement itself happens in the plugin, on the animation thread.

public class TandemPuppet {
  public let player: Uint32;
  public let name: String;
  public let id: EntityID;
  public let controlled: Bool;
  public let record: String;
  public let body: Int32;
  public let dressed: Bool;
  public let seenAt: Float;
  public let dressedAt: Float;
  // A body look is on it (theirs, or a stand-in): V's templates have no
  // meshes of their own, and clothes alone would float in the air.
  public let bodyOn: Bool;
  public let lookRetryAt: Float;
  // The looks received for this player that the puppet wears (0: ours, as a
  // stand-in), and the clothes it was given.
  public let appearanceChanges: Int32;
  public let worn: array<TweakDBID>;
  // The held items received for this player that the puppet holds.
  public let heldChanges: Int32;
  // Vehicles: the seats received for this player that the puppet sits in;
  // the copy of their car while they drive it; where the puppet is mounted;
  // a seat waiting for its vehicle to exist.
  public let seatChanges: Int32;
  public let car: EntityID;
  public let seatedIn: EntityID;
  public let seatedSlot: CName;
  public let wantsSeat: Bool;
  // Out of a seat: our movement controller goes back on at this time.
  public let reattachAt: Float;
  // No free seat for it in the vehicle it rides here (a quest NPC sits
  // there): hidden until one frees; tried again at this time.
  public let seatRetryAt: Float;
  // Rides (Transit.reds): the rides received we acted on; on the NCART, 1
  // seated in a train of ours, 2 hidden and carried along; that train; when
  // the hidden body was last placed; the notices shown; its last station.
  public let transitChanges: Int32;
  public let transitMode: Int32;
  public let transitTrain: EntityID;
  public let transitPlacedAt: Float;
  public let transitNoticed: Bool;
  public let transitSkipNoticed: Bool;
  public let transitAt: Int32;
  public let stance: gamedataNPCStanceState;
  public let stanceFeature: CName;
  // Its attitude group (Attitudes.reds), as read back once set.
  public let grouped: Bool;
  public let group: CName;
  // Teleports the movement controller asked for (SnapPuppet).
  public let snaps: Int32;
  // Consistency (CheckPuppets): since when its player is not in the session.
  public let orphanSince: Float;
}

// A puppet on its way out, removed over several frames: deleting a vehicle
// with a body mounted in it, then the body, in one frame froze the game
// (2026-11-18, engine hang dump, all threads waiting).
public class TandemLeaving {
  public let player: Uint32;
  public let id: EntityID;
  public let car: EntityID;
  public let since: Float;
  public let bodyGone: Bool;
}

// A player who joined before V's bodies were ready.
public class TandemJoin {
  public let player: Uint32;
  public let name: String;
  public let body: Int32;
}

public class TandemTick extends DelayCallback {
  public let system: wref<TandemSystem>;
  // The chain it belongs to: a session starting or ending starts a new one.
  public let gen: Int32;

  public func Call() -> Void {
    if IsDefined(this.system) {
      this.system.OnTick(this.gen);
    }
  }
}

public class TandemSystem extends ScriptableSystem {
  private let m_inGame: Bool;
  private let m_puppets: array<ref<TandemPuppet>>;
  private let m_lastSampleTime: Float;
  private let m_lastPosition: Vector4;
  private let m_haveLast: Bool;
  private let m_probe: ref<TandemProbe>;
  private let m_bodies: ref<TandemBodies>;
  private let m_joins: array<ref<TandemJoin>>;
  private let m_lastCommandCheck: Float;
  private let m_attachedAt: Float;
  // (tools/game.sh cmd record NAME) a record every puppet spawns from.
  private let m_recordOverride: String;
  private let m_gates: ref<TandemGates>;
  private let m_vehicleProbe: ref<TandemVehicleProbe>;
  private let m_leaving: array<ref<TandemLeaving>>;
  private let m_rideCar: EntityID;
  private let m_rideSlot: CName;
  // (test) "ride npc": the NPC to seat in our vehicle once it exists.
  private let m_rideNpc: EntityID;
  private let m_rideNpcSeated: Bool;
  private let m_world: ref<TandemWorld>;
  private let m_pause: ref<TandemPause>;
  private let m_radio: ref<TandemRadio>;
  private let m_time: ref<TandemTime>;
  // The NCART, rides and quest vehicles (Transit.reds).
  private let m_transit: ref<TandemTransit>;
  // Our V and the others in other worlds (Elsewhere.reds), and why each
  // puppet is hidden (PuppetVisibility.reds: private scenes, other worlds).
  private let m_elsewhere: ref<TandemElsewhere>;
  private let m_visibility: ref<TandemVisibility>;
  private let m_travel: ref<TandemTravel>;
  private let m_proxyTests: array<ref<TandemProxyTest>>;
  private let m_npcs: ref<TandemNpcs>;
  // Stealth, detection and combat per player, camo, noises (Stealth.reds).
  private let m_stealth: ref<TandemStealth>;
  // Down, revive, respawn (Revive.reds).
  private let m_revive: ref<TandemRevive>;
  // Hit outcomes: strikes, wounds, poise, difficulty, area hits (Outcomes.reds).
  private let m_outcomes: ref<TandemOutcomes>;
  // (test) hostile NPCs spawned by "npc hostile", and those made hostile.
  private let m_testNpcs: array<EntityID>;
  private let m_testHostile: array<EntityID>;
  private let m_lastHostileReport: Float;
  // (test) the attitude group puppets are put in instead of their own
  // ("puppet group NAME"; "puppet group" alone: their own again).
  private let m_puppetGroup: CName;
  private let m_attitudes: ref<TandemAttitudes>;
  // (test) "npc hostile": its attitude to our V (hostile unless "npc hostile puppets").
  private let m_hostileToV: Bool;
  private let m_hostileReports: Int32;
  // Car copies their drivers left (kept: see ProcessLeaving).
  private let m_parked: array<EntityID>;
  private let m_playing: Bool;
  // Our look as last sent.
  private let m_sentItems: array<TweakDBID>;
  private let m_sentAppearance: Bool;
  private let m_lastAppearanceCheck: Float;
  // What our V holds, as last sent.
  private let m_sentRight: TweakDBID;
  private let m_sentLeft: TweakDBID;
  private let m_sentHeld: Bool;
  // Our player id in the session, and our seat as last sent.
  private let m_you: Uint32;
  private let m_sentVehicle: TweakDBID;
  private let m_sentSlot: CName;
  private let m_sentOwner: Uint32;
  private let m_sentSeat: Bool;
  // A game is loaded (our V in a world that is not the main menu's): the
  // co-op menu can start a session in it.
  private let m_attached: Bool;
  // The current tick chain (TandemTick.gen).
  private let m_tickGen: Int32;
  // When we got into the current session (join notices come after it).
  private let m_sessionAt: Float;
  // Our V was down or out when we left a session: it is dying the game's
  // way (death screen, reload); nothing stands it up meanwhile (Revive.reds).
  private let m_leftDown: Bool;
  // Consistency (CheckPuppets): when the puppets were last checked against
  // the session's players, and the players without one, since when.
  private let m_lastPuppetCheck: Float;
  private let m_missingPlayers: array<Uint32>;
  private let m_missingSince: array<Float>;

  private func OnAttach() -> Void {
    if TandemNative.IsConfigured() {
      TandemNative.Log("system attached");
    }
  }

  public static func Of(game: GameInstance) -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
  }

  // A V attached to a world. The main menu's background scene attaches one
  // too, so the game's own pre-game flag decides. (Codeware's session events
  // were tried: they do not reach a system after a save loads.)
  private final func OnPlayerAttach(request: ref<PlayerAttachRequest>) -> Void {
    if GameInstance.GetSystemRequestsHandler().IsPreGame() {
      if TandemNative.IsConfigured() {
        TandemNative.Log("main menu world: not a game");
      }
      return;
    }
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    this.m_attached = true;
    this.m_leftDown = false;
    if TandemNative.IsConfigured() {
      this.BeginSession();
    } else {
      // No session yet: only listen for commands (the co-op menu needs nothing).
      this.m_tickGen += 1;
      this.Schedule(0.25);
    }
  }

  // The chosen session, in this game, now (at the load, or from the co-op
  // menu in game).
  public func BeginSession() -> Void {
    if this.m_inGame || !this.m_attached {
      return;
    }
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    let body = Equals(player.GetResolvedGenderName(), n"Male") ? 2 : 1;
    TandemNative.Log(s"in game, body \(body)");
    TandemNative.Start(body);
    this.m_inGame = true;
    this.m_haveLast = false;
    this.m_playing = false;
    this.m_sentAppearance = false;
    this.m_sentHeld = false;
    this.m_sentSeat = false;
    this.m_attachedAt = this.Now();
    this.m_bodies = new TandemBodies();
    this.m_bodies.Start(this.GetGameInstance());
    this.m_world = new TandemWorld();
    this.m_world.Start(this.GetGameInstance());
    this.m_pause = new TandemPause();
    this.m_pause.Start(this.GetGameInstance());
    this.m_radio = new TandemRadio();
    this.m_radio.Start(this.GetGameInstance());
    this.m_time = new TandemTime();
    this.m_time.Start(this.GetGameInstance());
    this.m_transit = new TandemTransit();
    this.m_transit.Start(this.GetGameInstance());
    this.m_visibility = new TandemVisibility();
    this.m_visibility.Start(this.GetGameInstance());
    this.m_elsewhere = new TandemElsewhere();
    this.m_elsewhere.Start(this.GetGameInstance());
    this.m_travel = new TandemTravel();
    this.m_travel.Start(this.GetGameInstance());
    this.m_npcs = new TandemNpcs();
    this.m_npcs.Start(this.GetGameInstance());
    this.m_attitudes = new TandemAttitudes();
    this.m_attitudes.Start(this.GetGameInstance());
    this.m_stealth = new TandemStealth();
    this.m_stealth.Start(this.GetGameInstance());
    this.m_revive = new TandemRevive();
    this.m_revive.Start(this.GetGameInstance(), this);
    if TandemNative.IsProbe() {
      this.m_probe = new TandemProbe();
      this.m_probe.Start(this.GetGameInstance());
    }
    this.m_tickGen += 1;
    this.Schedule(0.0);
    // The story side (Story.reds), which only starts with a session.
    let story = TandemStorySystem.Get(this.GetGameInstance());
    if IsDefined(story) {
      story.Begin();
    }
    let loot = TandemLoot.Get(this.GetGameInstance());
    if IsDefined(loot) {
      loot.Begin();
    }
  }

  private final func OnPlayerDetach(request: ref<PlayerDetachRequest>) -> Void {
    this.m_attached = false;
    this.m_tickGen += 1;
    if !this.m_inGame {
      return;
    }
    TandemNative.Log("left the game");
    this.EndSession(false);
  }

  // Out of the session, its puppets, proxies and borrowed state with it.
  // worldStays: we stay in this game (the co-op menu's Leave), so bodies go
  // the careful way and the game's own NPCs come back; else the world is
  // going away with them.
  private func EndSession(worldStays: Bool) -> Void {
    if !this.m_inGame {
      return;
    }
    TandemNative.PoseSource(null);
    this.m_inGame = false;
    this.m_you = 0u;
    this.RemoveAllPuppets();
    if !worldStays {
      // Nothing left to delete step by step.
      ArrayClear(this.m_leaving);
    }
    if IsDefined(this.m_npcs) {
      if worldStays {
        this.m_npcs.End();
      } else {
        this.m_npcs.Stop();
      }
    }
    this.m_npcs = null;
    this.m_world = null;
    this.m_attitudes = null;
    if IsDefined(this.m_stealth) {
      this.m_stealth.End();
    }
    this.m_stealth = null;
    if IsDefined(this.m_revive) {
      // Down or out and leaving (the menu's Leave, another session): our V
      // dies the game's normal way, in the world that stays.
      if worldStays && this.m_revive.DieIfDown("we left the session") {
        this.m_leftDown = true;
      }
      this.m_revive.Stop();
    }
    this.m_revive = null;
    ArrayClear(this.m_joins);
    if IsDefined(this.m_pause) {
      this.m_pause.Stop();
      this.m_pause = null;
    }
    if IsDefined(this.m_radio) {
      this.m_radio.Stop();
      this.m_radio = null;
    }
    if IsDefined(this.m_time) {
      this.m_time.Stop();
      this.m_time = null;
    }
    if IsDefined(this.m_transit) {
      this.m_transit.End();
    }
    this.m_transit = null;
    if IsDefined(this.m_elsewhere) {
      this.m_elsewhere.Stop();
      this.m_elsewhere = null;
    }
    this.m_visibility = null;
    if IsDefined(this.m_travel) {
      this.m_travel.Stop();
      this.m_travel = null;
    }
    if IsDefined(this.m_bodies) {
      this.m_bodies.Stop();
      this.m_bodies = null;
    }
    if IsDefined(this.m_probe) {
      this.m_probe.Stop();
      this.m_probe = null;
    }
    TandemNative.Stop();
    this.m_tickGen += 1;
    if worldStays && this.m_attached {
      // Bodies on their way out, and commands.
      this.Schedule(0.25);
    }
  }

  public func LeftDown() -> Bool {
    return this.m_leftDown;
  }

  // No save while our V is down or out (or dying after it left down): a
  // game killed there reloads from before the fight, like any death
  // (ScriptableSystem.IsSavingLocked, final.reds:48411; the save locks ask
  // every scriptable system).
  // A guest never saves in a session, whatever tandem.ini's `story` says:
  // its world is the host's and it plays at the host's difficulty
  // (Outcomes.reds), and a game killed there must load its own save as it
  // was. Nor while the host's difficulty is still on after a session.
  private func IsSavingLocked() -> Bool {
    if this.m_leftDown || (IsDefined(this.m_revive) && this.m_revive.IsDown()) {
      return true;
    }
    if this.m_inGame && this.m_you != 0u && !TandemNative.IsHost() {
      return true;
    }
    return IsDefined(this.m_outcomes) && this.m_outcomes.FollowsHost();
  }

  // ---- the co-op menu, in game (Coop.reds) ----

  public func InWorld() -> Bool {
    return this.m_attached;
  }

  public func InSession() -> Bool {
    return this.m_inGame;
  }

  // Host this game now (port 0: tandem.ini's), out of any session it was in.
  public func CoopHost(port: Int32) -> Bool {
    this.EndSession(true);
    TandemNative.StartHost(port);
    this.BeginSession();
    return this.m_inGame;
  }

  // Join "address[:port]", or a join code (through tandem.ini's relay), now.
  // False: neither (nothing changed), or a code with no relay set (then out
  // of the session it was in).
  public func CoopJoin(address: String) -> Bool {
    if TandemCoop.IsCode(address) {
      this.EndSession(true);
      if !TandemNative.JoinByCode(address) {
        return false;
      }
      this.BeginSession();
      return this.m_inGame;
    }
    let target = TandemNative.ParseAddress(address);
    if StrLen(target.address) == 0 {
      return false;
    }
    this.EndSession(true);
    TandemNative.Join(target.address, target.port);
    this.BeginSession();
    return this.m_inGame;
  }

  // Leaving is never refused: a player can always quit the game anyway. A
  // V that is down or out when it leaves dies the game's way (EndSession):
  // leaving is no way out of being down.
  public func CoopLeave() -> Void {
    this.EndSession(true);
    TandemNative.Leave();
    TandemCoop.Notice(this.GetGameInstance(), "You left the co-op session");
  }

  // delay 0 s: the next frame.
  private func Schedule(delay: Float) -> Void {
    let tick = new TandemTick();
    tick.system = this;
    tick.gen = this.m_tickGen;
    // Real time (false): not slowed by time dilation.
    GameInstance.GetDelaySystem(this.GetGameInstance()).DelayCallback(tick, delay, false);
  }

  // The shared pause menu (Pause.reds): driven every frame by the plugin,
  // since this tick stands still while the world is paused.
  public func PauseSync() -> ref<TandemPause> {
    return this.m_pause;
  }

  public func Stealth() -> ref<TandemStealth> {
    return this.m_stealth;
  }

  // Other worlds (Elsewhere.reds), and the puppets hidden for a reason.
  public func Elsewhere() -> ref<TandemElsewhere> {
    return this.m_elsewhere;
  }

  public func Visibility() -> ref<TandemVisibility> {
    return this.m_visibility;
  }

  // Slowed time (Time.reds), driven by Tick.
  public func TimeSync() -> ref<TandemTime> {
    return this.m_time;
  }

  // Guest: our hit takes the last of this proxy's health (Combat.reds).
  public func PredictKill(npc: Uint32) -> Void {
    if IsDefined(this.m_npcs) && this.m_you != 0u && !TandemNative.IsHost() {
      this.m_npcs.PredictKill(npc);
    }
  }

  // The NPCs shared in this session (host: ours; guest: the proxies).
  public func NpcsSync() -> ref<TandemNpcs> {
    return this.m_you != 0u ? this.m_npcs : null;
  }

  // The world runs (past the loading screen, see Tick).
  public func IsPlaying() -> Bool {
    return this.m_playing;
  }

  // The remote players who have a puppet here.
  public func PuppetPlayers() -> array<Uint32> {
    let out: array<Uint32>;
    for puppet in this.m_puppets {
      ArrayPush(out, puppet.player);
    }
    return out;
  }

  // Fast travel together and regroup (Travel.reds).
  public func Travel() -> ref<TandemTravel> {
    return this.m_travel;
  }

  public func Revive() -> ref<TandemRevive> {
    return this.m_revive;
  }

  public func OnTick(gen: Int32) -> Void {
    // An older chain (a session started or ended since), or no game.
    if gen != this.m_tickGen || !this.m_attached {
      return;
    }
    if !this.m_inGame {
      // No session: bodies of the last one on their way out, and commands
      // (tools/game.sh cmd coop ...).
      this.ProcessLeaving();
      this.HandleCommand();
      this.Schedule(0.25);
      return;
    }
    // The world's clock stands still behind the loading screen: two seconds
    // of it mean the player is in (tools/game.sh waits for this line).
    if !this.m_playing && this.Now() - this.m_attachedAt > 2.0 {
      this.m_playing = true;
      TandemNative.Log("playing");
      if IsDefined(this.m_pause) {
        this.m_pause.SetReady();
      }
    }
    this.SampleLocal();
    this.SendAppearance();
    this.SendHeld();
    this.SendSeat();
    if IsDefined(this.m_world) {
      this.m_world.Tick(this.Now());
    }
    // Not behind the loading screen: listing NPCs while the world streams in
    // hung the load (2026-11-18).
    // Only in a session (m_you: this game's player id while in one): a
    // guest between sessions has its own world back.
    if IsDefined(this.m_npcs) && this.m_playing && this.m_you != 0u {
      // On real time: under our own slow motion (Time.reds) the world's
      // clock runs slower, and the others must hear of our NPCs as often.
      this.m_npcs.Tick(EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGameInstance())), this.m_you);
    }
    if IsDefined(this.m_stealth) && this.m_playing && this.m_you != 0u {
      this.m_stealth.Tick(this.Now(), this.m_you, this.m_npcs);
    }
    TandemHacks.Tick(this.GetGameInstance(), this.Now(), this.m_playing ? this.m_you : 0u);
    // Grapples, takedowns, carried bodies (Takedowns.reds); 0: not in a session.
    TandemTakedowns.Get(this.GetGameInstance()).Tick(this.Now(), this.m_playing && IsDefined(this.m_npcs) ? this.m_you : 0u);
    if IsDefined(this.m_revive) && this.m_playing {
      this.m_revive.Tick(this.Now(), this.m_you);
    }
    if IsDefined(this.m_bodies) {
      this.m_bodies.Tick();
    }
    this.HandleEvents();
    this.SpawnJoined();
    if this.m_playing && this.m_you != 0u {
      this.CheckPuppets();
    }
    this.DrivePuppets();
    // Our ride and the others' (the NCART, rides, quest scene vehicles).
    if IsDefined(this.m_transit) && this.m_playing {
      this.m_transit.Tick(this.Now(), this, this.m_puppets);
    }
    if IsDefined(this.m_elsewhere) {
      this.m_elsewhere.Tick(this.Now(), this, this.m_playing ? this.m_you : 0u);
    }
    if IsDefined(this.m_visibility) {
      this.m_visibility.Tick(this.Now(), this);
    }
    // The car radio (Radio.reds): the driver's for everyone aboard.
    if IsDefined(this.m_radio) && this.m_playing {
      this.m_radio.Tick(this.Now(), this);
    }
    if IsDefined(this.m_travel) && this.m_playing {
      this.m_travel.Tick(this.Now(), this.m_you);
    }
    // Slowed time (Time.reds): every frame, in the script tick, once the
    // world runs and we are in a session. (It ran from the plugin's
    // per-frame call before: the game crashed there loading a save and froze
    // right after "playing", 2026-11-18.)
    if IsDefined(this.m_time) && this.m_playing && this.m_you != 0u {
      this.m_time.Update();
    }

    this.TickAttitudes();
    this.ProcessLeaving();
    this.DrawShots();
    this.TickProxyTests();
    this.TickHostileTests();
    this.HandleCommand();
    if IsDefined(this.m_probe) {
      this.m_probe.Tick();
    }
    if IsDefined(this.m_vehicleProbe) {
      this.m_vehicleProbe.Tick();
    }
    this.Schedule(0.0);
  }

  // ---- commands from the developer (tools/game.sh cmd), twice a second ----

  private func HandleCommand() -> Void {
    let now = EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
    if now - this.m_lastCommandCheck < 0.45 {
      return;
    }
    this.m_lastCommandCheck = now;
    if this.m_inGame {
      this.BindPoses();
      this.SeatRideNpc();
    }
    let command = TandemNative.TakeCommand();
    if Equals(command, "") {
      return;
    }
    this.RunCommand(command);
  }

  // Also from the pause menu (Coop.reds), which takes the commands while the
  // world, and so our tick, is paused.
  public func RunCommand(command: String) -> Void {
    if StrBeginsWith(command, "time ") {
      if IsDefined(this.m_time) {
        this.m_time.Command(command);
      }
      return;
    }
    if StrBeginsWith(command, "coop") {
      this.CoopCommand(command);
    } else if !this.m_inGame && !TandemSystem.SoloCommand(command) {
      TandemNative.Log(s"not in a session: \(command)");
    } else if Equals(command, "probe") {
      // Start over: new bodies, so a reloaded script or tweak takes part.
      if IsDefined(this.m_probe) {
        this.m_probe.Stop();
      }
      this.m_probe = new TandemProbe();
      this.m_probe.Start(this.GetGameInstance());
    } else if Equals(command, "probe stop") {
      if IsDefined(this.m_probe) {
        this.m_probe.Stop();
        this.m_probe = null;
      }
    } else if Equals(command, "bodies") {
      // Capture the donors again (after a hot reload of Body.reds or tweaks).
      if IsDefined(this.m_bodies) {
        this.m_bodies.Recapture();
      }
    } else if StrBeginsWith(command, "record") {
      this.m_recordOverride = StrLen(command) > 7 ? StrAfterFirst(command, " ") : "";
      TandemNative.Log(s"puppets spawn from \(Equals(this.m_recordOverride, "") ? "the configured records" : this.m_recordOverride)");
    } else if Equals(command, "crouch target") || Equals(command, "stand target") {
      // (experiment) the stance calls on the NPC V looks at.
      let game = this.GetGameInstance();
      let target = GameInstance.GetTargetingSystem(game).GetLookAtObject(GetPlayer(game), false, false) as NPCPuppet;
      if IsDefined(target) {
        let crouch = Equals(command, "crouch target");
        let stance = crouch ? gamedataNPCStanceState.Crouch : gamedataNPCStanceState.Stand;
        NPCPuppet.ChangeStanceState(target, stance);
        AnimationControllerComponent.SetAnimWrapperWeightOnOwnerAndItems(target, n"inCrouch", crouch ? 1.0 : 0.0);
        let feature = new AnimFeature_NPCState();
        feature.state = EnumInt(stance);
        AnimationControllerComponent.ApplyFeature(target, n"stanceState", feature);
        TandemNative.Log(s"target \(NameToString(target.GetClassName())): \(command)");
      } else {
        TandemNative.Log("no NPC in view");
      }
    } else if StrBeginsWith(command, "gates ") {
      if IsDefined(this.m_gates) {
        this.m_gates.Clear();
      }
      let records: array<String>;
      let rest = StrAfterFirst(command, " ");
      while StrLen(rest) > 0 {
        let word = StrContains(rest, " ") ? StrBeforeFirst(rest, " ") : rest;
        rest = StrContains(rest, " ") ? StrAfterFirst(rest, " ") : "";
        if StrLen(word) > 0 {
          ArrayPush(records, word);
        }
      }
      this.m_gates = new TandemGates();
      this.m_gates.Run(this.GetGameInstance(), records);
    } else if Equals(command, "gates clear") {
      if IsDefined(this.m_gates) {
        this.m_gates.Clear();
      }
    } else if Equals(command, "pose hook") {
      TandemNative.PoseHook();
    } else if Equals(command, "npc count") {
      TandemNative.Log(s"npcs shared: \(IsDefined(this.m_npcs) ? this.m_npcs.Count() : 0), own hidden: \(IsDefined(this.m_npcs) ? this.m_npcs.Hidden() : 0), in the session's record: \(IsDefined(this.m_npcs) ? this.m_npcs.SharedCount() : 0) (\(IsDefined(this.m_npcs) ? this.m_npcs.SharedListed() : 0) listed)");
    } else if Equals(command, "npc anims") {
      TandemNative.Log(s"npc \(IsDefined(this.m_npcs) ? this.m_npcs.AnimsReport() : "anims: no session")");
    } else if StrBeginsWith(command, "npc seek") {
      // (test) "npc seek [RANGE]": the nearest NPC in a workspot within
      // RANGE m (150), V moved within sharing range of it.
      let range = StrLen(command) > 9 ? StringToFloat(StrAfterFirst(command, "seek ")) : 150.0;
      if IsDefined(this.m_npcs) {
        this.m_npcs.SeekWorkspot(range > 0.0 ? range : 150.0);
      }
    } else if Equals(command, "proxy clear") {
      for test in this.m_proxyTests {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(test.id);
      }
      ArrayClear(this.m_proxyTests);
    } else if StrBeginsWith(command, "proxy test") {
      let record = StrLen(command) > 11 ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "Character.kab_tyger_claws_biker1_ranged1_saratoga_ma";
      let player = GetPlayer(this.GetGameInstance());
      let forward = player.GetWorldForward();
      let spec = new DynamicEntitySpec();
      spec.recordID = TDBID.Create(record);
      spec.position = player.GetWorldPosition();
      spec.position.X += forward.X * 8.0;
      spec.position.Y += forward.Y * 8.0;
      spec.orientation = player.GetWorldOrientation();
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      ArrayPush(spec.tags, n"Tandem.Proxy");
      let test = new TandemProxyTest();
      test.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
      test.since = this.Now();
      ArrayPush(this.m_proxyTests, test);
      TandemNative.Log(s"proxy test: \(record) spawning (\(EntityID.IsDefined(test.id)))");
    } else if StrBeginsWith(command, "npcs") {
      // (measure) the NPCs around V and how they are named: a placed NPC
      // should have the same static id in every game (ARCHITECTURE, identity).
      let player = GetPlayer(this.GetGameInstance());
      let range = 40.0;
      if StrLen(command) > 5 {
        let text = StrAfterFirst(command, " ");
        range = StringToFloat(text, 40.0);
      }
      let npcs = player.GetNPCsAroundObject(range);
      TandemNative.Log(s"npcs: \(ArraySize(npcs)) within \(range) m");
      for npc in npcs {
        let id = npc.GetEntityID();
        let at = npc.GetWorldPosition();
        TandemNative.Log(s"npcs: \(EntityID.ToDebugStringDecimal(id)) static \(EntityID.IsStatic(id)) \(TDBID.ToStringDEBUG(npc.GetRecordID())) \(NameToString(npc.GetCurrentAppearanceName())) at (\(at.X), \(at.Y), \(at.Z))");
      }
    } else if StrBeginsWith(command, "loot") {
      TandemLoot.Command(this.GetGameInstance(), command);
    } else if Equals(command, "puppet attitude") {
      this.ReportAttitudes();
    } else if StrBeginsWith(command, "puppet group") {
      // (test) the puppets' attitude group, now and for those to come
      // ("puppet group" alone: each its own again).
      this.m_puppetGroup = StrLen(command) > 13 ? StringToName(StrAfterFirst(command, "group ")) : n"";
      for puppet in this.m_puppets {
        puppet.grouped = false;
        this.Group(puppet);
      }
      TandemNative.Log(s"puppet group: \(IsNameValid(this.m_puppetGroup) ? NameToString(this.m_puppetGroup) : "their own")");
    } else if StrBeginsWith(command, "npc spawn") {
      // (test) "npc spawn [N]": N gangers (3) standing 6 m ahead, 2 m apart,
      // brain on, left as they are: NPCs to share when the street is empty
      // (rain, night), for recordings and takedown tests.
      let count = StrLen(command) > 10 ? StringToInt(StrAfterFirst(command, "spawn "), 3) : 3;
      let spawner = GetPlayer(this.GetGameInstance());
      let ahead = spawner.GetWorldForward();
      let side = spawner.GetWorldRight();
      let n = 0;
      while n < count {
        let offset = (Cast<Float>(n) - Cast<Float>(count - 1) * 0.5) * 2.0;
        let spec = new DynamicEntitySpec();
        spec.recordID = t"Character.kab_tyger_claws_biker1_ranged1_saratoga_ma";
        spec.position = spawner.GetWorldPosition();
        spec.position.X += ahead.X * 6.0 + side.X * offset;
        spec.position.Y += ahead.Y * 6.0 + side.Y * offset;
        spec.orientation = spawner.GetWorldOrientation();
        spec.alwaysSpawned = true;
        spec.persistState = false;
        spec.persistSpawn = false;
        spec.spawnInView = true;
        spec.active = true;
        ArrayPush(spec.tags, n"Tandem.Test");
        GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
        n += 1;
      }
      TandemNative.Log(s"npc spawn: \(count) gangers ahead");
    } else if StrBeginsWith(command, "npc hostile") && !Equals(command, "npc hostile clear") {
      // (test) a hostile ganger 12 m ahead, brain on: does it see, target
      // and shoot a remote player's puppet? (milestone 5) "npc hostile
      // puppets": neutral to our V, hostile to the puppets only.
      this.m_hostileToV = !Equals(command, "npc hostile puppets");
      let record = StrLen(command) > 12 && this.m_hostileToV ? StrAfterFirst(command, "hostile ") : "Character.kab_tyger_claws_biker1_ranged1_saratoga_ma";
      let player = GetPlayer(this.GetGameInstance());
      let forward = player.GetWorldForward();
      let spec = new DynamicEntitySpec();
      spec.recordID = TDBID.Create(record);
      spec.position = player.GetWorldPosition();
      spec.position.X += forward.X * 12.0;
      spec.position.Y += forward.Y * 12.0;
      spec.orientation = player.GetWorldOrientation();
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      ArrayPush(spec.tags, n"Tandem.Test");
      let id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
      ArrayPush(this.m_testNpcs, id);
      TandemNative.Log(s"npc hostile: \(record) spawning (\(EntityID.IsDefined(id)))");
    } else if Equals(command, "npc hostile clear") {
      for id in this.m_testNpcs {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(id);
      }
      ArrayClear(this.m_testNpcs);
      ArrayClear(this.m_testHostile);
      this.m_hostileReports = 0;
    } else if Equals(command, "god on") || Equals(command, "god off") {
      // (test) our V cannot die (a test fight must not end the session).
      let player = GetPlayer(this.GetGameInstance());
      let gods = GameInstance.GetGodModeSystem(this.GetGameInstance());
      if Equals(command, "god on") {
        let a = gods.AddGodMode(player.GetEntityID(), gameGodModeType.Invulnerable, n"Default");
        let b = gods.AddGodMode(player.GetEntityID(), gameGodModeType.Immortal, n"Default");
        TandemNative.Log(s"god on: invulnerable \(a), immortal \(b), has \(gods.HasGodMode(player.GetEntityID(), gameGodModeType.Invulnerable))");
      } else {
        gods.RemoveGodMode(player.GetEntityID(), gameGodModeType.Invulnerable, n"Default");
        gods.RemoveGodMode(player.GetEntityID(), gameGodModeType.Immortal, n"Default");
      }
      TandemNative.Log(s"\(command)");
    } else if StrBeginsWith(command, "npc punch") {
      // (test) a hit from our V on the nearest proxy, through the game's
      // damage pipeline as a real one would go (Combat.reds): lethal
      // ("npc punch"), or N physical damage ("npc punch N").
      // "npc punch burn": 10 damage that also sets it on fire.
      let burn = Equals(command, "npc punch burn");
      let amount = burn ? 10.0 : StrLen(command) > 10 ? StringToFloat(StrAfterFirst(command, "punch "), 0.0) : 0.0;
      let player = GetPlayer(this.GetGameInstance());
      let target = IsDefined(this.m_npcs) ? this.m_npcs.NearestProxy(player.GetWorldPosition()) : null;
      if IsDefined(target) {
        let context: AttackInitContext;
        context.record = TweakDBInterface.GetAttackRecord(t"Attacks.VehicleImpact");
        context.instigator = player;
        context.source = player;
        let hit = new gameHitEvent();
        hit.attackData = new AttackData();
        hit.target = target;
        if amount > 0.0 {
          if !IsDefined(hit.attackComputed) {
            hit.attackComputed = new gameAttackComputed();
          }
          hit.attackComputed.SetAttackValue(amount, gamedataDamageType.Physical);
          if burn {
            hit.attackData.AddStatusEffect(t"BaseStatusEffect.Burning", 1.0);
          }
        } else {
          hit.attackData.AddFlag(hitFlag.Kill, n"tandem_test");
        }
        hit.attackData.SetSource(player);
        hit.attackData.SetInstigator(player);
        hit.attackData.SetAttackDefinition(IAttack.Create(context));
        GameInstance.GetDamageSystem(this.GetGameInstance()).QueueHitEvent(hit, target);
        TandemNative.Log(s"npc punch: \(EntityID.ToDebugStringDecimal(target.GetEntityID()))");
      } else {
        TandemNative.Log("npc punch: no proxy");
      }
    } else if StrBeginsWith(command, "hack ") {
      // (test, Hacks.reds) "hack npc [RECORD]": our V quickhacks the nearest
      // proxy; "hack car": brakes on the first remote player's car copy.
      let hacker = GetPlayer(this.GetGameInstance());
      let proxy = IsDefined(this.m_npcs) && IsDefined(hacker) ? this.m_npcs.NearestProxy(hacker.GetWorldPosition()) : null;
      let copy: ref<VehicleObject>;
      for puppet in this.m_puppets {
        if !IsDefined(copy) && EntityID.IsDefined(puppet.car) {
          copy = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.car) as VehicleObject;
        }
      }
      TandemHacksTest.Run(this.GetGameInstance(), command, proxy, copy);
    } else if StrBeginsWith(command, "takedown") {
      TandemTakedowns.Get(this.GetGameInstance()).Command(command);
    } else if Equals(command, "vehicle clear") {
      // (experiment) delete the parked car copies: this froze the game
      // (engine-knowledge §13.6); run tools/game.sh bt if it does again.
      for car in this.m_parked {
        TandemNative.Log(s"vehicle clear: deleting \(EntityID.ToDebugStringDecimal(car))");
        GameInstance.GetDynamicEntitySystem().DeleteEntity(car);
      }
      ArrayClear(this.m_parked);
      TandemNative.Log("vehicle clear: done");
    } else if Equals(command, "where") {
      let player = GetPlayer(this.GetGameInstance());
      let at = player.GetWorldPosition();
      TandemNative.Log(s"where: \(at.X) \(at.Y) \(at.Z) yaw \(player.GetWorldYaw()), time \(TandemWorld.GameSeconds(this.GetGameInstance())) s, weather \(NameToString(TandemWorld.Weather(this.GetGameInstance())))");
    } else if StrBeginsWith(command, "teleport") {
      // teleport home | teleport X Y Z [YAW]
      let space = " ";
      let words = StrSplit(command, space);
      let to = new Vector4(-915.0, 1879.5, 36.3, 1.0);
      let yaw = 0.0;
      if ArraySize(words) >= 4 {
        let x = words[1];
        let y = words[2];
        let z = words[3];
        to = new Vector4(StringToFloat(x), StringToFloat(y), StringToFloat(z), 1.0);
        if ArraySize(words) >= 5 {
          let w = words[4];
          yaw = StringToFloat(w);
        }
      }
      GameInstance.GetTeleportationFacility(this.GetGameInstance()).Teleport(GetPlayer(this.GetGameInstance()), to, TandemYawAngles(yaw));
      TandemNative.Log(s"teleport: (\(to.X), \(to.Y), \(to.Z)) yaw \(yaw)");
    } else if Equals(command, "vehicle ride") {
      // (test) our V into a free passenger seat (the front one if free) of
      // the first remote car copy.
      let seated = false;
      for puppet in this.m_puppets {
        if !seated && EntityID.IsDefined(puppet.car) {
          let car = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.car) as VehicleObject;
          let player = GetPlayer(this.GetGameInstance());
          let slot = TandemVehicles.FreeSlot(this.GetGameInstance(), car, n"seat_front_right", player.GetEntityID());
          if IsDefined(car) && IsNameValid(slot) && !VehicleComponent.IsDriverSlot(slot) {
            TandemVehicles.Seat(this.GetGameInstance(), player, car, slot);
            this.m_rideCar = puppet.car;
            this.m_rideSlot = slot;
            seated = true;
            TandemNative.Log(s"vehicle ride: in player \(puppet.player)'s car, seat \(NameToString(slot))");
          } else if IsDefined(car) {
            TandemNative.Log(s"vehicle ride: no free passenger seat in player \(puppet.player)'s car");
          }
        }
      }
    } else if Equals(command, "vehicle unride") {
      TandemVehicles.Unseat(this.GetGameInstance(), GetPlayer(this.GetGameInstance()), this.m_rideCar, this.m_rideSlot);
      TandemNative.Log("vehicle ride: out");
    } else if Equals(command, "ride") {
      // (test) our ride and the others' (Transit.reds).
      if IsDefined(this.m_transit) {
        this.m_transit.Report(this, this.m_puppets);
      }
    } else if StrBeginsWith(command, "ride npc") {
      // (test) an NPC into a free seat of the vehicle our V sits in, brain
      // off (as a quest seats a companion): the host shares where it sits.
      let record = StrLen(command) > 9 ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "Character.Panam";
      let player = GetPlayer(this.GetGameInstance());
      let spec = new DynamicEntitySpec();
      spec.recordID = TDBID.Create(record);
      spec.position = player.GetWorldPosition();
      spec.position.X += 2.0;
      spec.orientation = player.GetWorldOrientation();
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      this.m_rideNpc = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
      this.m_rideNpcSeated = false;
      TandemNative.Log(s"ride npc: \(record) spawning");
    } else if Equals(command, "ride along") || Equals(command, "ride follow") {
      // (test) the answer to a partner's scene ride: ride along, or follow.
      if IsDefined(this.m_transit) {
        this.m_transit.Choose(Equals(command, "ride along"));
      }
    } else if Equals(command, "ride hide") || Equals(command, "ride show") {
      // (test) every puppet out of sight and back (PuppetVisibility.reds).
      let hide = Equals(command, "ride hide");
      for puppet in this.m_puppets {
        if hide {
          TandemVisibility.Hide(this.GetGameInstance(), puppet.player, n"test", 0.3);
        } else {
          TandemVisibility.Show(this.GetGameInstance(), puppet.player, n"test", 0.3);
        }
      }
      TandemNative.Log(s"ride: \(ArraySize(this.m_puppets)) puppet(s) \(hide ? "hidden" : "shown")");
    } else if Equals(command, "vehicle probe stop") {
      if IsDefined(this.m_vehicleProbe) {
        this.m_vehicleProbe.Stop();
        this.m_vehicleProbe = null;
      }
    } else if StrBeginsWith(command, "vehicle probe") {
      if IsDefined(this.m_vehicleProbe) {
        this.m_vehicleProbe.Stop();
      }
      let record = StrLen(command) > 14 ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "Vehicle.v_standard2_archer_hella_player";
      this.m_vehicleProbe = new TandemVehicleProbe();
      this.m_vehicleProbe.Start(this.GetGameInstance(), record);
    } else if Equals(command, "pose stream") {
      TandemNative.PoseStreamReport();
    } else if Equals(command, "pose loop") {
      // (test) our own pose on the first puppet, with no network.
      if ArraySize(this.m_puppets) > 0 {
        TandemNative.PoseLoop(this.m_puppets[0].player);
      } else {
        TandemNative.Log("pose loop: no puppet");
      }
    } else if Equals(command, "pose loop off") {
      TandemNative.PoseLoop(0u);
    } else if Equals(command, "pose report") {
      TandemNative.PoseReport();
    } else if StrBeginsWith(command, "pose watch") {
      // Which argument of the output node's sample call leads to an entity:
      // "pose watch" (two pointers deep) or "pose watch 3".
      let depth = StrLen(command) > 11 ? StringToInt(StrAfterFirst(StrAfterFirst(command, " "), " ")) : 2;
      TandemNative.PoseWatch(GetPlayer(this.GetGameInstance()), "V", depth);
      for puppet in this.m_puppets {
        TandemNative.PoseWatch(GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id), "puppet", depth);
      }
    } else if Equals(command, "pose copy") {
      // (experiment) our V's final pose on every body of the first puppet's rig.
      if ArraySize(this.m_puppets) > 0 {
        TandemNative.PoseCopy(GetPlayer(this.GetGameInstance()), GameInstance.GetDynamicEntitySystem().GetEntity(this.m_puppets[0].id));
      } else {
        TandemNative.Log("pose copy: no puppet");
      }
    } else if Equals(command, "pose copy off") {
      TandemNative.PoseCopy(null, null);
    } else if Equals(command, "pose mark") {
      TandemNative.PoseMark(GetPlayer(this.GetGameInstance()), false);
    } else if Equals(command, "pose diff") {
      TandemNative.PoseMark(GetPlayer(this.GetGameInstance()), true);
    } else if Equals(command, "pose bones") {
      TandemNative.PoseBones(GetPlayer(this.GetGameInstance()));
      for puppet in this.m_puppets {
        TandemNative.PoseBones(GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id));
      }
    } else if Equals(command, "pose tree") {
      TandemNative.PoseTree(GetPlayer(this.GetGameInstance()));
      for puppet in this.m_puppets {
        TandemNative.PoseTree(GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id));
      }
    } else if Equals(command, "pose scan") {
      TandemNative.PoseScan(GetPlayer(this.GetGameInstance()));
      for puppet in this.m_puppets {
        TandemNative.PoseScan(GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id));
      }
    } else if Equals(command, "pose") {
      // The pose probe on our V and on every puppet.
      TandemNative.PoseProbe(GetPlayer(this.GetGameInstance()));
      for puppet in this.m_puppets {
        TandemNative.PoseProbe(GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id));
      }
    } else if Equals(command, "components") {
      // (measure) the component classes an NPC has and a puppet lacks, and
      // the other way round: what NPC senses and hits need (milestone 5).
      let player = GetPlayer(this.GetGameInstance());
      let npc: ref<ScriptedPuppet>;
      for candidate in player.GetNPCsAroundObject(60.0) {
        if !IsDefined(npc) && this.PlayerOfPuppet(candidate.GetEntityID()) == 0u {
          npc = candidate;
        }
      }
      let body = ArraySize(this.m_puppets) > 0 ? GameInstance.GetDynamicEntitySystem().GetEntity(this.m_puppets[0].id) : null;
      if !IsDefined(npc) || !IsDefined(body) {
        TandemNative.Log("components: need an NPC near V and a puppet");
      } else {
        let npcClasses: array<CName>;
        let puppetClasses: array<CName>;
        for c in npc.GetComponents() {
          if !ArrayContains(npcClasses, c.GetClassName()) {
            ArrayPush(npcClasses, c.GetClassName());
          }
        }
        for c in body.GetComponents() {
          if !ArrayContains(puppetClasses, c.GetClassName()) {
            ArrayPush(puppetClasses, c.GetClassName());
          }
        }
        let missing = "";
        for name in npcClasses {
          if !ArrayContains(puppetClasses, name) {
            missing += s" \(NameToString(name))";
          }
        }
        let extra = "";
        for name in puppetClasses {
          if !ArrayContains(npcClasses, name) {
            extra += s" \(NameToString(name))";
          }
        }
        TandemNative.Log(s"components: NPC \(TDBID.ToStringDEBUG(npc.GetRecordID())) (\(NameToString(npc.GetClassName()))) \(ArraySize(npcClasses)) classes, puppet (\(NameToString(body.GetClassName()))) \(ArraySize(puppetClasses))");
        TandemNative.Log(s"components: the puppet lacks\(missing)");
        TandemNative.Log(s"components: only the puppet has\(extra)");
        // What hits and senses use, by name and state, on both.
        TandemNative.Log(s"components: NPC hit/sense parts\(TandemSystem.HitParts(npc))");
        TandemNative.Log(s"components: puppet hit/sense parts\(TandemSystem.HitParts(body))");
      }
    } else if Equals(command, "inspect") {
      this.Inspect();
    } else if Equals(command, "respawn") {
      // Build every puppet again (after a hot reload), same players.
      for puppet in this.m_puppets {
        let join = new TandemJoin();
        join.player = puppet.player;
        join.name = puppet.name;
        join.body = puppet.body;
        ArrayPush(this.m_joins, join);
      }
      this.RemoveAllPuppets();
    } else if Equals(command, "travel") || StrBeginsWith(command, "regroup") {
      if IsDefined(this.m_travel) {
        this.m_travel.Command(command);
      }
    } else if Equals(command, "markers") {
      TandemMarkers.Get(this.GetGameInstance()).Report();
    } else if Equals(command, "despawn") {
      this.RemoveAllPuppets();
    } else if StrBeginsWith(command, "revive") {
      // Revive.reds: revive down | hold | report.
      if IsDefined(this.m_revive) {
        this.m_revive.Command(command);
      }
    } else if StrBeginsWith(command, "elsewhere") && IsDefined(this.m_elsewhere) {
      // Elsewhere.reds: elsewhere test PLACE|off, elsewhere state.
      this.m_elsewhere.Command(command);
    } else if StrBeginsWith(command, "story") || StrBeginsWith(command, "quest") {
      // Story.reds: story …, quest freeze on|off, quest report.
      TandemStorySystem.Get(this.GetGameInstance()).Command(command);
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }

  // (test) "ride npc": once the NPC exists, into a free passenger seat of
  // the vehicle our V sits in.
  private func SeatRideNpc() -> Void {
    if !EntityID.IsDefined(this.m_rideNpc) || this.m_rideNpcSeated {
      return;
    }
    let game = this.GetGameInstance();
    let npc = GameInstance.GetDynamicEntitySystem().GetEntity(this.m_rideNpc) as ScriptedPuppet;
    let car: wref<VehicleObject>;
    if !IsDefined(npc) || !VehicleComponent.GetVehicle(game, GetPlayer(game), car) || !IsDefined(car) {
      return;
    }
    TandemProxies.Pacify(npc);
    let slot = TandemVehicles.FreeSlot(game, car, n"seat_front_right", npc.GetEntityID());
    this.m_rideNpcSeated = true;
    if !IsNameValid(slot) || VehicleComponent.IsDriverSlot(slot) {
      TandemNative.Log("ride npc: no free passenger seat in our vehicle");
      return;
    }
    TandemVehicles.Seat(game, npc, car, slot);
    TandemNative.Log(s"ride npc: seated in \(NameToString(slot)) of our vehicle");
  }

  // Commands that make sense without a session.
  private static func SoloCommand(command: String) -> Bool {
    return Equals(command, "where") || StrBeginsWith(command, "teleport") || StrBeginsWith(command, "god ");
  }

  // coop status | coop host [PORT] | coop join ADDRESS[:PORT] | coop leave:
  // what the pause menu's co-op page does, without the menu. The menu
  // pages themselves (coop menu, coop pick) are the menus' own (Coop.reds).
  private func CoopCommand(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = "status";
    let arg = "";
    if ArraySize(words) > 1 {
      verb = words[1];
    }
    if ArraySize(words) > 2 {
      arg = words[2];
    }
    if Equals(verb, "status") {
      TandemCoop.LogStatus();
    } else if Equals(verb, "host") {
      let hosting = this.CoopHost(StringToInt(arg, 0));
      TandemNative.Log(s"coop: host, in session \(hosting)");
    } else if Equals(verb, "join") {
      let joining = this.CoopJoin(arg);
      TandemNative.Log(s"coop: join \(arg), in session \(joining)");
    } else if Equals(verb, "leave") {
      this.CoopLeave();
      TandemNative.Log("coop: left the session");
    } else {
      TandemNative.Log(s"coop: \(verb) needs the co-op menu open (the pause menu's or the main menu's)");
    }
  }

  // Body poses (plugin: engine/posestream.cpp): which V is read, which body
  // draws which player. Repeated twice a second: entities come and go.
  private func BindPoses() -> Void {
    TandemNative.PoseSource(GetPlayer(this.GetGameInstance()));
    let entities = GameInstance.GetDynamicEntitySystem();
    for puppet in this.m_puppets {
      TandemNative.PoseBind(puppet.player, entities.GetEntity(puppet.id));
    }
  }

  // ---- our V ----

  // Where our V sits (vehicle record, seat, whose car), when it changes.
  private func SendSeat() -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    // Set explicitly: a TweakDBID local left unset is not zero in the
    // script VM (on foot the seat went out as a perk's record, 2026-11-18,
    // and puppets of an echoed seat sat in a phantom car, never moving).
    let record = TDBID.None();
    let slot = n"None";
    let owner = 0u;
    let car: wref<VehicleObject>;
    // On the NCART no seat is sent: every game rides its own train
    // (Transit.reds says where we are). Nor when we ride along, carried
    // by a copy's trunk body slot: no seat of anyone's (Transit Along).
    let along = IsDefined(this.m_transit) && this.m_transit.CarriedBy() != 0u;
    if !along && VehicleComponent.GetVehicle(game, player, car) && IsDefined(car) && !TandemTransit.IsMetro(game, player, car) {
      record = car.GetRecordID();
      VehicleComponent.GetMountedSlotName(game, player, slot);
      owner = this.CarOwner(car.GetEntityID());
    }
    if this.m_sentSeat && record == this.m_sentVehicle && Equals(slot, this.m_sentSlot) && owner == this.m_sentOwner {
      return;
    }
    TandemNative.SendSeat(record, slot, owner);
    TandemNative.Log(s"our seat: \(TDBID.ToStringDEBUG(record)) \(NameToString(slot)) (car of player \(owner))");
    this.m_sentVehicle = record;
    this.m_sentSlot = slot;
    this.m_sentOwner = owner;
    this.m_sentSeat = true;
  }

  // The player whose car this is, when it is a copy we made; 0 otherwise.
  // (measure) the hit, collider and sense components of an entity, named,
  // with whether they are on.
  public static func HitParts(entity: ref<Entity>) -> String {
    let out = "";
    for c in entity.GetComponents() {
      let kind = NameToString(c.GetClassName());
      if StrContains(kind, "Hit") || StrContains(kind, "Collider") || StrContains(kind, "sense") || StrContains(kind, "Targeting") {
        out += s" \(kind):\(NameToString(c.GetName()))\(c.IsEnabled() ? "" : "(off)")";
      }
    }
    return out;
  }

  // A remote player's puppet in our world (null if none).
  public func PuppetOf(player: Uint32) -> ref<GameObject> {
    for puppet in this.m_puppets {
      if puppet.player == player {
        return GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
      }
    }
    return null;
  }

  // The remote player a puppet entity is (0: none).
  public func PlayerOfPuppet(id: EntityID) -> Uint32 {
    for puppet in this.m_puppets {
      if puppet.id == id {
        return puppet.player;
      }
    }
    return 0u;
  }

  // Hit outcomes (Outcomes.reds), and the session's NPCs for them.
  public func Outcomes() -> ref<TandemOutcomes> {
    if !IsDefined(this.m_outcomes) {
      this.m_outcomes = new TandemOutcomes();
      this.m_outcomes.Start(this.GetGameInstance());
    }
    return this.m_outcomes;
  }

  public func Npcs() -> ref<TandemNpcs> {
    return this.m_you != 0u ? this.m_npcs : null;
  }

  // The host NPC a proxy in our world stands for (0: none; Combat.reds).
  // The proxy of a host NPC in our world, alive or dead (Loot.reds).
  public func ProxyOfNpc(npc: Uint32) -> EntityID {
    let none = EntityID.FromHash(0ul);
    return IsDefined(this.m_npcs) ? this.m_npcs.ProxyEntity(npc) : none;
  }

  public func NpcOfProxy(id: EntityID) -> Uint32 {
    return IsDefined(this.m_npcs) && this.m_you != 0u ? this.m_npcs.NpcOfProxy(id) : 0u;
  }

  // Quickhacks (Hacks.reds): session NPCs by id and entity, the player whose
  // car a copy is, our player id (0: not in a session).
  public func NpcEntity(npc: Uint32) -> EntityID {
    return IsDefined(this.m_npcs) && this.m_you != 0u ? this.m_npcs.EntityOfNpc(npc) : EntityID();
  }

  public func NpcOf(id: EntityID) -> Uint32 {
    return IsDefined(this.m_npcs) && this.m_you != 0u ? this.m_npcs.NpcOfEntity(id) : 0u;
  }

  public func PlayerOfCar(id: EntityID) -> Uint32 {
    return this.CarOwner(id);
  }

  public func You() -> Uint32 {
    return this.m_you;
  }

  // Host: the id one of our NPCs is shared under (0: none; Combat.reds).
  public func SharedNpcOf(id: EntityID) -> Uint32 {
    return IsDefined(this.m_npcs) && this.m_you != 0u ? this.m_npcs.NpcOf(id) : 0u;
  }

  // Guest: a hit of the host's NPC on our V, run by our damage system.
  public func IsForwardedHit(hit: ref<gameHitEvent>) -> Bool {
    return IsDefined(this.m_npcs) && this.m_npcs.IsForwarded(hit);
  }

  public func ForwardedPierce(hit: ref<gameHitEvent>, out pierce: Float) -> Bool {
    return IsDefined(this.m_npcs) && this.m_npcs.ForwardedPierce(hit, pierce);
  }

  public func ForgetForwardedHit(hit: ref<gameHitEvent>) -> Void {
    if IsDefined(this.m_npcs) {
      this.m_npcs.Forget(hit);
    }
  }

  private func CarOwner(id: EntityID) -> Uint32 {
    for puppet in this.m_puppets {
      if EntityID.IsDefined(puppet.car) && puppet.car == id {
        return puppet.player;
      }
    }
    return 0u;
  }

  // For Radio.reds: whose copy a car is (0: ours or the world's), the remote
  // players sitting in our car, and every car copy we made (driven or parked).
  public func CarOwnerOf(id: EntityID) -> Uint32 {
    return this.CarOwner(id);
  }

  public func Riders() -> Int32 {
    let riders = 0;
    if this.m_you != 0u {
      for puppet in this.m_puppets {
        if TDBID.IsValid(TandemNative.RemoteSeatVehicle(puppet.player)) && TandemNative.RemoteSeatOwner(puppet.player) == this.m_you {
          riders += 1;
        }
      }
    }
    return riders;
  }

  // The copy of the vehicle a remote player is in (none: no copy here).
  public func CarOf(player: Uint32) -> EntityID {
    for puppet in this.m_puppets {
      if puppet.player == player {
        return puppet.car;
      }
    }
    let none = EntityID.FromHash(0ul);
    return none;
  }

  public func CarCopies() -> array<EntityID> {
    let copies: array<EntityID>;
    for puppet in this.m_puppets {
      if EntityID.IsDefined(puppet.car) {
        ArrayPush(copies, puppet.car);
      }
    }
    for car in this.m_parked {
      ArrayPush(copies, car);
    }
    return copies;
  }

  // What our V holds, every frame (two slot reads), sent when it changes.
  private func SendHeld() -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    let right = TandemWeapons.Held(player, t"AttachmentSlots.WeaponRight");
    let left = TandemWeapons.Held(player, t"AttachmentSlots.WeaponLeft");
    if this.m_sentHeld && right == this.m_sentRight && left == this.m_sentLeft {
      return;
    }
    TandemNative.SendHeld(right, left);
    this.m_sentRight = right;
    this.m_sentLeft = left;
    this.m_sentHeld = true;
  }

  // Our look, to the others: once, then whenever the clothes shown change
  // (checked every 2 s).
  private func SendAppearance() -> Void {
    if this.m_sentAppearance && this.Now() - this.m_lastAppearanceCheck < 2.0 {
      return;
    }
    this.m_lastAppearanceCheck = this.Now();
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let shown = TandemOutfit.Shown(player);
    if this.m_sentAppearance && TandemOutfit.Same(shown, this.m_sentItems) {
      return;
    }
    let male = Equals(player.GetResolvedGenderName(), n"Male");
    let keys = TandemNative.SubmitAppearance(GameInstance.GetCharacterCustomizationSystem(game).GetState(), male, shown);
    this.m_sentItems = shown;
    this.m_sentAppearance = true;
    TandemNative.Log(s"our look sent: \(keys) keys, \(ArraySize(shown)) clothes");
  }

  private func SampleLocal() -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    // Real time, not the world's: under our own slow motion (Time.reds) the
    // world's clock runs slower, and states (host-clock stamped) would thin
    // out and carry a velocity too high.
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(game));
    // 30 states a second.
    if this.m_haveLast && now - this.m_lastSampleTime < 0.0333 {
      return;
    }

    let position = player.GetWorldPosition();
    let velocity = new Vector4(0.0, 0.0, 0.0, 0.0);
    if this.m_haveLast && now > this.m_lastSampleTime {
      let dt = now - this.m_lastSampleTime;
      velocity.X = (position.X - this.m_lastPosition.X) / dt;
      velocity.Y = (position.Y - this.m_lastPosition.Y) / dt;
      velocity.Z = (position.Z - this.m_lastPosition.Z) / dt;
    }
    this.m_lastPosition = position;
    this.m_lastSampleTime = now;
    this.m_haveLast = true;

    let psm = GameInstance.GetBlackboardSystem(game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let locomotion = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Locomotion) : 0;

    // In another world away from its body (Elsewhere.reds): the body, as it
    // was, is what the others see (standing still where it was left).
    let orientation = player.GetWorldOrientation();
    let flags = TandemMovement.Flags(psm);
    if IsDefined(this.m_elsewhere) {
      let vehicle: wref<VehicleObject>;
      let mounted = VehicleComponent.GetVehicle(game, player, vehicle) && IsDefined(vehicle);
      let body = this.m_elsewhere.Sample(position, orientation, mounted);
      if body.held {
        position = new Vector4(body.x, body.y, body.z, 1.0);
        orientation.i = body.i;
        orientation.j = body.j;
        orientation.k = body.k;
        orientation.r = body.r;
        velocity = new Vector4(0.0, 0.0, 0.0, 0.0);
        flags = 0;
      }
    }

    // How we move beyond walking (Movement.reds): the puppet's placement.
    TandemNative.SendLocalState(position, orientation, velocity, locomotion, flags);

    // Driving our own car, or riding a vehicle our game moves for us (an
    // AI, a scene: Transit.reds): its transform, for the copies elsewhere.
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(game, player, car) && IsDefined(car) && this.CarOwner(car.GetEntityID()) == 0u
      && (VehicleComponent.IsDriver(game, player) || IsDefined(TandemTransit.Carried(game, this))) {
      TandemNative.SendVehicle(car.GetWorldPosition(), car.GetWorldOrientation(), car.GetLinearVelocity());
    }
  }

  // ---- the others ----

  private func HandleEvents() -> Void {
    for event in TandemNative.TakeEvents() {
      switch event.kind {
        case 1:
          let join = new TandemJoin();
          join.player = event.player;
          join.name = event.name;
          join.body = event.body;
          ArrayPush(this.m_joins, join);
          // Those already there when we came in are not news.
          if this.m_you != 0u && this.Now() - this.m_sessionAt > 3.0 {
            TandemCoop.Notice(this.GetGameInstance(), s"\(event.name) joined the session");
          }
          break;
        case 2:
          this.NoticeLeft(event.player);
          this.RemovePuppet(event.player);
          if IsDefined(this.m_elsewhere) {
            this.m_elsewhere.Forget(event.player);
          }
          if IsDefined(this.m_visibility) {
            this.m_visibility.Forget(event.player);
          }
          break;
        case 4:
          this.m_you = event.player;
          if IsDefined(this.m_revive) {
            this.m_revive.InSession();
          }
          this.m_sessionAt = this.Now();
          TandemCoop.NoticeInSession(this.GetGameInstance());
          break;
        case 3:
          TandemNative.Log(s"session ended: \(event.name)");
          if this.m_you != 0u {
            TandemCoop.Notice(this.GetGameInstance(), s"Co-op session ended: \(event.name)");
          }
          this.m_you = 0u;
          if IsDefined(this.m_time) {
            this.m_time.End();
          }
          // Riding along: out, while the copy is still where it was.
          if IsDefined(this.m_transit) {
            this.m_transit.End();
          }
          this.RemoveAllPuppets();
          if IsDefined(this.m_npcs) {
            this.m_npcs.End();
          }
          if IsDefined(this.m_stealth) {
            this.m_stealth.End();
          }
          if IsDefined(this.m_revive) {
            this.m_revive.SessionEnded();
          }
          if IsDefined(this.m_elsewhere) {
            this.m_elsewhere.End();
          }
          if IsDefined(this.m_visibility) {
            this.m_visibility.End();
          }
          ArrayClear(this.m_missingPlayers);
          ArrayClear(this.m_missingSince);
          break;
        case 5:
          // Something serious, said once (the plugin keeps it rare).
          TandemCoop.Notice(this.GetGameInstance(), event.name);
          break;
        default:
          break;
      }
    }
  }

  // ---- consistency (docs/ARCHITECTURE.md, Consistency) ----

  // Our puppets against the session's players: a puppet whose player is no
  // longer in the session goes; a player in it with no puppet (none on its
  // way) gets one. Each waits 2 s first, in case the event is on its way.
  private func CheckPuppets() -> Void {
    let now = this.Now();
    if now - this.m_lastPuppetCheck < 1.0 {
      return;
    }
    this.m_lastPuppetCheck = now;
    let orphans: array<Uint32>;
    for puppet in this.m_puppets {
      if TandemSync.InSession(puppet.player) {
        puppet.orphanSince = 0.0;
      } else if puppet.orphanSince <= 0.0 {
        puppet.orphanSince = now;
      } else if now - puppet.orphanSince >= 2.0 {
        ArrayPush(orphans, puppet.player);
      }
    }
    for player in orphans {
      TandemNative.Log(s"consistency: puppet of player \(player) has no player in the session, removed");
      this.RemovePuppet(player);
    }
    let missing: array<Uint32>;
    let since: array<Float>;
    for info in TandemSync.Players() {
      if info.player != this.m_you && !this.HasPuppet(info.player) && !this.JoinPending(info.player) {
        let first = now;
        let i = 0;
        while i < ArraySize(this.m_missingPlayers) {
          if this.m_missingPlayers[i] == info.player {
            first = this.m_missingSince[i];
          }
          i += 1;
        }
        if now - first >= 2.0 {
          TandemNative.Log(s"consistency: player \(info.player) in the session had no puppet, spawning one");
          let join = new TandemJoin();
          join.player = info.player;
          join.name = info.name;
          join.body = info.body;
          ArrayPush(this.m_joins, join);
        } else {
          ArrayPush(missing, info.player);
          ArrayPush(since, first);
        }
      }
    }
    this.m_missingPlayers = missing;
    this.m_missingSince = since;
  }

  private func HasPuppet(player: Uint32) -> Bool {
    for puppet in this.m_puppets {
      if puppet.player == player {
        return true;
      }
    }
    return false;
  }

  private func JoinPending(player: Uint32) -> Bool {
    for join in this.m_joins {
      if join.player == player {
        return true;
      }
    }
    return false;
  }

  // Joins wait for V's bodies (their donors are captured in the first second).
  private func SpawnJoined() -> Void {
    if ArraySize(this.m_joins) == 0 || !IsDefined(this.m_bodies) || !this.m_bodies.Ready() {
      return;
    }
    for join in this.m_joins {
      this.SpawnPuppet(join.player, join.name, join.body);
    }
    ArrayClear(this.m_joins);
  }

  private func SpawnPuppet(player: Uint32, name: String, body: Int32) -> Void {
    this.RemovePuppet(player);
    let game = this.GetGameInstance();
    let local = GetPlayer(game);
    if !IsDefined(local) {
      return;
    }

    // Where the network says they are, else beside us until their first state.
    let position = TandemNative.RemotePosition(player);
    if position.W == 0.0 {
      position = local.GetWorldPosition();
      position.X += 2.0;
    }
    position.W = 1.0;

    let spec = new DynamicEntitySpec();
    let record = Equals(this.m_recordOverride, "") ? TandemNative.PuppetRecord(body) : this.m_recordOverride;
    spec.recordID = TDBID.Create(record);
    spec.position = position;
    spec.orientation = local.GetWorldOrientation();
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Puppet");

    let id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    if !EntityID.IsDefined(id) {
      TandemNative.Log(s"could not spawn a puppet for player \(player) (\(TandemNative.PuppetRecord(body)))");
      return;
    }

    let puppet = new TandemPuppet();
    puppet.player = player;
    puppet.name = name;
    puppet.id = id;
    puppet.record = record;
    puppet.body = body;
    ArrayPush(this.m_puppets, puppet);
    TandemNative.RegisterPuppet(player, id);
    TandemNative.Log(s"spawning \(name) (player \(player), \(TandemNative.PuppetRecord(body)))");
    TandemMarkers.Show(game, player, name, id);
  }

  private func DrivePuppets() -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    // (redscript has no `continue`)
    for puppet in this.m_puppets {
      if !puppet.dressed {
        this.Dress(puppet);
      } else if TandemNative.RemoteAppearanceChanges(puppet.player) != puppet.appearanceChanges {
        this.Redress(puppet);
      } else if !puppet.bodyOn && this.Now() >= puppet.lookRetryAt {
        // Hidden for want of a body: a stand-in may be known by now (a
        // player of that body joined).
        puppet.lookRetryAt = this.Now() + 2.0;
        let entity = entities.GetEntity(puppet.id);
        if IsDefined(entity) && StrBeginsWith(puppet.record, "Character.TandemPuppet")
          && !Equals(puppet.record, "Character.TandemPuppetTest") {
          this.StandIn(puppet, entity, "still no look");
        }
      }
      if puppet.dressed && TandemNative.RemoteHeldChanges(puppet.player) != puppet.heldChanges {
        this.Rearm(puppet);
      }
      if puppet.dressed && TandemNative.RemoteSeatChanges(puppet.player) != puppet.seatChanges {
        this.Reseat(puppet);
      }
      if puppet.wantsSeat && this.Now() >= puppet.seatRetryAt {
        this.TrySeat(puppet);
      }
      if EntityID.IsDefined(puppet.car) {
        let car = entities.GetEntity(puppet.car) as GameObject;
        if IsDefined(car) {
          TandemVehicles.Follow(this.GetGameInstance(), car, puppet.player);
        }
      }
      // (mirror experiment: nothing of ours drives the body but the mirror)
      if puppet.dressed && !TandemNative.IsMirror() {
        this.Pose(puppet);
      }
      if !puppet.controlled && puppet.reattachAt > 0.0 && this.Now() > puppet.reattachAt
        && !GameInstance.GetMountingFacility(this.GetGameInstance()).IsMountedToAnything(puppet.id) {
        let body = entities.GetEntity(puppet.id);
        if IsDefined(body) {
          puppet.controlled = TandemNative.AttachController(puppet.player, body);
          puppet.reattachAt = 0.0;
          TandemNative.Log(s"player \(puppet.player): on foot again, movement controller back (\(puppet.controlled))");
        }
      }
      if !TandemNative.IsMirror() && !puppet.controlled && TandemNative.WantsController(puppet.player) {
        let entity = entities.GetEntity(puppet.id);
        if IsDefined(entity) {
          puppet.controlled = TandemNative.AttachController(puppet.player, entity);
        }
      }
      if puppet.controlled && TandemNative.TakeSnap(puppet.player) {
        this.SnapPuppet(puppet);
      }
      if !puppet.grouped {
        this.Group(puppet);
      }
    }
  }

  // The movement controller could not bring the body where its player is
  // (a ledge an NPC body does not climb, a move the engine refused): put it
  // there. The controller faces it the right way; keep its heading.
  private func SnapPuppet(puppet: ref<TandemPuppet>) -> Void {
    let game = this.GetGameInstance();
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    let position = TandemNative.RemotePosition(puppet.player);
    if !IsDefined(body) || position.W == 0.0 || GameInstance.GetMountingFacility(game).IsMountedToAnything(puppet.id) {
      return;
    }
    let angles = Quaternion.ToEulerAngles(body.GetWorldOrientation());
    GameInstance.GetTeleportationFacility(game).Teleport(body, position, angles);
    puppet.snaps += 1;
    if puppet.snaps <= 10 {
      TandemNative.Log(s"player \(puppet.player): body teleported to its player (placement \(TandemNative.RemotePlacement(puppet.player)), \(puppet.snaps) so far)");
    }
  }

  // A puppet's attitude group, once its body is in the world: its own
  // (Attitudes.reds), or the one "puppet group NAME" set (test).
  private func Group(puppet: ref<TandemPuppet>) -> Void {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if !IsDefined(this.m_attitudes) || !IsDefined(body) || !body.IsAttached() || !IsDefined(body.GetAttitudeAgent()) {
      return;
    }
    let wanted = IsNameValid(this.m_puppetGroup) ? this.m_puppetGroup : this.m_attitudes.Claim(puppet.player);
    puppet.group = this.m_attitudes.Put(body, wanted);
    puppet.grouped = true;
    TandemNative.Log(s"player \(puppet.player): puppet in the \(NameToString(puppet.group)) attitude group");
  }

  private func TickAttitudes() -> Void {
    if !IsDefined(this.m_attitudes) || ArraySize(this.m_puppets) == 0 {
      return;
    }
    let bodies: array<ref<GameObject>>;
    for puppet in this.m_puppets {
      let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
      if IsDefined(body) {
        ArrayPush(bodies, body);
      }
    }
    this.m_attitudes.Tick(this.Now(), bodies);
  }

  // (test, tools/game.sh cmd puppet attitude) each puppet's attitude group,
  // and how the NPCs around V see it next to how they see V (EAIAttitude:
  // 0 friendly, 1 neutral, 2 hostile), with the two group relations.
  private func ReportAttitudes() -> Void {
    let game = this.GetGameInstance();
    let player = GetPlayer(game);
    if ArraySize(this.m_puppets) == 0 || !IsDefined(player) {
      TandemNative.Log("puppet attitude: no puppet");
      return;
    }
    let system = GameInstance.GetAttitudeSystem(game);
    let vGroup = player.GetAttitudeAgent().GetAttitudeGroup();
    let npcs = player.GetNPCsAroundObject(60.0);
    for puppet in this.m_puppets {
      let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
      if !IsDefined(body) || !IsDefined(body.GetAttitudeAgent()) {
        TandemNative.Log(s"puppet attitude: player \(puppet.player) has no body yet");
      } else {
        let group = body.GetAttitudeAgent().GetAttitudeGroup();
        let count = 0;
        let same = 0;
        let shown = 0;
        for npc in npcs {
          if this.PlayerOfPuppet(npc.GetEntityID()) == 0u && IsDefined(npc.GetAttitudeAgent()) {
            count += 1;
            let toV = GameObject.GetAttitudeBetween(npc, player);
            let toPuppet = GameObject.GetAttitudeBetween(npc, body);
            let npcGroup = npc.GetAttitudeAgent().GetAttitudeGroup();
            if Equals(toV, toPuppet) {
              same += 1;
            }
            if NotEquals(toV, toPuppet) || shown < 5 {
              shown += 1;
              TandemNative.Log(s"puppet attitude: npc \(EntityID.ToDebugStringDecimal(npc.GetEntityID())) (\(NameToString(npcGroup))) to V \(EnumInt(toV)), to player \(puppet.player) \(EnumInt(toPuppet)); group relations \(EnumInt(system.GetAttitudeRelation(npcGroup, vGroup)))/\(EnumInt(system.GetAttitudeRelation(npcGroup, group)))");
            }
          }
        }
        TandemNative.Log(s"puppet attitude: player \(puppet.player) in \(NameToString(group)) (V in \(NameToString(vGroup))), \(count) NPCs, \(same) see it as V, V to it \(EnumInt(GameObject.GetAttitudeBetween(player, body))), it to V \(EnumInt(GameObject.GetAttitudeBetween(body, player)))");
      }
    }
  }

  // What each puppet is made of, for the log (tools/game.sh cmd inspect).
  private func Inspect() -> Void {
    // The anim feature names a live NPC's state machine uses (the NPC V
    // looks at; a puppet's own are empty).
    let game = this.GetGameInstance();
    let target = GameInstance.GetTargetingSystem(game).GetLookAtObject(GetPlayer(game), false, false) as NPCPuppet;
    if IsDefined(target) {
      let states = target.FindComponentByType(n"NPCStatesComponent");
      if IsDefined(states) {
        let names = (states as NPCStatesComponent).TandemFeatureNames();
        TandemNative.Log(s"inspect: the NPC in view uses state features\(TandemBodies.Names(names))");
      }
    } else {
      TandemNative.Log("inspect: no NPC in view");
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    for puppet in this.m_puppets {
      let entity = entities.GetEntity(puppet.id);
      if !IsDefined(entity) {
        TandemNative.Log(s"inspect player \(puppet.player): no entity");
      } else {
        let cutscene = Equals(entity.GetTemplatePath(), r"base\\characters\\entities\\player\\player_ma_tpp_cutscene.ent")
          || Equals(entity.GetTemplatePath(), r"base\\characters\\entities\\player\\player_wa_tpp_cutscene.ent");
        let meshes = 0;
        let enabled = 0;
        let names = "";
        for component in entity.GetComponents() {
          if StrContains(NameToString(component.GetClassName()), "Mesh") {
            meshes += 1;
            if component.IsEnabled() {
              enabled += 1;
            }
            names += s" \(NameToString(component.GetName()))\(component.IsEnabled() ? "" : "(off)")";
          }
        }
        let position = entity.GetWorldPosition();
        let transactions = GameInstance.GetTransactionSystem(this.GetGameInstance());
        let slots = "";
        for slot in TandemOutfit.Slots() {
          let item = transactions.GetItemInSlot(entity as GameObject, slot);
          if IsDefined(item) {
            slots += s" \(TDBID.ToStringDEBUG(slot))=\(TDBID.ToStringDEBUG(ItemID.GetTDBID(item.GetItemID())))";
          }
        }
        let ps = (entity as GameObject).GetPS() as GamePuppetPS;
        TandemNative.Log(s"inspect player \(puppet.player): slots\(slots), tpp \(IsDefined(ps) && ps.GetTpp()), crouch \(IsDefined(ps) && ps.IsCrouch())");
        TandemNative.Log(s"inspect player \(puppet.player): cutscene template \(cutscene), at (\(position.X), \(position.Y), \(position.Z)), \(ArraySize(entity.GetComponents())) components, \(meshes) meshes, \(enabled) on:\(names)");
      }
    }
  }

  // V's bodies have no meshes until a look is applied. For now every remote
  // V wears ours (the mock player has none to send): only a body of our
  // gender can wear it.
  private func Dress(puppet: ref<TandemPuppet>) -> Void {
    if !StrBeginsWith(puppet.record, "Character.TandemPuppet") || Equals(puppet.record, "Character.TandemPuppetTest") {
      puppet.dressed = true;
      return;
    }
    let entity = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id);
    if !IsDefined(entity) {
      return;
    }
    let game = this.GetGameInstance();
    if puppet.seenAt == 0.0 {
      puppet.seenAt = this.Now();
    }
    // Third person first (as NCO found): items given before take FPP looks.
    TandemNative.MarkThirdPerson(entity);
    // The puppet's inventory takes a moment after it appears; the player's
    // look may take a moment too.
    let changes = TandemNative.RemoteAppearanceChanges(puppet.player);
    if this.Now() - puppet.seenAt < 0.5 || (changes == 0 && this.Now() - puppet.seenAt < 5.0) {
      return;
    }
    puppet.dressed = true;
    puppet.dressedAt = this.Now();
    if TandemNative.IsMirror() {
      TandemNative.MirrorPlayerTo(entity);
    }
    if changes > 0 {
      this.Redress(puppet);
      return;
    }
    // No look from them (an older peer): a whole body first (StandIn), then
    // our clothes if our body is theirs.
    if this.StandIn(puppet, entity, "no look received") < 0 {
      return;
    }
    let localMale = Equals(GetPlayer(game).GetResolvedGenderName(), n"Male");
    if Equals(localMale, puppet.body == 2) {
      puppet.worn = TandemOutfit.Shown(GetPlayer(game));
      let on = TandemOutfit.WearPreview(entity as GameObject, puppet.worn);
      TandemNative.Log(s"player \(puppet.player): no look received, wearing our clothes (\(on))");
    }
  }

  // A whole body for a puppet whose player's look gave none (no
  // character-creator keys, as the mock player's random look; or they
  // failed): the last real look of that body seen (ours or another
  // player's), else ours when our body is theirs. With none at all the
  // puppet stays hidden (PuppetVisibility.reds, n"nolook") until a look
  // comes: clothes alone are never shown. Answers the keys scheduled, -1
  // when nothing was.
  private func StandIn(puppet: ref<TandemPuppet>, entity: ref<Entity>, why: String) -> Int32 {
    let game = this.GetGameInstance();
    if puppet.bodyOn {
      return 0;
    }
    let keys = TandemNative.ApplyStandInLook(entity, puppet.body);
    let from = "the last look of that body seen";
    if keys < 0 {
      let localMale = Equals(GetPlayer(game).GetResolvedGenderName(), n"Male");
      if Equals(localMale, puppet.body == 2) {
        keys = TandemNative.ApplyLook(entity, GameInstance.GetCharacterCustomizationSystem(game).GetState(), localMale);
        from = "ours";
      }
    }
    if keys < 0 {
      if !TandemVisibility.IsHidden(game, puppet.player) {
        TandemNative.Log(s"player \(puppet.player): \(why), and no stand-in body: hidden until a look comes");
      }
      TandemVisibility.Hide(game, puppet.player, n"nolook", 0.0);
      return -1;
    }
    puppet.bodyOn = true;
    TandemVisibility.Show(game, puppet.player, n"nolook", 0.3);
    TandemNative.Log(s"player \(puppet.player): \(why): a stand-in body (\(from), \(keys) keys)");
    return keys;
  }

  // The player changed seats: out of the old one, their car copy kept only
  // while they drive it, then into the new one as soon as it exists.
  private func Reseat(puppet: ref<TandemPuppet>) -> Void {
    let game = this.GetGameInstance();
    let entities = GameInstance.GetDynamicEntitySystem();
    puppet.seatChanges = TandemNative.RemoteSeatChanges(puppet.player);
    let record = TandemNative.RemoteSeatVehicle(puppet.player);
    let owner = TandemNative.RemoteSeatOwner(puppet.player);
    let body = entities.GetEntity(puppet.id) as GameObject;
    let none = EntityID.FromHash(0ul);
    if EntityID.IsDefined(puppet.seatedIn) && IsDefined(body) {
      TandemVehicles.Unseat(game, body, puppet.seatedIn, puppet.seatedSlot);
      // Mounting detached its movement controller: put ours back once the
      // body is on foot again (DrivePuppets).
      puppet.controlled = false;
      puppet.reattachAt = this.Now() + 0.5;
    }
    puppet.seatedIn = none;
    puppet.wantsSeat = false;
    puppet.seatRetryAt = 0.0;
    // Hidden for want of a seat: seen again (TrySeat hides it again if the
    // new seat is taken too).
    TandemVisibility.Show(game, puppet.player, n"noseat", 0.3);
    if EntityID.IsDefined(puppet.car) && (owner != puppet.player || !TDBID.IsValid(record)) {
      // Left parked where it stopped (ProcessLeaving: copies are not deleted).
      let parked = new TandemLeaving();
      parked.player = puppet.player;
      parked.car = puppet.car;
      parked.bodyGone = true;
      parked.since = this.Now();
      ArrayPush(this.m_leaving, parked);
      puppet.car = none;
    }
    if !TDBID.IsValid(record) {
      TandemNative.Log(s"player \(puppet.player): on foot");
      return;
    }
    if owner == puppet.player && !EntityID.IsDefined(puppet.car) {
      let at = TandemNative.RemoteVehiclePosition(puppet.player);
      if at.W == 0.0 && IsDefined(body) {
        at = body.GetWorldPosition();
      }
      at.W = 1.0;
      puppet.car = TandemVehicles.SpawnCopy(record, at, TandemNative.RemoteVehicleOrientation(puppet.player));
      TandemNative.Log(s"player \(puppet.player): drives \(TDBID.ToStringDEBUG(record)), copy spawning");
    }
    puppet.seatedSlot = TandemNative.RemoteSeatSlot(puppet.player);
    puppet.wantsSeat = true;
  }

  // Mounts the puppet once its vehicle exists: their own car's copy, our
  // car (they ride with us), or another player's copy.
  private func TrySeat(puppet: ref<TandemPuppet>) -> Void {
    let game = this.GetGameInstance();
    let entities = GameInstance.GetDynamicEntitySystem();
    let body = entities.GetEntity(puppet.id) as GameObject;
    let owner = TandemNative.RemoteSeatOwner(puppet.player);
    let vehicle: wref<GameObject>;
    if owner == puppet.player {
      vehicle = entities.GetEntity(puppet.car) as GameObject;
    } else if owner == this.m_you {
      let ours: wref<VehicleObject>;
      if VehicleComponent.GetVehicle(game, GetPlayer(game), ours) {
        vehicle = ours;
      }
    } else {
      for other in this.m_puppets {
        if other.player == owner && EntityID.IsDefined(other.car) {
          vehicle = entities.GetEntity(other.car) as GameObject;
        }
      }
    }
    if !IsDefined(body) || !IsDefined(vehicle) {
      return;
    }
    // Their seat if it is free here, else another: the seat may be taken in
    // our world (the host's quest vehicle, an NPC seated in a copy), and a
    // seat the scene's own passenger holds is never taken from it.
    let slot = TandemVehicles.FreeSlot(game, vehicle as VehicleObject, puppet.seatedSlot, body.GetEntityID());
    if !IsNameValid(slot) {
      if !TandemVisibility.IsHidden(game, puppet.player) {
        TandemNative.Log(s"player \(puppet.player): no free seat in player \(owner)'s vehicle here, hidden until one frees");
      }
      TandemVisibility.Hide(game, puppet.player, n"noseat", 0.3);
      puppet.seatRetryAt = this.Now() + 0.5;
      return;
    }
    TandemVisibility.Show(game, puppet.player, n"noseat", 0.3);
    TandemVehicles.Seat(game, body, vehicle, slot);
    puppet.seatedIn = vehicle.GetEntityID();
    puppet.wantsSeat = false;
    let theirs = NameToString(puppet.seatedSlot);
    let moved = Equals(slot, puppet.seatedSlot) ? "" : " (theirs, " + theirs + ", is taken here)";
    TandemNative.Log(s"player \(puppet.player): seated in \(NameToString(slot))\(moved) of player \(owner)'s car");
    puppet.seatedSlot = slot;
  }

  // (experiment) pacify each test proxy as soon as it exists; report how it
  // behaves (its high-level AI state, whether it sees V as hostile).
  // (test) each "npc hostile" NPC, once it exists: hostile to our V and to
  // every remote player's puppet.
  private func TickHostileTests() -> Void {
    let report = this.Now() - this.m_lastHostileReport >= 5.0;
    if report {
      this.m_lastHostileReport = this.Now();
    }
    for id in this.m_testNpcs {
      let npc = GameInstance.GetDynamicEntitySystem().GetEntity(id) as ScriptedPuppet;
      if report && IsDefined(npc) {
        let player = GetPlayer(this.GetGameInstance());
        let tracker = npc.GetTargetTrackerComponent();
        let threats = IsDefined(tracker) ? ArraySize(tracker.GetHostileThreats(false)) : -1;
        let all = IsDefined(tracker) ? ArraySize(tracker.GetThreats(false)) : -1;
        let towards = "";
        for puppet in this.m_puppets {
          let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
          if IsDefined(body) {
            towards += s" player \(puppet.player): attitude \(EnumInt(GameObject.GetAttitudeBetween(npc, body))), detection \(npc.GetSensesComponent().GetDetection(body.GetEntityID())), visible \(npc.GetSensesComponent().IsAgentVisible(body))";
          }
        }
        let target = FromVariant<wref<GameObject>>(npc.GetAIControllerComponent().GetBehaviorArgument(n"CombatTarget"));
        let targetName = !IsDefined(target) ? "none" : target == player ? "our V" : this.PlayerOfPuppet(target.GetEntityID()) != 0u ? s"player \(this.PlayerOfPuppet(target.GetEntityID()))'s puppet" : EntityID.ToDebugStringDecimal(target.GetEntityID());
        TandemNative.Log(s"npc hostile: combat target \(targetName), \(all) threat(s) in all;\(towards); V detection \(npc.GetSensesComponent().GetDetection(player.GetEntityID())), visible \(npc.GetSensesComponent().IsAgentVisible(player))");
        TandemNative.Log(s"npc hostile: \(Vector4.Distance(npc.GetWorldPosition(), player.GetWorldPosition())) m from V, state \(EnumInt(npc.GetHighLevelStateFromBlackboard())), \(threats) hostile threat(s), dead \(npc.IsDead())");
        // Threats again, now that it has surely settled: the puppets, and
        // after 10 s our V too (control: do injected threats work at all?).
        this.m_hostileReports += 1;
        // As vanilla's "inject combat target" does: hostile, a threat, and
        // a persistent one (it would otherwise drop while out of sight).
        let source = Cast<Uint32>(TweakDBInterface.GetAIThreatPersistenceSourceRecord(t"AIThreatPersistenceSource.CommandInjectCombatTarget").EnumValue());
        for puppet in this.m_puppets {
          let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
          if IsDefined(body) {
            GameObject.ChangeAttitudeToHostile(npc, body);
            TargetTrackingExtension.InjectThreat(npc, body);
            TargetTrackingExtension.SetThreatPersistence(npc, body, true, source);
          }
        }

      }
      if IsDefined(npc) && !ArrayContains(this.m_testHostile, id) {
        ArrayPush(this.m_testHostile, id);
        let agent = npc.GetAttitudeAgent();
        let player = GetPlayer(this.GetGameInstance());
        agent.SetAttitudeTowards(player.GetAttitudeAgent(), this.m_hostileToV ? EAIAttitude.AIA_Hostile : EAIAttitude.AIA_Neutral);
        let groups = s"npc \(NameToString(agent.GetAttitudeGroup())), V \(NameToString(player.GetAttitudeAgent().GetAttitudeGroup()))";
        for puppet in this.m_puppets {
          let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
          if IsDefined(body) {
            agent.SetAttitudeTowards(body.GetAttitudeAgent(), EAIAttitude.AIA_Hostile);
            body.GetAttitudeAgent().SetAttitudeTowards(agent, EAIAttitude.AIA_Hostile);
            // Their V is its threat, not ours: does it fight a puppet?
            TargetTrackingExtension.InjectThreat(npc, body);
            groups += s", player \(puppet.player) \(NameToString(body.GetAttitudeAgent().GetAttitudeGroup()))";
          }
        }
        TandemNative.Log(s"npc hostile: now hostile (attitude groups: \(groups))");
      }
    }
  }

  private func TickProxyTests() -> Void {
    for test in this.m_proxyTests {
      let npc = GameInstance.GetDynamicEntitySystem().GetEntity(test.id) as ScriptedPuppet;
      if IsDefined(npc) && !test.pacified {
        test.pacified = true;
        TandemNative.Log(s"proxy test: pacified:\(TandemProxies.Pacify(npc))");
      }
    }
  }

  // What the player holds, in the puppet's hands.
  private func Rearm(puppet: ref<TandemPuppet>) -> Void {
    let entity = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if !IsDefined(entity) {
      return;
    }
    puppet.heldChanges = TandemNative.RemoteHeldChanges(puppet.player);
    let right = TandemNative.RemoteHeld(puppet.player, false);
    let left = TandemNative.RemoteHeld(puppet.player, true);
    let ok = TandemWeapons.Hold(entity, t"AttachmentSlots.WeaponRight", right) && TandemWeapons.Hold(entity, t"AttachmentSlots.WeaponLeft", left);
    TandemNative.Log(s"player \(puppet.player): holds \(TDBID.ToStringDEBUG(right)) / \(TDBID.ToStringDEBUG(left)) (\(ok))");
  }

  // The others' shots, on their puppets' weapons.
  private func DrawShots() -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    for shot in TandemNative.TakeShots() {
      for puppet in this.m_puppets {
        if puppet.player == shot.player {
          let entity = entities.GetEntity(puppet.id) as GameObject;
          if IsDefined(entity) {
            TandemWeapons.Fire(entity, shot.left, shot.effect);
          }
        }
      }
    }
  }

  // The player's own look: face and body (skipped by the plugin when
  // unchanged), then their clothes in place of the previous ones.
  private func Redress(puppet: ref<TandemPuppet>) -> Void {
    let entity = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if !IsDefined(entity) {
      return;
    }
    puppet.appearanceChanges = TandemNative.RemoteAppearanceChanges(puppet.player);
    let items = TandemNative.RemoteItems(puppet.player);
    let keys = TandemNative.ApplyRemoteLook(entity, puppet.player);
    if keys >= 0 {
      // Theirs (0: what it already wears).
      puppet.bodyOn = true;
      TandemVisibility.Show(this.GetGameInstance(), puppet.player, n"nolook", 0.3);
    } else {
      // No keys, or they failed: never their clothes on no body.
      this.StandIn(puppet, entity, "their look has no body");
    }
    if !TandemOutfit.Same(items, puppet.worn) {
      TandemOutfit.TakeOff(entity, puppet.worn);
      puppet.worn = items;
      TandemOutfit.WearPreview(entity, items);
    }
    TandemNative.Log(s"player \(puppet.player): their look (\(keys) keys scheduled, \(ArraySize(items)) clothes)");
  }

  // The remote player's stance on the NPC body, through the NPC state machine
  // (NPCPuppet.ChangeStanceState: the stance anim feature + puppet blackboard).
  private func Pose(puppet: ref<TandemPuppet>) -> Void {
    let stance = gamedataNPCStanceState.Stand;
    switch TandemNative.RemoteStance(puppet.player) {
      case 1:  // Crouch
      case 11: // CrouchSprint
      case 12: // CrouchDodge
        stance = gamedataNPCStanceState.Crouch;
        break;
      default:
        stance = gamedataNPCStanceState.Stand;
    }
    if Equals(stance, puppet.stance) {
      return;
    }
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as NPCPuppet;
    if IsDefined(body) {
      // An NPC's stance shows through anim wrappers (inCrouch, inCover,
      // inSwim, inVehicle: NPCStatesComponent.GetAnimWrapperNameBasedOnStanceState),
      // weighted 1 on the body and what it holds. The state machine's own
      // call is kept for the puppet's blackboard (it needs AI to do more).
      NPCPuppet.ChangeStanceState(body, stance);
      for wrapper in [n"inCrouch", n"inCover", n"inSwim", n"inVehicle"] {
        AnimationControllerComponent.SetAnimWrapperWeightOnOwnerAndItems(body, wrapper, 0.0);
      }
      let wrapper = NPCStatesComponent.GetAnimWrapperNameBasedOnStanceState(stance);
      if IsNameValid(wrapper) {
        AnimationControllerComponent.SetAnimWrapperWeightOnOwnerAndItems(body, wrapper, 1.0);
      }
      puppet.stance = stance;
      TandemNative.Log(s"player \(puppet.player): stance \(EnumValueToString("gamedataNPCStanceState", Cast<Int64>(EnumInt(stance))))");
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
  }

  private func NoticeLeft(player: Uint32) -> Void {
    for puppet in this.m_puppets {
      if puppet.player == player {
        TandemCoop.Notice(this.GetGameInstance(), s"\(puppet.name) left the session");
      }
    }
  }

  private func RemovePuppet(player: Uint32) -> Void {
    TandemNative.ClearMirrors();
    let kept: array<ref<TandemPuppet>>;
    for puppet in this.m_puppets {
      if puppet.player == player {
        this.Leave(puppet);
      } else {
        ArrayPush(kept, puppet);
      }
    }
    this.m_puppets = kept;
  }

  // Out of the plugin's hands at once (no more poses or movement), out of
  // its seat, then deleted by ProcessLeaving once nothing is mounted.
  private func Leave(puppet: ref<TandemPuppet>) -> Void {
    let game = this.GetGameInstance();
    TandemNative.PoseBind(puppet.player, null);
    TandemNative.UnregisterPuppet(puppet.player);
    TandemMarkers.Hide(game, puppet.player);
    if IsDefined(this.m_attitudes) {
      this.m_attitudes.Release(puppet.player);
    }
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if EntityID.IsDefined(puppet.seatedIn) && IsDefined(body) {
      TandemVehicles.Unseat(game, body, puppet.seatedIn, puppet.seatedSlot);
    }
    // Our own V riding in their car: its driver is gone (it may have
    // vanished without a word) and the copy will never move again. Out of
    // it at once, where it stands.
    let local = GetPlayer(game);
    if IsDefined(local) && EntityID.IsDefined(puppet.car) {
      let ride = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithObjects(local);
      if EntityID.IsDefined(ride.parentId) && ride.parentId == puppet.car {
        TandemVehicles.Unseat(game, local, puppet.car, ride.slotId.id);
        TandemNative.Log(s"player \(puppet.player): gone at the wheel, our V let out of their car");
      }
    }
    let leaving = new TandemLeaving();
    leaving.player = puppet.player;
    leaving.id = puppet.id;
    leaving.car = puppet.car;
    leaving.since = this.Now();
    ArrayPush(this.m_leaving, leaving);
    TandemNative.Log(s"player \(puppet.player): leaving (seated \(EntityID.IsDefined(puppet.seatedIn)), car \(EntityID.IsDefined(puppet.car)))");
  }

  // One step per frame: the body when it is no longer mounted (or after
  // 2 s), then, the next frame, its car, once nobody else sits in it.
  private func ProcessLeaving() -> Void {
    if ArraySize(this.m_leaving) == 0 {
      return;
    }
    let game = this.GetGameInstance();
    let entities = GameInstance.GetDynamicEntitySystem();
    let kept: array<ref<TandemLeaving>>;
    for leaving in this.m_leaving {
      if !leaving.bodyGone {
        if GameInstance.GetMountingFacility(game).IsMountedToAnything(leaving.id) && this.Now() - leaving.since < 2.0 {
          ArrayPush(kept, leaving);
        } else {
          TandemNative.Log(s"player \(leaving.player): deleting the body");
          entities.DeleteEntity(leaving.id);
          leaving.bodyGone = true;
          ArrayPush(kept, leaving);
        }
      } else if EntityID.IsDefined(leaving.car) {
        // Car copies are not deleted: deleting one that has had a body
        // seated in it froze the game, even after the body got out
        // (2026-11-18, two hang dumps; engine-knowledge.md §13.6). It stays
        // parked where the driver left it; parked cars belong to the host
        // later (ARCHITECTURE authority table).
        this.EmptyCar(leaving.car);
        ArrayPush(this.m_parked, leaving.car);
        TandemNative.Log(s"player \(leaving.player): car left parked");
      } else {
        TandemNative.Log(s"removed player \(leaving.player)");
      }
    }
    this.m_leaving = kept;
  }

  // Takes every puppet out of a car that is about to go.
  private func EmptyCar(car: EntityID) -> Void {
    let game = this.GetGameInstance();
    let none = EntityID.FromHash(0ul);
    for puppet in this.m_puppets {
      if EntityID.IsDefined(puppet.seatedIn) && puppet.seatedIn == car {
        let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
        if IsDefined(body) {
          TandemVehicles.Unseat(game, body, car, puppet.seatedSlot);
        }
        puppet.seatedIn = none;
        puppet.controlled = false;
        puppet.reattachAt = this.Now() + 0.5;
      }
    }
  }

  private func RemoveAllPuppets() -> Void {
    TandemNative.ClearMirrors();
    for puppet in this.m_puppets {
      this.Leave(puppet);
    }
    ArrayClear(this.m_puppets);
  }

}

// A rotation about the vertical axis only. EulerAngles is a native struct
// whose script definition redscript does not trust: constructing it with
// arguments may be undefined behaviour (its warning), so set the field.
public func TandemYawAngles(yaw: Float) -> EulerAngles {
  let angles: EulerAngles; // assigned on every path: each field below
  angles.Pitch = 0.0;
  angles.Roll = 0.0;
  angles.Yaw = yaw;
  return angles;
}
