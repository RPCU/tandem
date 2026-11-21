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
  // Guest: the proxy's face follows the host's (scene_rigs).
  public let faceOn: EntityID;
  public let faced: Bool;
  // Guest: its proxy has been there; a ghost: shown now.
  public let seen: Bool;
  public let shown: Bool;
}

public class TandemSceneVoice {
  public let line: Uint64;
  public let speaker: EntityID;
}

// Guest: something the host's scene put in an actor's (or a V's) hand.
public class TandemSceneHeld {
  public let prop: Uint32;
  public let record: TweakDBID;
  public let npc: Uint32;
  public let player: Uint32;
  public let left: Bool;
  // In the hand of this body now.
  public let placed: Bool;
  public let on: EntityID;
}

// Guest: something the host's scene set down that nobody holds, spawned
// here from the same template.
public class TandemScenePlaced {
  public let prop: Uint32;
  public let entity: Uint64;
  public let id: EntityID;
  public let at: Vector4;
  public let turn: Quaternion;
}

// The next frame, while a cutscene's camera is sampled (host) or followed
// (guest).
public class TandemSceneFrame extends DelayCallback {
  public let scenes: wref<TandemScenes>;

  public func Call() -> Void {
    if IsDefined(this.scenes) {
      this.scenes.Frame();
    }
  }
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
  // Host: item entities that appeared while our scene played (props), the
  // test prop (cmd story scene prop), whether we listen for them.
  // Host: the NPCs whose faces are read (they speak) and until when.
  private let m_rigs: Bool;
  private let m_faces: array<Uint32>;
  private let m_propSeen: array<EntityID>;
  private let m_testProp: TweakDBID;
  private let m_propsListening: Bool;

  // Guest: the host's scene as heard.
  private let m_on: Bool;
  private let m_tier: Int32;
  private let m_flags: Int32;
  private let m_actors: array<ref<TandemSceneActor>>;
  private let m_voices: array<ref<TandemSceneVoice>>;
  private let m_held: array<ref<TandemSceneHeld>>;
  private let m_placed: array<ref<TandemScenePlaced>>;

  // The watch camera (tandem.ini scene_camera). Host: our tier as last
  // sampled. Guest: the camera body we asked for, whether the picture is
  // its now, whether the player left it for this scene. Both: whether the
  // per-frame tick runs.
  private let m_cameraMode: Bool;
  private let m_hostTier: Int32;
  private let m_frameOn: Bool;
  private let m_camera: EntityID;
  private let m_cameraAsked: Bool;
  private let m_cameraLive: Bool;
  private let m_cameraOff: Bool;
  private let m_cameraLogged: Bool;
  // (test, host) cmd story scene camera on: sampled outside a cutscene too.
  private let m_testCamera: Bool;
  private let m_lastTick: Float;
  private let m_watchShown: Bool;
  private let m_watchOpen: Bool;
  private let m_pushed: Bool;

