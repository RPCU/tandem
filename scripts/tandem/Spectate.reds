// Spectating the host's scripted moments (docs/architecture/story.md,
// "Spectating"; the rule: core/story.hpp MomentReasons, HostMoment,
// Spectator). The quest runs on the host's game alone: while its V is in a
// scripted moment (a story scene of tier 2 and up, quest restrictions on how
// it moves, a ride its scene or quest put it in, a scripted body or camera
// mode, another world), a guest beside it watches through the host's camera
// instead of playing a V the quest knows nothing of.
//   - the host reads its own V four times a second, from the game's data
//     (no list of quests), says the moment as kStoryMoment in its
//     StoryState (Story.reds SendFlags) and sends its camera's view every
//     frame while it lasts (SceneCamera, paced by the endpoint);
//   - a guest that shares the moment when it begins (grouped, in this
//     world, within 40 m) watches it on a camera of its own, following the
//     host's V wherever the moment takes it; its own V is parked where it
//     stands: invisible to the NPCs (the visible object component, as
//     vanilla's grace period after a spawn), held still and safe (the
//     spectating holds, Story.reds UpdateHolds: no movement, no combat, no
//     camera, invulnerable), its body out of the picture, and hidden from
//     the others (kStorySpectating). "Stop watching" (Choice1) leaves; the
//     moment's end, the host vanishing or the session ending release it.
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemSpectateFrame extends DelayCallback {
  public let spectate: wref<TandemSpectate>;

  public func Call() -> Void {
    if IsDefined(this.spectate) {
      this.spectate.Frame();
    }
  }
}

