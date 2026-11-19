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
// TimeEffect. Two things our game alone knows are sent as a Cue: where a
// shot of ours ended (the others draw the bullet: tracer, impact, decal,
// its sound) and that a melee swing landed.

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
  // Automatic fire: the weapon's loop is ended when the shots stop.
  public let shotAt: Float;
  public let firing: Bool;
  public let firingLeft: Bool;
  // What it held last frame (the holster sound names the item put away).
  public let right: TweakDBID;
  // Ability sounds playing on it (stopped when the ability ends).
  public let sfx: array<CName>;
  public let traces: Int32;
  public let flinches: Int32;
}

// The menu and photo mode are heard through the UI's blackboards: our tick
// stands still while a menu pauses the world.
public class TandemPresenceListener {
  public let sync: wref<TandemPresenceSync>;

  protected cb func OnMenu(value: Bool) -> Bool {
    if IsDefined(this.sync) {
      this.sync.MenuChanged(value);
    }
  }

  protected cb func OnPhotoMode(value: Bool) -> Bool {
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
  private let m_hud: ref<TandemPartyHud>;
  private let m_reactions: ref<TandemReactionsSync>;

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
  }

  // The session ended: nothing of it is heard or shown any more.
  public func End() -> Void {
    for heard in this.m_heard {
      TandemPresence.Forget(heard.player);
    }
    ArrayClear(this.m_heard);
    ArrayClear(this.m_cosmeticBy);
    ArrayClear(this.m_cosmeticAt);
    if IsDefined(this.m_hud) {
      this.m_hud.Clear();
    }
  }

  public func Hud() -> ref<TandemPartyHud> {
    return this.m_hud;
  }

  public func Reactions() -> ref<TandemReactionsSync> {
    return this.m_reactions;
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
  // Smart bullets bend and launchers lob: their flight is not a line.
  public static func ShotFired(player: ref<GameObject>, weapon: ref<WeaponObject>, left: Bool) -> Void {
    if !IsDefined(player) || !IsDefined(weapon) {
      return;
    }
    let record = weapon.GetWeaponRecord();
    if IsDefined(record) && IsDefined(record.Evolution()) && Equals(record.Evolution().Type(), gamedataWeaponEvolution.Smart) {
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
    for player in system.PuppetPlayers() {
      let body = system.PuppetOf(player);
      if IsDefined(body) {
        let heard = this.Known(player);
        // A body out of sight (hidden, in another world) makes no sound.
        let silent = TandemVisibility.IsHidden(this.m_game, player);
        for event in TandemPresence.Observe(player, body.GetWorldPosition(), dt) {
          if !silent && dt > 0.0 {
            this.Play(heard, body, event, now);
          }
        }
        heard.right = TandemNative.RemoteHeld(player, false);
        // A burst or a magazine emptied on automatic: the weapon's fire
        // loop ends with the last shot, as the game ends an NPC's
        // (AIWeapon.OnStopShooting).
        if heard.firing && now - heard.shotAt > 0.3 {
          heard.firing = false;
          let weapon = TandemPresenceSync.WeaponOf(body, heard.firingLeft);
          if IsDefined(weapon) {
            WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.ExitShoot);
          }
        }
      }
    }
    this.DrawCues(system, now);
    while ArraySize(this.m_cosmeticAt) > 0 && now - this.m_cosmeticAt[0] > 2.0 {
      ArrayErase(this.m_cosmeticBy, 0);
      ArrayErase(this.m_cosmeticAt, 0);
    }
    if IsDefined(this.m_reactions) {
      this.m_reactions.Tick(now, system);
    }
    if IsDefined(this.m_hud) {
      this.m_hud.Tick(now, system, you);
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
      heard.shotAt = now;
      heard.firing = true;
      heard.firingLeft = left;
      heard.sounds += 1;
      this.Once(n"shot", s"audio: player \(player) shot (the weapon's own shoot effects and its Shoot event)");
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
        this.Hurt(heard, body, event.arg);
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
      default:
        break;
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

  // Hit: V's own pain context, the NPC body's twitch (the one hit reaction
  // that plays without its brain and without moving it) and a flinch laid
  // over the streamed pose.
  private func Hurt(heard: ref<TandemHeardPuppet>, body: ref<GameObject>, how: Int32) -> Void {
    this.Voice(heard, body, n"onPlayerHit", "hurt");
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
      for fx in sfx {
        if on {
          GameObject.PlaySoundEvent(body, fx.Name());
          ArrayPush(heard.sfx, fx.Name());
        } else {
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
        let weapon = TandemPresenceSync.WeaponOf(body, (cue.arg & 1) != 0);
        if cue.kind == 1 {
          if IsDefined(weapon) && weapon.IsAttached() && this.Bullet(body, weapon, at, now) {
            heard.traces += 1;
            if heard.traces == 1 || heard.traces % 20 == 0 {
              TandemNative.Log(s"bullet: player \(cue.player)'s shot \(heard.traces) drawn from its puppet's weapon to (\(at.X) \(at.Y) \(at.Z)) (flags \(cue.arg))");
            }
          }
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
          this.Hurt(this.Known(player), body, 2);
        }
      }
    } else if Equals(command, "bullet") {
      // A drawn bullet from every puppet at what we look at.
      let ours = GetPlayer(this.m_game);
      let at = GameInstance.GetTargetingSystem(this.m_game).GetLookAtPosition(ours, true, true);
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        let weapon = IsDefined(body) ? TandemPresenceSync.WeaponOf(body, false) : null;
        let drawn = IsDefined(weapon) && this.Bullet(body, weapon, at, this.m_last);
        TandemNative.Log(s"presence: bullet from player \(player)'s puppet to (\(at.X) \(at.Y) \(at.Z)): \(drawn)");
      }
    } else if StrBeginsWith(command, "hud") {
      if IsDefined(this.m_hud) {
        this.m_hud.Command(StrAfterFirst(command, "hud "), system);
      }
    } else if StrBeginsWith(command, "react") {
      if IsDefined(this.m_reactions) {
        this.m_reactions.Command(StrAfterFirst(command, "react "), system);
      }
    } else {
      TandemNative.Log(s"presence: unknown command '\(command)' (report, steps auto|on|off, vo NAME, sound NAME, fx NAME, flinch, bullet, hud ..., react ...)");
    }
  }

  private func Report(system: ref<TandemSystem>) -> Void {
    TandemNative.Log(s"presence: our health \(Cast<Int32>(this.m_health * 100.0)) %, menu \(this.m_menu), photo mode \(this.m_photo); steps mode \(this.m_stepsMode); drawn bullets' hits dropped \(this.m_dropped)");
    for heard in this.m_heard {
      TandemNative.Log(s"presence: player \(heard.player): health \(Cast<Int32>(TandemPresence.Health(heard.player) * 100.0)) %, flags \(TandemPresence.Flags(heard.player)); footsteps ours \(heard.steps), its animation's \(heard.graphSteps); other sounds \(heard.sounds); bullets drawn \(heard.traces); flinches \(heard.flinches)");
    }
    if IsDefined(this.m_hud) {
      this.m_hud.Report();
    }
    if IsDefined(this.m_reactions) {
      this.m_reactions.Report();
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
