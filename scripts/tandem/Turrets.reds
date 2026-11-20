// A device's fire between players (docs/architecture/devices.md, "A
// device's fire"; engine facts: docs/research/engine/devices.md §13.90).
//
// A security turret is the host's, as every device is: what it turns to
// and shoots at is the host's turret's doing.
// - Host: each shot of a placed turret (AIWeapon.Fire) and each target it
//   turns to are told to the guests linked with us; while it follows a
//   target its aim is said again every second, then that it follows nobody.
// - Guest, linked with the host: our copy of the turret never fires on its
//   own (SecurityTurret.ShootStart). It turns where the host's turret turns
//   and draws each of its shots from its own weapon: at another V or an
//   NPC a drawn bullet that hurts nobody (Presence.reds), at our own V a
//   real shot of our copy, which our game decides as it always did.
// - Sensing stays each game's own (the game's sensing knows the local
//   player only): when our copy locks our V we say so, and the host's
//   turret takes our puppet as a target. A host that says nothing of that
//   turret for 1.5 s (it is not in its world) leaves it to our game.
// - Not ours: a turret the player controls, the police's (police.md), a
//   spawned one (no id every game knows).
// - The hooks run on whatever thread the engine calls them: they only read,
//   write the turret's own fields and queue (StreamingNatives.reds).
//
// tools/game.sh cmd turret (counts) | turret aim (the nearest turret's copy
// turns to what V looks at for 3 s, as on the host's word) | turret shot
// (...and draws a shot there) | turret engage (host: the nearest turret
// takes the first puppet as a target, as when its guest's copy locked it)

// A turret we speak of (host), or whose copy has our V (guest).
public class TandemTurret {
  public let entity: EntityID;
  // Host: when its aim was last said. Guest: when we said it has our V.
  public let saidAt: Float;
  // Guest: when the host last spoke of it (0: never), when it last shot.
  public let heardAt: Float;
  public let shotAt: Float;
  public let shooting: Bool;
  // Guest: our copy locked our V, and we said so.
  public let seen: Bool;
  // Guest: the host is silent about it: our copy fires on its own.
  public let own: Bool;
}

