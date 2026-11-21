// Thrown objects between players (docs/architecture/combat-coverage.md,
// "Throws"): grenades, throwing knives and axes, arm launcher rounds and
// smart weapons' guided rounds.
//
// The thrower's game owns the real object: its flight, its bounces, its
// hits and its explosion, decided as in single player (hits on proxies go to
// the host as strikes, on another V as AreaHit while friendly fire is on).
// Every other game launches a copy of it from the thrower's puppet, with the
// thrower's origin and velocity (proto::Throw), so it flies the same arc on
// the same geometry, and ends it where and how the real one ended
// (proto::ThrowEnd: exploded, stuck, picked up). A copy deals no damage: a
// remote V's puppet never does on our game (TandemOutcomes.IsRemoteComputed).
// What a copy does besides damage stays: its explosion's look and sound, the
// grenade marker on the HUD, its smoke's vision blocker and its stimuli,
// which on the host are what its NPCs see and hear of a guest's grenade.
// A copy's fuse is held: it explodes when the real one did, where it did.
// A grenade's path is said as it flies (proto::ThrowPath, four times a
// second, and at each bounce: its collision only queues the place, the
// tick says it): a copy that strayed from it (it bounced off something one game
// alone has) is put back on the real one's path.
//
// The host also sends its NPCs' grenades (kThrowByNpc), launched on the
// guests from the NPC's proxy: the damage is the host's (PlayerHit).
//
// A thrown knife is retrieved by its owner only: the real one lies in the
// owner's world (vanilla's pickup), the copies refuse every interaction and
// go when the owner picks it up.

public class TandemThrowCopy {
  public let player: Uint32;
  public let id: Uint32;
  public let kind: Int32;
  public let flags: Int32;
  public let item: TweakDBID;
  public let origin: Vector4;
  public let velocity: Vector4;
  public let npc: Uint32;
  public let at: Uint32;
  // When it arrived, and whether we gave the puppet the item to throw.
  public let since: Float;
  public let given: Bool;
  public let launched: Bool;
  public let launchedAt: Float;
  // The copy, once it exists (knives and rounds: when their projectile
  // reports in, TandemThrows.Bind).
  public let object: wref<ItemObject>;
  // Its end, when it came before the copy existed.
  public let ended: Bool;
  public let endHow: Int32;
  public let endAt: Vector4;
}

// One of our real objects in flight, for its path (ThrowPath).
public class TandemThrowTrack {
  // Whether its path is said at all (grenades: what bounces and rolls).
  public let says: Bool;
  public let lastPosition: Vector4;
  public let lastAt: Float;
  public let saidAt: Float;
  // It lay still when last said.
  public let resting: Bool;
  public let said: Int32;
  // A bounce not said yet: where and when (engine time).
  public let bounced: Bool;
  public let bounceAt: Vector4;
  public let bounceTime: Float;
  public let bounceSaidAt: Float;
}

public class TandemThrows {
  private let m_game: GameInstance;
  // Ours: the real objects in flight and their numbers.
  private let m_next: Uint32;
  private let m_ours: array<wref<ItemObject>>;
  private let m_ourIds: array<Uint32>;
  private let m_ourStuck: array<Bool>;
  private let m_ourTracks: array<ref<TandemThrowTrack>>;
  private let m_pathsSaid: Int32;
  private let m_bouncesSaid: Int32;
  private let m_pathsFixed: Int32;
  // Ours whose launch velocity the game did not give: measured over the
  // first frames (where it was, when).
  private let m_measuring: array<ref<TandemThrowCopy>>;
  private let m_measuringObject: array<wref<ItemObject>>;
  // The others': their copies here.
  private let m_copies: array<ref<TandemThrowCopy>>;
  private let m_copyIds: array<EntityID>;
  private let m_logged: array<CName>;

  public static func Of(game: GameInstance) -> ref<TandemThrows> {
    let outcomes = TandemOutcomes.Of(game);
    return IsDefined(outcomes) ? outcomes.Throws() : null;
  }

  public static func GrenadeKind() -> Int32 {
    return 0;
  }

  public static func KnifeKind() -> Int32 {
    return 1;
  }

  public static func LauncherKind() -> Int32 {
    return 2;
  }

  public static func SmartKind() -> Int32 {
    return 3;
  }

  // proto::kThrow*
  public static func StickyFlag() -> Int32 {
    return 1;
  }

  public static func HomingFlag() -> Int32 {
    return 2;
  }

  public static func ByNpcFlag() -> Int32 {
    return 16;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_next = 1u;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }

