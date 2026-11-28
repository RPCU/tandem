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
// - A turret in a player's hands (the takeover quickhack, a device's
//   control): that player's game says where it points and whether it fires
//   (TurretHands, 5 a second); every other copy turns there, holds its own
//   fire and draws the controller's, which hurts nobody (the controller's
//   game decides its hits). Silent for a second, or let go: its game's again.
//   One player's hands at a time, first come (the host decides): a claim on
//   a turret another player holds is refused, and our V lets go of it at
//   once (the game's own TakeOverControlSystem.ReleaseControl) with a
//   notice saying who has it.
// - Guest: a copy's laser and scan light show the host's turret (red and
//   the laser while it follows a target, yellow when it follows nobody),
//   held against the copy's own changes while the host's word lasts.
// - The hooks run on whatever thread the engine calls them: they only read,
//   write the turret's own fields and queue (StreamingNatives.reds).
//
// tools/game.sh cmd turret (counts) | turret aim (the nearest turret's copy
// turns to what V looks at for 3 s, as on the host's word) | turret shot
// (...and draws a shot there) | turret engage (host: the nearest turret
// takes the first puppet as a target, as when its guest's copy locked it)
// | turret take (our V takes control of the nearest turret, as its control
// panel gives it) | turret provoke (the nearest turret's copy takes our own
// V as a target). All but take: the nearest turret we follow ("sentry",
// Devices.reds) when one is within 60 m, else the nearest.

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
  // A turret in another player's hands: whose, where it points, when we
  // heard of it, whether it fires, our copy's last drawn shot.
  public let hands: Uint32;
  public let handsAt: Vector4;
  public let handsHeard: Float;
  public let handsFiring: Bool;
  public let handsShotAt: Float;
}