public class TandemTurrets extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_host: Bool;
  private let m_turrets: array<ref<TandemTurret>>;
  private let m_lastPoll: Float;
  private let m_shotsSaid: Int32;
  private let m_shotsDrawn: Int32;
  private let m_shotsReal: Int32;
  private let m_engaged: Int32;
  private let m_left: Int32;

  public static func Get(game: GameInstance) -> ref<TandemTurrets> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemTurrets") as TandemTurrets;
  }

  // A turret the session speaks of: placed (the same id in every game), on
  // its own (not in the player's hands), not the police's.
  public static func Follows(turret: ref<SecurityTurret>) -> Bool {
    if !IsDefined(turret) || !EntityID.IsStatic(turret.GetEntityID()) {
      return false;
    }
    let ps = turret.GetDevicePS();
    return IsDefined(ps) && !ps.IsControlledByPlayer() && !ps.IsPartOfPrevention();
  }

  // Guest, linked with the host: this turret's fire is the host's to decide.
  public static func HostFires(turret: ref<SecurityTurret>) -> Bool {
    return TandemNative.IsStarted() && !TandemNative.IsHost() && TandemRegions.IsLinked(1u) && TandemTurrets.Follows(turret);
  }

  private func Find(entity: EntityID) -> ref<TandemTurret> {
    for turret in this.m_turrets {
      if turret.entity == entity {
        return turret;
      }
    }
    return null;
  }

  private func Track(entity: EntityID) -> ref<TandemTurret> {
    let turret = this.Find(entity);
    if !IsDefined(turret) {
      turret = new TandemTurret();
      turret.entity = entity;
      ArrayPush(this.m_turrets, turret);
    }
    return turret;
  }

  private func Body(entity: EntityID) -> ref<SecurityTurret> {
    return GameInstance.FindEntityByID(this.GetGameInstance(), entity) as SecurityTurret;
  }

  // Every tick, session or not (TandemObjectSync.Update): the hooks' queues
  // are always taken. you: 0 out of a session.
  public func Update(you: Uint32, now: Float) -> Void {
    let shots = TandemStreaming.TakePoints(TandemPointKind.DeviceShot());
    let aims = TandemStreaming.TakePoints(TandemPointKind.DeviceAim());
    if this.m_you != you {
      this.Release();
      this.m_you = you;
      this.m_host = you != 0u && TandemNative.IsHost();
    }
    if you == 0u {
      return;
    }
    if this.m_host {
      for aim in aims {
        if aim.value == 1 {
          this.SayAim(aim.entity, now);
        }
      }
      for shot in shots {
        this.SayShot(shot);
      }
    } else {
      for aim in aims {
        if aim.value == 3 {
          this.Locked(aim.entity, aim.other, now);
        }
      }
    }
    if now - this.m_lastPoll >= 0.5 || now < this.m_lastPoll {
      this.m_lastPoll = now;
      if this.m_host {
        this.PollHost(now);
      } else {
        this.PollGuest(now);
      }
    }
  }

  // The session is over, or our role changed: every copy is its game's own.
  private func Release() -> Void {
    if !this.m_host {
      for known in this.m_turrets {
        let turret = this.Body(known.entity);
        if IsDefined(turret) {
          turret.TandemHeld(0.0);
          if known.shooting {
            turret.TandemShooting(false);
          }
          turret.TandemOwnFire(false);
          // One we held while it had our V fires now, as alone.
          if known.seen && !known.own {
            this.Resume(turret);
          }
        }
      }
    }
    ArrayClear(this.m_turrets);
  }

  // Our copy's own fire starts (its ShootStart, let through).
  private func Resume(turret: ref<SecurityTurret>) -> Void {
    TandemStreaming.SetPassing(turret.GetEntityID());
    turret.TandemResume();
    TandemStreaming.SetPassing(EntityID.FromHash(0ul));
  }

  // What an entity is to the session: 1 a shared NPC, 3 a player, 2 a
  // placed entity, 0 nothing every game knows (its place will do).
  private func TargetKind(target: EntityID, out id: Uint32) -> Int32 {
    id = 0u;
    if !EntityID.IsDefined(target) {
      return 0;
    }
    let game = this.GetGameInstance();
    let ours = GetPlayer(game);
    if IsDefined(ours) && ours.GetEntityID() == target {
      id = this.m_you;
      return 3;
    }
    let system = TandemSystem.Of(game);
    if IsDefined(system) {
      id = system.PlayerOfPuppet(target);
      if id != 0u {
        return 3;
      }
      id = system.SharedNpcOf(target);
      if id != 0u {
        return 1;
      }
    }
    return EntityID.IsStatic(target) ? 2 : 0;
  }

  private func Say(entity: EntityID, kind: Int32, target: EntityID, at: Vector4) -> Void {
    let id = 0u;
    let what = this.TargetKind(target, id);
    TandemObjects.Fire(entity, kind, what == 1 ? id : 0u, what == 3 ? id : 0u, what == 2 ? target : EntityID.FromHash(0ul), at);
  }

  // ---- host ----

  // Our turret turned to a target: said now, and again while it lasts.
  private func SayAim(entity: EntityID, now: Float) -> Void {
    let turret = this.Body(entity);
    let target = IsDefined(turret) ? turret.GetCurrentlyFollowedTarget() : null;
    if !IsDefined(target) {
      return;
    }
    let known = this.Track(entity);
    known.saidAt = now;
    this.Say(entity, 1, target.GetEntityID(), target.GetWorldPosition());
    TandemNative.LogDebug(s"turrets: ours \(TandemNative.EntityHex(entity)) turns to \(TandemNative.EntityHex(target.GetEntityID())): said");
  }

  private func SayShot(shot: TandemStreamedPoint) -> Void {
    this.Track(shot.entity);
    this.Say(shot.entity, 2, shot.other, new Vector4(shot.x, shot.y, shot.z, 1.0));
    this.m_shotsSaid += 1;
    if this.m_shotsSaid == 1 || this.m_shotsSaid % 50 == 0 {
      TandemNative.Log(s"turrets: our turret \(TandemNative.EntityHex(shot.entity)) fires at \(TandemNative.EntityHex(shot.other)): told to the guests (\(this.m_shotsSaid) shot(s) so far)");
    }
  }

  private func PollHost(now: Float) -> Void {
    let i = ArraySize(this.m_turrets) - 1;
    while i >= 0 {
      let known = this.m_turrets[i];
      let turret = this.Body(known.entity);
      let target = IsDefined(turret) ? turret.GetCurrentlyFollowedTarget() : null;
      if !IsDefined(target) {
        // It follows nobody (or left our world): said once.
        TandemObjects.Fire(known.entity, 0, 0u, 0u, EntityID.FromHash(0ul), new Vector4(0.0, 0.0, 0.0, 1.0));
        ArrayErase(this.m_turrets, i);
      } else if now - known.saidAt >= 1.0 {
        known.saidAt = now;
        this.Say(known.entity, 1, target.GetEntityID(), target.GetWorldPosition());
      }
      i -= 1;
    }
  }

  // A guest's copy of a turret locked that guest's V (seen), or lost it:
  // our turret takes its puppet as a target, or lets it go.
  private func OnSeen(entity: EntityID, player: Uint32, seen: Bool) -> Void {
    let system = TandemSystem.Of(this.GetGameInstance());
    let puppet = IsDefined(system) ? system.PuppetOf(player) : null;
    let turret = this.Body(entity);
    if !IsDefined(turret) || !IsDefined(puppet) || !TandemTurrets.Follows(turret) {
      // Not in our world: the guest's game runs its own copy (it hears
      // nothing from us about it).
      TandemNative.Log(s"turrets: player \(player)'s copy of \(TandemNative.EntityHex(entity)) \(seen ? "has" : "lost") its V; no such turret or puppet here");
      return;
    }
    if seen {
      let taken = turret.TandemEngage(puppet);
      this.m_engaged += taken ? 1 : 0;
      TandemNative.Log(s"turrets: player \(player)'s copy of \(TandemNative.EntityHex(entity)) has its V: our turret takes its puppet (\(taken))");
    } else {
      turret.TandemDisengage(puppet);
      TandemNative.Log(s"turrets: player \(player)'s copy of \(TandemNative.EntityHex(entity)) lost its V: our turret lets its puppet go");
    }
  }

  // ---- guest ----

  // Our copy locked a target and would fire (ShootStart, held): our own V
  // is said to the host.
  private func Locked(entity: EntityID, target: EntityID, now: Float) -> Void {
    let ours = GetPlayer(this.GetGameInstance());
    if !IsDefined(ours) || ours.GetEntityID() != target {
      return;
    }
    let known = this.Track(entity);
    if known.seen {
      return;
    }
    known.seen = true;
    known.saidAt = now;
    TandemObjects.Fire(entity, 3, 0u, 0u, EntityID.FromHash(0ul), ours.GetWorldPosition());
    TandemNative.Log(s"turrets: our copy of \(TandemNative.EntityHex(entity)) has our V: said to the host");
  }

  private func PollGuest(now: Float) -> Void {
    let ours = GetPlayer(this.GetGameInstance());
    let i = ArraySize(this.m_turrets) - 1;
    while i >= 0 {
      let known = this.m_turrets[i];
      let turret = this.Body(known.entity);
      // The burst is over: the barrels stop.
      if known.shooting && now - known.shotAt > 0.6 {
        known.shooting = false;
        if IsDefined(turret) {
          turret.TandemShooting(false);
        }
      }
      let has = IsDefined(turret) && IsDefined(ours) && turret.GetCurrentlyFollowedTarget() == ours && TandemTurrets.HostFires(turret);
      if known.seen && !has {
        known.seen = false;
        TandemObjects.Fire(known.entity, 4, 0u, 0u, EntityID.FromHash(0ul), new Vector4(0.0, 0.0, 0.0, 1.0));
        TandemNative.Log(s"turrets: our copy of \(TandemNative.EntityHex(known.entity)) lost our V: said to the host");
        if known.own && IsDefined(turret) {
          turret.TandemOwnFire(false);
        }
        known.own = false;
      } else if known.seen && !known.own && now - known.heardAt > 1.5 && now - known.saidAt > 1.5 {
        // The host says nothing of it (not in its world): ours to fire.
        known.own = true;
        this.m_left += 1;
        turret.TandemOwnFire(true);
        this.Resume(turret);
        TandemNative.Log(s"turrets: the host is silent about \(TandemNative.EntityHex(known.entity)): our copy fires on its own");
      }
      if !known.seen && !known.shooting && now - known.heardAt > 5.0 {
        ArrayErase(this.m_turrets, i);
      }
      i -= 1;
    }
  }

  // What an event's target is here: our V, a puppet, a proxy, a placed entity.
  private func TargetOf(event: TandemObjectEvent) -> ref<GameObject> {
    let game = this.GetGameInstance();
    let system = TandemSystem.Of(game);
    if event.targetKind == 3 {
      if event.target == this.m_you {
        return GetPlayer(game);
      }
      return IsDefined(system) ? system.PuppetOf(event.target) : null;
    }
    if event.targetKind == 1 {
      let npcs = IsDefined(system) ? system.NpcsSync() : null;
      return IsDefined(npcs) ? GameInstance.FindEntityByID(game, npcs.EntityOfNpc(event.target)) as GameObject : null;
    }
    if event.targetKind == 2 {
      return GameInstance.FindEntityByID(game, event.targetEntity) as GameObject;
    }
    return null;
  }

  // The host's turret's doing, on our copy.
  private func OnHostFire(event: TandemObjectEvent, now: Float) -> Void {
    let turret = this.Body(event.entity);
    if !IsDefined(turret) {
      return; // not in our world
    }
    let known = this.Track(event.entity);
    known.heardAt = now;
    if known.own {
      // The host speaks of it after all: its fire is the host's again.
      known.own = false;
      turret.TandemOwnFire(false);
      turret.TandemStop();
    }
    if event.value == 0 {
      turret.TandemHeld(0.0);
      return;
    }
    let target = this.TargetOf(event);
    let at = new Vector4(event.x, event.y, event.z, 1.0);
    this.Aim(turret, target, at);
    if event.value == 2 {
      this.Shoot(known, turret, target, at, now);
    }
  }

  private func Aim(turret: ref<SecurityTurret>, target: ref<GameObject>, at: Vector4) -> Void {
    turret.TandemAim(target, at, 1.5);
  }

  // One shot of the host's turret, from our copy's weapon. At our own V it
  // is our copy's real shot (the game's own, with its aim and miss rules);
  // at anyone else a drawn bullet whose hits are nobody's.
  private func Shoot(known: ref<TandemTurret>, turret: ref<SecurityTurret>, target: ref<GameObject>, at: Vector4, now: Float) -> Void {
    let game = this.GetGameInstance();
    let weapon = GameInstance.GetTransactionSystem(game).GetItemInSlot(turret, t"AttachmentSlots.WeaponRight") as WeaponObject;
    if !IsDefined(weapon) || !weapon.IsAttached() {
      return;
    }
    known.shotAt = now;
    if !known.shooting {
      known.shooting = true;
      turret.TandemShooting(true);
    }
    let ours = GetPlayer(game);
    if IsDefined(target) && target == ours {
      TandemStreaming.SetPassing(turret.GetEntityID());
      turret.TandemFireAt(weapon, ours);
      TandemStreaming.SetPassing(EntityID.FromHash(0ul));
      this.m_shotsReal += 1;
      if this.m_shotsReal == 1 || this.m_shotsReal % 50 == 0 {
        TandemNative.Log(s"turrets: the host's turret \(TandemNative.EntityHex(known.entity)) fires at our V: our copy's real shot (\(this.m_shotsReal) so far)");
      }
    } else {
      let end = at;
      if IsDefined(target) {
        end = target.GetWorldPosition();
      }
      end.Z += 1.2;
      let presence = TandemPresenceSync.Of(game);
      if IsDefined(presence) && presence.Bullet(turret, weapon, end, EngineTime.ToFloat(GameInstance.GetEngineTime(game))) {
        WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot);
        this.m_shotsDrawn += 1;
        if this.m_shotsDrawn == 1 || this.m_shotsDrawn % 50 == 0 {
          TandemNative.Log(s"turrets: the host's turret \(TandemNative.EntityHex(known.entity)) fires at (\(end.X) \(end.Y) \(end.Z)): drawn from our copy (\(this.m_shotsDrawn) so far)");
        }
      }
    }
    turret.TandemShotLook();
  }

  // A device's fire from the others (TandemObjectSync, kind 2 events).
  public func OnEvent(event: TandemObjectEvent, now: Float) -> Void {
    if this.m_you == 0u {
      return;
    }
    if this.m_host {
      if event.value == 3 || event.value == 4 {
        this.OnSeen(event.entity, event.player, event.value == 3);
      }
    } else if event.value <= 2 {
      this.OnHostFire(event, now);
    }
  }

  // ---- commands (tools/game.sh cmd turret ...) ----

  private func Nearest() -> ref<SecurityTurret> {
    let devices = TandemDevices.Get(this.GetGameInstance());
    return IsDefined(devices) ? devices.NearestOf("turret", 60.0) as SecurityTurret : null;
  }

  public func Command(command: String, now: Float) -> Void {
    let game = this.GetGameInstance();
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "") {
      let role = this.m_host ? "host" : "guest";
      TandemNative.Log(s"turrets: \(ArraySize(this.m_turrets)) spoken of; shots told \(this.m_shotsSaid), drawn \(this.m_shotsDrawn), real at our V \(this.m_shotsReal); puppets taken as targets \(this.m_engaged); left to our game \(this.m_left) (\(role))");
      return;
    }
    let turret = this.Nearest();
    let ours = GetPlayer(game);
    if !IsDefined(turret) || !IsDefined(ours) {
      TandemNative.Log("turrets: no turret within 60 m");
      return;
    }
    let hex = TandemNative.EntityHex(turret.GetEntityID());
    let at = GameInstance.GetTargetingSystem(game).GetLookAtPosition(ours, true, true);
    if Equals(verb, "aim") || Equals(verb, "shot") {
      if Vector4.IsZero(at) {
        TandemNative.Log("turrets: V looks at nothing");
        return;
      }
      at.Z -= 1.0; // the look-at adds a metre to a place
      turret.TandemAim(null, at, 3.0);
      TandemNative.Log(s"turrets: \(hex) turns to (\(at.X) \(at.Y) \(at.Z)) for 3 s, as on the host's word");
      if Equals(verb, "shot") {
        this.Shoot(this.Track(turret.GetEntityID()), turret, null, at, now);
        TandemNative.Log(s"turrets: \(hex) draws a shot there (drawn \(this.m_shotsDrawn))");
      }
    } else if Equals(verb, "engage") {
      let system = TandemSystem.Of(game);
      let players = IsDefined(system) ? system.PuppetPlayers() : [];
      let puppet = ArraySize(players) > 0 ? system.PuppetOf(players[0]) : null;
      if !IsDefined(puppet) {
        TandemNative.Log("turrets: no puppet here");
        return;
      }
      TandemNative.Log(s"turrets: \(hex) takes player \(players[0])'s puppet as a target: \(turret.TandemEngage(puppet))");
    } else {
      TandemNative.Log(s"turrets: unknown command: \(command)");
    }
  }
}