  private func System() -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
  }

  private func Once(key: CName, text: String) -> Void {
    if !ArrayContains(this.m_logged, key) {
      ArrayPush(this.m_logged, key);
      TandemNative.Log(text);
    }
  }

  // ---- who is who ----

  public static func IsOurs(game: GameInstance, owner: wref<GameObject>) -> Bool {
    let player = GetPlayer(game);
    return IsDefined(owner) && IsDefined(player) && owner.GetEntityID() == player.GetEntityID();
  }

  // The session's number for a target: a shared NPC, ours or a proxy.
  public static func NpcOf(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    let system = TandemCombat.System(game);
    if !IsDefined(system) {
      return 0u;
    }
    return system.SharedNpcOf(target.GetEntityID());
  }

  // ...or a V: ours (TandemNative.You()), or another's puppet.
  public static func PlayerOf(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    if TandemThrows.IsOurs(game, target) {
      return TandemNative.You();
    }
    return TandemCombat.PuppetPlayer(game, target);
  }

  // What a throw homes on, here.
  private func TargetOf(npc: Uint32, at: Uint32) -> ref<GameObject> {
    let system = this.System();
    if !IsDefined(system) {
      return null;
    }
    if npc != 0u {
      let npcs = system.NpcsSync();
      return IsDefined(npcs) ? GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(npc)) as GameObject : null;
    }
    if at != 0u {
      return at == system.You() ? GetPlayer(this.m_game) : system.PuppetOf(at);
    }
    return null;
  }

  // ---- ours: the real objects ----

  // Our V's grenade, knife or launcher round left the hand (its OnShoot).
  public func OnOurThrow(object: ref<ItemObject>, kind: Int32, flags: Int32, item: TweakDBID, origin: Vector4, velocity: Vector4, target: wref<GameObject>) -> Void {
    let copy = new TandemThrowCopy();
    copy.id = this.m_next;
    this.m_next += 1u;
    copy.kind = kind;
    copy.flags = flags;
    copy.item = item;
    copy.origin = Vector4.IsZero(origin) ? object.GetWorldPosition() : origin;
    copy.velocity = velocity;
    copy.npc = TandemThrows.NpcOf(this.m_game, target);
    copy.at = copy.npc == 0u ? TandemThrows.PlayerOf(this.m_game, target) : 0u;
    copy.since = this.Now();
    ArrayPush(this.m_ours, object);
    ArrayPush(this.m_ourIds, copy.id);
    ArrayPush(this.m_ourStuck, false);
    ArrayPush(this.m_ourTracks, this.Track(object, copy.kind, copy.since));
    if Vector4.Length(velocity) < 0.5 {
      // The game gave no velocity: measured over the next frames.
      ArrayPush(this.m_measuring, copy);
      ArrayPush(this.m_measuringObject, object);
      return;
    }
    this.SendOurs(copy);
  }

  // The host's NPC threw a grenade: the guests launch it from its proxy.
  public func OnNpcThrow(object: ref<ItemObject>, npc: Uint32, item: TweakDBID, origin: Vector4, velocity: Vector4, flags: Int32) -> Void {
    let copy = new TandemThrowCopy();
    copy.id = this.m_next;
    this.m_next += 1u;
    copy.kind = TandemThrows.GrenadeKind();
    copy.flags = flags | TandemThrows.ByNpcFlag();
    copy.item = item;
    copy.origin = Vector4.IsZero(origin) ? object.GetWorldPosition() : origin;
    copy.velocity = velocity;
    copy.npc = npc;
    copy.since = this.Now();
    ArrayPush(this.m_ours, object);
    ArrayPush(this.m_ourIds, copy.id);
    ArrayPush(this.m_ourStuck, false);
    ArrayPush(this.m_ourTracks, this.Track(object, copy.kind, copy.since));
    if Vector4.Length(velocity) < 0.5 {
      ArrayPush(this.m_measuring, copy);
      ArrayPush(this.m_measuringObject, object);
      return;
    }
    this.SendOurs(copy);
  }

  private func Track(object: ref<ItemObject>, kind: Int32, now: Float) -> ref<TandemThrowTrack> {
    let track = new TandemThrowTrack();
    track.says = kind == TandemThrows.GrenadeKind();
    track.lastPosition = object.GetWorldPosition();
    track.lastAt = now;
    track.saidAt = now;
    return track;
  }

  // proto::kThrowPathHz: a path sample this often while it flies.
  public static func PathGap() -> Float {
    return 0.25;
  }

  // A bounce is said once its new course is measured: this long after it.
  public static func BounceGap() -> Float {
    return 0.04;
  }

  // A rolling grenade touches the ground every frame: a bounce is said at
  // most this often an object (proto::ThrowPath's budget: the referee's 24
  // a second with the 4 of PathGap).
  public static func BounceEvery() -> Float {
    return 0.15;
  }

  // Our grenades' bounces since the last frame (BaseGrenade.OnCollision
  // queued them: it may run on a job thread): kept on their track.
  private func TakeBounces(now: Float) -> Void {
    for bounce in TandemStreaming.TakePoints(TandemPointKind.ThrowBounce()) {
      let i = 0;
      while i < ArraySize(this.m_ours) {
        let object = this.m_ours[i];
        if IsDefined(object) && object.GetEntityID() == bounce.entity {
          let track = this.m_ourTracks[i];
          if track.says && !this.m_ourStuck[i] && !track.bounced && !track.resting && (now - track.bounceSaidAt >= TandemThrows.BounceEvery() || now < track.bounceSaidAt) {
            let time = Cast<Float>(bounce.value) / 1000.0;
            track.bounced = true;
            track.bounceAt = new Vector4(bounce.x, bounce.y, bounce.z, 1.0);
            // A time from another clock (engine time past 24 days): now.
            track.bounceTime = time <= now && now - time < 1.0 ? time : now;
          }
          i = ArraySize(this.m_ours);
        } else {
          i += 1;
        }
      }
    }
  }

  // Every frame: where each of our grenades is and how it moves, said
  // every PathGap while it flies, once more when it lies still, and at
  // each bounce: from the bounce's place, at the speed it left it with
  // (measured BounceGap later).
  private func SayPaths(now: Float) -> Void {
    this.TakeBounces(now);
    let i = 0;
    while i < ArraySize(this.m_ours) {
      let object = this.m_ours[i];
      let track = this.m_ourTracks[i];
      if IsDefined(object) && track.bounced && now - track.bounceTime >= TandemThrows.BounceGap() {
        track.bounced = false;
        let at = object.GetWorldPosition();
        let velocity = (at - track.bounceAt) / (now - track.bounceTime);
        if Vector4.Length(velocity) < 300.0 && !this.m_ourStuck[i] {
          if Vector4.Length(velocity) < 0.5 {
            velocity = new Vector4(0.0, 0.0, 0.0, 0.0);
          }
          TandemFight.SendThrowPath(this.m_ourIds[i], track.bounceAt, velocity);
          track.saidAt = now;
          track.bounceSaidAt = now;
          track.resting = false;
          track.said += 1;
          this.m_pathsSaid += 1;
          this.m_bouncesSaid += 1;
        }
      }
      if IsDefined(object) && track.says && !this.m_ourStuck[i] && now - track.lastAt >= 0.04 {
        let at = object.GetWorldPosition();
        let velocity = (at - track.lastPosition) / (now - track.lastAt);
        track.lastPosition = at;
        track.lastAt = now;
        let still = Vector4.Length(velocity) < 0.5;
        let due = now - track.saidAt >= TandemThrows.PathGap();
        // Lying still is said once; a throw measured at 300 m/s moved by a
        // teleport, not by flying.
        if due && !(still && track.resting) && Vector4.Length(velocity) < 300.0 {
          if still {
            velocity = new Vector4(0.0, 0.0, 0.0, 0.0);
          }
          TandemFight.SendThrowPath(this.m_ourIds[i], at, velocity);
          track.saidAt = now;
          track.resting = still;
          track.said += 1;
          this.m_pathsSaid += 1;
          if track.said == 1 {
            TandemNative.LogDebug(s"throws: our throw \(this.m_ourIds[i])'s path is said as it flies (\(Vector4.Length(velocity)) m/s now)");
          }
        }
      }
      i += 1;
    }
  }

  private func SendOurs(copy: ref<TandemThrowCopy>) -> Void {
    TandemFight.SendThrow(copy.id, copy.kind, copy.flags, copy.item, copy.origin, copy.velocity, copy.npc, copy.at);
    this.Once(n"ours", s"throws: our throw \(copy.id) (kind \(copy.kind), flags \(copy.flags), \(TDBID.ToStringDEBUG(copy.item))) sent, at \(Vector4.Length(copy.velocity)) m/s");
  }

  // Our smart weapon fired: the round homes on what it is locked on, on the
  // others' games too (the shot's flash and sound go as a Shot). False: no
  // lock the others know (nothing locked, or an NPC they do not share): the
  // round is drawn as a plain one, to where it was aimed (Presence.reds).
  public func OnOurSmartShot(weapon: ref<WeaponObject>) -> Bool {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || !IsDefined(weapon) {
      return false;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ActiveWeaponData);
    if !IsDefined(board) {
      return false;
    }
    let smart = FromVariant<ref<smartGunUIParameters>>(board.GetVariant(GetAllBlackboardDefs().UI_ActiveWeaponData.SmartGunParams));
    if !IsDefined(smart) {
      return false;
    }
    let chosen: wref<GameObject>;
    for target in smart.targets {
      let entity = GameInstance.FindEntityByID(this.m_game, target.entityID) as GameObject;
      if IsDefined(entity) && (!IsDefined(chosen) || target.isLocked) {
        chosen = entity;
        if target.isLocked {
          break;
        }
      }
    }
    let npc = TandemThrows.NpcOf(this.m_game, chosen);
    let at = npc == 0u ? TandemThrows.PlayerOf(this.m_game, chosen) : 0u;
    if npc == 0u && at == 0u {
      return false; // nothing locked that the others know
    }
    let forward = GameInstance.GetCameraSystem(this.m_game).GetActiveCameraForward();
    TandemFight.SendThrow(this.m_next, TandemThrows.SmartKind(), TandemThrows.HomingFlag(), ItemID.GetTDBID(weapon.GetItemID()), weapon.GetWorldPosition(), forward, npc, at);
    this.m_next += 1u;
    this.Once(n"smart", s"throws: our smart round at npc \(npc) / player \(at) sent");
    return true;
  }

  // Our real object ended (how: 0 exploded, 1 stuck, 2 gone).
  public func OnOurEnd(object: ref<ItemObject>, how: Int32) -> Void {
    let i = ArrayFindFirst(this.m_ours, object);
    if i < 0 {
      return;
    }
    if how == 1 {
      // A knife stuck: said once, it stays ours until picked up.
      if this.m_ourStuck[i] {
        return;
      }
      this.m_ourStuck[i] = true;
      TandemFight.SendThrowEnd(this.m_ourIds[i], how, object.GetWorldPosition());
      return;
    }
    TandemFight.SendThrowEnd(this.m_ourIds[i], how, object.GetWorldPosition());
    this.Once(n"ourEnd", s"throws: our throw \(this.m_ourIds[i]) ended (\(how)), sent");
    ArrayErase(this.m_ours, i);
    ArrayErase(this.m_ourIds, i);
    ArrayErase(this.m_ourStuck, i);
    ArrayErase(this.m_ourTracks, i);
  }

  public func IsOurObject(object: ref<ItemObject>) -> Bool {
    return ArrayContains(this.m_ours, object);
  }

  private func Measure(now: Float) -> Void {
    let i = 0;
    while i < ArraySize(this.m_measuring) {
      let copy = this.m_measuring[i];
      let object = this.m_measuringObject[i];
      let dt = now - copy.since;
      if !IsDefined(object) || dt >= 0.05 {
        if IsDefined(object) && dt > 0.0 {
          copy.velocity = (object.GetWorldPosition() - copy.origin) / dt;
        }
        this.SendOurs(copy);
        ArrayErase(this.m_measuring, i);
        ArrayErase(this.m_measuringObject, i);
      } else {
        i += 1;
      }
    }
  }

  // ---- the others': copies ----

  // A copy here is never ours to pick up, and never hurts (its hits come
  // from a remote V's puppet: dropped, IsRemoteComputed).
  public func IsCopy(object: wref<GameObject>) -> Bool {
    return IsDefined(object) && ArrayContains(this.m_copyIds, object.GetEntityID());
  }

  public func Arrive(event: TandemFightEvent) -> Void {
    let copy = new TandemThrowCopy();
    copy.player = event.player;
    copy.id = event.id;
    copy.kind = event.level;
    copy.flags = event.flags;
    copy.item = event.attack;
    copy.origin = new Vector4(event.x, event.y, event.z, 1.0);
    copy.velocity = new Vector4(event.dx, event.dy, event.dz, 0.0);
    copy.npc = event.npc;
    copy.at = event.target;
    copy.since = this.Now();
    if copy.kind == TandemThrows.SmartKind() {
      // Fired at once or never: a late round is worth nothing.
      this.FireSmart(copy);
      return;
    }
    while ArraySize(this.m_copies) >= 32 {
      ArrayErase(this.m_copies, 0);
    }
    ArrayPush(this.m_copies, copy);
  }

  public func Ended(event: TandemFightEvent) -> Void {
    for copy in this.m_copies {
      if copy.player == event.player && copy.id == event.id {
        copy.ended = true;
        copy.endHow = event.level;
        copy.endAt = new Vector4(event.x, event.y, event.z, 1.0);
        if IsDefined(copy.object) {
          this.Finish(copy);
        }
        return;
      }
    }
  }

  // Where the real one is now (ThrowPath): a copy that strayed from it is
  // put on its path (TandemFight.PathFix: left alone within 1.5 m in flight,
  // 0.4 m lying still).
  public func OnPath(event: TandemFightEvent) -> Void {
    for copy in this.m_copies {
      if copy.player == event.player && copy.id == event.id {
        let grenade = copy.object as BaseGrenade;
        if !IsDefined(grenade) || copy.ended {
          return;
        }
        let at = new Vector4(event.x, event.y, event.z, 1.0);
        let velocity = new Vector4(event.dx, event.dy, event.dz, 0.0);
        let here = grenade.GetWorldPosition();
        let fix = TandemFight.PathFix(here, at, velocity);
        if fix != 0 {
          let record = TweakDBInterface.GetGrenadeRecord(copy.item);
          let gravity = IsDefined(record) && IsDefined(record.DeliveryMethod()) ? record.DeliveryMethod().AccelerationZ() : -9.8;
          grenade.TandemFollow(at, velocity, gravity, fix == 2);
          this.m_pathsFixed += 1;
          TandemNative.Log(s"throws: player \(copy.player)'s throw \(copy.id) was \(Vector4.Distance(here, at)) m off the real one's path: put on it (\(fix == 2 ? "flying on" : "lying still"))");
        }
        return;
      }
    }
  }

  // tools/game.sh cmd throws report
  public func Report() -> Void {
    TandemNative.Log(s"throws: ours in flight \(ArraySize(this.m_ours)), path samples said \(this.m_pathsSaid) (\(this.m_bouncesSaid) at a bounce); copies here \(ArraySize(this.m_copies)), put back on their real path \(this.m_pathsFixed); stale throws dropped \(TandemFight.StaleThrows())");
  }

  // The thrower's body here: a puppet, or (kThrowByNpc) the NPC's proxy.
  private func ThrowerOf(copy: ref<TandemThrowCopy>) -> ref<GameObject> {
    if (copy.flags & TandemThrows.ByNpcFlag()) != 0 {
      return this.TargetOf(copy.npc, 0u);
    }
    let system = this.System();
    return IsDefined(system) ? system.PuppetOf(copy.player) : null;
  }

  // Every frame of the session (TandemOutcomes.Tick).
  public func Tick() -> Void {
    let now = this.Now();
    this.Measure(now);
    this.SayPaths(now);
    let i = 0;
    while i < ArraySize(this.m_copies) {
      let copy = this.m_copies[i];
      let keep = true;
      if !copy.launched {
        if this.Launch(copy, now) {
          copy.launched = true;
          copy.launchedAt = now;
        } else if now - copy.since > 1.5 {
          TandemNative.Log(s"throws: player \(copy.player)'s throw \(copy.id) (\(TDBID.ToStringDEBUG(copy.item))) not shown: no thrower or item to throw it from");
          keep = false;
        }
      } else if !IsDefined(copy.object) {
        // Knives and rounds: their projectile reports in (Bind) within a
        // moment; one that never does was not spawned. A grenade is bound
        // at its launch: gone, it was destroyed before its end.
        if copy.kind == TandemThrows.GrenadeKind() {
          if !copy.ended {
            this.Once(n"grenadeGone", s"throws: player \(copy.player)'s grenade \(copy.id) launched, its copy gone before its end");
          }
          keep = false;
        } else if now - copy.launchedAt > 2.0 {
          this.Once(n"unbound", s"throws: player \(copy.player)'s throw \(copy.id) launched, its copy never seen");
          keep = false;
        }
      } else if copy.ended {
        keep = false;
      } else if now - copy.launchedAt > 15.0 {
        // No end came (its thrower gone): it goes quietly.
        this.Finish(copy, 2);
        keep = false;
      }
      if keep {
        i += 1;
      } else {
        ArrayErase(this.m_copies, i);
      }
    }
  }

  private func Launch(copy: ref<TandemThrowCopy>, now: Float) -> Bool {
    let thrower = this.ThrowerOf(copy);
    if !IsDefined(thrower) {
      return false;
    }
    if copy.kind == TandemThrows.GrenadeKind() {
      return this.LaunchGrenade(copy, thrower);
    }
    let slot = copy.kind == TandemThrows.LauncherKind() ? t"AttachmentSlots.WeaponLeft" : t"AttachmentSlots.WeaponRight";
    let item = GameInstance.GetTransactionSystem(this.m_game).GetItemInSlot(thrower, slot);
    if !IsDefined(item) {
      return false;
    }
    let template = TweakDBInterface.GetCName(copy.item + t".projectileTemplateName", n"None");
    if !IsNameValid(template) {
      this.Once(n"template", s"throws: no projectile template for \(TDBID.ToStringDEBUG(copy.item)): its copies are not shown");
      return false;
    }
    let launch = new gameprojectileSpawnerLaunchEvent();
    launch.launchParams.logicalPositionProvider = TandemThrows.At(copy.origin);
    launch.launchParams.logicalOrientationProvider = TandemThrows.Towards(copy.velocity);
    launch.templateName = template;
    launch.appearance = n"None";
    launch.owner = thrower;
    launch.projectileParams.ignoreMountedVehicleCollision = true;
    let component = this.AimComponent(copy, item.GetWorldPosition(), copy.velocity);
    if IsDefined(component) {
      launch.projectileParams.trackedTargetComponent = component;
      launch.projectileParams.targetPosition = ProjectileTargetingHelper.GetTargetingComponentsWorldPosition(component);
    }
    item.QueueEvent(launch);
    return true;
  }

  // A grenade: the AI's own way of throwing one (AISubActionThrowItem
  // .ThrowItem): the item in the thrower's left hand, launched from there
  // with the thrower's arc.
  private func LaunchGrenade(copy: ref<TandemThrowCopy>, thrower: ref<GameObject>) -> Bool {
    let transactions = GameInstance.GetTransactionSystem(this.m_game);
    let slot = t"AttachmentSlots.WeaponLeft";
    let held = transactions.GetItemInSlot(thrower, slot);
    if !IsDefined(held) || ItemID.GetTDBID(held.GetItemID()) != copy.item {
      if !copy.given {
        copy.given = true;
        transactions.RemoveItemFromSlot(thrower, slot);
        let id = ItemID.FromTDBID(copy.item);
        transactions.GiveItem(thrower, id, 1);
        transactions.AddItemToSlot(thrower, slot, id);
      }
      return false;
    }
    // In the slot but still spawning (its entity is there before the slot's
    // spawn is done): RemoveItemFromSlot destroys it then, whatever its
    // shouldDestroyEntity says (engine/presence.md §13.97). Launched once
    // it is done, a frame or a few later.
    if transactions.IsSlotSpawningItem(thrower, slot, held.GetItemID()) {
      return false;
    }
    let grenade = held as BaseGrenade;
    if !IsDefined(grenade) {
      this.Once(n"notGrenade", s"throws: \(TDBID.ToStringDEBUG(copy.item)) is not a grenade here");
      return false;
    }
    let record = TweakDBInterface.GetGrenadeRecord(copy.item);
    let gravity = IsDefined(record) && IsDefined(record.DeliveryMethod()) ? record.DeliveryMethod().AccelerationZ() : -9.8;
    let launch = new gameprojectileSetUpAndLaunchEvent();
    launch.launchParams.launchMode = gameprojectileELaunchMode.FromLogic;
    launch.launchParams.logicalPositionProvider = TandemThrows.At(copy.origin);
    launch.launchParams.logicalOrientationProvider = TandemThrows.Towards(copy.velocity);
    launch.launchParams.visualPositionProvider = TandemThrows.At(copy.origin);
    launch.launchParams.visualOrientationProvider = TandemThrows.Towards(copy.velocity);
    launch.lerpMultiplier = 15.0;
    launch.owner = thrower;
    launch.trajectoryParams = ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), Vector4.Length(copy.velocity));
    let component = this.AimComponent(copy, copy.origin, copy.velocity);
    if IsDefined(component) {
      launch.projectileParams.trackedTargetComponent = component;
      launch.projectileParams.targetPosition = ProjectileTargetingHelper.GetTargetingComponentsWorldPosition(component);
    }
    // A copy before it flies: its OnShoot holds its fuse (HoldsFuse).
    this.Adopt(copy, grenade);
    grenade.QueueEvent(launch);
    transactions.RemoveItemFromSlot(thrower, slot, false);
    this.Once(n"grenade", s"throws: player \(copy.player)'s grenade \(copy.id) (\(TDBID.ToStringDEBUG(copy.item))) launched from \((copy.flags & TandemThrows.ByNpcFlag()) != 0 ? "its proxy" : "their puppet") at \(Vector4.Length(copy.velocity)) m/s");
    return true;
  }

  // Homing: the part of what the copy flies at here that it aims for.
  private func AimComponent(copy: ref<TandemThrowCopy>, from: Vector4, forward: Vector4) -> wref<TargetingComponent> {
    let target = this.TargetOf((copy.flags & TandemThrows.ByNpcFlag()) != 0 ? 0u : copy.npc, copy.at);
    if !IsDefined(target) {
      return null;
    }
    return GameInstance.GetTargetingSystem(this.m_game).GetBestComponentOnTargetObject(from, Vector4.Normalize(forward), target, TargetComponentFilterType.Shooting);
  }

  // A smart round: the thrower's puppet's weapon fires a guided one at the
  // target, as an NPC's smart gun does (AIWeapon.Fire, shouldTrackTarget).
  private func FireSmart(copy: ref<TandemThrowCopy>) -> Void {
    let system = this.System();
    let puppet = IsDefined(system) ? system.PuppetOf(copy.player) : null;
    let target = this.TargetOf(copy.npc, copy.at);
    if !IsDefined(puppet) || !IsDefined(target) {
      return;
    }
    let weapon = GameInstance.GetTransactionSystem(this.m_game).GetItemInSlot(puppet, t"AttachmentSlots.WeaponRight") as WeaponObject;
    if !IsDefined(weapon) {
      return;
    }
    let component = GameInstance.GetTargetingSystem(this.m_game).GetBestComponentOnTargetObject(weapon.GetWorldPosition(), weapon.GetWorldForward(), target, TargetComponentFilterType.Shooting);
    if !IsDefined(component) {
      return;
    }
    let params: gameprojectileWeaponParams;
    params.smartGunAccuracy = 1.0;
    params.smartGunIsProjectileGuided = true;
    params.trackedTargetComponent = component;
    params.targetPosition = ProjectileTargetingHelper.GetTargetingComponentsWorldPosition(component);
    // Ours to see, never to catch: our V is not in its way.
    let player = GetPlayer(this.m_game);
    if IsDefined(player) && target != player {
      gameprojectileWeaponParams.AddObjectToIgnoreCollisionWith(params, player.GetEntityID());
    }
    weapon.AI_ShootAt(IPositionProvider.CreatePlacedComponentPositionProvider(component), target, puppet, Cast<Uint16>(0), params, Cast<Uint8>(1), 0.0);
    this.Once(n"smartCopy", s"throws: player \(copy.player)'s smart round fired from their puppet at npc \(copy.npc) / player \(copy.at)");
  }

  // A projectile a puppet's (or proxy's) item spawned: the copy of that
  // thrower's oldest launched one still unbound.
  public func Bind(object: ref<ItemObject>, thrower: wref<GameObject>) -> Bool {
    let player = TandemCombat.PuppetPlayer(this.m_game, thrower);
    for copy in this.m_copies {
      if copy.launched && !IsDefined(copy.object) && copy.kind != TandemThrows.GrenadeKind() && copy.player == player && player != 0u {
        this.Adopt(copy, object);
        this.Once(n"bound", s"throws: player \(copy.player)'s throw \(copy.id) (kind \(copy.kind)) flies here");
        if copy.ended {
          this.Finish(copy);
        }
        return true;
      }
    }
    return false;
  }

  private func Adopt(copy: ref<TandemThrowCopy>, object: ref<ItemObject>) -> Void {
    copy.object = object;
    while ArraySize(this.m_copyIds) >= 64 {
      ArrayErase(this.m_copyIds, 0);
    }
    ArrayPush(this.m_copyIds, object.GetEntityID());
  }

  // The real one's end, on its copy.
  private func Finish(copy: ref<TandemThrowCopy>, opt how: Int32) -> Void {
    let object = copy.object;
    if !IsDefined(object) {
      return;
    }
    let end = copy.ended ? copy.endHow : how;
    let at = copy.ended ? copy.endAt : object.GetWorldPosition();
    let grenade = object as BaseGrenade;
    let knife = object as MeleeProjectile;
    let round = object as ProjectileLauncherRound;
    if IsDefined(grenade) {
      grenade.TandemEnd(end, at);
    } else if IsDefined(knife) {
      knife.TandemEnd(end, at);
    } else if IsDefined(round) {
      round.TandemEnd(end, at);
    }
    this.Once(n"finish", s"throws: player \(copy.player)'s throw \(copy.id) ended here (\(end))");
  }

  // A grenade copy's own fuse waits for the real one's end.
  public func HoldsFuse(grenade: ref<BaseGrenade>) -> Bool {
    return this.IsCopy(grenade);
  }

  public static func At(position: Vector4) -> ref<IPositionProvider> {
    let world: WorldPosition; // assigned on every path: filled whole by SetVector4
    WorldPosition.SetVector4(world, position);
    return IPositionProvider.CreateStaticPositionProvider(world);
  }

  public static func Towards(velocity: Vector4) -> ref<IOrientationProvider> {
    let way = Vector4.Length(velocity) > 0.01 ? Vector4.Normalize(velocity) : new Vector4(0.0, 1.0, 0.0, 0.0);
    return IOrientationProvider.CreateStaticOrientationProvider(Quaternion.BuildFromDirectionVector(way));
  }

  // A knife's or a round's projectile left its item: ours (the record the
  // others spawn it from: the knife, with its projectile template; the
  // launcher's round), or a puppet's copy to bind.
  public static func OnProjectileShot(object: ref<ItemObject>, eventData: ref<gameprojectileShootEvent>, kind: Int32) -> Void {
    let throws = TandemThrows.Of(object.GetGame());
    if !IsDefined(throws) {
      return;
    }
    if TandemThrows.IsOurs(object.GetGame(), eventData.owner) {
      let item = TDBID.None();
      let round = object as ProjectileLauncherRound;
      if IsDefined(round) {
        item = round.TandemInstalledRound();
      } else {
        let weapon = eventData.weapon as ItemObject;
        if IsDefined(weapon) {
          item = ItemID.GetTDBID(weapon.GetItemID());
        }
      }
      let target: wref<GameObject>;
      if IsDefined(eventData.params.trackedTargetComponent) {
        target = eventData.params.trackedTargetComponent.GetEntity() as GameObject;
      }
      throws.OnOurThrow(object, kind, 0, item, eventData.startPoint, eventData.startVelocity, target);
    } else if TandemCombat.PuppetPlayer(object.GetGame(), eventData.owner) != 0u {
      throws.Bind(object, eventData.owner);
    }
  }

  // test reset: each once-only line may be said again.
  public func ResayForTests() -> Void {
    ArrayClear(this.m_logged);
  }

  public func End() -> Void {
    ArrayClear(this.m_ours);
    ArrayClear(this.m_ourIds);
    ArrayClear(this.m_ourStuck);
    ArrayClear(this.m_ourTracks);
    ArrayClear(this.m_measuring);
    ArrayClear(this.m_measuringObject);
    ArrayClear(this.m_copies);
    ArrayClear(this.m_copyIds);
    ArrayClear(this.m_logged);
  }
}