public class TandemTurrets extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_host: Bool;
  private let m_turrets: array<ref<TandemTurret>>;
  // Turrets in another player's hands, and the one in ours (said).
  private let m_hands: array<ref<TandemTurret>>;
  private let m_held: EntityID;
  private let m_heldSaidAt: Float;
  // A turret another player holds, our claim on it refused: when, when our
  // V was last let go of it, how many refusals.
  private let m_refused: EntityID;
  private let m_refusedAt: Float;
  private let m_releasedAt: Float;
  private let m_refusals: Int32;
  private let m_handsDrawn: Int32;
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
    this.PollOurHands(now);
    this.UpdateHands(now);
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
    for known in this.m_hands {
      this.LetGo(known);
    }
    ArrayClear(this.m_hands);
    this.m_held = EntityID.FromHash(0ul);
    this.m_refused = EntityID.FromHash(0ul);
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
    let target = IsDefined(turret) && !turret.TandemInHands() ? turret.GetCurrentlyFollowedTarget() : null;
    if !IsDefined(target) {
      return;
    }
    let known = this.Track(entity);
    known.saidAt = now;
    this.Say(entity, 1, target.GetEntityID(), target.GetWorldPosition());
    TandemNative.LogDebug(s"turrets: ours \(TandemNative.EntityHex(entity)) turns to \(TandemNative.EntityHex(target.GetEntityID())): said");
  }

  private func SayShot(shot: TandemStreamedPoint) -> Void {
    let turret = this.Body(shot.entity);
    if IsDefined(turret) && turret.TandemInHands() {
      return; // a stray of a copy in another player's hands
    }
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
      let why = !IsDefined(turret) ? "no such turret in our world" : (!IsDefined(puppet) ? "no puppet of that player yet" : (turret.GetDevicePS().IsPartOfPrevention() ? "a turret we do not follow (the police's)" : "a turret we do not follow (in a player's hands, or spawned)"));
      TandemNative.Log(s"turrets: player \(player)'s copy of \(TandemNative.EntityHex(entity)) \(seen ? "has" : "lost") its V; \(why)");
      return;
    }
    if seen {
      let taken = turret.TandemTake(puppet);
      this.m_engaged += taken ? 1 : 0;
      TandemNative.Log(s"turrets: player \(player)'s copy of \(TandemNative.EntityHex(entity)) has its V: our turret takes its puppet (\(taken)); \(turret.TandemSenses())");
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
    // Turned (any player's doing) or in another player's hands: it has no V
    // to say (Devices.reds turns our copy for every V on the tick).
    let turret = this.Body(entity);
    if IsDefined(turret) && (turret.GetDevicePS().IsAttitudeChanged() || turret.TandemInHands()) {
      TandemNative.LogDebug(s"turrets: our copy of \(TandemNative.EntityHex(entity)) locked our V while turned: not said");
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
    if turret.GetDevicePS().IsControlledByPlayer() || turret.TandemInHands() {
      return; // in our V's hands, or another player's: theirs to show
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
      turret.TandemHostLook(false);
      return;
    }
    if event.value == 1 || !turret.TandemLookHeld() {
      turret.TandemHostLook(true);
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
    if event.kind == 3 {
      this.OnHands(event, now);
      return;
    }
    if event.kind == 4 {
      this.Refused(event, now);
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

  // ---- a turret in a player's hands ----

  // The turret our V controls (the takeover system's), when the session
  // speaks of it: placed. The police's too, as OnHands takes another
  // player's: in a player's hands it is that player's (arasaka-park's were
  // never claimed, so never refused, and two players held one, 2026-11-21).
  private func OurDevice() -> ref<GameObject> {
    let control = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"TakeOverControlSystem") as TakeOverControlSystem;
    return IsDefined(control) ? control.GetControlledObject() : null;
  }

  private func OurHands() -> ref<SecurityTurret> {
    let turret = this.OurDevice() as SecurityTurret;
    if !IsDefined(turret) || !EntityID.IsStatic(turret.GetEntityID()) {
      return null;
    }
    return IsDefined(turret.GetDevicePS()) ? turret : null;
  }

  // Ours: where it points (40 m along the camera, which is the turret's
  // while V holds it) and whether it fires, 5 times a second; once that V
  // let go.
  private func PollOurHands(now: Float) -> Void {
    let turret = this.OurHands();
    let held = IsDefined(turret) ? turret.GetEntityID() : EntityID.FromHash(0ul);
    // Another player's (refused): our V is let go of it, taken again or
    // not out yet, and nothing is said of it.
    if EntityID.IsDefined(this.m_refused) && (now - this.m_refusedAt > TandemTurrets.RefusedHold() || now < this.m_refusedAt) {
      this.m_refused = EntityID.FromHash(0ul);
    }
    if EntityID.IsDefined(this.m_refused) && held == this.m_refused {
      if now - this.m_releasedAt >= 0.5 || now < this.m_releasedAt {
        this.m_releasedAt = now;
        TakeOverControlSystem.ReleaseControl(this.GetGameInstance());
      }
      turret = null;
      held = EntityID.FromHash(0ul);
    }
    if EntityID.IsDefined(this.m_held) && this.m_held != held {
      TandemObjects.Hands(this.m_held, new Vector4(0.0, 0.0, 0.0, 1.0), 2);
      TandemNative.Log(s"turrets: our V let go of \(TandemNative.EntityHex(this.m_held)): said");
      this.m_held = EntityID.FromHash(0ul);
    }
    if !IsDefined(turret) {
      return;
    }
    if this.m_held != held {
      this.m_held = held;
      this.m_heldSaidAt = -1.0;
      TandemNative.Log(s"turrets: our V holds \(TandemNative.EntityHex(held)): where it points said to the others");
    }
    if now - this.m_heldSaidAt < TandemTurrets.HandsEvery() && now >= this.m_heldSaidAt {
      return;
    }
    this.m_heldSaidAt = now;
    let forward = Vector4.Normalize(GameInstance.GetCameraSystem(this.GetGameInstance()).GetActiveCameraForward());
    let from = turret.GetWorldPosition();
    let at = new Vector4(from.X + forward.X * 40.0, from.Y + forward.Y * 40.0, from.Z + forward.Z * 40.0, 1.0);
    TandemObjects.Hands(held, at, turret.TandemIsShooting() ? 1 : 0);
  }

  public static func HandsEvery() -> Float { return 0.2; }
  // A refused turret taken again this soon is let go again unsaid (its
  // holder's for now; later a claim asks the host again).
  public static func RefusedHold() -> Float { return 3.0; }
  // Unheard of this long: a turret in a player's hands is its game's again.
  public static func HandsHold() -> Float { return 1.0; }
  // A drawn shot of a held turret's fire, at most this often.
  public static func HandsShotEvery() -> Float { return 0.12; }

  private func FindHands(entity: EntityID) -> ref<TandemTurret> {
    for known in this.m_hands {
      if known.entity == entity {
        return known;
      }
    }
    return null;
  }

  // Another player's word on a turret in its hands.
  private func OnHands(event: TandemObjectEvent, now: Float) -> Void {
    if event.player == this.m_you {
      return;
    }
    let turret = this.Body(event.entity);
    if !IsDefined(turret) {
      return; // not in our world
    }
    let known = this.FindHands(event.entity);
    if turret.GetDevicePS().IsControlledByPlayer() {
      // In our V's hands for now (our claim on its way to a refusal): the
      // holder's hold goes on, our copy follows it again once our V is out.
      if IsDefined(known) && event.value < 2 {
        known.handsHeard = now;
      }
      return;
    }
    if event.value >= 2 {
      if IsDefined(known) {
        this.LetGo(known);
        ArrayRemove(this.m_hands, known);
      }
      return;
    }
    if !IsDefined(known) {
      known = new TandemTurret();
      known.entity = event.entity;
      ArrayPush(this.m_hands, known);
      // Its own fire and the host's words about it wait.
      turret.TandemStop();
      TandemNative.Log(s"turrets: \(TandemNative.EntityHex(event.entity)) is in player \(event.player)'s hands: our copy follows it");
    }
    known.hands = event.player;
    known.handsHeard = now;
    known.handsAt = new Vector4(event.x, event.y, event.z, 1.0);
    known.handsFiring = event.value % 2 == 1;
    turret.TandemInHandsFor(TandemTurrets.HandsHold());
    let place = known.handsAt;
    place.Z -= 1.0; // the look-at adds a metre to a place
    turret.TandemAim(null, place, TandemTurrets.HandsHold());
  }

  // Every tick: the held turrets' fire, drawn; a silent one let go.
  private func UpdateHands(now: Float) -> Void {
    let i = ArraySize(this.m_hands) - 1;
    while i >= 0 {
      let known = this.m_hands[i];
      if now - known.handsHeard > TandemTurrets.HandsHold() || now < known.handsHeard {
        this.LetGo(known);
        ArrayErase(this.m_hands, i);
      } else if known.handsFiring && (now - known.handsShotAt >= TandemTurrets.HandsShotEvery() || now < known.handsShotAt) {
        known.handsShotAt = now;
        this.DrawHandsShot(known, now);
      } else if !known.handsFiring && known.shooting && now - known.handsShotAt > 0.3 {
        known.shooting = false;
        let turret = this.Body(known.entity);
        if IsDefined(turret) {
          turret.TandemShooting(false);
        }
      }
      i -= 1;
    }
  }

  // One shot of the controller's fire on our copy: its weapon's tracer and
  // effects toward where it points; its hits are nobody's.
  private func DrawHandsShot(known: ref<TandemTurret>, now: Float) -> Void {
    let game = this.GetGameInstance();
    let turret = this.Body(known.entity);
    let weapon = IsDefined(turret) ? GameInstance.GetTransactionSystem(game).GetItemInSlot(turret, t"AttachmentSlots.WeaponRight") as WeaponObject : null;
    if !IsDefined(weapon) || !weapon.IsAttached() {
      return;
    }
    if !known.shooting {
      known.shooting = true;
      turret.TandemShooting(true);
    }
    let presence = TandemPresenceSync.Of(game);
    if IsDefined(presence) && presence.Bullet(turret, weapon, known.handsAt, now) {
      WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot);
      this.m_handsDrawn += 1;
      if this.m_handsDrawn == 1 || this.m_handsDrawn % 50 == 0 {
        TandemNative.Log(s"turrets: player \(known.hands)'s turret \(TandemNative.EntityHex(known.entity)) fires: drawn from our copy (\(this.m_handsDrawn) so far)");
      }
    }
    turret.TandemShotLook();
  }

  // Our claim on a turret was refused: another player held it first (the
  // host's word; devices.md, "A turret in a player's hands"). Our V lets go
  // at once, the game's own way out of a device's control (as a quest ends
  // one: TakeOverControlSystem.ReleaseControl), and the player is told who
  // has it. Nothing more is said of it: its copy follows its holder.
  private func Refused(event: TandemObjectEvent, now: Float) -> Void {
    this.m_refusals += 1;
    this.m_refused = event.entity;
    this.m_refusedAt = now;
    this.m_releasedAt = -1.0;
    if this.m_held == event.entity {
      this.m_held = EntityID.FromHash(0ul);
    }
    let turret = this.OurHands();
    let ours = IsDefined(turret) && turret.GetEntityID() == event.entity;
    if ours {
      this.m_releasedAt = now;
      TakeOverControlSystem.ReleaseControl(this.GetGameInstance());
    }
    TandemNative.Log(s"turrets: our claim on \(TandemNative.EntityHex(event.entity)) refused: player \(event.player) holds it; \(ours ? "our V lets go" : "out of our V's hands already")");
    TandemText.Notice(this.GetGameInstance(), "Turrets-Busy", TandemCoop.NameOf(event.player));
  }

  // A held turret is its game's again.
  private func LetGo(known: ref<TandemTurret>) -> Void {
    let turret = this.Body(known.entity);
    if IsDefined(turret) {
      turret.TandemInHandsFor(0.0);
      turret.TandemHeld(0.0);
      if known.shooting {
        turret.TandemShooting(false);
      }
    }
    known.shooting = false;
    TandemNative.Log(s"turrets: \(TandemNative.EntityHex(known.entity)) is out of player \(known.hands)'s hands: our copy is our game's again");
  }

  // ---- commands (tools/game.sh cmd turret ...) ----

  private func Nearest(kind: String) -> ref<SecurityTurret> {
    let devices = TandemDevices.Get(this.GetGameInstance());
    return IsDefined(devices) ? devices.NearestOf(kind, 60.0) as SecurityTurret : null;
  }

  public func Command(command: String, now: Float) -> Void {
    let game = this.GetGameInstance();
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "") {
      let role = this.m_host ? "host" : "guest";
      TandemNative.Log(s"turrets: \(ArraySize(this.m_turrets)) spoken of; shots told \(this.m_shotsSaid), drawn \(this.m_shotsDrawn), real at our V \(this.m_shotsReal); puppets taken as targets \(this.m_engaged); left to our game \(this.m_left) (\(role)); in others' hands \(ArraySize(this.m_hands)), their shots drawn \(this.m_handsDrawn); in ours \(TandemNative.EntityHex(this.m_held)); claims refused \(this.m_refusals); our V controls a device: \(IsDefined(this.OurDevice()))");
      return;
    }
    let turret = Equals(verb, "take") ? null : this.Nearest("sentry");
    if !IsDefined(turret) {
      turret = this.Nearest("turret");
    }
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
    } else if Equals(verb, "take") {
      // As the turret's control panel gives it (a device's own takeover:
      // TakeOverControlSystem.RequestTakeControl, not the quickhack).
      let take = new ToggleTakeOverControl();
      take.SetProperties(false, false);
      TakeOverControlSystem.RequestTakeControl(turret, take);
      TandemNative.Log(s"turrets: our V takes control of \(hex)");
    } else if Equals(verb, "provoke") {
      // Our copy takes our own V as a target (as the host's takes a puppet):
      // a spot's turret is not always hostile to V, or has it in sight.
      TandemNative.Log(s"turrets: \(hex) takes our V as a target: \(turret.TandemTake(ours)) (followed \(TandemTurrets.Follows(turret)), the police's \(turret.GetDevicePS().IsPartOfPrevention())); \(turret.TandemSenses())");
    } else if Equals(verb, "engage") {
      let system = TandemSystem.Of(game);
      let players = IsDefined(system) ? system.PuppetPlayers() : [];
      let puppet = ArraySize(players) > 0 ? system.PuppetOf(players[0]) : null;
      if !IsDefined(puppet) {
        TandemNative.Log("turrets: no puppet here");
        return;
      }
      TandemNative.Log(s"turrets: \(hex) takes player \(players[0])'s puppet as a target: \(turret.TandemTake(puppet)); \(turret.TandemSenses())");
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

// Until then (engine time) the turret is in another player's hands: our
// copy's own fire waits.
@addField(SensorDevice)
public let m_tandemHandsUntil: Float;

// Until then (engine time) the scan light shows the host's turret: our
// copy's own changes of it wait.
@addField(SensorDevice)
public let m_tandemLookUntil: Float;

@addMethod(SensorDevice)
public func TandemInHandsFor(seconds: Float) -> Void {
  this.m_tandemHandsUntil = seconds > 0.0 ? EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) + seconds : 0.0;
}

@addMethod(SensorDevice)
public func TandemInHands() -> Bool {
  return this.m_tandemHandsUntil > 0.0 && EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) < this.m_tandemHandsUntil;
}

@addMethod(SecurityTurret)
public func TandemIsShooting() -> Bool {
  return this.m_animFeature.Shoot;
}

@addMethod(SensorDevice)
public func TandemLookHeld() -> Bool {
  return this.m_tandemLookUntil > 0.0 && EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) < this.m_tandemLookUntil;
}