// ---- the turret's side ----

// Guest: the host is silent about this turret, our copy fires on its own.
@addField(SecurityTurret)
public let m_tandemOwnFire: Bool;

// Until then (engine time) the host's aim holds: our copy's own look-ats wait.
@addField(SensorDevice)
public let m_tandemAimUntil: Float;

@addMethod(SecurityTurret)
public func TandemOwnFire(own: Bool) -> Void {
  this.m_tandemOwnFire = own;
}

// Our copy's own fire, started (the wrap below lets it through when it is
// ours to fire) and stopped.
@addMethod(SecurityTurret)
public func TandemResume() -> Void {
  this.ShootStart();
}

@addMethod(SecurityTurret)
public func TandemStop() -> Void {
  this.ShootStop();
}

// The barrels spin while the host's turret fires.
@addMethod(SecurityTurret)
public func TandemShooting(on: Bool) -> Void {
  this.m_animFeature.Shoot = on;
  this.ApplyAnimFeatureToReplicate(this, n"SecurityTurretData", this.m_animFeature);
}

// What the turret itself shows of a shot (ShootAttachedWeapon's own).
@addMethod(SecurityTurret)
public func TandemShotLook() -> Void {
  AnimationControllerComponent.PushEvent(this, n"Shoot");
  GameObjectEffectHelper.StartEffectEvent(this, StringToName(this.GetDevicePS().GetVfxNameOnShoot()));
}

