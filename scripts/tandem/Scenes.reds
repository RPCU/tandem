// Scene replay (docs/architecture/scene-replay.md): the host's story scenes
// as its guests watch them. The scene runs on the host's game only; a guest
// never plays its own copy. The host says that a scene is on and where it is
// staged, which of the NPCs it shares act in it, each actor's body pose
// (plugin, engine/posestream.cpp) and each voiced line with the moment it
// began. A guest draws the poses on its proxies, plays each line's
// voice-over itself (the game's own dialog line event, queued on the
// speaker: the guest's language, the speaker's place in the world), shows
// Johnny with his glitch when he comes and goes, offers its V a place to
// watch from, and never leaves it standing inside an actor.
//
// Off unless tandem.ini says scene_replay = on (nothing here is verified in
// game yet). The plugin turns the host's messages into cues (core/scene.hpp,
// tested without the game): this script only applies them.
//
// Owned by TandemStorySystem (Story.reds), which ticks it and passes it the
// story events 50 to 56 (StoryNatives.reds).

public class TandemSceneActor {
  public let npc: Uint32;
  // 1: a ghost (Johnny: there through the Relic only), 2: not shown now,
  // 4: its body is streamed.
  public let flags: Int32;
  // Guest: the proxy body bound to the pose stream.
  public let bound: Bool;
  public let boundTo: EntityID;
  // Guest: its proxy has been there; a ghost: shown now.
  public let seen: Bool;
  public let shown: Bool;
}

public class TandemSceneVoice {
  public let line: Uint64;
  public let speaker: EntityID;
}

public class TandemScenes {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  // 0 off, 1 on, 2 with the actors' bodies (TandemStory.SceneReplay).
  private let m_mode: Int32;

  // Host: when we last sampled, the play we announced (0: none), the NPCs
  // whose bodies are read, and the test scene (cmd story scene test).
  private let m_lastSample: Float;
  private let m_play: Int32;
  private let m_sources: array<Uint32>;
  private let m_test: Bool;

  // Guest: the host's scene as heard.
  private let m_on: Bool;
  private let m_tier: Int32;
  private let m_flags: Int32;
  private let m_actors: array<ref<TandemSceneActor>>;
  private let m_voices: array<ref<TandemSceneVoice>>;
  private let m_lastTick: Float;
  private let m_watchShown: Bool;
  private let m_watchOpen: Bool;
  private let m_pushed: Bool;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
    this.m_mode = TandemStory.SceneReplay();
    if this.m_mode > 0 {
      TandemNative.Log(s"scene: replay on\(this.m_mode == 2 ? ", with the actors' bodies" : "")");
    }
  }

  private func Npcs() -> ref<TandemNpcs> {
    let system = TandemSystem.Of(this.m_game);
    return IsDefined(system) ? system.Npcs() : null;
  }

  // An NPC that is there through the Relic only: it comes and goes with a
  // glitch (Johnny; gamePhantomEntityComponent, Codeware's import).
  public static func IsGhost(body: ref<GameObject>) -> Bool {
    return IsDefined(body.FindComponentByType(n"gamePhantomEntityComponent"));
  }

  // ---- host ----