// Guest: the host's turret follows a target (the laser on, the scan light
// red) or nobody (off, yellow; green to a V it is friendly to, as the
// game's own choice of colour does). Held a while against our copy's own.
@addMethod(SecurityTurret)
public func TandemHostLook(locked: Bool) -> Void {
  this.m_tandemLookUntil = 0.0;
  if IsDefined(this.m_laserMesh) {
    this.m_laserMesh.Toggle(locked);
  }
  this.DetermineLightScanRefs(locked ? this.m_lightColors.red : this.m_lightColors.yellow);
  this.m_tandemLookUntil = locked ? EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) + 1.5 : 0.0;
}

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
// the security system thinks of it), and locks it at once. The game's own
// lock (OnCurrentTargetAppears) never comes for it: a turret of a security
// system that is not in combat only follows its target (SAFE: nothing,
// ALERTED: no lock), and ours is not, the guest's game's is (arasaka-park's
// sentry followed a puppet for 6 s without a shot, 2026-11-21); elsewhere
// it waits its time to act, which the guest's copy already has. The lock is
// ours, not OnTargetLocked: that one sets our security system to combat,
// against our own V too. False: ours is off, turned (by any player:
// Devices.reds keeps it friendly to every V), in a player's hands (our
// V's, or another player's: TurretHands), or it would not follow the
// target (it cannot detect, the target is down, a forced state).
@addMethod(SensorDevice)
public func TandemEngage(target: ref<GameObject>) -> Bool {
  let ps = this.GetDevicePS();
  if !IsDefined(target) || !IsDefined(ps) || !ps.IsON() || ps.IsAttitudeChanged() || ps.IsControlledByPlayer() || this.TandemInHands() {
    return false;
  }
  let mine = this.GetAttitudeAgent();
  let theirs = target.GetAttitudeAgent();
  if IsDefined(mine) && IsDefined(theirs) {
    mine.SetAttitudeTowards(theirs, EAIAttitude.AIA_Hostile);
  }
  this.RecognizeTarget(target, true);
  if this.GetCurrentlyFollowedTarget() != target {
    return false;
  }
  this.StartBehaviourResolve(ESensorDeviceStates.TARGETLOCK);
  this.SetQuestVisibility(true);
  return Equals(this.GetSensorDeviceState(), ESensorDeviceStates.TARGETLOCK);
}