public class TandemSpectate {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  private let m_frameOn: Bool;
  // Host: when we last looked at our V, the moment as last decided and its
  // reasons (story::kMoment*).
  private let m_lastScan: Float;
  private let m_moment: Bool;
  private let m_reasons: Int32;
  // Guest: when we last checked, whether we watch, the camera body we asked
  // for, whether the picture is its, what we did to our V and the host's
  // puppet, whether the player asked to leave (OnAction, acted on the frame).
  private let m_lastCheck: Float;
  private let m_watching: Bool;
  private let m_camera: EntityID;
  private let m_cameraAsked: Bool;
  private let m_cameraLive: Bool;
  private let m_cameraLogged: Bool;
  private let m_madeInvisible: Bool;
  private let m_bodyHidden: Bool;
  private let m_hostHidden: Bool;
  private let m_listening: Bool;
  private let m_leaveAsked: Bool;
  // Everyone: the other guests that watch now (their puppets hidden here).
  private let m_spectators: array<Uint32>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
  }

  // ---- which effects ----

  // Quest restrictions on how the V moves (no sprint, no jump, forced walk,
  // no vehicle...): a moment. Weapons kept away alone is a place's rule (a
  // club, a safe area), not a moment: Restrictions.reds shares it as is.
  public static func Restricts(record: wref<StatusEffect_Record>) -> Bool {
    return TandemRestrictions.IsShared(record) && !TandemRestrictions.KeepsWeaponsAway(record.GetID());
  }

  // A scripted body or camera mode the story holds the V in (no movement,
  // no camera control, forced crouch, a camera animation), not one of its
  // own activities (a body carried, a call, a braindance, focus, a locker,
  // a contest, driving in combat): those are not the story's moments.
  public static func IsMode(record: wref<StatusEffect_Record>) -> Bool {
    if !TandemRestrictions.IsRestriction(record) || TandemRestrictions.IsShared(record) {
      return false;
    }
    if record.GameplayTagsContains(n"FistFight") || record.GameplayTagsContains(n"ShootingRangeCompetition") {
      return false;
    }
    let name = TDBID.ToStringDEBUG(record.GetID());
    let own = [
      "BodyCarrying", "PhoneCall", "Braindance", "FastForward", "FocusModeLocomotion", "DriverCombat",
      "VehicleCombat", "SecurityLocker", "FistFight", "ShootingRange", "BlockAllHubMenu", "BlockAllMenu",
      "BlockFastTravel", "InfiniteAmmo"
    ];
    for part in own {
      if StrContains(name, part) {
        return false;
      }
    }
    return record.GameplayTagsContains(n"CameraAnimation") || StrContains(name, "NoMovement")
      || StrContains(name, "NoCameraControl") || StrContains(name, "ForceCrouch");
  }

  // ---- host ----

  // Four times a second (Story.reds HostTick): is our V in a scripted
  // moment now?
  public func HostTick(now: Float, tier: Int32, isPrivate: Bool) -> Void {
    if now - this.m_lastScan < 0.25 {
      return;
    }
    this.m_lastScan = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let restricted = false;
    let mode = false;
    let effects: array<ref<StatusEffect>>;
    GameInstance.GetStatusEffectSystem(this.m_game).GetAppliedEffects(player.GetEntityID(), effects);
    for effect in effects {
      let record = effect.GetRecord();
      if TandemSpectate.Restricts(record) {
        restricted = true;
      } else if TandemSpectate.IsMode(record) {
        mode = true;
      }
    }
    // Put in a vehicle by its scene or quest (Transit.reds: kind 3, a ride
    // our game moves while a scene runs it), not one we drive or called.
    let system = TandemSystem.Of(this.m_game);
    let transit = IsDefined(system) ? system.Transit() : null;
    let mounted = IsDefined(transit) && transit.OurKind() == 3;
    // Cyberspace, a braindance, playing as Johnny (Elsewhere.reds).
    let away = TandemElsewhere.Inside(this.m_game);
    let reasons = TandemStory.SpectateReasons(tier, isPrivate, restricted, mounted, mode, away);
    let on = TandemStory.SpectateMoment(tier, isPrivate, restricted, mounted, mode, away);
    if NotEquals(on, this.m_moment) || (on && reasons != 0 && reasons != this.m_reasons) {
      if on {
        TandemNative.Log(s"spectate: our V is in a scripted moment (reasons \(reasons): 1 scene, 2 restrictions, 4 quest ride, 8 scripted mode, 16 another world, 32 test): the guests beside us watch");
      } else {
        TandemNative.Log("spectate: our moment is over");
      }
    }
    if reasons != 0 {
      this.m_reasons = reasons;
    }
    this.m_moment = on;
    this.StartFrames();
  }

  public func HostMoment() -> Bool {
    return this.m_moment;
  }

  // The session is over (host): no moment, nobody watched.
  public func HostEnd() -> Void {
    this.m_moment = false;
    this.m_reasons = 0;
    TandemStory.SpectateTest(false);
    this.ShowSpectators();
  }

  // ---- guest ----

  // Four times a second (Story.reds GuestTick): watch the host's moment, or
  // stop.
  public func GuestTick(now: Float) -> Void {
    if now - this.m_lastCheck < 0.25 {
      return;
    }
    this.m_lastCheck = now;
    let player = GetPlayer(this.m_game);
    let story = this.m_story;
    if !IsDefined(player) || !IsDefined(story) {
      return;
    }
    let host = story.HostPlayer();
    let flags = story.StoryFlagsOf(host);
    let at = TandemNative.RemotePosition(host);
    let here = player.GetWorldPosition();
    let change = TandemStory.SpectateUpdate((flags & 16) != 0, (flags & 8) != 0, story.Grouped(), TandemElsewhere.Inside(this.m_game), at.W >= 0.5, at.X, at.Y, at.Z, here.X, here.Y, here.Z);
    if change == 2 {
      this.Begin(player);
    } else if change == 3 {
      this.Finish(true);
    }
    if this.m_watching {
      this.Park(player);
    }
  }

  // Our V is parked (Story.reds adds the spectating holds while this says so).
  public func Watching() -> Bool {
    return this.m_watching;
  }

  private func Begin(player: ref<PlayerPuppet>) -> Void {
    this.m_watching = true;
    this.m_leaveAsked = false;
    let name = this.m_story.NameOf(this.m_story.HostPlayer());
    this.m_story.Notice("Spectate-Watching", name);
    TandemText.Hint(this.m_game, n"TandemSpectate", n"Choice1", true, TandemText.Get("Spectate-Leave"), inkInputHintHoldIndicationType.Press);
    if !this.m_listening {
      this.m_listening = true;
      player.RegisterInputListener(this, n"Choice1");
    }
    this.Park(player);
    this.StartFrames();
    TandemNative.Log(s"spectate: watching \(name)'s moment through their camera, our V parked");
  }

  // What keeps our V out of the moment: unseen by the NPCs (PlayerPuppet.
  // SetInvisible, final.reds:43505: the visible object component vanilla
  // turns off for its grace period after a spawn, :39493), out of the
  // picture (the animation system's fade, PuppetVisibility.reds). Only what
  // we did is undone: an invisibility of its own (optical camo) is left.
  private func Park(player: ref<PlayerPuppet>) -> Void {
    if !this.m_madeInvisible && !player.IsInvisible() {
      player.SetInvisible(true);
      this.m_madeInvisible = true;
    }
    if !this.m_bodyHidden {
      TandemVisibility.Fade(this.m_game, player, false, 0.3);
      this.m_bodyHidden = true;
    }
  }

  // Our own eyes and V again. `ended`: the moment is over (not left by the
  // player): far from the host, we join it the game's way.
  private func Finish(ended: Bool) -> Void {
    if !this.m_watching {
      return;
    }
    this.m_watching = false;
    this.ReleaseCamera();
    this.ShowHost();
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      if this.m_madeInvisible {
        player.SetInvisible(false);
      }
      if this.m_bodyHidden {
        TandemVisibility.Fade(this.m_game, player, true, 0.3);
      }
      if this.m_listening {
        player.UnregisterInputListener(this, n"Choice1");
      }
    }
    this.m_madeInvisible = false;
    this.m_bodyHidden = false;
    this.m_listening = false;
    TandemText.Hint(this.m_game, n"TandemSpectate", n"Choice1", false, "", inkInputHintHoldIndicationType.Press);
    let host = this.m_story.HostPlayer();
    let distance = this.m_story.DistanceToHost();
    TandemNative.Log(s"spectate: our own V again (\(ended ? "the moment is over" : "we left it"), \(distance) m from the host)");
    // The moment took the host's V away (a ride, a quest teleport): we go
    // to it, behind a loading screen (Travel.reds).
    if ended && this.m_story.InSession() && distance > 60.0 && TandemTravel.RegroupTo(this.m_game, host) {
      TandemNative.Log("spectate: joining the host where the moment left it");
    }
  }

  // The session is over or the host is gone: everything back.
  public func Leave() -> Void {
    TandemStory.SpectateLift();
    this.Finish(false);
    this.ShowSpectators();
  }

  // ---- every frame ----

  private func WantsFrames() -> Bool {
    return this.m_moment || this.m_watching || this.m_cameraAsked;
  }

  private func StartFrames() -> Void {
    if !this.m_frameOn && this.WantsFrames() {
      this.m_frameOn = true;
      this.ScheduleFrame();
    }
  }

  private func ScheduleFrame() -> Void {
    let frame = new TandemSpectateFrame();
    frame.spectate = this;
    // 0 s: the next frame. Real time: not slowed by time dilation.
    GameInstance.GetDelaySystem(this.m_game).DelayCallback(frame, 0.0, false);
  }

  public func Frame() -> Void {
    if this.m_moment {
      this.HostFrame();
    }
    if this.m_watching && this.m_leaveAsked {
      this.m_leaveAsked = false;
      if TandemStory.SpectateLeave() {
        TandemNative.Log("spectate: the player stops watching");
        this.Finish(false);
      }
    }
    if this.m_watching {
      this.GuestFrame();
    } else {
      this.ReleaseCamera();
    }
    if this.WantsFrames() {
      this.ScheduleFrame();
    } else {
      this.m_frameOn = false;
    }
  }

  // Host: what our screen shows now, first person or not (the active
  // camera, as the watch camera samples it: Scenes.reds HostFrame).
  private func HostFrame() -> Void {
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let transform: Transform; // assigned on every path
    if IsDefined(cameras) && cameras.GetActiveCameraWorldTransform(transform) {
      let at = Transform.GetPosition(transform);
      let q = Transform.GetOrientation(transform);
      TandemStory.MomentCameraSample(at.X, at.Y, at.Z, q.i, q.j, q.k, q.r, cameras.GetActiveCameraFOV());
    }
  }

  // Guest: the host's view on a camera of ours (the watch camera's way,
  // Scenes.reds GuestFrame).
  private func GuestFrame() -> Void {
    let view = TandemStory.SpectateView();
    if !view.on {
      // No view yet, or a gap (a loading screen on the host): the last
      // picture stays.
      return;
    }
    let position = new Vector4(view.x, view.y, view.z, 1.0);
    let orientation = new Quaternion(view.i, view.j, view.k, view.r);
    if !this.m_cameraAsked {
      let spec = new DynamicEntitySpec();
      spec.templatePath = r"base\\entities\\cameras\\simple_free_camera.ent";
      spec.position = position;
      spec.orientation = orientation;
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      ArrayPush(spec.tags, n"Tandem.Spectate");
      this.m_camera = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
      this.m_cameraAsked = true;
      this.m_cameraLogged = false;
      TandemNative.Log("spectate: a camera for the host's view asked for");
      return;
    }
    let body = GameInstance.FindEntityByID(this.m_game, this.m_camera);
    if !IsDefined(body) {
      return;
    }
    let lens = body.FindComponentByType(n"gameCameraComponent") as CameraComponent;
    if !IsDefined(lens) {
      if !this.m_cameraLogged {
        this.m_cameraLogged = true;
        TandemNative.Log("spectate: the camera body has no camera component: our own eyes stay");
      }
      return;
    }
    let object = body as GameObject;
    if IsDefined(object) {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(object, position, Quaternion.ToEulerAngles(orientation));
    } else {
      let placed: WorldTransform; // assigned on every path
      WorldTransform.SetPosition(placed, position);
      WorldTransform.SetOrientation(placed, orientation);
      body.SetWorldTransform(placed);
    }
    lens.SetFOV(view.fov);
    if !this.m_cameraLive {
      this.m_cameraLive = true;
      lens.Activate(0.4, false);
      TandemNative.Log(s"spectate: watching through the host's camera (fov \(view.fov))");
    }
    this.FirstPerson(position);
  }

  // The host's own eyes (first person): its puppet's head would fill the
  // picture, so the puppet is hidden while the view sits in it (within 1 m
  // of it, 1.5 m to show it again).
  private func FirstPerson(view: Vector4) -> Void {
    let host = this.m_story.HostPlayer();
    let at = TandemNative.RemotePosition(host);
    if at.W < 0.5 {
      return;
    }
    let distance = Vector4.Distance2D(view, at);
    if !this.m_hostHidden && distance < 1.0 {
      this.m_hostHidden = true;
      TandemVisibility.Hide(this.m_game, host, n"spectate", 0.0);
    } else if this.m_hostHidden && distance > 1.5 {
      this.ShowHost();
    }
  }

  private func ShowHost() -> Void {
    if this.m_hostHidden {
      this.m_hostHidden = false;
      TandemVisibility.Show(this.m_game, this.m_story.HostPlayer(), n"spectate", 0.0);
    }
  }

  // Our own eyes again; the camera body goes.
  private func ReleaseCamera() -> Void {
    if this.m_cameraLive {
      this.m_cameraLive = false;
      let body = GameInstance.FindEntityByID(this.m_game, this.m_camera);
      let lens = IsDefined(body) ? body.FindComponentByType(n"gameCameraComponent") as CameraComponent : null;
      if IsDefined(lens) {
        lens.Deactivate(0.4, false);
      }
      TandemNative.Log("spectate: our own camera again");
    }
    if this.m_cameraAsked {
      this.m_cameraAsked = false;
      GameInstance.GetDynamicEntitySystem().DeleteEntity(this.m_camera);
    }
  }

  // "Stop watching": only noted here, done on the next frame.
  // job thread safe: sets a field of its own; Frame acts on it.
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if this.m_watching && Equals(ListenerAction.GetName(action), n"Choice1")
      && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      this.m_leaveAsked = true;
    }
  }

  // ---- the others' spectators ----

  // A player's story flags (Story.reds, every StoryState): a guest that
  // watches the host's moment is parked where it stood: its puppet hidden,
  // and unseen by our NPCs (its visible object component off: ScriptedPuppet
  // .GetVisibleObjectComponent, final.reds:20573).
  public func OnPlayerFlags(player: Uint32, flags: Int32) -> Void {
    let watching = (flags & 32) != 0;
    if NotEquals(watching, ArrayContains(this.m_spectators, player)) {
      if watching {
        ArrayPush(this.m_spectators, player);
        TandemVisibility.Hide(this.m_game, player, n"spectate", 0.5);
        TandemNative.Log(s"spectate: player \(player) watches the host's moment: its puppet parked out of sight");
      } else {
        ArrayRemove(this.m_spectators, player);
        TandemVisibility.Show(this.m_game, player, n"spectate", 0.5);
        TandemNative.Log(s"spectate: player \(player) plays again");
      }
      this.SeenByNpcs(player, !watching);
    }
  }

  private func SeenByNpcs(player: Uint32, seen: Bool) -> Void {
    let puppet = this.m_story.PuppetOf(player) as ScriptedPuppet;
    let sense = IsDefined(puppet) ? puppet.GetVisibleObjectComponent() : null;
    if IsDefined(sense) {
      sense.Toggle(seen);
    }
  }

  // Every second or so: a puppet built again is unseen again.
  public func KeepSpectatorsHidden() -> Void {
    for player in this.m_spectators {
      this.SeenByNpcs(player, false);
    }
  }

  // A player left: nothing of ours held for it.
  public func OnPlayerGone(player: Uint32) -> Void {
    if ArrayContains(this.m_spectators, player) {
      ArrayRemove(this.m_spectators, player);
      TandemVisibility.Show(this.m_game, player, n"spectate", 0.0);
    }
  }

  private func ShowSpectators() -> Void {
    for player in this.m_spectators {
      TandemVisibility.Show(this.m_game, player, n"spectate", 0.0);
      this.SeenByNpcs(player, true);
    }
    ArrayClear(this.m_spectators);
  }

  // ---- commands (tools/game.sh cmd story spectate …) ----

  public func Command(command: String) -> Void {
    let host = TandemNative.IsHost();
    if Equals(command, "story spectate on") || Equals(command, "story spectate off") {
      let on = Equals(command, "story spectate on");
      if host {
        // (test, host) as if our V were in a scripted moment.
        TandemStory.SpectateTest(on);
        this.m_lastScan = 0.0;
        TandemNative.Log(s"spectate: test moment \(on ? "on" : "off")");
      } else {
        // (guest) watching allowed again, or not for the rest of the session.
        TandemStory.SpectateAllow(on);
        if !on && TandemStory.SpectateLeave() {
          this.Finish(false);
        }
        TandemNative.Log(s"spectate: watching \(on ? "allowed" : "off for this session")");
      }
    } else {
      this.Report();
    }
  }

  public func Report() -> Void {
    let spectators = "";
    for player in this.m_spectators {
      spectators += s" \(player)";
    }
    TandemNative.Log(s"spectate: on \(TandemStory.SpectateOn()), host moment \(this.m_moment) (reasons \(this.m_reasons)), test \(TandemStory.SpectateTestOn()), watching \(this.m_watching), camera \(this.m_cameraLive ? "the host's" : "ours"), spectators:\(StrLen(spectators) > 0 ? spectators : " none")");
  }
}