// ---- the objects' own scripts: what each says and how a copy ends ----

// A grenade leaves a hand (OnShootTarget calls this too): ours, our NPC's
// (host), or a copy to bind.
@wrapMethod(BaseGrenade)
protected cb func OnShoot(eventData: ref<gameprojectileShootEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.BaseGrenade.OnShoot");
  let result = wrappedMethod(eventData);
  let throws = TandemThrows.Of(this.GetGame());
  if !IsDefined(throws) {
    return result;
  }
  if throws.HoldsFuse(this) {
    // It explodes when and where the real one did (ThrowEnd).
    this.m_detonationTimerActive = false;
    return result;
  }
  let flags = 0;
  if Equals(this.GetDeliveryMethod(), gamedataGrenadeDeliveryMethodType.Sticky) {
    flags += TandemThrows.StickyFlag();
  }
  if Equals(this.GetDeliveryMethod(), gamedataGrenadeDeliveryMethodType.Homing) {
    flags += TandemThrows.HomingFlag();
  }
  let item = ItemID.GetTDBID(this.GetItemID());
  if TandemThrows.IsOurs(this.GetGame(), eventData.owner) {
    let target: wref<GameObject>;
    if IsDefined(eventData.params.trackedTargetComponent) {
      target = eventData.params.trackedTargetComponent.GetEntity() as GameObject;
    }
    throws.OnOurThrow(this, TandemThrows.GrenadeKind(), flags, item, eventData.startPoint, eventData.startVelocity, target);
  } else {
    // An NPC we run (the host's, or a guest's own: regions.md).
    let npc = TandemThrows.NpcOf(this.GetGame(), eventData.owner);
    if npc != 0u && TandemOwn.Npc(this.GetGame(), npc) {
      throws.OnNpcThrow(this, npc, item, eventData.startPoint, eventData.startVelocity, flags);
    }
  }
  return result;
}