// ...and fires at it (OnTargetLocked's own two lines). A guest's copy holds
// that fire and says what it locked (the ShootStart wrap below).
@addMethod(SecurityTurret)
public func TandemTake(target: ref<GameObject>) -> Bool {
  if !this.TandemEngage(target) {
    return false;
  }
  if !this.m_animFeature.Shoot {
    this.SelectShootingPattern(this.GetWeapon(), this, true);
    this.ShootStart();
  }
  return true;
}

// For the log: what the turret's lock depends on.
@addMethod(SensorDevice)
public func TandemSenses() -> String {
  let ps = this.GetDevicePS();
  let security = IsDefined(ps) && ps.IsConnectedToSecuritySystem() ? ps.GetSecuritySystem() : null;
  let alarm = IsDefined(security) ? EnumValueToString("ESecuritySystemState", Cast<Int64>(EnumInt(security.GetSecurityState()))) : "none";
  let state = EnumValueToString("ESensorDeviceStates", Cast<Int64>(EnumInt(this.GetSensorDeviceState())));
  let detects = IsDefined(ps) && ps.GetBehaviourCanDetectIntruders();
  return s"security system \(alarm), sensor \(state), follows a target \(IsDefined(this.GetCurrentlyFollowedTarget())), detects intruders \(detects)\(this.TandemShooting())";
}