// Our copy's real shot at our own V, as the turret fires one
// (ShootAttachedWeapon): the game aims it and decides the hit.
@addMethod(SecurityTurret)
public func TandemFireAt(weapon: ref<WeaponObject>, target: ref<GameObject>) -> Void {
  let record = TweakDBInterface.GetItemRecord(ItemID.GetTDBID(weapon.GetItemID())) as WeaponItem_Record;
  if !IsDefined(record) || !IsDefined(record.PrimaryTriggerMode()) {
    return;
  }
  let simTime = EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGame()));
  // Chest high: where the shot goes when our copy's senses lost the target.
  let at = target.GetWorldPosition();
  at.Z += 1.2;
  AIWeapon.Fire(this, weapon, simTime, 1.0, record.PrimaryTriggerMode().Type(), at, target);
}

// The host's aim, on our copy: at a body here, else at a place (the
// look-at adds a metre to a place: a ground position is given). It holds
// for `seconds`, then our copy's own logic has the turret again.
@addMethod(SensorDevice)
public func TandemAim(target: ref<GameObject>, at: Vector4, seconds: Float) -> Void {
  this.m_tandemAimUntil = 0.0;
  if IsDefined(target) {
    this.CreateLookAt(new Vector4(0.0, 0.0, 0.0, 0.0), target);
  } else {
    this.CreateLookAt(at);
  }
  this.m_animFeatureData.state = 0;
  this.ApplyAnimFeatureToReplicate(this, this.m_animFeatureDataName, this.m_animFeatureData);
  this.TandemHeld(seconds);
}