// A grenade bounced, on whatever thread: its place and moment queued, our
// own said on the tick (TandemThrows.TakeBounces) as an extra path sample.
@wrapMethod(BaseGrenade)
protected cb func OnCollision(eventData: ref<gameprojectileHitEvent>) -> Bool {
  let result = wrappedMethod(eventData);
  if TandemNative.IsStarted() && TandemNative.You() != 0u && IsDefined(eventData) && ArraySize(eventData.hitInstances) > 0 && this.m_isAlive {
    let user = this.GetUser();
    if IsDefined(user) && user.IsPlayer() {
      let hit = eventData.hitInstances[0];
      let at = Vector4.IsZero(hit.projectilePosition) ? hit.position : hit.projectilePosition;
      let time = Cast<Int32>(EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGame())) * 1000.0);
      TandemStreaming.QueuePoint(TandemPointKind.ThrowBounce(), this.GetEntityID(), EntityID.FromHash(0ul), n"None", at, time);
    }
  }
  return result;
}

@wrapMethod(BaseGrenade)
protected func Detonate(opt hitNormal: Vector4) {
  TandemNative.HookRan(n"Throws.BaseGrenade.Detonate");
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && !this.m_hasExploded && throws.IsOurObject(this) {
    throws.OnOurEnd(this, 0);
  }
  wrappedMethod(hitNormal);
}

