// Presence: a remote V seen and heard like a character of the game
// (docs/architecture/presence.md; engine facts in
// docs/research/engine/presence.md).
//
// The pose stream shows the body. Everything here is what a pose cannot
// carry, played on the puppet as the game itself would play it on a
// character: 3D, from the entity, by the audio system's own rules
// (attenuation, occlusion, the surface under its feet). Nothing is a 2D
// sound.
//
// Most of it is heard from messages that exist already (TandemPresence.
// Observe: core/include/tandem/presence.hpp): footsteps, jumps, landings,
// climbs, dashes from PlayerState; draws and holsters from Held; pain from
// the health of Presence; going down from Vitals; time abilities from
// TimeEffect; swimming (in and out of the water, under and back up, each
// stroke) from PlayerState's swimming state. Two things our game alone knows are sent as a Cue: where a
// shot of ours ended (the others draw the bullet: tracer, impact, decal,
// its sound) and that a melee swing landed. The status effects that show
// on our V travel as a set (StatusVisuals.reds).

// What the weapon in one hand of a puppet was last heard doing. One per
// hand: a dual wielding V fires, bounces and charges each weapon on its own.
public class TandemHeardHand {
  // Automatic fire: the weapon's loop is ended when the shots stop.
  public let shotAt: Float;
  public let firing: Bool;
  // Its last shot's end and when it came (a ricochet's leg starts there).
  public let traceEnd: Vector4;
  public let traceAt: Float;
  // A tech weapon's charge shown on it (proto::kCharge*), and when it was
  // last said.
  public let charge: Int32;
  public let chargeAt: Float;
  public let chargeBoard: ref<worldEffectBlackboard>;
}

public class TandemHeardPuppet {
  public let player: Uint32;
  // Footsteps: the body's own locomotion clip may raise them already (the
  // movement controller plays the game's walk and run clips): ours are
  // planted only while it does not.
  public let graphStepAt: Float;
  public let ourStepAt: Float;
  public let steps: Int32;
  public let graphSteps: Int32;
  public let sounds: Int32;
  // Each hand's weapon: its fire loop, its last shot's end, its charge.
  public let rightHand: ref<TandemHeardHand>;
  public let leftHand: ref<TandemHeardHand>;
  // What it held last frame (the holster sound names the item put away).
  public let right: TweakDBID;
  // Ability sounds playing on it (stopped when the ability ends).
  public let sfx: array<CName>;
  // The bullets drawn (those of its left hand's weapon among them), the
  // trails and the ricochets (Tracers.reds), the charges shown.
  public let traces: Int32;
  public let leftTraces: Int32;
  public let trails: Int32;
  public let ricochets: Int32;
  public let charges: Int32;
  public let flinches: Int32;
  // Its health fell: the flinch for it is due then (0: none), unless its
  // game tells the hit it was meanwhile (HitsTaken.reds).
  public let hurtDue: Float;
  public let hurtHow: Int32;
  // Water: the swimming loop effect started on it (0 none, 1 the surface's,
  // 2 the dive's), its strokes so far.
  public let water: Int32;
  public let strokes: Int32;

  public func Hand(left: Bool) -> ref<TandemHeardHand> {
    return left ? this.leftHand : this.rightHand;
  }
}

// The menu and photo mode are heard through the UI's blackboards: our tick
// stands still while a menu pauses the world.
public class TandemPresenceListener {
  public let sync: wref<TandemPresenceSync>;

  protected cb func OnMenu(value: Bool) -> Bool {
    TandemNative.HookRan(n"Presence.TandemPresenceListener.OnMenu");
    if IsDefined(this.sync) {
      this.sync.MenuChanged(value);
    }
  }

  protected cb func OnPhotoMode(value: Bool) -> Bool {
    TandemNative.HookRan(n"Presence.TandemPresenceListener.OnPhotoMode");
    if IsDefined(this.sync) {
      this.sync.PhotoChanged(value);
    }
  }
}

public class TandemPresenceSync {
  private let m_game: GameInstance;
  private let m_heard: array<ref<TandemHeardPuppet>>;
  private let m_last: Float;
  private let m_menu: Bool;
  private let m_photo: Bool;
  private let m_health: Float;
  private let m_listener: ref<TandemPresenceListener>;
  private let m_menuListener: ref<CallbackHandle>;
  private let m_photoListener: ref<CallbackHandle>;
  // Bullets drawn for someone else's shot: whose weapon fired them and
  // when. Their hits are nobody's here (the shooter's game decided).
  private let m_cosmeticBy: array<EntityID>;
  private let m_cosmeticAt: array<Float>;
  private let m_dropped: Int32;
  private let m_logged: array<CName>;
  // Footsteps of ours: 0 only where the body's own are silent, 1 always,
  // 2 never (tools/game.sh cmd presence steps auto|on|off).
  private let m_stepsMode: Int32;
  // Swimming: 0 the swimming states' own effects and the game's water
  // sounds, 1 the effects alone, 2 the sounds alone (cmd presence swim
  // both|fx|sound: whichever the puppet's body turns out to carry).
  private let m_swimMode: Int32;
  // The others' shots: 0 the drawn bullet and the weapon's trail, 1 the
  // bullet alone, 2 the trail alone (cmd presence tracer both|bullet|trail:
  // whichever shows one tracer, not two or none).
  private let m_tracerMode: Int32;
  // Our V's charge as last said (proto::kCharge*), and when; its left
  // hand's weapon's (dual wielding).
  private let m_chargeSent: Int32;
  private let m_chargeSentAt: Float;
  private let m_leftChargeSent: Int32;
  private let m_leftChargeSentAt: Float;
  // Our ricochets said in the current second (a shotgun's pellets: 8 at most).
  private let m_ricochetSecond: Float;
  private let m_ricochetsSaid: Int32;
  private let m_hud: ref<TandemPartyHud>;
  private let m_reactions: ref<TandemReactionsSync>;
  private let m_voices: ref<TandemNpcVoices>;
  private let m_status: ref<TandemStatusVisuals>;
  private let m_hitsTaken: ref<TandemHitsTaken>;