@addMethod(SensorDevice)
public func TandemHeld(seconds: Float) -> Void {
  this.m_tandemAimUntil = seconds > 0.0 ? EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) + seconds : 0.0;
}

// Host: a guest's copy locked that guest's V; ours takes its puppet as a
// target the way a quest forces one (hostile to it, recognized whatever
// the security system thinks of it). False: ours is off, turned, or in a
// player's hands.
@addMethod(SensorDevice)
public func TandemEngage(target: ref<GameObject>) -> Bool {
  let ps = this.GetDevicePS();
  if !IsDefined(target) || !IsDefined(ps) || !ps.IsON() || ps.IsAttitudeChanged() || ps.IsControlledByPlayer() {
    return false;
  }
  let mine = this.GetAttitudeAgent();
  let theirs = target.GetAttitudeAgent();
  if IsDefined(mine) && IsDefined(theirs) {
    mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Hostile);
  }
  this.RecognizeTarget(target, true);
  return true;
}

@addMethod(SensorDevice)
public func TandemDisengage(target: ref<GameObject>) -> Void {
  if IsDefined(target) {
    this.LoseTarget(target, true);
  }
}

// Our copy's own look-ats wait while the host's aim holds (its own fields
// only: safe on any thread).
@wrapMethod(SensorDevice)
private final func CreateLookAt(opt position: Vector4, opt otherTarget: ref<GameObject>) -> Void {
  if this.m_tandemAimUntil > 0.0 && EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) < this.m_tandemAimUntil {
    return;
  }
  wrappedMethod(position, otherTarget);
}

// Guest: our copy would start firing. The host's turret fires, not ours:
// held, and what it locked is queued (our own V: said to the host).
@wrapMethod(SecurityTurret)
private final func ShootStart() -> Void {
  if !this.m_tandemOwnFire && !TandemStreaming.IsPassing(this.GetEntityID()) && TandemTurrets.HostFires(this) {
    let target = this.GetCurrentlyFollowedTarget();
    if IsDefined(target) {
      TandemStreaming.QueuePoint(TandemPointKind.DeviceAim(), this.GetEntityID(), target.GetEntityID(), n"None", target.GetWorldPosition(), 3);
    }
    return;
  }
  wrappedMethod();
}

// Host: our turret turned to a target (queued: said on the tick).
@wrapMethod(SecurityTurret)
public func OnCurrentTargetAppears(target: wref<GameObject>) -> Void {
  wrappedMethod(target);
  if TandemNative.IsStarted() && TandemNative.IsHost() && IsDefined(target) && TandemTurrets.Follows(this) {
    TandemStreaming.QueuePoint(TandemPointKind.DeviceAim(), this.GetEntityID(), target.GetEntityID(), n"None", target.GetWorldPosition(), 1);
  }
}