@wrapMethod(BaseGrenade)
protected final func Release(opt isInstant: Bool) {
  TandemNative.HookRan(n"Throws.BaseGrenade.Release");
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && this.m_isAlive && throws.IsOurObject(this) {
    throws.OnOurEnd(this, 2);
  }
  wrappedMethod(isInstant);
}

// A copy that strayed from the real one's path: put where the real one is,
// flying on from there the real one's way (a parabola of its speed, as it
// was launched: TandemThrows.LaunchGrenade), or lying still.
@addMethod(BaseGrenade)
public func TandemFollow(at: Vector4, velocity: Vector4, gravity: Float, flying: Bool) -> Void {
  if !this.m_isAlive {
    return;
  }
  let speed = Vector4.Length(velocity);
  let way = speed > 0.01 ? velocity / speed : this.GetWorldForward();
  let facing = Quaternion.BuildFromDirectionVector(way);
  this.m_projectileComponent.ClearTrajectories();
  GameInstance.GetTeleportationFacility(this.GetGame()).Teleport(this, at, Quaternion.ToEulerAngles(facing));
  if !flying {
    return;
  }
  // The trajectory starts from the component's desired transform
  // (ProjectileComponent.SetDesiredTransform, as a homing miss does).
  let from: Transform; // assigned on every path: position and orientation set below
  Transform.SetPosition(from, at);
  Transform.SetOrientation(from, facing);
  this.m_projectileComponent.SetDesiredTransform(from);
  this.m_projectileComponent.AddParabolic(ParabolicTrajectoryParams.GetAccelVelParabolicParams(new Vector4(0.0, 0.0, gravity, 0.0), speed));
}