  // A few times a second: the scene our game plays, its actors among the
  // NPCs we share, where it is staged.
  public func HostTick(now: Float, tier: Int32, isPrivate: Bool, held: Bool) -> Void {
    if this.m_mode == 0 || now - this.m_lastSample < 0.25 {
      return;
    }
    this.m_lastSample = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let at = player.GetWorldPosition();
    let scenes = GameInstance.GetSceneSystem(this.m_game).GetScriptInterface();
    let workspots = GameInstance.GetWorkspotSystem(this.m_game);
    let npcs = this.Npcs();
    let ours = IsDefined(scenes) && scenes.IsEntityInScene(player.GetEntityID());
    let playing = this.m_test || ours || tier >= 3;
    // Chats and one-to-one scenes are the host's own, whatever their tier.
    let ownOnly = isPrivate || (playing && !this.m_test && TandemStory.SceneClass() >= 2);
    let actors: array<Uint32>;
    let bodies: array<ref<GameObject>>;
    if playing && !ownOnly && IsDefined(npcs) {
      for known in npcs.Known() {
        let acts = !known.dead && IsDefined(scenes) && scenes.IsEntityInScene(known.id);
        if (acts || this.m_test) && ArraySize(actors) < 16 {
          let body = GameInstance.FindEntityByID(this.m_game, known.id) as GameObject;
          let range = this.m_test ? 12.0 : 30.0;
          if IsDefined(body) && Vector4.Distance(body.GetWorldPosition(), at) < range {
            let flags = this.m_mode == 2 ? 4 : 0;
            if TandemScenes.IsGhost(body) {
              flags += 1;
              // Shown while a workspot holds it (the phantom system's rule).
              if !workspots.IsActorInWorkspot(body) {
                flags += 2;
              }
            }
            let where = body.GetWorldPosition();
            TandemStory.SceneActor(known.npc, where.X, where.Y, where.Z, flags);
            ArrayPush(actors, known.npc);
            ArrayPush(bodies, body);
          }
        }
      }
    }
    let rate = IsDefined(scenes) && scenes.IsFastForwardingActive(scnFastForwardMode.Default) ? 4.0 : 1.0;
    let play = TandemStory.SceneSample(playing, ownOnly, tier, rate, held, at.X, at.Y, at.Z, Deg2Rad(player.GetWorldYaw()));
    if play != this.m_play {
      if play != 0 {
        TandemNative.Log(s"scene: our scene plays for the guests (play \(play), \(ArraySize(actors)) actor(s), tier \(tier))");
      } else {
        TandemNative.Log("scene: our scene is over for the guests");
      }
      this.m_play = play;
    }
    // The actors' bodies: read while they act, let go after.
    if this.m_mode == 2 {
      for old in this.m_sources {
        if play == 0 || !ArrayContains(actors, old) {
          TandemStory.ActorSource(old, null);
        }
      }
      ArrayClear(this.m_sources);
      if play != 0 {
        let i = 0;
        while i < ArraySize(actors) {
          TandemStory.ActorSource(actors[i], bodies[i]);
          ArrayPush(this.m_sources, actors[i]);
          i += 1;
        }
      }
    }
  }

  // A line shows on our game (Dialogue.reds, UIGameData.ShowDialogLine): its
  // voice, for the guests. `kind`: scnDialogLineType.
  public func HostLine(line: Uint64, kind: Int32, speaker: wref<GameObject>, speakerPlayer: Uint32, duration: Float) -> Void {
    if this.m_mode == 0 {
      return;
    }
    let npc = 0u;
    let npcs = this.Npcs();
    if speakerPlayer == 0u && IsDefined(speaker) && IsDefined(npcs) {
      npc = npcs.NpcOf(speaker.GetEntityID());
    }
    let flags = 0;
    if kind == 2 {
      flags = 2;
    } else if kind == 4 || kind == 8 {
      flags = 4;
    }
    // Nobody a guest could hear it from: an NPC we do not share.
    if npc == 0u && speakerPlayer == 0u && kind != 2 {
      return;
    }
    if TandemStory.SceneVoice(line, npc, speakerPlayer, duration, flags) {
      TandemNative.Log(s"scene: line voiced for the guests (npc \(npc), player \(speakerPlayer), \(duration) s)");
    }
  }

  public func HostLineHidden(line: Uint64) -> Void {
    if this.m_mode != 0 && TandemStory.SceneVoiceHide(line) {
      TandemNative.Log("scene: line cut for the guests");
    }
  }

  // The session is over: nobody's body is read any more.
  public func HostEnd() -> Void {
    for old in this.m_sources {
      TandemStory.ActorSource(old, null);
    }
    ArrayClear(this.m_sources);
    this.m_play = 0;
    this.m_test = false;
  }

  // ---- guest ----