// For the log (turret-host, turret-guest: a lock that never fired, a take
// that never followed): what the turret's own fire depends on. Empty for a
// camera.
@addMethod(SensorDevice)
public func TandemShooting() -> String {
  return "";
}

@addMethod(SecurityTurret)
public func TandemShooting() -> String {
  return s", shooting \(this.m_animFeature.Shoot), weapon \(IsDefined(this.GetWeapon())), burst delay \(this.m_isBurstDelayOngoing), own fire \(this.m_tandemOwnFire)";
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

// Our copy's own changes of its scan light wait while it shows the host's
// turret (its own fields only: safe on any thread).
@wrapMethod(SensorDevice)
protected final func DetermineLightScanRefs(desiredColor: ScriptLightSettings) -> Void {
  if this.m_tandemLookUntil > 0.0 && EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) < this.m_tandemLookUntil {
    return;
  }
  wrappedMethod(desiredColor);
}

// Guest: our copy would start firing. The host's turret fires, not ours:
// held, and what it locked is queued (our own V: said to the host). A copy
// in another player's hands fires only what that player fires (drawn).
@wrapMethod(SecurityTurret)
private final func ShootStart() -> Void {
  if this.TandemInHands() && !TandemStreaming.IsPassing(this.GetEntityID()) {
    return;
  }
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