// A copy's end: where the real one ended, as it ended.
@addMethod(BaseGrenade)
public func TandemEnd(how: Int32, at: Vector4) -> Void {
  if !this.m_isAlive {
    return;
  }
  this.m_projectileComponent.ClearTrajectories();
  GameInstance.GetTeleportationFacility(this.GetGame()).Teleport(this, at, new EulerAngles());
  if how == 0 {
    this.Detonate(this.lastHitNormal);
  } else if how == 2 {
    this.Release(true);
  }
}

// A thrown knife or axe leaves the hand, straight or at a target.
@wrapMethod(MeleeProjectile)
protected cb func OnShoot(eventData: ref<gameprojectileShootEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.MeleeProjectile.OnShoot");
  let result = wrappedMethod(eventData);
  TandemThrows.OnProjectileShot(this, eventData, TandemThrows.KnifeKind());
  return result;
}

@wrapMethod(MeleeProjectile)
protected cb func OnShootTarget(eventData: ref<gameprojectileShootTargetEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.MeleeProjectile.OnShootTarget");
  let result = wrappedMethod(eventData);
  TandemThrows.OnProjectileShot(this, eventData, TandemThrows.KnifeKind());
  return result;
}

// Stuck in a wall or a body: said once, where.
@wrapMethod(MeleeProjectile)
protected cb func OnCollision(eventData: ref<gameprojectileHitEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.MeleeProjectile.OnCollision");
  let result = wrappedMethod(eventData);
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) {
    if throws.IsOurObject(this) {
      throws.OnOurEnd(this, 1);
    } else if throws.IsCopy(this) {
      // Not ours to pick up: no prompt.
      let off = new InteractionSetEnableEvent();
      off.enable = false;
      this.QueueEvent(off);
    }
  }
  return result;
}