  public func OnEvent(event: TandemStoryEvent) -> Void {
    if this.m_mode == 0 {
      return;
    }
    switch event.kind {
      case 50:
        this.m_on = true;
        this.m_tier = event.state;
        this.m_flags = event.flags;
        this.m_watchShown = false;
        TandemNative.Log(s"scene: the host's scene begins (play \(event.hash), tier \(event.state), \(event.duration) s in)");
        break;
      case 51:
        if (event.flags & 1) != 0 && (this.m_flags & 1) == 0 {
          TandemNative.Log("scene: the host skips ahead");
        }
        this.m_tier = event.state;
        this.m_flags = event.flags;
        break;
      case 52:
        TandemNative.Log(s"scene: the host's scene is over\((event.flags & 4) != 0 ? " (interrupted)" : "")");
        this.End();
        break;
      case 53:
        this.OnActor(event.npc, event.flags);
        break;
      case 54:
        this.OnActorGone(event.npc);
        break;
      case 55:
        this.OnVoice(event);
        break;
      case 56:
        this.OnVoiceStop(event.line);
        break;
      default:
        break;
    }
  }

  private func FindActor(npc: Uint32) -> ref<TandemSceneActor> {
    for actor in this.m_actors {
      if actor.npc == npc {
        return actor;
      }
    }
    return null;
  }

  private func OnActor(npc: Uint32, flags: Int32) -> Void {
    let actor = this.FindActor(npc);
    if !IsDefined(actor) {
      actor = new TandemSceneActor();
      actor.npc = npc;
      ArrayPush(this.m_actors, actor);
      TandemNative.Log(s"scene: actor \(npc) joins (flags \(flags))");
    }
    actor.flags = flags;
    // Its proxy is bound on the next tick (it may not be there yet).
    this.m_lastTick = 0.0;
  }

  private func Release(actor: ref<TandemSceneActor>) -> Void {
    if actor.bound {
      TandemStory.ActorBind(actor.npc, null);
      actor.bound = false;
    }
    // A ghost out of the scene is not there for the host either.
    if (actor.flags & 1) != 0 && actor.shown {
      let npcs = this.Npcs();
      let proxy = IsDefined(npcs) ? npcs.ReadyProxy(actor.npc) : null;
      if IsDefined(proxy) {
        this.Ghost(actor.npc, proxy, false);
      }
      actor.shown = false;
    }
  }

  private func OnActorGone(npc: Uint32) -> Void {
    let kept: array<ref<TandemSceneActor>>;
    for actor in this.m_actors {
      if actor.npc == npc {
        this.Release(actor);
        TandemNative.Log(s"scene: actor \(npc) released");
      } else {
        ArrayPush(kept, actor);
      }
    }
    this.m_actors = kept;
  }

  // The scene is over, or the session is: every proxy is its own again, no
  // voice goes on, no offer stays up.
  public func End() -> Void {
    for actor in this.m_actors {
      this.Release(actor);
    }
    if ArraySize(this.m_actors) > 0 {
      TandemNative.Log(s"scene: \(ArraySize(this.m_actors)) actor(s) released");
    }
    ArrayClear(this.m_actors);
    this.m_on = false;
    this.m_tier = 0;
    this.m_flags = 0;
    this.CloseWatch();
    this.m_watchShown = false;
  }

  // Leaving the session (we quit, or the host is gone).
  public func Leave() -> Void {
    for voice in this.m_voices {
      this.StopVoice(voice);
    }
    ArrayClear(this.m_voices);
    this.End();
    TandemStory.ActorsClear();
  }

  // A ghost's proxy comes or goes the way the host's does: the entity's own
  // effects, then the fade.
  private func Ghost(npc: Uint32, proxy: ref<GameObject>, show: Bool) -> Void {
    let effect: CName = n"None";
    let component = proxy.FindComponentByType(n"gamePhantomEntityComponent") as gamePhantomEntityComponent;
    if IsDefined(component) {
      effect = show ? component.params.teleportEndEffect : component.params.teleportStartEffect;
      if show && !IsNameValid(effect) {
        effect = component.params.spawnEffect;
      }
    }
    if IsNameValid(effect) {
      GameObjectEffectHelper.StartEffectEvent(proxy, effect);
    }
    TandemVisibility.Fade(this.m_game, proxy, show, show ? 0.2 : 0.35);
    TandemNative.Log(s"scene: ghost \(npc) \(show ? "appears" : "goes") (effect \(NameToString(effect)))");
  }