  public static func Of(game: GameInstance) -> ref<TandemPresenceSync> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Presence() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_health = 1.0;
    this.m_hud = new TandemPartyHud();
    this.m_hud.Start(game);
    this.m_reactions = new TandemReactionsSync();
    this.m_reactions.Start(game);
    this.m_voices = new TandemNpcVoices();
    this.m_voices.Start(game);
    this.m_status = new TandemStatusVisuals();
    this.m_status.Start(game);
    this.m_hitsTaken = new TandemHitsTaken();
    this.m_hitsTaken.Start(game);
    this.m_listener = new TandemPresenceListener();
    this.m_listener.sync = this;
    let boards = GameInstance.GetBlackboardSystem(game);
    let ui = boards.Get(GetAllBlackboardDefs().UI_System);
    if IsDefined(ui) {
      this.m_menu = ui.GetBool(GetAllBlackboardDefs().UI_System.IsInMenu);
      this.m_menuListener = ui.RegisterListenerBool(GetAllBlackboardDefs().UI_System.IsInMenu, this.m_listener, n"OnMenu");
    }
    let photo = boards.Get(GetAllBlackboardDefs().PhotoMode);
    if IsDefined(photo) {
      this.m_photo = photo.GetBool(GetAllBlackboardDefs().PhotoMode.IsActive);
      this.m_photoListener = photo.RegisterListenerBool(GetAllBlackboardDefs().PhotoMode.IsActive, this.m_listener, n"OnPhotoMode");
    }
  }

  public func Stop() -> Void {
    let boards = GameInstance.GetBlackboardSystem(this.m_game);
    let ui = boards.Get(GetAllBlackboardDefs().UI_System);
    if IsDefined(ui) && IsDefined(this.m_menuListener) {
      ui.UnregisterListenerBool(GetAllBlackboardDefs().UI_System.IsInMenu, this.m_menuListener);
    }
    let photo = boards.Get(GetAllBlackboardDefs().PhotoMode);
    if IsDefined(photo) && IsDefined(this.m_photoListener) {
      photo.UnregisterListenerBool(GetAllBlackboardDefs().PhotoMode.IsActive, this.m_photoListener);
    }
    this.m_menuListener = null;
    this.m_photoListener = null;
    this.m_listener = null;
    this.End();
    if IsDefined(this.m_hud) {
      this.m_hud.Stop();
    }
    this.m_hud = null;
    this.m_reactions = null;
    this.m_voices = null;
    this.m_status = null;
    this.m_hitsTaken = null;
  }

  // The session ended: nothing of it is heard or shown any more.
  public func End() -> Void {
    for heard in this.m_heard {
      TandemPresence.Forget(heard.player);
    }
    ArrayClear(this.m_heard);
    ArrayClear(this.m_cosmeticBy);
    ArrayClear(this.m_cosmeticAt);
    this.m_chargeSent = 0;
    this.m_chargeSentAt = 0.0;
    this.m_leftChargeSent = 0;
    this.m_leftChargeSentAt = 0.0;
    if IsDefined(this.m_hud) {
      this.m_hud.Clear();
    }
    if IsDefined(this.m_status) {
      this.m_status.End();
    }
    if IsDefined(this.m_hitsTaken) {
      this.m_hitsTaken.End();
    }
  }

  public func Hud() -> ref<TandemPartyHud> {
    return this.m_hud;
  }

  public func Reactions() -> ref<TandemReactionsSync> {
    return this.m_reactions;
  }

  public func Status() -> ref<TandemStatusVisuals> {
    return this.m_status;
  }

  public func HitsTaken() -> ref<TandemHitsTaken> {
    return this.m_hitsTaken;
  }

  // A time ability's sound plays on that player's puppet now (Ability).
  public func PlaysAbilitySound(player: Uint32, name: CName) -> Bool {
    let heard = this.Find(player);
    return IsDefined(heard) && ArrayContains(heard.sfx, name);
  }

  // test reset: each once-only line may be said again (a scenario reads
  // it, the session keeps the list).
  public func ResayForTests() -> Void {
    ArrayClear(this.m_logged);
  }

  private func Once(key: CName, text: String) -> Void {
    if !ArrayContains(this.m_logged, key) {
      ArrayPush(this.m_logged, key);
      TandemNative.Log(text);
    }
  }

  private func Find(player: Uint32) -> ref<TandemHeardPuppet> {
    for heard in this.m_heard {
      if heard.player == player {
        return heard;
      }
    }
    return null;
  }

  private func Known(player: Uint32) -> ref<TandemHeardPuppet> {
    let heard = this.Find(player);
    if !IsDefined(heard) {
      heard = new TandemHeardPuppet();
      heard.player = player;
      heard.graphStepAt = -10.0;
      heard.ourStepAt = -10.0;
      heard.rightHand = new TandemHeardHand();
      heard.rightHand.traceAt = -10.0;
      heard.leftHand = new TandemHeardHand();
      heard.leftHand.traceAt = -10.0;
      heard.right = TandemNative.RemoteHeld(player, false);
      ArrayPush(this.m_heard, heard);
    }
    return heard;
  }

  // A puppet is gone (its player left, or its body is being rebuilt).
  public func Forget(player: Uint32) -> Void {
    let kept: array<ref<TandemHeardPuppet>>;
    for heard in this.m_heard {
      if heard.player != player {
        ArrayPush(kept, heard);
      }
    }
    this.m_heard = kept;
    TandemPresence.Forget(player);
    if IsDefined(this.m_hud) {
      this.m_hud.Forget(player);
    }
    if IsDefined(this.m_status) {
      this.m_status.ForgetPlayer(player);
    }
  }

  // ---- our V, for the others ----

  public func MenuChanged(value: Bool) -> Void {
    this.m_menu = value;
    this.SendOurs();
  }

  public func PhotoChanged(value: Bool) -> Void {
    this.m_photo = value;
    this.SendOurs();
    if IsDefined(this.m_hud) {
      this.m_hud.Refresh();
    }
  }

  public func InPhotoMode() -> Bool {
    return this.m_photo;
  }

  // proto::kPresence*: 1 in a menu, 2 in photo mode, 4 in combat.
  private func SendOurs() -> Void {
    if !TandemNative.IsStarted() {
      return;
    }
    let flags = 0;
    if this.m_menu {
      flags += 1;
    }
    if this.m_photo {
      flags += 2;
    }
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      let id = Cast<StatsObjectID>(player.GetEntityID());
      this.m_health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(id, gamedataStatPoolType.Health, true) / 100.0;
      let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
      if IsDefined(psm) && psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Combat) == EnumInt(gamePSMCombat.InCombat) {
        flags += 4;
      }
    }
    if TandemPresence.Send(this.m_health, flags) {
      this.Once(n"sent", s"presence: our V's health \(Cast<Int32>(this.m_health * 100.0)) % and flags \(flags) sent (then on change)");
    }
  }

  // Where a shot of ours ended, for the others to draw its bullet: what
  // the crosshair is on (the game's own look-at query), else 120 m ahead.
  // A smart round that homes on a lock the others know is not drawn this
  // way but guided to it (Throws.reds, Weapons.reds decides); one with no
  // such lock flies straight, as here. A launcher's round and a grenade are
  // thrown objects (Throws.reds).
  public static func ShotFired(player: ref<GameObject>, weapon: ref<WeaponObject>, left: Bool) -> Void {
    if !IsDefined(player) || !IsDefined(weapon) {
      return;
    }
    let game = player.GetGame();
    let targeting = GameInstance.GetTargetingSystem(game);
    let at = targeting.GetLookAtPosition(player, true, true);
    let arg = left ? 1 : 0;
    if Vector4.IsZero(at) {
      let camera = GameInstance.GetCameraSystem(game);
      let from: Transform;
      camera.GetActiveCameraWorldTransform(from);
      at = Transform.GetPosition(from) + camera.GetActiveCameraForward() * 120.0;
      arg += 4;
    } else {
      if IsDefined(targeting.GetLookAtObject(player, true, true) as ScriptedPuppet) {
        arg += 2;
      }
    }
    TandemPresence.SendCue(1, arg, at);
  }

  // A bullet of ours that ricocheted hit something (a hit with bounces:
  // DamageSystem.ProcessPipeline, Combat.reds): the others draw its leg
  // from where the shot ended (the bounce) to here. The game tells scripts
  // nothing of a bounce that hit nothing.
  public static func OurRicochet(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Void {
    if !TandemNative.IsStarted() || !IsDefined(hitEvent) || hitEvent.projectionPipeline || !IsDefined(hitEvent.attackData) || hitEvent.attackData.GetNumRicochetBounces() <= 0 {
      return;
    }
    let player = GetPlayer(game);
    let instigator = hitEvent.attackData.GetInstigator();
    let presence = TandemPresenceSync.Of(game);
    if !IsDefined(player) || !IsDefined(presence) || instigator != player {
      return;
    }
    presence.SayRicochet(hitEvent.hitPosition, IsDefined(hitEvent.target as ScriptedPuppet), TandemWeapons.ByLeftHand(player, hitEvent));
  }

  // `left`: the bullet was the left hand's weapon's (its leg goes on from
  // that hand's last shot).
  private func SayRicochet(at: Vector4, character: Bool, left: Bool) -> Void {
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    if now - this.m_ricochetSecond >= 1.0 || now < this.m_ricochetSecond {
      this.m_ricochetSecond = now;
      this.m_ricochetsSaid = 0;
    }
    if this.m_ricochetsSaid >= 8 {
      return;
    }
    this.m_ricochetsSaid += 1;
    TandemPresence.SendCue(3, (character ? 2 : 0) + (left ? 1 : 0), at);
    this.Once(n"ricochetSaid", s"bullet: our ricochet said, to (\(at.X) \(at.Y) \(at.Z))");
  }

  // A tech weapon's charge, for the others' puppet of our V: each change,
  // and again every 0.25 s while it lasts, with how charged it is (its glow
  // follows; a lost end ends on its own there). Each hand's weapon has its
  // own (the weapon's own state machine and blackboard).
  private func SendOurCharge(now: Float) -> Void {
    if !TandemNative.IsStarted() {
      return;
    }
    let player = GetPlayer(this.m_game);
    this.SendHandCharge(player, false, now);
    this.SendHandCharge(player, true, now);
  }

  private func SendHandCharge(player: ref<GameObject>, left: Bool, now: Float) -> Void {
    let weapon = IsDefined(player) ? TandemPresenceSync.WeaponOf(player, left) : null;
    let step = TandemTracers.OurStep(weapon);
    let sent = left ? this.m_leftChargeSent : this.m_chargeSent;
    let sentAt = left ? this.m_leftChargeSentAt : this.m_chargeSentAt;
    if step == sent && (step == 0 || now - sentAt < 0.25) {
      return;
    }
    if step != sent {
      if left {
        this.Once(n"chargeSaidLeft", s"charge: our V's left hand's weapon charges (step \(step)), said to the others");
      } else {
        this.Once(n"chargeSaid", s"charge: our V's weapon charges (step \(step)), said to the others");
      }
    }
    if left {
      this.m_leftChargeSent = step;
      this.m_leftChargeSentAt = now;
    } else {
      this.m_chargeSent = step;
      this.m_chargeSentAt = now;
    }
    // proto::Cue Charge: `at.x` how charged it is, `at.y` the hand
    // (kChargeLeftHand).
    TandemPresence.SendCue(4, step, new Vector4(TandemTracers.OurCharge(weapon), left ? 1.0 : 0.0, 0.0, 0.0));
  }

  // ---- the others, every frame of the session ----

  // `now` is real time (the engine's): under a Sandevistan the world's
  // clock runs slow, and a partner's steps do not.
  public func Tick(now: Float, system: ref<TandemSystem>, you: Uint32) -> Void {
    let dt = now - this.m_last;
    this.m_last = now;
    if you == 0u || dt <= 0.0 || dt > 1.0 {
      // No session, or the first frame after a stall: nothing was heard.
      dt = 0.0;
    }
    this.SendOurs();
    this.SendOurCharge(now);
    for player in system.PuppetPlayers() {
      let body = system.PuppetOf(player);
      if IsDefined(body) {
        let heard = this.Known(player);
        // A body out of sight (hidden, in another world) makes no sound.
        let silent = TandemVisibility.IsHidden(this.m_game, player);
        // Seated (a car, a train): the body is carried, it does not walk.
        let walked = VehicleComponent.IsMountedToVehicle(this.m_game, body) ? 0.0 : dt;
        for event in TandemPresence.Observe(player, body.GetWorldPosition(), walked) {
          if !silent && dt > 0.0 {
            this.Play(heard, body, event, now);
          } else if event.kind >= 15 && event.kind <= 18 {
            // Unheard, its water still follows it: no loop left on a body
            // that got out while hidden.
            this.Water(heard, body, event.kind, event.arg, false);
          }
        }
        heard.right = TandemNative.RemoteHeld(player, false);
        this.HandTick(heard, body, false, now);
        this.HandTick(heard, body, true, now);
        if heard.hurtDue > 0.0 && now >= heard.hurtDue {
          this.Flinched(heard, body, now);
        }
      }
    }
    // The hits the others' Vs took, as their games tell them.
    if IsDefined(this.m_hitsTaken) {
      this.m_hitsTaken.Tick(now, system);
    }
    this.DrawCues(system, now);
    while ArraySize(this.m_cosmeticAt) > 0 && now - this.m_cosmeticAt[0] > 2.0 {
      ArrayErase(this.m_cosmeticBy, 0);
      ArrayErase(this.m_cosmeticAt, 0);
    }
    if IsDefined(this.m_reactions) {
      this.m_reactions.Tick(now, system);
    }
    if IsDefined(this.m_voices) {
      this.m_voices.Tick(system);
    }
    if IsDefined(this.m_status) && you != 0u {
      this.m_status.SendOurs(now);
      this.m_status.Tick(system);
    }
    if IsDefined(this.m_hud) {
      this.m_hud.Tick(now, system, you);
    }
  }

  // One hand's weapon of a puppet, every frame.
  private func HandTick(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, left: Bool, now: Float) -> Void {
    let hand = heard.Hand(left);
    // A charge no longer said (its end lost, or the player gone quiet).
    if hand.charge >= 1 && hand.charge <= 3 && now - hand.chargeAt > 1.2 {
      TandemTracers.ShowCharge(hand, TandemPresenceSync.WeaponOf(body, left), 0, 0.0);
      TandemNative.Log(s"charge: player \(heard.player)'s charge no longer said: ended on its puppet\(left ? ", left hand" : "")");
    }
    // A burst or a magazine emptied on automatic: the weapon's fire
    // loop ends with the last shot, as the game ends an NPC's
    // (AIWeapon.OnStopShooting).
    if hand.firing && now - hand.shotAt > 0.3 {
      hand.firing = false;
      let weapon = TandemPresenceSync.WeaponOf(body, left);
      if IsDefined(weapon) {
        WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.ExitShoot);
      }
    }
  }

  public static func WeaponOf(body: ref<GameObject>, left: Bool) -> ref<WeaponObject> {
    let slot = left ? t"AttachmentSlots.WeaponLeft" : t"AttachmentSlots.WeaponRight";
    return GameInstance.GetTransactionSystem(body.GetGame()).GetItemInSlot(body, slot) as WeaponObject;
  }

  // A shot, reload or swing was drawn on a puppet's weapon (TandemSystem.
  // DrawShots): what goes with it.
  public func Fired(player: Uint32, body: ref<GameObject>, left: Bool, effect: Int32, now: Float) -> Void {
    let heard = this.Known(player);
    if !IsDefined(TandemPresenceSync.WeaponOf(body, left)) {
      // Nothing was drawn or played (TandemWeapons.Fire): say so, once.
      this.Once(n"unarmed", s"audio: player \(player)'s shot or swing has no weapon in its puppet's hand: nothing played");
      return;
    }
    if effect == 15 || effect == 16 || effect == 17 {
      let hand = heard.Hand(left);
      hand.shotAt = now;
      hand.firing = true;
      heard.sounds += 1;
      this.Once(n"shot", s"audio: player \(player) shot (the weapon's own shoot effects and its Shoot event)");
      if left {
        this.Once(n"shotLeft", s"audio: player \(player) shot with its left hand's weapon (its own shoot effects and its Shoot event)");
      }
    } else if effect == 200 || effect == 201 {
      // The swing's effort, as the game voices V's own (MeleeAttackGenericEvents).
      this.Voice(heard, body, n"meleeAttack", "swing");
    } else {
      heard.sounds += 1;
      this.Once(n"reload", s"audio: player \(player) reload (the weapon's own reload effects, action \(effect))");
    }
  }

  // ---- what was heard, on the puppet ----

  private func Play(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, event: TandemHeard, now: Float) -> Void {
    switch event.kind {
      case 1:
        this.Step(heard, body, event.arg, now);
        break;
      case 2:
        // Taking off: the foot that pushes. A double or charged jump has
        // its own sound in the scripts (JumpEvents and siblings).
        this.Plant(heard, body, event.arg, now, true);
        if event.arg != 0 {
          this.Sound(heard, body, n"lcm_player_double_jump", "double jump");
        } else {
          this.Once(n"jump", s"audio: player \(heard.player) jump (foot planted)");
        }
        break;
      case 3:
        // Both feet, and V's own landing efforts (LandEvents, final.reds
        // 281021-281058, 558026).
        this.Plant(heard, body, 16, now, true);
        this.Plant(heard, body, 0, now, true);
        if event.arg >= 2 {
          GameObjectEffectHelper.StartEffectEvent(body, n"landing_very_hard", false);
          this.Voice(heard, body, n"veryhardLanding", "very hard landing");
        } else if event.arg == 1 {
          GameObjectEffectHelper.StartEffectEvent(body, n"landing_hard", false);
          this.Voice(heard, body, n"hardLanding", "hard landing");
        } else {
          this.Voice(heard, body, n"regularLanding", "landing");
        }
        break;
      case 4:
        // The engine tells the audio system of an item put in a slot by
        // itself (AudioNotifyItemEquippedEvent is never built in scripts).
        heard.sounds += 1;
        this.Once(n"draw", s"audio: player \(heard.player) draw (the engine's own equip notification)");
        break;
      case 5:
        this.Holster(heard, body);
        break;
      case 6:
        this.Hurt(heard, body, event.arg, now);
        break;
      case 7:
        this.Voice(heard, body, n"stlh_death", "down");
        break;
      case 9:
        this.Ability(heard, body, event.arg, true);
        break;
      case 10:
        this.Ability(heard, body, event.arg, false);
        break;
      case 11:
        this.Voice(heard, body, n"climbStart", "climb");
        break;
      case 12:
        this.Voice(heard, body, n"Vault", "vault");
        break;
      case 13:
        this.Sound(heard, body, n"lcm_dash", "dash");
        break;
      case 14:
        this.Sound(heard, body, n"lcm_fs_additional_slide_stop", "slide end");
        break;
      case 15:
      case 16:
      case 17:
      case 18:
        this.Water(heard, body, event.kind, event.arg, true);
        break;
      case 19:
        this.Stroke(heard, body, event.arg);
        break;
      default:
        break;
    }
  }

  // ---- water ----

  // The swimming state's loop effect on the body (LocomotionSwimming's
  // swimming_surface and swimming_diving, final.reds:524022, 528133): the
  // one of `state`, the other stopped.
  private func WaterLoop(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, state: Int32) -> Void {
    if heard.water == state {
      return;
    }
    if heard.water == 1 {
      GameObjectEffectHelper.StopEffectEvent(body, n"swimming_surface");
    } else if heard.water == 2 {
      GameObjectEffectHelper.StopEffectEvent(body, n"swimming_diving");
    }
    heard.water = 0;
    if this.m_swimMode != 2 {
      if state == 1 {
        GameObjectEffectHelper.StartEffectEvent(body, n"swimming_surface", false);
      } else if state == 2 {
        GameObjectEffectHelper.StartEffectEvent(body, n"swimming_diving", false);
      }
      heard.water = state;
    }
  }

  // Into the water (15), out of it (16), under (17), back up (18): what
  // V's own swimming states play (SwimmingSurfaceEvents, SwimmingTransition
  // Events, SwimmingDivingEvents), and the game's own splashes of a body in
  // water (NPCPuppet.OnRagdollBodyPartWaterImpactEvent: a torso's, a
  // limb's), which are world sounds, placed in 3D. `audible` false: the
  // loops only (a hidden body).
  private func Water(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, kind: Int32, arg: Int32, audible: Bool) -> Void {
    let effects = audible && this.m_swimMode != 2;
    let sounds = audible && this.m_swimMode != 1;
    switch kind {
      case 15:
        this.WaterLoop(heard, body, (arg & 1) != 0 ? 2 : 1);
        // Seen swimming from the start (arg 2): its water, no splash.
        if (arg & 2) == 0 {
          if effects && (arg & 1) != 0 {
            GameObjectEffectHelper.StartEffectEvent(body, n"swimming_enter_dive", false);
          }
          if sounds {
            this.Sound(heard, body, n"w_melee_water_medium_splash", "into the water");
          }
        }
        break;
      case 16:
        this.WaterLoop(heard, body, 0);
        if sounds {
          this.Sound(heard, body, n"w_bul_hit_water", "out of the water");
        }
        break;
      case 17:
        this.WaterLoop(heard, body, 2);
        if effects {
          GameObjectEffectHelper.StartEffectEvent(body, n"swimming_enter_dive", false);
        }
        if sounds {
          this.Sound(heard, body, n"w_bul_hit_water", "dive");
        }
        break;
      case 18:
        this.WaterLoop(heard, body, 1);
        if audible {
          // V's breath as it comes up (SwimmingSurfaceEvents.OnEnterFromDiving).
          let breath = new SoundPlayEvent();
          breath.soundName = n"surface_from_dive";
          body.QueueEvent(breath);
        }
        if sounds {
          this.Sound(heard, body, n"w_bul_hit_water", "back to the surface");
        }
        break;
      default:
        break;
    }
  }

  // One stroke (arg: 1 under the surface, 2 fast): the state's own stroke
  // effect; on the surface, a limb's splash too.
  private func Stroke(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, arg: Int32) -> Void {
    let diving = (arg & 1) != 0;
    let fast = (arg & 2) != 0;
    if this.m_swimMode != 2 {
      let effect = diving ? (fast ? n"swimming_diving_stroke_fast" : n"swimming_diving_stroke") : (fast ? n"swimming_surface_stroke_fast" : n"swimming_surface_stroke");
      GameObjectEffectHelper.StartEffectEvent(body, effect, false);
    }
    if this.m_swimMode != 1 && !diving {
      GameObject.PlaySoundEvent(body, n"w_bul_hit_water");
    }
    heard.strokes += 1;
    if heard.strokes == 1 || heard.strokes % 20 == 0 {
      TandemNative.Log(s"audio: player \(heard.player) swimming stroke \(heard.strokes) (\(diving ? "under the surface" : "on the surface")\(fast ? ", fast" : ""); mode \(this.m_swimMode))");
    }
  }

  // The body's own animation raised a footstep (the NPCPuppet callback
  // below): ours are held back while it does.
  public func GraphStep(player: Uint32, name: CName, now: Float) -> Void {
    let heard = this.Find(player);
    if !IsDefined(heard) {
      return;
    }
    // Ours comes back to the entity as an audio event too, right away.
    if now - heard.ourStepAt < 0.1 {
      return;
    }
    heard.graphStepAt = now;
    heard.graphSteps += 1;
    if heard.graphSteps == 1 {
      TandemNative.Log(s"audio: player \(heard.player) footstep from its own animation (\(NameToString(name))): ours held back while these come");
    }
  }

  private func Step(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, arg: Int32, now: Float) -> Void {
    if this.m_stepsMode == 2 || (this.m_stepsMode == 0 && now - heard.graphStepAt < 1.2) {
      return;
    }
    this.Plant(heard, body, arg, now, false);
    heard.steps += 1;
    if heard.steps == 1 || heard.steps % 40 == 0 {
      TandemNative.Log(s"audio: player \(heard.player) footstep \(heard.steps) (foot planted: gait \(arg & 15), left \((arg & 16) != 0), ladder \((arg & 32) != 0); from its own animation so far: \(heard.graphSteps))");
    }
  }

  // The engine's own footstep: the event its animation graph raises when a
  // clip plants a foot. The audio system picks the sound from the surface
  // under the entity, its footwear and its speed, and places it in 3D.
  private func Plant(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, arg: Int32, now: Float, always: Bool) -> Void {
    if !always && this.m_stepsMode == 2 {
      return;
    }
    let plant = new entFootPlantedEvent();
    plant.footSide = (arg & 16) != 0 ? animEventSide.Left : animEventSide.Right;
    heard.ourStepAt = now;
    body.QueueEvent(plant);
  }

  private func Sound(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, name: CName, what: String) -> Void {
    // Queued on the entity: played from it, in 3D (GameObject.PlaySoundEvent).
    GameObject.PlaySoundEvent(body, name);
    heard.sounds += 1;
    TandemNative.Log(s"audio: player \(heard.player) \(what) (sound \(NameToString(name)))");
  }

  private func Voice(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, context: CName, what: String) -> Void {
    // V's own effort and pain contexts, spoken by the puppet (its record's
    // voice tag picks the voice); true: in a vehicle too.
    GameObject.PlayVoiceOver(body, context, n"Scripts:TandemPresence", 0.0, EMPTY_ENTITY_ID(), true);
    heard.sounds += 1;
    TandemNative.Log(s"audio: player \(heard.player) \(what) (voice \(NameToString(context)))");
  }

  // The weapon put away: the game's own unequip notification names the
  // item (EquipmentSystem, final.reds:59820).
  private func Holster(heard: ref<TandemHeardPuppet>, body: ref<GameObject>) -> Void {
    let record = TweakDBInterface.GetItemRecord(heard.right);
    if IsDefined(record) {
      let notify = new AudioNotifyItemUnequippedEvent();
      notify.itemName = record.EntityName();
      body.QueueEvent(notify);
      heard.sounds += 1;
      TandemNative.Log(s"audio: player \(heard.player) holster (unequip notification for \(NameToString(notify.itemName)))");
    }
  }

  // Its health fell: V's own pain context now. The reaction is the hit's
  // own when its game tells it (HitsTaken.reds: where it landed, the way it
  // went, through the game's pipeline); what is never told (damage over
  // time, a fall, a lost message) flinches a quarter of a second later,
  // from the front.
  private func Hurt(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, how: Int32, now: Float) -> Void {
    this.Voice(heard, body, n"onPlayerHit", "hurt");
    heard.hurtDue = now + 0.25;
    heard.hurtHow = how;
  }

  // The flinch of a fall of health no hit explained: the NPC body's twitch
  // (the one hit reaction that plays without its brain and without moving
  // it) and a flinch laid over the streamed pose.
  private func Flinched(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, now: Float) -> Void {
    let how = heard.hurtHow;
    let fellAt = heard.hurtDue - 0.25;
    heard.hurtDue = 0.0;
    // A hit shown on it around the fall (told a moment before or after
    // the health, or run by our own game) was that fall.
    if IsDefined(this.m_hitsTaken) && this.m_hitsTaken.ShownSince(heard.player, fellAt - 0.6) {
      TandemNative.LogDebug(s"reaction: player \(heard.player)'s health fell with a hit shown on its puppet: no flinch of ours");
      return;
    }
    let strength = how >= 2 ? 1.0 : (how == 1 ? 0.6 : 0.25);
    TandemPresence.Flinch(heard.player, strength, new Vector4(0.0, 0.0, 0.0, 0.0), body.GetWorldForward());
    TandemReactionsSync.Twitch(body, 3, how, 4);
    heard.flinches += 1;
    TandemNative.Log(s"reaction: player \(heard.player)'s puppet flinches (strength \(strength), hit \(heard.flinches)); its place stays its player's");
  }

  // A time ability began or ended (proto::kTime*: 1 Sandevistan, 2
  // Kerenzikov): the sounds of the status effect V's game put on V, played
  // on the puppet the way the game plays any status effect's
  // (GameObject.StartStatusEffectSFX, final.reds:12274).
  private func Ability(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, bits: Int32, on: Bool) -> Void {
    let effects: array<TweakDBID>;
    if (bits & 1) != 0 {
      ArrayPush(effects, t"BaseStatusEffect.SandevistanPlayerBuff");
    }
    if (bits & 2) != 0 {
      ArrayPush(effects, t"BaseStatusEffect.KerenzikovPlayerBuff");
    }
    for effect in effects {
      let record = TweakDBInterface.GetStatusEffectRecord(effect);
      let sfx: array<wref<StatusEffectFX_Record>>;
      if IsDefined(record) {
        record.SFX(sfx);
      }
      // The same record shown as a status visual plays its sounds already
      // (StatusVisuals.reds): one of them, never both.
      let status = this.m_status;
      for fx in sfx {
        if on && !ArrayContains(heard.sfx, fx.Name()) && !(IsDefined(status) && status.PlaysSound(heard.player, fx.Name())) {
          GameObject.PlaySoundEvent(body, fx.Name());
          ArrayPush(heard.sfx, fx.Name());
        } else if !on && ArrayContains(heard.sfx, fx.Name()) {
          GameObject.StopSoundEvent(body, fx.Name());
          ArrayRemove(heard.sfx, fx.Name());
        }
      }
      heard.sounds += 1;
      TandemNative.Log(s"audio: player \(heard.player) ability \(bits) \(on ? "on" : "off") (\(ArraySize(sfx)) sounds of \(TDBID.ToStringDEBUG(effect)))");
    }
  }

  // ---- cues ----

  private func DrawCues(system: ref<TandemSystem>, now: Float) -> Void {
    for cue in TandemPresence.TakeCues() {
      let body = system.PuppetOf(cue.player);
      if IsDefined(body) && !TandemVisibility.IsHidden(this.m_game, cue.player) {
        let heard = this.Known(cue.player);
        let at = new Vector4(cue.x, cue.y, cue.z, 1.0);
        // The hand: a charge's is its `y` (its arg is its step:
        // proto::kChargeLeftHand), every other's in its kTrace* (1).
        let left = cue.kind == 4 ? cue.y > 0.5 : (cue.arg & 1) != 0;
        let hand = heard.Hand(left);
        let weapon = TandemPresenceSync.WeaponOf(body, left);
        if cue.kind == 1 {
          if IsDefined(weapon) && weapon.IsAttached() {
            let drawn = this.m_tracerMode != 2 && this.Bullet(body, weapon, at, now);
            let trail = this.m_tracerMode != 1 && TandemTracers.Trail(weapon, TandemTracers.Muzzle(weapon), at, false);
            hand.traceEnd = at;
            hand.traceAt = now;
            if trail {
              heard.trails += 1;
            }
            if (drawn || trail) && left {
              heard.leftTraces += 1;
              if heard.leftTraces == 1 || heard.leftTraces % 20 == 0 {
                TandemNative.Log(s"bullet: player \(cue.player)'s left hand's shot \(heard.leftTraces) drawn from its puppet's left weapon (\(TDBID.ToStringDEBUG(ItemID.GetTDBID(weapon.GetItemID()))))");
              }
            }
            if drawn || trail {
              heard.traces += 1;
              if heard.traces == 1 || heard.traces % 20 == 0 {
                TandemNative.Log(s"bullet: player \(cue.player)'s shot \(heard.traces) drawn from its puppet's weapon to (\(at.X) \(at.Y) \(at.Z)) (flags \(cue.arg); bullet \(drawn), trail \(trail))");
              }
            }
          }
        } else if cue.kind == 3 {
          // A ricochet: on from that hand's last shot's end, if it is that
          // shot's.
          if IsDefined(weapon) && now - hand.traceAt < 1.0 && now >= hand.traceAt {
            let trail = TandemTracers.Trail(weapon, hand.traceEnd, at, true);
            hand.traceEnd = at;
            if trail {
              heard.ricochets += 1;
              if heard.ricochets == 1 || heard.ricochets % 20 == 0 {
                TandemNative.Log(s"bullet: player \(cue.player)'s ricochet \(heard.ricochets) drawn on to (\(at.X) \(at.Y) \(at.Z)) (flags \(cue.arg))");
              } else {
                TandemNative.LogDebug(s"bullet: player \(cue.player)'s ricochet \(heard.ricochets) drawn on to (\(at.X) \(at.Y) \(at.Z)) (flags \(cue.arg))");
              }
            } else {
              this.Once(n"noRicochetTrail", s"bullet: player \(cue.player)'s ricochet not drawn: its weapon has no trail");
            }
          } else {
            this.Once(n"lateRicochet", s"bullet: player \(cue.player)'s ricochet came with no shot of theirs just before: not drawn");
          }
        } else if cue.kind == 4 {
          let was = hand.charge;
          let side = left ? ", left hand" : "";
          TandemTracers.ShowCharge(hand, weapon, cue.arg, cue.x);
          if cue.arg != was {
            if cue.arg >= 1 && cue.arg <= 3 && (was < 1 || was > 3) {
              heard.charges += 1;
              TandemNative.Log(s"charge: player \(cue.player)'s puppet charges its weapon (\(heard.charges) so far; the weapon's charge effects: \(IsDefined(weapon)))\(side)");
            } else if cue.arg == 3 {
              TandemNative.Log(s"charge: player \(cue.player)'s weapon fully charged\(side)");
            } else if cue.arg == 0 || cue.arg == 4 {
              let how = cue.arg == 4 ? "discharged" : "released";
              TandemNative.Log(s"charge: player \(cue.player)'s charge ended (\(how))\(side)");
            }
          }
          hand.chargeAt = now;
        } else if cue.kind == 2 {
          if IsDefined(weapon) {
            // The weapon's own impact effects, as its hit handler plays
            // them (WeaponObject.OnMeleeHitEvent's MeleeHit action).
            WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.MeleeHit);
            heard.sounds += 1;
            TandemNative.Log(s"audio: player \(cue.player) melee hit (the weapon's own MeleeHit effects, flags \(cue.arg))");
          }
        }
      }
    }
  }

  // Someone else's bullet, drawn here: a shot of the weapon in the body's
  // hand at the point the shooter's game says it ended, fired the way the
  // game fires an NPC's (AIWeapon.Fire's AI_ShootAt), so the tracer, the
  // impact, the decal and their sounds are the weapon's own. It costs no
  // ammunition and its hits are nobody's (Cosmetic below).
  public func Bullet(shooter: ref<GameObject>, weapon: ref<WeaponObject>, at: Vector4, now: Float) -> Bool {
    if Vector4.IsZero(at) || Vector4.Distance(at, shooter.GetWorldPosition()) < 0.5 {
      return false;
    }
    ArrayPush(this.m_cosmeticBy, shooter.GetEntityID());
    ArrayPush(this.m_cosmeticAt, now);
    let params: gameprojectileWeaponParams;
    // A native struct local is not cleared by the script VM: every field
    // the shot reads is set.
    params.targetPosition = at;
    params.smartGunSpreadOnHitPlane = new Vector3(0.0, 0.0, 0.0);
    params.charge = 0.0;
    params.smartGunAccuracy = 0.0;
    params.smartGunIsProjectileGuided = false;
    params.hitPlaneOffset = new Vector4(0.0, 0.0, 0.0, 0.0);
    params.shootingOffset = 0.0;
    params.ignoreWeaponOwnerCollision = true;
    params.range = 0.0;
    params.ignoreMountedVehicleCollision = true;
    // Never caught by the Vs standing here: who it hit is not ours to say.
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if IsDefined(system) {
      for other in system.PuppetPlayers() {
        let puppet = system.PuppetOf(other);
        if IsDefined(puppet) {
          gameprojectileWeaponParams.AddObjectToIgnoreCollisionWith(params, puppet.GetEntityID());
        }
      }
    }
    let ours = GetPlayer(this.m_game);
    if IsDefined(ours) {
      gameprojectileWeaponParams.AddObjectToIgnoreCollisionWith(params, ours.GetEntityID());
    }
    let target: WorldPosition; // assigned on every path: filled whole by WorldPosition.SetVector4 at once
    WorldPosition.SetVector4(target, at);
    AIWeapon.SetAttackBasedOnTimeDilation(shooter, weapon);
    weapon.AI_ShootAt(IPositionProvider.CreateStaticPositionProvider(target), null, shooter, Cast<Uint16>(0), params, Cast<Uint8>(1), 0.0, 0.01);
    return true;
  }

  // A hit of a bullet drawn for someone else's shot (DamageSystem.
  // ProcessPipeline, Combat.reds): the shooter's game decided what it hit.
  public func Cosmetic(hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) || ArraySize(this.m_cosmeticBy) == 0 {
      return false;
    }
    let from = hitEvent.attackData.GetInstigator();
    if !IsDefined(from) || !ArrayContains(this.m_cosmeticBy, from.GetEntityID()) {
      return false;
    }
    if !AttackData.IsRangedOrDirect(hitEvent.attackData.GetAttackType()) {
      return false;
    }
    // Not the hits tandem builds itself: a report shown on a puppet, a hit
    // another game computed, a proxy's real shot at our V (Shots.reds).
    if hitEvent.attackData.HasFlag(hitFlag.DealNoDamage) {
      return false;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if IsDefined(system) && system.IsForwardedHit(hitEvent) {
      return false;
    }
    let shots = TandemShots.Of(this.m_game);
    if IsDefined(shots) && shots.FiredFactor(from.GetEntityID()) >= 0.0 {
      return false;
    }
    this.m_dropped += 1;
    this.Once(n"cosmetic", "bullet: a drawn bullet's hit is left to the shooter's game (said once)");
    return true;
  }

  // ---- developer commands (tools/game.sh cmd presence ...) ----

  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    if Equals(command, "report") {
      this.Report(system);
    } else if Equals(command, "steps auto") {
      this.m_stepsMode = 0;
      TandemNative.Log("presence: our footsteps only where the body's own are silent");
    } else if Equals(command, "steps on") {
      this.m_stepsMode = 1;
      TandemNative.Log("presence: our footsteps always");
    } else if Equals(command, "steps off") {
      this.m_stepsMode = 2;
      TandemNative.Log("presence: our footsteps never");
    } else if Equals(command, "swim both") {
      this.m_swimMode = 0;
      TandemNative.Log("presence: swimming: the states' effects and the water sounds");
    } else if Equals(command, "swim fx") {
      this.m_swimMode = 1;
      TandemNative.Log("presence: swimming: the states' effects alone");
    } else if Equals(command, "swim sound") {
      this.m_swimMode = 2;
      TandemNative.Log("presence: swimming: the water sounds alone");
    } else if StrBeginsWith(command, "vo ") || StrBeginsWith(command, "sound ") || StrBeginsWith(command, "fx ") {
      // Try a voice context, a sound event or an effect on every puppet:
      // to find by ear what resolves on a puppet's body.
      let voice = StrBeginsWith(command, "vo ");
      let sound = StrBeginsWith(command, "sound ");
      let name = StringToName(StrAfterFirst(command, " "));
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        if IsDefined(body) {
          if voice {
            GameObject.PlayVoiceOver(body, name, n"Scripts:TandemPresence", 0.0, EMPTY_ENTITY_ID(), true);
          } else if sound {
            GameObject.PlaySoundEvent(body, name);
          } else {
            GameObjectEffectHelper.StartEffectEvent(body, name, false);
          }
          TandemNative.Log(s"presence: \(command) on player \(player)'s puppet");
        }
      }
    } else if Equals(command, "flinch") {
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        if IsDefined(body) {
          this.Hurt(this.Known(player), body, 2, this.m_last);
        }
      }
    } else if Equals(command, "tracer both") {
      this.m_tracerMode = 0;
      TandemNative.Log("presence: the others' shots: the drawn bullet and the weapon's trail");
    } else if Equals(command, "tracer bullet") {
      this.m_tracerMode = 1;
      TandemNative.Log("presence: the others' shots: the drawn bullet alone");
    } else if Equals(command, "tracer trail") {
      this.m_tracerMode = 2;
      TandemNative.Log("presence: the others' shots: the weapon's trail alone");
    } else if Equals(command, "trail") {
      // The weapon's trail from every puppet's muzzle at what we look at.
      let ours = GetPlayer(this.m_game);
      let at = GameInstance.GetTargetingSystem(this.m_game).GetLookAtPosition(ours, true, true);
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        let weapon = IsDefined(body) ? TandemPresenceSync.WeaponOf(body, false) : null;
        let drawn = IsDefined(weapon) && TandemTracers.Trail(weapon, TandemTracers.Muzzle(weapon), at, false);
        TandemNative.Log(s"presence: trail from player \(player)'s puppet to (\(at.X) \(at.Y) \(at.Z)): \(drawn)");
      }
    } else if StrBeginsWith(command, "charge on") || StrBeginsWith(command, "charge full") || StrBeginsWith(command, "charge off") {
      // Every puppet's weapon charging, fully charged, or let go (held an
      // hour: no cue ends it); "... left": its left hand's.
      let step = 0;
      if StrBeginsWith(command, "charge on") {
        step = 1;
      } else if StrBeginsWith(command, "charge full") {
        step = 3;
      }
      let left = StrContains(command, " left");
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        let heard = this.Known(player);
        let hand = heard.Hand(left);
        let weapon = IsDefined(body) ? TandemPresenceSync.WeaponOf(body, left) : null;
        TandemTracers.ShowCharge(hand, weapon, step, step == 3 ? 1.0 : 0.4);
        hand.chargeAt = this.m_last + 3600.0;
        TandemNative.Log(s"presence: \(command) on player \(player)'s puppet (a weapon: \(IsDefined(weapon)))");
      }
    } else if Equals(command, "bullet") || Equals(command, "bullet left") {
      // A drawn bullet from every puppet (its left hand's weapon: "bullet
      // left") at what we look at.
      let ours = GetPlayer(this.m_game);
      let at = GameInstance.GetTargetingSystem(this.m_game).GetLookAtPosition(ours, true, true);
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        let weapon = IsDefined(body) ? TandemPresenceSync.WeaponOf(body, Equals(command, "bullet left")) : null;
        let drawn = IsDefined(weapon) && this.Bullet(body, weapon, at, this.m_last);
        TandemNative.Log(s"presence: bullet from player \(player)'s puppet to (\(at.X) \(at.Y) \(at.Z)): \(drawn)");
      }
    } else if StrBeginsWith(command, "hud") {
      if IsDefined(this.m_hud) {
        this.m_hud.Command(StrAfterFirst(command, "hud "), system);
      }
    } else if StrBeginsWith(command, "hit") {
      if IsDefined(this.m_hitsTaken) {
        this.m_hitsTaken.Command(StrAfterFirst(command, "hit "), system);
      }
    } else if StrBeginsWith(command, "react") {
      if IsDefined(this.m_reactions) {
        this.m_reactions.Command(StrAfterFirst(command, "react "), system);
      }
    } else if StrBeginsWith(command, "status") {
      if IsDefined(this.m_status) {
        this.m_status.Command(StrAfterFirst(command, "status "));
      }
    } else {
      TandemNative.Log(s"presence: unknown command '\(command)' (report, steps auto|on|off, swim both|fx|sound, vo NAME, sound NAME, fx NAME, flinch, hit [npc], bullet [left], trail, tracer both|bullet|trail, charge on|full|off [left], hud ..., react ..., status ...)");
    }
  }

  private func Report(system: ref<TandemSystem>) -> Void {
    TandemNative.Log(s"presence: our health \(Cast<Int32>(this.m_health * 100.0)) %, menu \(this.m_menu), photo mode \(this.m_photo); steps mode \(this.m_stepsMode), swim mode \(this.m_swimMode), tracer mode \(this.m_tracerMode); drawn bullets' hits dropped \(this.m_dropped)");
    for heard in this.m_heard {
      TandemNative.Log(s"presence: player \(heard.player): health \(Cast<Int32>(TandemPresence.Health(heard.player) * 100.0)) %, flags \(TandemPresence.Flags(heard.player)); footsteps ours \(heard.steps), its animation's \(heard.graphSteps); other sounds \(heard.sounds); bullets drawn \(heard.traces) (trails \(heard.trails), ricochets \(heard.ricochets)); charges \(heard.charges); left hand's bullets \(heard.leftTraces); flinches \(heard.flinches); water \(heard.water), strokes \(heard.strokes)");
    }
    if IsDefined(this.m_hud) {
      this.m_hud.Report();
    }
    if IsDefined(this.m_reactions) {
      this.m_reactions.Report();
    }
    if IsDefined(this.m_voices) {
      this.m_voices.Report();
    }
    if IsDefined(this.m_status) {
      this.m_status.Report();
    }
    if IsDefined(this.m_hitsTaken) {
      this.m_hitsTaken.Report();
    }
  }
}