// The owner picks it up (or its time is up): the copies go.
@wrapMethod(MeleeProjectile)
public final func TryToReleaseProjectile() {
  TandemNative.HookRan(n"Throws.MeleeProjectile.TryToReleaseProjectile");
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && !this.m_wasPicked && throws.IsOurObject(this) {
    throws.OnOurEnd(this, 2);
  }
  wrappedMethod();
}

// A copy is never picked up: the real one is its owner's, in their world.
@wrapMethod(MeleeProjectile)
protected cb func OnInteractionActivationEvent(evt: ref<InteractionActivationEvent>) -> Bool {
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && throws.IsCopy(this) {
    return false;
  }
  return wrappedMethod(evt);
}

@addMethod(MeleeProjectile)
public func TandemEnd(how: Int32, at: Vector4) -> Void {
  if how == 2 {
    this.TryToReleaseProjectile();
    return;
  }
  this.m_projectileComponent.ClearTrajectories();
  GameInstance.GetTeleportationFacility(this.GetGame()).Teleport(this, at, new EulerAngles());
}

// An arm launcher round, straight or tracking.
@wrapMethod(ProjectileLauncherRound)
protected cb func OnShoot(eventData: ref<gameprojectileShootEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.ProjectileLauncherRound.OnShoot");
  let result = wrappedMethod(eventData);
  TandemThrows.OnProjectileShot(this, eventData, TandemThrows.LauncherKind());
  return result;
}