  public func GuestTick(now: Float) -> Void {
    if this.m_mode == 0 || now - this.m_lastTick < 0.2 {
      return;
    }
    this.m_lastTick = now;
    if !this.m_on && ArraySize(this.m_actors) == 0 {
      return;
    }
    let npcs = this.Npcs();
    for actor in this.m_actors {
      let proxy = IsDefined(npcs) ? npcs.ReadyProxy(actor.npc) : null;
      if !IsDefined(proxy) {
        // Its body is gone (streamed out, rebuilt): bound again when back.
        if actor.bound {
          TandemStory.ActorBind(actor.npc, null);
          actor.bound = false;
        }
      } else {
        if this.m_mode == 2 && (actor.flags & 4) != 0 && (!actor.bound || actor.boundTo != proxy.GetEntityID()) {
          TandemStory.ActorBind(actor.npc, proxy);
          actor.bound = true;
          actor.boundTo = proxy.GetEntityID();
          TandemNative.Log(s"scene: actor \(actor.npc) follows the host's pose");
        }
        if (actor.flags & 1) != 0 {
          let want = (actor.flags & 2) == 0;
          if !actor.seen || NotEquals(want, actor.shown) {
            this.Ghost(actor.npc, proxy, want);
            actor.shown = want;
          }
        }
        actor.seen = true;
      }
    }
    this.UpdateWatch();
    this.KeepClear();
  }

  // ---- voices ----

  private func OnVoice(event: TandemStoryEvent) -> Void {
    let holocall = (event.flags & 2) != 0;
    let player = GetPlayer(this.m_game);
    let speaker: ref<GameObject> = null;
    let ghost = false;
    if event.npc != 0u {
      let npcs = this.Npcs();
      speaker = IsDefined(npcs) ? npcs.ReadyProxy(event.npc) : null;
      let actor = this.FindActor(event.npc);
      ghost = IsDefined(actor) && (actor.flags & 1) != 0;
    } else if event.player != 0u {
      speaker = this.m_story.PuppetOf(event.player);
    } else if holocall {
      speaker = player;
    }
    if !IsDefined(speaker) || !IsDefined(player) {
      TandemNative.Log(s"scene: line not voiced (its speaker is not here: npc \(event.npc), player \(event.player))");
      return;
    }
    // Out of earshot: the voice would be silent anyway, and its subtitle is
    // not shown either (Dialogue.reds).
    if !holocall && Vector4.Distance(speaker.GetWorldPosition(), player.GetWorldPosition()) > 40.0 {
      TandemNative.Log("scene: line not voiced (out of earshot)");
      return;
    }
    let data: audioDialogLineEventData;
    data.stringId = TandemStory.LineId(event.line);
    data.context = locVoiceoverContext.Vo_Context_Quest;
    data.expression = locVoiceoverExpression.Vo_Expression_Spoken;
    if holocall {
      data.expression = locVoiceoverExpression.Vo_Expression_Phone;
    } else if ghost {
      data.expression = locVoiceoverExpression.Vo_Expression_InnerDialog;
    }
    data.isPlayer = event.player != 0u;
    data.isHolocall = holocall;
    // A guest arriving mid-line hears it from where it is now.
    data.seekTime = event.duration;
    let voice = new DialogLineEvent();
    voice.data = data;
    speaker.QueueEvent(voice);
    let spoken = new TandemSceneVoice();
    spoken.line = event.line;
    spoken.speaker = speaker.GetEntityID();
    ArrayPush(this.m_voices, spoken);
    if ArraySize(this.m_voices) > 16 {
      ArrayErase(this.m_voices, 0);
    }
    TandemNative.Log(s"scene: line voiced (npc \(event.npc), player \(event.player), from \(event.duration) s\(holocall ? ", holocall" : "")\(ghost ? ", inner voice" : ""))");
  }