  // test reset: whether replay is on, said again (said once at the start;
  // a scenario after the first of its settings reads it).
  public func ResayForTests() -> Void {
    if this.m_mode > 0 {
      TandemNative.Log(s"scene: replay on\(this.m_mode == 2 ? ", with the actors' bodies" : "")");
    }
  }

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
    this.m_mode = TandemStory.SceneReplay();
    this.m_cameraMode = this.m_mode > 0 && TandemStory.SceneCameraOn();
    this.m_rigs = TandemStory.SceneRigs();
    if this.m_mode > 0 {
      TandemNative.Log(s"scene: replay on\(this.m_mode == 2 ? ", with the actors' bodies" : "")");
      // Props: item entities appearing while our scene plays (host).
      if !this.m_propsListening {
        this.m_propsListening = true;
        GameInstance.GetCallbackSystem().RegisterCallback(n"Entity/Attach", this, n"OnPropAttach").AddTarget(EntityTarget.Type(n"gameItemObject"));
      }
    }
  }

  // An item entity appeared. May run on a job thread while the world
  // streams in: queued only, looked at on HostTick.
  private cb func OnPropAttach(event: ref<EntityLifecycleEvent>) {
    let entity = event.GetEntity();
    if this.m_play == 0 || !IsDefined(entity) {
      return;
    }
    TandemStreaming.QueueStreamed(TandemStreamKind.Prop(), entity.GetEntityID(), n"None", true);
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
    this.m_hostTier = tier;
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
          // Far ones too: their poses come less often (the scene's budget).
          let range = this.m_test ? 12.0 : 100.0;
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
    this.HostFaces(actors, bodies);
    if playing && !ownOnly {
      this.HostProps(actors, bodies, scenes, player);
    } else {
      TandemStreaming.TakeStreamed(TandemStreamKind.Prop());
      ArrayClear(this.m_propSeen);
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
    this.StartFrames();
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

  // What the scene's actors hold, for the next sample. An item in an
  // actor's left hand slot (the right one travels with its animation state,
  // NpcAnims); an item entity the scene brought in, in the hands of whoever
  // of the cast or our V stands at it.
  private func HostProps(actors: array<Uint32>, bodies: array<ref<GameObject>>, scenes: ref<SceneSystemInterface>, player: ref<PlayerPuppet>) -> Void {
    let transactions = GameInstance.GetTransactionSystem(this.m_game);
    let i = 0;
    while i < ArraySize(actors) {
      let item = transactions.GetItemInSlot(bodies[i], t"AttachmentSlots.WeaponLeft");
      if IsDefined(item) {
        TandemStory.SceneProp(Cast<Uint64>(actors[i]) * 2ul + 1ul, ItemID.GetTDBID(item.GetItemID()), actors[i], 0u, true);
      }
      i += 1;
    }
    if this.m_test && TDBID.IsValid(this.m_testProp) && ArraySize(actors) > 0 {
      TandemStory.SceneProp(7ul, this.m_testProp, actors[0], 0u, false);
    }
    for event in TandemStreaming.TakeStreamed(TandemStreamKind.Prop()) {
      if event.attached && !ArrayContains(this.m_propSeen, event.entity) && ArraySize(this.m_propSeen) < 64 {
        ArrayPush(this.m_propSeen, event.entity);
      }
    }
    let kept: array<EntityID>;
    for id in this.m_propSeen {
      let item = GameInstance.FindEntityByID(this.m_game, id) as ItemObject;
      if IsDefined(item) {
        ArrayPush(kept, id);
        if IsDefined(scenes) && scenes.IsEntityInScene(id) {
          this.HostProp(item, actors, bodies, player);
        }
      }
    }
    this.m_propSeen = kept;
  }

  // One item entity of the scene: held by the nearest of its actors and our
  // V, within arm's reach (at a hand, when that body's hands can be read);
  // not what already sits in that body's hand slots. In nobody's hands: set
  // down where it is (a glass on a table, a dropped gun).
  private func HostProp(item: ref<ItemObject>, actors: array<Uint32>, bodies: array<ref<GameObject>>, player: ref<PlayerPuppet>) -> Void {
    let transactions = GameInstance.GetTransactionSystem(this.m_game);
    let at = item.GetWorldPosition();
    let holder: ref<GameObject> = null;
    let npc = 0u;
    let best = 1.5;
    let i = 0;
    while i < ArraySize(actors) {
      let distance = Vector4.Distance(bodies[i].GetWorldPosition(), at);
      if distance < best && this.AtHand(bodies[i], at) {
        best = distance;
        holder = bodies[i];
        npc = actors[i];
      }
      i += 1;
    }
    if Vector4.Distance(player.GetWorldPosition(), at) < best && this.AtHand(player, at) {
      holder = player;
      npc = 0u;
    }
    if !IsDefined(holder) {
      this.HostPlaced(item);
      return;
    }
    let right = transactions.GetItemInSlot(holder, t"AttachmentSlots.WeaponRight");
    let left = transactions.GetItemInSlot(holder, t"AttachmentSlots.WeaponLeft");
    if right == item || left == item {
      return;
    }
    let record = ItemID.GetTDBID(item.GetItemID());
    if !TDBID.IsValid(record) {
      return;
    }
    TandemStory.SceneProp(EntityID.ToHash(item.GetEntityID()), record, npc, npc == 0u ? TandemStory.You() : 0u, IsDefined(right) && !IsDefined(left));
  }

  // `at` is within a hand's reach of one of the body's hands, or its hands
  // cannot be read (then within 1.5 m of it is enough, as before).
  private func AtHand(body: ref<GameObject>, at: Vector4) -> Bool {
    let puppet = body as ScriptedPuppet;
    let slots = IsDefined(puppet) ? puppet.GetSlotComponent() : null;
    if !IsDefined(slots) {
      return true;
    }
    let read = false;
    for name in [n"RightHand", n"LeftHand"] {
      let hand = new WorldTransform();
      if slots.GetSlotTransform(name, hand) {
        read = true;
        if Vector4.Distance(WorldPosition.ToVector4(WorldTransform.GetWorldPosition(hand)), at) < 0.35 {
          return true;
        }
      }
    }
    return !read;
  }

  // An item entity of the scene in nobody's hands: where it is, for the guests.
  private func HostPlaced(item: ref<ItemObject>) -> Void {
    let entity = TandemStory.PathHash(item.GetTemplatePath());
    if entity == 0ul {
      return;
    }
    let at = item.GetWorldPosition();
    let q = item.GetWorldOrientation();
    TandemStory.ScenePlaced(EntityID.ToHash(item.GetEntityID()), entity, item.GetCurrentAppearanceName(), at.X, at.Y, at.Z, q.i, q.j, q.k, q.r);
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

  // The faces of the actors that speak now (the plugin's stage knows the
  // lines being spoken) are read, at most three at once; let go half a
  // second after their line.
  private func HostFaces(actors: array<Uint32>, bodies: array<ref<GameObject>>) -> Void {
    if !this.m_rigs {
      return;
    }
    let speakers = TandemStory.SceneSpeakers();
    let i = ArraySize(this.m_faces) - 1;
    while i >= 0 {
      if !ArrayContains(speakers, this.m_faces[i]) || !ArrayContains(actors, this.m_faces[i]) {
        TandemStory.FaceSource(this.m_faces[i], null);
        ArrayErase(this.m_faces, i);
      }
      i -= 1;
    }
    for npc in speakers {
      let at = ArrayFindFirst(actors, npc);
      if at >= 0 && !ArrayContains(this.m_faces, npc) && ArraySize(this.m_faces) < 3 {
        ArrayPush(this.m_faces, npc);
        TandemStory.FaceSource(npc, bodies[at]);
        TandemNative.Log(s"scene: npc \(npc)'s face is read while it speaks");
      }
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
    for face in this.m_faces {
      TandemStory.FaceSource(face, null);
    }
    ArrayClear(this.m_faces);
    ArrayClear(this.m_propSeen);
    this.m_play = 0;
    this.m_hostTier = 0;
    this.m_testCamera = false;
    this.m_test = false;
    this.m_testProp = TDBID.None();
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
        this.m_cameraOff = false;
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
      case 57:
        this.OnProp(event);
        break;
      case 58:
        this.OnPropGone(event.hash);
        break;
      case 62:
        this.OnPlaced(event.hash);
        break;
      case 63:
        this.OnPlacedGone(event.hash);
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
    if actor.faced {
      TandemStory.FaceBind(actor.npc, null);
      actor.faced = false;
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

  // ---- props ----

  private func HandSlot(left: Bool) -> TweakDBID {
    return left ? t"AttachmentSlots.WeaponLeft" : t"AttachmentSlots.WeaponRight";
  }

  // The host's scene put `record` in someone's hand, or it changed hands:
  // out of the old hand now, into the new one on the next tick.
  private func OnProp(event: TandemStoryEvent) -> Void {
    let held: ref<TandemSceneHeld> = null;
    for known in this.m_held {
      if known.prop == event.hash {
        held = known;
      }
    }
    if IsDefined(held) {
      this.Unplace(held);
    } else {
      held = new TandemSceneHeld();
      held.prop = event.hash;
      ArrayPush(this.m_held, held);
    }
    held.record = event.reward;
    held.npc = event.npc;
    held.player = event.player;
    held.left = (event.flags & 2) != 0;
    TandemNative.Log(s"scene: prop \(held.prop) (\(TDBID.ToStringDEBUG(held.record))) held by npc \(held.npc), player \(held.player)\(held.left ? ", left hand" : "")");
    this.m_lastTick = 0.0;
  }

  private func OnPropGone(prop: Uint32) -> Void {
    let kept: array<ref<TandemSceneHeld>>;
    for held in this.m_held {
      if held.prop == prop {
        this.Unplace(held);
        TandemNative.Log(s"scene: prop \(prop) gone");
      } else {
        ArrayPush(kept, held);
      }
    }
    this.m_held = kept;
  }

  // Out of the hand it is in, if it is still what that hand holds.
  private func Unplace(held: ref<TandemSceneHeld>) -> Void {
    if !held.placed {
      return;
    }
    held.placed = false;
    let body = GameInstance.FindEntityByID(this.m_game, held.on) as GameObject;
    let slot = this.HandSlot(held.left);
    if IsDefined(body) && Equals(TandemWeapons.Held(body, slot), held.record) {
      TandemWeapons.Hold(body, slot, TDBID.None());
      GameInstance.GetTransactionSystem(this.m_game).RemoveItem(body, ItemID.FromTDBID(held.record), 1);
    }
  }

  // A prop set down by the host's scene: spawned from its template there,
  // or moved (again on the tick, once the entity is in).
  private func OnPlaced(prop: Uint32) -> Void {
    let place = TandemStory.ScenePlaceOf(prop);
    if !place.on {
      return;
    }
    let known: ref<TandemScenePlaced> = null;
    for placed in this.m_placed {
      if placed.prop == prop {
        known = placed;
      }
    }
    if IsDefined(known) && known.entity != place.entity {
      this.OnPlacedGone(prop);
      known = null;
    }
    if IsDefined(known) {
      this.MovePlaced(known, place);
      return;
    }
    if ArraySize(this.m_placed) >= 16 {
      return;
    }
    let spec = new DynamicEntitySpec();
    spec.templatePath = TandemStory.PathOf(place.entity);
    if NotEquals(place.appearance, n"None") {
      spec.appearanceName = place.appearance;
    }
    spec.position = new Vector4(place.x, place.y, place.z, 1.0);
    spec.orientation = new Quaternion(place.i, place.j, place.k, place.r);
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    ArrayPush(spec.tags, n"Tandem.SceneProp");
    let placed = new TandemScenePlaced();
    placed.prop = prop;
    placed.entity = place.entity;
    placed.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    placed.at = spec.position;
    placed.turn = spec.orientation;
    ArrayPush(this.m_placed, placed);
    TandemNative.Log(s"scene: prop \(prop) set down (template \(place.entity))");
  }

  // Where the host says it is now, if it moved.
  private func MovePlaced(placed: ref<TandemScenePlaced>, place: TandemScenePlace) -> Void {
    let at = new Vector4(place.x, place.y, place.z, 1.0);
    let turn = new Quaternion(place.i, place.j, place.k, place.r);
    let body = GameInstance.FindEntityByID(this.m_game, placed.id);
    if !IsDefined(body) {
      placed.at = at;
      placed.turn = turn;
      return;
    }
    let shown = body.GetWorldPosition();
    if Vector4.Distance(shown, at) < 0.01 && Vector4.Distance(placed.at, at) < 0.01 {
      return;
    }
    placed.at = at;
    placed.turn = turn;
    let object = body as GameObject;
    if IsDefined(object) {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(object, at, Quaternion.ToEulerAngles(turn));
    } else {
      let moved = new WorldTransform();
      WorldTransform.SetPosition(moved, at);
      WorldTransform.SetOrientation(moved, turn);
      body.SetWorldTransform(moved);
    }
  }

  private func OnPlacedGone(prop: Uint32) -> Void {
    let kept: array<ref<TandemScenePlaced>>;
    for placed in this.m_placed {
      if placed.prop == prop {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(placed.id);
        TandemNative.Log(s"scene: prop \(prop) taken away");
      } else {
        ArrayPush(kept, placed);
      }
    }
    this.m_placed = kept;
  }

  // Each prop set down where the host last said (a spawn that came in late).
  private func MoveProps() -> Void {
    for placed in this.m_placed {
      let place = TandemStory.ScenePlaceOf(placed.prop);
      if place.on {
        this.MovePlaced(placed, place);
      }
    }
  }

  // Each prop into the hand of its holder's body here (an actor's proxy, a
  // player's puppet), once the body is there; again when it was rebuilt.
  private func PlaceProps() -> Void {
    let npcs = this.Npcs();
    for held in this.m_held {
      if held.placed && !IsDefined(GameInstance.FindEntityByID(this.m_game, held.on)) {
        held.placed = false;
      }
      if !held.placed && TDBID.IsValid(held.record) {
        let body: ref<GameObject> = null;
        if held.npc != 0u {
          body = IsDefined(npcs) ? npcs.ReadyProxy(held.npc) : null;
        } else if held.player != 0u && held.player != TandemStory.You() {
          body = this.m_story.PuppetOf(held.player);
        }
        if IsDefined(body) {
          let ok = TandemWeapons.Hold(body, this.HandSlot(held.left), held.record);
          held.placed = true;
          held.on = body.GetEntityID();
          TandemNative.Log(s"scene: prop \(held.prop) in the hand of npc \(held.npc), player \(held.player)\(ok ? "" : " (the slot refused it)")");
        }
      }
    }
  }

  // The scene is over, or the session is: every proxy is its own again, no
  // voice goes on, no offer stays up.
  public func End() -> Void {
    for held in this.m_held {
      this.Unplace(held);
    }
    ArrayClear(this.m_held);
    for placed in this.m_placed {
      GameInstance.GetDynamicEntitySystem().DeleteEntity(placed.id);
    }
    ArrayClear(this.m_placed);
    this.ReleaseCamera();
    this.m_cameraOff = false;
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
        if actor.faced {
          TandemStory.FaceBind(actor.npc, null);
          actor.faced = false;
        }
      } else {
        // Its face takes what the host says of it while it speaks.
        if this.m_rigs && (!actor.faced || actor.faceOn != proxy.GetEntityID()) {
          TandemStory.FaceBind(actor.npc, proxy);
          actor.faced = true;
          actor.faceOn = proxy.GetEntityID();
        }
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
    this.PlaceProps();
    this.MoveProps();
    this.StartFrames();
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
    // A spectator hears from the host's camera, wherever the moment went.
    if !holocall && !TandemStory.SpectateWatching() && Vector4.Distance(speaker.GetWorldPosition(), player.GetWorldPosition()) > 40.0 {
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
    // A V's line in the recording of that V's body, not of ours.
    let gendered = data.isPlayer && TandemStory.VoiceGender(voice, this.BodyOf(event.player));
    speaker.QueueEvent(voice);
    let spoken = new TandemSceneVoice();
    spoken.line = event.line;
    spoken.speaker = speaker.GetEntityID();
    ArrayPush(this.m_voices, spoken);
    if ArraySize(this.m_voices) > 16 {
      ArrayErase(this.m_voices, 0);
    }
    TandemNative.Log(s"scene: line voiced (npc \(event.npc), player \(event.player), from \(event.duration) s\(holocall ? ", holocall" : "")\(ghost ? ", inner voice" : "")\(gendered ? ", their V's voice" : ""))");
  }

  // A player's body in the session: 1 female, 2 male, 0 unknown.
  private func BodyOf(player: Uint32) -> Int32 {
    for info in TandemSync.Players() {
      if info.player == player {
        return info.body;
      }
    }
    return 0;
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
    // Not while we watch the host's moment through its camera (Spectate.reds).
    let offer = this.m_on && this.m_tier >= 3 && anchor.W >= 0.0 && IsDefined(player) && !TandemStory.SpectateWatching();
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

      if IsDefined(player) && !this.m_cameraLive {
        player.UnregisterInputListener(this, n"Choice1");
      }
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Scenes.TandemScenes.OnAction");
    // Watching through the host's camera: the key leaves it.
    if this.m_cameraLive && Equals(ListenerAction.GetName(action), n"Choice1")
      && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      this.m_cameraOff = true;
      TandemNative.Log("scene: the player leaves the host's camera");
      return true;
    }
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

  // ---- the watch camera ----
  // A full cutscene (tier 5) is a scene camera's picture, not V's eyes. The
  // host samples its game's active camera every frame; a guest in the
  // host's moment shows the same view on a camera of its own, and gets its
  // own eyes back when the views stop (the scene is over, the host left the
  // cutscene or is gone), when it leaves the moment, or when the player
  // asks. A private scene is never announced, so never sampled.

  private func WantsFrames() -> Bool {
    if !this.m_cameraMode {
      return false;
    }
    return (this.m_play != 0 && (this.m_hostTier == 5 || this.m_testCamera)) || (this.m_on && this.m_tier == 5) || this.m_cameraLive || this.m_cameraAsked;
  }

  private func StartFrames() -> Void {
    if !this.m_frameOn && this.WantsFrames() {
      this.m_frameOn = true;
      this.ScheduleFrame();
    }
  }

  private func ScheduleFrame() -> Void {
    let frame = new TandemSceneFrame();
    frame.scenes = this;
    // 0 s: the next frame. Real time: not slowed by time dilation.
    GameInstance.GetDelaySystem(this.m_game).DelayCallback(frame, 0.0, false);
  }

  public func Frame() -> Void {
    if this.m_play != 0 && (this.m_hostTier == 5 || this.m_testCamera) {
      this.HostFrame();
    } else if this.m_on && this.m_tier == 5 {
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

  // Host: what our screen shows now, whatever owns the camera.
  private func HostFrame() -> Void {
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let transform: Transform; // assigned on every path
    if IsDefined(cameras) && cameras.GetActiveCameraWorldTransform(transform) {
      let at = Transform.GetPosition(transform);
      let q = Transform.GetOrientation(transform);
      TandemStory.SceneCameraSample(at.X, at.Y, at.Z, q.i, q.j, q.k, q.r, cameras.GetActiveCameraFOV());
    }
  }

  // Guest: the host's view on a camera of ours.
  private func GuestFrame() -> Void {
    let view = TandemStory.SceneView();
    if !view.on || this.m_cameraOff || !TandemStory.RestrictInMoment() {
      this.ReleaseCamera();
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
      ArrayPush(spec.tags, n"Tandem.Camera");
      this.m_camera = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
      this.m_cameraAsked = true;
      this.m_cameraLogged = false;
      TandemNative.Log("scene: a camera for the host's cutscene asked for");
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
        TandemNative.Log("scene: the camera body has no camera component: our own eyes stay");
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
      let player = GetPlayer(this.m_game);
      if IsDefined(player) {
        TandemText.Hint(this.m_game, n"TandemCamera", n"Choice1", true, TandemText.Get("Scene-LeaveCamera"), inkInputHintHoldIndicationType.Press);
        player.RegisterInputListener(this, n"Choice1");
      }
      TandemNative.Log(s"scene: watching through the host's camera (fov \(view.fov))");
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
      TandemText.Hint(this.m_game, n"TandemCamera", n"Choice1", false, "", inkInputHintHoldIndicationType.Press);
      let player = GetPlayer(this.m_game);
      if IsDefined(player) && !this.m_watchOpen {
        player.UnregisterInputListener(this, n"Choice1");
      }
      TandemNative.Log("scene: our own camera again");
    }
    if this.m_cameraAsked {
      this.m_cameraAsked = false;
      GameInstance.GetDynamicEntitySystem().DeleteEntity(this.m_camera);
    }
  }

  // ---- commands (tools/game.sh cmd story scene …) ----

  public func Command(command: String) -> Void {
    if Equals(command, "story scene report") {
      TandemNative.Log(TandemStory.SceneReport());
      TandemNative.Log(s"scene: mode \(this.m_mode), on \(this.m_on), tier \(this.m_tier), flags \(this.m_flags), \(ArraySize(this.m_actors)) actor(s), \(ArraySize(this.m_voices)) voice(s), \(ArraySize(this.m_held)) prop(s), announced play \(this.m_play), camera \(this.m_cameraLive ? "followed" : "ours"), \(TandemStory.SceneViews()) view(s) heard");
    } else if Equals(command, "story scene watch") {
      this.Watch();
    } else if Equals(command, "story scene test on") || Equals(command, "story scene test off") {
      // (test, host) as if a scene played with the shared NPCs around our V.
      this.m_test = Equals(command, "story scene test on");
      this.m_lastSample = 0.0;
      TandemNative.Log(s"scene: test scene \(this.m_test ? "on" : "off")");
    } else if Equals(command, "story scene camera on") || Equals(command, "story scene camera off") {
      // (test, host) our camera's view is sent as if our scene were a cutscene.
      this.m_testCamera = Equals(command, "story scene camera on");
      this.StartFrames();
      TandemNative.Log(s"scene: test camera \(this.m_testCamera ? "on" : "off")");
    } else if StrBeginsWith(command, "story scene prop ") {
      // (test, host) story scene prop RECORD|off: as if the test scene's
      // first actor held that item in its right hand.
      let name = StrAfterFirst(command, "prop ");
      this.m_testProp = Equals(name, "off") ? TDBID.None() : TDBID.Create(name);
      this.m_lastSample = 0.0;
      TandemNative.Log(s"scene: test prop \(name)");
    } else if StrBeginsWith(command, "story scene voice ") {
      // (test) story scene voice STRINGID [female|male]: that line's
      // voice-over on the nearest shared NPC or proxy (or our V), as a guest
      // would play it; with a body, as a V's line in that body's voice.
      let player = GetPlayer(this.m_game);
      let args = StrAfterFirst(command, "voice ");
      let body = 0;
      if StrEndsWith(args, " female") {
        body = 1;
        args = StrBeforeFirst(args, " ");
      } else if StrEndsWith(args, " male") {
        body = 2;
        args = StrBeforeFirst(args, " ");
      }
      let npcs = this.Npcs();
      let speaker: ref<GameObject> = IsDefined(npcs) ? npcs.NearestProxy(player.GetWorldPosition()) : null;
      if !IsDefined(speaker) {
        speaker = player;
      }
      let data: audioDialogLineEventData;
      data.stringId = TandemStory.LineId(StringToUint64(args));
      data.context = locVoiceoverContext.Vo_Context_Quest;
      data.expression = locVoiceoverExpression.Vo_Expression_Spoken;
      data.isPlayer = body != 0;
      let voice = new DialogLineEvent();
      voice.data = data;
      let gendered = body != 0 && TandemStory.VoiceGender(voice, body);
      speaker.QueueEvent(voice);
      TandemNative.Log(s"scene: test line \(args) queued on \(EntityID.ToDebugString(speaker.GetEntityID()))\(gendered ? s", body \(body)" : "")");
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }
}