@wrapMethod(ProjectileLauncherRound)
protected cb func OnShootTarget(eventData: ref<gameprojectileShootTargetEvent>) -> Bool {
  TandemNative.HookRan(n"Throws.ProjectileLauncherRound.OnShootTarget");
  let result = wrappedMethod(eventData);
  TandemThrows.OnProjectileShot(this, eventData, TandemThrows.LauncherKind());
  return result;
}

@wrapMethod(ProjectileLauncherRound)
protected final func ExecuteGameEffect(projectilePosition: Vector4) {
  TandemNative.HookRan(n"Throws.ProjectileLauncherRound.ExecuteGameEffect");
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && throws.IsOurObject(this) {
    throws.OnOurEnd(this, 0);
  }
  wrappedMethod(projectilePosition);
}

@wrapMethod(ProjectileLauncherRound)
protected final func Release() {
  TandemNative.HookRan(n"Throws.ProjectileLauncherRound.Release");
  let throws = TandemThrows.Of(this.GetGame());
  if IsDefined(throws) && this.m_isAlive && throws.IsOurObject(this) {
    throws.OnOurEnd(this, 2);
  }
  wrappedMethod();
}

@addMethod(ProjectileLauncherRound)
public func TandemEnd(how: Int32, at: Vector4) -> Void {
  if !this.m_isAlive {
    return;
  }
  if how == 0 {
    this.ExecuteGameEffect(at);
  }
  this.Release();
}

@addMethod(ProjectileLauncherRound)
public func TandemInstalledRound() -> TweakDBID {
  return ItemID.GetTDBID(this.m_installedProjectile);
}