  private func StopVoice(voice: ref<TandemSceneVoice>) -> Void {
    let speaker = GameInstance.FindEntityByID(this.m_game, voice.speaker);
    if IsDefined(speaker) {
      let stop = new gameaudioeventsStopDialogLine();
      stop.stringId = TandemStory.LineId(voice.line);
      stop.fadeOut = 0.2;
      speaker.QueueEvent(stop);
    }
  }

  private func OnVoiceStop(line: Uint64) -> Void {
    let kept: array<ref<TandemSceneVoice>>;
    for voice in this.m_voices {
      if voice.line == line {
        this.StopVoice(voice);
        TandemNative.Log("scene: line cut");
      } else {
        ArrayPush(kept, voice);
      }
    }
    this.m_voices = kept;
  }

  // ---- our own V around the scene ----

  // The bodies around the scene, for the plugin's geometry: the actors'
  // proxies and, for a place to stand, the other players' puppets.
  private func ListBodies(withPuppets: Bool) -> Void {
    TandemStory.SceneBodies();
    let npcs = this.Npcs();
    for actor in this.m_actors {
      let proxy = IsDefined(npcs) ? npcs.ReadyProxy(actor.npc) : null;
      if IsDefined(proxy) {
        let at = proxy.GetWorldPosition();
        TandemStory.SceneBody(at.X, at.Y, at.Z);
      }
    }
    if withPuppets {
      let system = TandemSystem.Of(this.m_game);
      if IsDefined(system) {
        for other in system.PuppetPlayers() {
          let at = TandemNative.RemotePosition(other);
          if at.W > 0.5 && other != this.m_story.HostPlayer() {
            TandemStory.SceneBody(at.X, at.Y, at.Z);
          }
        }
      }
    }
  }

  // A staged scene the host cannot leave (tier 3 and up), our V nearby but
  // not at it: offered a place to watch from, once per scene.
  private func UpdateWatch() -> Void {
    let player = GetPlayer(this.m_game);
    let anchor = TandemStory.SceneAnchor();
    let offer = this.m_on && this.m_tier >= 3 && anchor.W >= 0.0 && IsDefined(player);
    if offer {
      let distance = Vector4.Distance2D(anchor, player.GetWorldPosition());
      offer = distance > anchor.W + 4.0 && distance < 60.0;
    }
    if offer && !this.m_watchShown {
      this.m_watchShown = true;
      this.m_watchOpen = true;
      let host = this.m_story.NameOf(this.m_story.HostPlayer());
      this.m_story.Notice("Scene-InScene", host);
      // The hint bar draws the player's own binding for Choice1.
      TandemText.Hint(this.m_game, n"TandemWatch", n"Choice1", true, TandemText.Get("Scene-Watch"), inkInputHintHoldIndicationType.Press);
      player.RegisterInputListener(this, n"Choice1");
      TandemNative.Log("scene: watch offer shown");
    } else if !offer && this.m_watchOpen {
      this.CloseWatch();
    }
  }

  private func CloseWatch() -> Void {
    if this.m_watchOpen {
      this.m_watchOpen = false;
      TandemText.Hint(this.m_game, n"TandemWatch", n"Choice1", false, "", inkInputHintHoldIndicationType.Press);
      let player = GetPlayer(this.m_game);

      if IsDefined(player) {
        player.UnregisterInputListener(this, n"Choice1");
      }
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if this.m_watchOpen && Equals(ListenerAction.GetName(action), n"Choice1")
      && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      this.Watch();
    }
  }