// ---- our V's side ----

// A melee swing of ours landed (the weapon queues this on its owner): the
// others play the weapon's impact on our puppet's.
@wrapMethod(PlayerPuppet)
protected cb func OnMeleeHitEvent(evt: ref<MeleeHitEvent>) -> Bool {
  if TandemNative.IsStarted() && IsDefined(evt) && !evt.hitBlocked {
    let at = IsDefined(evt.target) ? evt.target.GetWorldPosition() : this.GetWorldPosition();
    TandemPresence.SendCue(2, IsDefined(evt.target as ScriptedPuppet) ? 2 : 0, at);
  }
  return wrappedMethod(evt);
}

// ---- a puppet's own footsteps ----

// Sounds an animation raises reach the entity as audio events (the
// Minotaur's and the exoskeleton's footsteps are read this way,
// final.reds:292585, 294097). On a puppet they tell whether its own
// locomotion clip steps audibly; nothing else is done with them.
@addMethod(NPCPuppet)
protected cb func OnAudioEvent(evt: ref<AudioEvent>) -> Bool {
  if !TandemNative.IsStarted() {
    return false;
  }
  let player = TandemCombat.PuppetPlayer(this.GetGame(), this);
  if player != 0u {
    let name = NameToString(evt.eventName);
    if StrContains(name, "_fs_") || StrContains(name, "footstep") {
      let sync = TandemPresenceSync.Of(this.GetGame());
      if IsDefined(sync) {
        sync.GraphStep(player, evt.eventName, EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())));
      }
    }
  }
  return false;
}