  // Our V goes to a place beside the host, clear of everyone, on ground it
  // can stand on, facing the scene.
  public func Watch() -> Void {
    this.CloseWatch();
    let player = GetPlayer(this.m_game);
    let host = TandemNative.RemotePosition(this.m_story.HostPlayer());
    if !this.m_on || !IsDefined(player) || host.W < 0.5 {
      TandemNative.Log("scene: no place to watch from (no scene, or the host is not drawn)");
      return;
    }
    this.ListBodies(true);
    let own = player.GetWorldPosition();
    let spot = TandemStory.WatchSpot(host.X, host.Y, host.Z, own.X, own.Y, own.Z);
    if spot.W > 100.0 {
      TandemNative.Log("scene: no clear place to watch from");
      return;
    }
    // Never into a wall or onto furniture: the nearest ground a character
    // stands on, or nothing.
    let ground = GameInstance.GetAINavigationSystem(this.m_game).FindPointInSphereForCharacter(new Vector4(spot.X, spot.Y, spot.Z, 1.0), 0.75, player);
    if NotEquals(ground.status, worldNavigationRequestStatus.OK) {
      TandemNative.Log("scene: no place to watch from (no ground there)");
      return;
    }
    let rotation = TandemYawAngles(Rad2Deg(spot.W));
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, ground.point, rotation);
    TandemNative.Log(s"scene: watching from \(ground.point.X) \(ground.point.Y) \(ground.point.Z)");
  }

  // An actor walked into where our V stands: a small step out, each tick,
  // until clear (the scene's actors go where the host's scene puts them).
  private func KeepClear() -> Void {
    let player = GetPlayer(this.m_game);
    if !this.m_on || ArraySize(this.m_actors) == 0 || !IsDefined(player) {
      return;
    }
    this.ListBodies(false);
    let own = player.GetWorldPosition();
    let step = TandemStory.KeepOut(own.X, own.Y, own.Z, 0.45, 0.12);
    if step.W < 0.01 {
      this.m_pushed = false;
      return;
    }
    let rotation = Quaternion.ToEulerAngles(player.GetWorldOrientation());
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, new Vector4(own.X + step.X, own.Y + step.Y, own.Z, 1.0), rotation);
    if !this.m_pushed {
      this.m_pushed = true;
      TandemNative.Log("scene: our V steps out of an actor's way");
    }
  }

  // ---- commands (tools/game.sh cmd story scene …) ----

  public func Command(command: String) -> Void {
    if Equals(command, "story scene report") {
      TandemNative.Log(TandemStory.SceneReport());
      TandemNative.Log(s"scene: mode \(this.m_mode), on \(this.m_on), tier \(this.m_tier), flags \(this.m_flags), \(ArraySize(this.m_actors)) actor(s), \(ArraySize(this.m_voices)) voice(s), announced play \(this.m_play)");
    } else if Equals(command, "story scene watch") {
      this.Watch();
    } else if Equals(command, "story scene test on") || Equals(command, "story scene test off") {
      // (test, host) as if a scene played with the shared NPCs around our V.
      this.m_test = Equals(command, "story scene test on");
      this.m_lastSample = 0.0;
      TandemNative.Log(s"scene: test scene \(this.m_test ? "on" : "off")");
    } else if StrBeginsWith(command, "story scene voice ") {
      // (test) story scene voice STRINGID: that line's voice-over on the
      // nearest shared NPC or proxy (or our V), as a guest would play it.
      let player = GetPlayer(this.m_game);
      let npcs = this.Npcs();
      let speaker: ref<GameObject> = IsDefined(npcs) ? npcs.NearestProxy(player.GetWorldPosition()) : null;
      if !IsDefined(speaker) {
        speaker = player;
      }
      let data: audioDialogLineEventData;
      data.stringId = TandemStory.LineId(StringToUint64(StrAfterFirst(command, "voice ")));
      data.context = locVoiceoverContext.Vo_Context_Quest;
      data.expression = locVoiceoverExpression.Vo_Expression_Spoken;
      let voice = new DialogLineEvent();
      voice.data = data;
      speaker.QueueEvent(voice);
      TandemNative.Log(s"scene: test line \(StrAfterFirst(command, "voice ")) queued on \(EntityID.ToDebugString(speaker.GetEntityID()))");
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }
}
