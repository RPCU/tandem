// NPC shots at a guest's V (docs/ARCHITECTURE.md, authority table: "NPC
// shots at a V"). The host's NPC decides to fire: when, at whom, with which
// weapon and attack, and its AI's time-between-hits coefficient. The guest's
// game fires that shot from its proxy of the NPC at its own V and simulates
// it: its own miss rules (TargetShootComponent.HandleBeingShot, the
// player-side "time between hits", final.reds:173757), the weapon's spread,
// the bullet or the slow projectile (under a time bubble or its own
// Sandevistan) that its V can see and dodge, and the hit on its V with the
// damage split's target side as for any hit. So dodging under slow motion is
// single player's. The host no longer decides those hits (SimulatedByGuest)
// and shows the guest's report on the puppet (Reported).
//
// Melee and everything that is not shot (explosions, area attacks) stays
// the host's, sent as PlayerHit (Combat.reds). Cheating is out of scope:
// players trust each other's game in co-op among friends.

public class TandemShots {
  private let m_game: GameInstance;
  // Host: the shots announced to guests, so their hits on the puppet are
  // left to that guest (shooter, guest, when).
  private let m_announcedBy: array<EntityID>;
  private let m_announcedAt: array<Uint32>;
  private let m_announcedTime: array<Float>;
  // Guest: shots waiting for their proxy's weapon to be in its hand.
  private let m_waiting: array<TandemNpcShot>;
  private let m_waitingSince: array<Float>;
  // Guest: the time factor and catch-up of shots fired from a proxy just
  // now (their projectiles take it as they are set up), and projectiles
  // still catching up.
  private let m_firedBy: array<EntityID>;
  private let m_firedFactor: array<Float>;
  private let m_firedBoost: array<Float>;
  private let m_firedBoostSeconds: array<Float>;
  private let m_firedTime: array<Float>;
  private let m_boosted: array<wref<ItemObject>>;
  private let m_boostUntil: array<Float>;
  private let m_boostFactor: array<Float>;
  private let m_logged: array<CName>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public static func Of(game: GameInstance) -> ref<TandemShots> {
    let time = TandemTime.Of(game);
    return IsDefined(time) ? time.Shots() : null;
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

  // Every frame of the session (TandemTime.Update).
  public func Tick() -> Void {
    let now = this.Now();
    if TandemNative.IsHost() {
      this.Prune(now);
      this.ShowReports();
    } else {
      this.FireIncoming(now);
      this.FinishCatchUps(now);
    }
  }

  // ---- host ----

  // AIWeapon.Fire is about to run on the host (the wrap below): at a
  // guest's puppet, the shot goes to that guest's game instead of deciding
  // the hit here. The host still fires it, for its own view (the flash, the
  // sound, the NPC's ammo and timing): its hit on the puppet is dropped.
  public func Announce(owner: wref<GameObject>, weapon: wref<WeaponObject>, target: ref<GameObject>, tbh: Float, trigger: gamedataTriggerMode, attack: TweakDBID) -> Void {
    if !TandemNative.IsHost() || !IsDefined(owner) || !IsDefined(weapon) || !IsDefined(target) {
      return;
    }
    let system = this.System();
    if !IsDefined(system) {
      return;
    }
    let guest = system.PlayerOfPuppet(target.GetEntityID());
    let npc = system.SharedNpcOf(owner.GetEntityID());
    // An NPC the guests have no proxy of: the host decides, as before.
    if guest == 0u || npc == 0u {
      return;
    }
    let time = TandemTime.Of(this.m_game);
    let factor = IsDefined(time) ? time.AppliedFactor(owner.GetEntityID()) : -1.0;
    if factor < 0.0 {
      factor = 1.0;
    }
    TandemNative.SendNpcShot(guest, npc, attack, ItemID.GetTDBID(weapon.GetItemID()), tbh, EnumInt(trigger), factor);
    ArrayPush(this.m_announcedBy, owner.GetEntityID());
    ArrayPush(this.m_announcedAt, guest);
    ArrayPush(this.m_announcedTime, this.Now());
    this.Once(n"announce", s"shots: npc \(npc)'s shots at player \(guest) are fired and decided on their game (first at speed \(factor))");
  }

  // A hit on a guest's puppet from a shot that guest's game simulates: not
  // decided here (ProcessPipeline, Combat.reds). Our own report-driven hit
  // reaction carries DealNoDamage and passes.
  public func SimulatedByGuest(hitEvent: ref<gameHitEvent>) -> Bool {
    if !TandemNative.IsHost() || !IsDefined(hitEvent) || !IsDefined(hitEvent.target) || hitEvent.attackData.HasFlag(hitFlag.DealNoDamage) {
      return false;
    }
    if !AttackData.IsRangedOrDirect(hitEvent.attackData.GetAttackType()) {
      return false;
    }
    let from = hitEvent.attackData.GetInstigator();
    let system = this.System();
    if !IsDefined(from) || !IsDefined(system) {
      return false;
    }
    let guest = system.PlayerOfPuppet(hitEvent.target.GetEntityID());
    if guest == 0u {
      return false;
    }
    let id = from.GetEntityID();
    let i = 0;
    while i < ArraySize(this.m_announcedBy) {
      if this.m_announcedBy[i] == id && this.m_announcedAt[i] == guest {
        this.Once(n"dropped", s"shots: the host's own hit of a shot at player \(guest) is left to their game");
        return true;
      }
      i += 1;
    }
    return false;
  }

  // Announcements older than any shot's flight (3 s at a bubble's speed
  // covers 150 m at 0.25 for the slowest NPC projectiles).
  private func Prune(now: Float) -> Void {
    while ArraySize(this.m_announcedTime) > 0 && (now - this.m_announcedTime[0] > 3.0 || ArraySize(this.m_announcedTime) > 128) {
      ArrayErase(this.m_announcedBy, 0);
      ArrayErase(this.m_announcedAt, 0);
      ArrayErase(this.m_announcedTime, 0);
    }
  }

  // The guests' reports: the game's own hit reaction on the puppet, from
  // the NPC that shot, dealing nothing (the damage was the guest's).
  private func ShowReports() -> Void {
    let system = this.System();
    if !IsDefined(system) {
      return;
    }
    for hit in TandemNative.TakeShotHits() {
      let body = system.PuppetOf(hit.player) as ScriptedPuppet;
      let npcs = system.NpcsSync();
      let shooter = IsDefined(npcs) ? GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(hit.npc)) as GameObject : null;
      if IsDefined(body) && IsDefined(shooter) {
        let event: TandemNpcEvent;
        event.x = hit.x;
        event.y = hit.y;
        event.z = hit.z;
        event.dx = hit.dx;
        event.dy = hit.dy;
        event.dz = hit.dz;
        TandemNpcs.React(body, shooter, event);
      }
      this.Once(n"report", s"shots: player \(hit.player) reports npc \(hit.npc)'s shot hit their V (shown on the puppet: \(IsDefined(body) && IsDefined(shooter)))");
    }
  }

  // ---- guest ----

  private func FireIncoming(now: Float) -> Void {
    for shot in TandemNative.TakeNpcShots() {
      ArrayPush(this.m_waiting, shot);
      ArrayPush(this.m_waitingSince, now);
    }
    let waiting = this.m_waiting;
    let since = this.m_waitingSince;
    ArrayClear(this.m_waiting);
    ArrayClear(this.m_waitingSince);
    let i = 0;
    while i < ArraySize(waiting) {
      if !this.Fire(waiting[i], now) {
        // Its weapon is being put in the proxy's hand: half a second at most.
        if now - since[i] < 0.5 {
          ArrayPush(this.m_waiting, waiting[i]);
          ArrayPush(this.m_waitingSince, since[i]);
        } else {
          this.Once(n"lost", s"shots: npc \(waiting[i].npc)'s shot at us lost (no proxy or no weapon in its hand)");
        }
      }
      i += 1;
    }
    // Fired just now: their projectiles have been set up.
    while ArraySize(this.m_firedTime) > 0 && now - this.m_firedTime[0] > 0.5 {
      ArrayErase(this.m_firedBy, 0);
      ArrayErase(this.m_firedFactor, 0);
      ArrayErase(this.m_firedBoost, 0);
      ArrayErase(this.m_firedBoostSeconds, 0);
      ArrayErase(this.m_firedTime, 0);
    }
  }

  // The shot, from the proxy's weapon at our V, as AIWeapon.Fire aims one at
  // a player (final.reds:173195-173307): the head, 15 cm lower, the
  // target's miss offset, the weapon's AI spread. No ammo is spent on the
  // proxy (the host's NPC has the magazine). False: not ready yet.
  private func Fire(shot: TandemNpcShot, now: Float) -> Bool {
    let system = this.System();
    let npcs = IsDefined(system) ? system.NpcsSync() : null;
    let player = GetPlayer(this.m_game);
    if !IsDefined(npcs) || !IsDefined(player) {
      return true;
    }
    let proxy = GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(shot.npc)) as ScriptedPuppet;
    if !IsDefined(proxy) {
      return false;
    }
    let slot = t"AttachmentSlots.WeaponRight";
    let weapon = GameInstance.GetTransactionSystem(this.m_game).GetItemInSlot(proxy, slot) as WeaponObject;
    if TDBID.IsValid(shot.weapon) && (!IsDefined(weapon) || ItemID.GetTDBID(weapon.GetItemID()) != shot.weapon) {
      TandemWeapons.Hold(proxy, slot, shot.weapon);
      return false;
    }
    if !IsDefined(weapon) || !weapon.IsAttached() {
      return false;
    }
    let aim: Vector4;
    let head = IPositionProvider.CreateSlotPositionProvider(player, n"Head");
    if !IsDefined(head) || !head.CalculatePosition(aim) {
      aim = player.GetWorldPosition();
      aim.Z += 1.6;
    }
    aim.Z -= 0.15;
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let weaponStats = Cast<StatsObjectID>(weapon.GetEntityID());
    let maxSpread = stats.GetStatValue(weaponStats, gamedataStatType.SpreadMaxAI);
    let offset: Vector4;
    let target = player.GetTargetShootComponent();
    if IsDefined(target) {
      offset = target.HandleBeingShot(proxy, weapon, aim, maxSpread, shot.tbh);
    }
    let params: gameprojectileWeaponParams;
    params.hitPlaneOffset = offset;
    params.shootingOffset = stats.GetStatValue(weaponStats, gamedataStatType.ShootingOffsetAI);
    let perShot = Cast<Int32>(stats.GetStatValue(weaponStats, gamedataStatType.ProjectilesPerShot));
    let at: WorldPosition;
    WorldPosition.SetVector4(at, aim + offset);
    ArrayPush(this.m_firedBy, proxy.GetEntityID());
    ArrayPush(this.m_firedFactor, shot.factor);
    ArrayPush(this.m_firedBoost, shot.boost);
    ArrayPush(this.m_firedBoostSeconds, shot.boostSeconds);
    ArrayPush(this.m_firedTime, now);
    // The attack it fires: the host's, as a slowed one is (Time.reds).
    AIWeapon.SetAttackBasedOnTimeDilation(proxy, weapon, shot.attack);
    weapon.AI_ShootAt(IPositionProvider.CreateStaticPositionProvider(at), null, proxy, Cast<Uint16>(0), params, Cast<Uint8>(Max(perShot, 1)), 0.0, maxSpread);
    WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot);
    AnimationControllerComponent.PushEventToReplicate(weapon, n"Shoot");
    this.Once(n"fired", s"shots: npc \(shot.npc)'s shot at us fired from its proxy (speed \(shot.factor), catching up x\(shot.boost) for \(shot.boostSeconds) s)");
    return true;
  }

  // The time factor of a shot a proxy just fired (-1: none): Time.reds'
  // projectile and attack choices read it on a guest.
  public func FiredFactor(shooter: EntityID) -> Float {
    let i = ArraySize(this.m_firedBy) - 1;
    while i >= 0 {
      if this.m_firedBy[i] == shooter {
        return this.m_firedFactor[i];
      }
      i -= 1;
    }
    return -1.0;
  }

  // A projectile from a proxy's shot is set up: at the shot's speed, faster
  // for a moment so it flies where the host's timeline has it.
  public func SetUp(bullet: ref<ItemObject>, shooter: wref<GameObject>) -> Bool {
    if TandemNative.IsHost() || !IsDefined(bullet) || !IsDefined(shooter) {
      return false;
    }
    let i = ArraySize(this.m_firedBy) - 1;
    while i >= 0 && this.m_firedBy[i] != shooter.GetEntityID() {
      i -= 1;
    }
    if i < 0 {
      return false;
    }
    let factor = this.m_firedFactor[i];
    let boost = this.m_firedBoost[i];
    if factor >= 0.999 && boost <= 1.0 {
      return true; // at the game's own speed (its own dilation, if any)
    }
    bullet.SetIndividualTimeDilation(n"tandemTime", factor * boost, 30.0, n"None", n"None", true, false);
    if boost > 1.0 {
      ArrayPush(this.m_boosted, bullet);
      ArrayPush(this.m_boostUntil, this.Now() + this.m_firedBoostSeconds[i]);
      ArrayPush(this.m_boostFactor, factor);
    }
    return true;
  }

  private func FinishCatchUps(now: Float) -> Void {
    let i = 0;
    while i < ArraySize(this.m_boosted) {
      if now >= this.m_boostUntil[i] {
        let bullet = this.m_boosted[i];
        if IsDefined(bullet) {
          bullet.SetIndividualTimeDilation(n"tandemTime", this.m_boostFactor[i], 30.0, n"None", n"None", true, false);
        }
        ArrayErase(this.m_boosted, i);
        ArrayErase(this.m_boostUntil, i);
        ArrayErase(this.m_boostFactor, i);
      } else {
        i += 1;
      }
    }
  }

  // A hit on our V from a proxy's shot (DealDamages, Combat.reds): the host
  // shows it on our puppet.
  public func Report(hitEvent: ref<gameHitEvent>) -> Void {
    if TandemNative.IsHost() || !IsDefined(hitEvent) || hitEvent.target != GetPlayer(this.m_game) {
      return;
    }
    let from = hitEvent.attackData.GetInstigator();
    let system = this.System();
    let npc = IsDefined(from) && IsDefined(system) ? system.NpcOfProxy(from.GetEntityID()) : 0u;
    if npc != 0u {
      TandemNative.SendShotHit(npc, hitEvent.hitPosition, hitEvent.hitDirection);
      this.Once(n"hit", s"shots: npc \(npc)'s shot hit our V (our game's decision), reported to the host");
    }
  }

  public func End() -> Void {
    ArrayClear(this.m_announcedBy);
    ArrayClear(this.m_announcedAt);
    ArrayClear(this.m_announcedTime);
    ArrayClear(this.m_waiting);
    ArrayClear(this.m_waitingSince);
    ArrayClear(this.m_firedBy);
    ArrayClear(this.m_firedFactor);
    ArrayClear(this.m_firedBoost);
    ArrayClear(this.m_firedBoostSeconds);
    ArrayClear(this.m_firedTime);
    ArrayClear(this.m_boosted);
    ArrayClear(this.m_boostUntil);
    ArrayClear(this.m_boostFactor);
  }
}

// Every NPC shot (final.reds:173136): at a guest's puppet, announced to that
// guest first. The shot itself runs unchanged.
@wrapMethod(AIWeapon)
public final static func Fire(weaponOwner: wref<GameObject>, weapon: wref<WeaponObject>, const timeStamp: Float, tbhCoefficient: Float, requestedTriggerMode: gamedataTriggerMode, opt targetPosition: Vector4, opt target: ref<GameObject>, opt rangedAttack: TweakDBID, opt maxSpreadOverride: Float, opt aimingDelay: Float, opt offset: Vector4, opt shouldTrackTarget: Bool, opt predictionTime: Float, opt posProviderOverride: ref<IPositionProvider>, opt muzzleOffset: Vector4, opt weaponCustomEvent: CName) -> Void {
  if IsDefined(weaponOwner) && IsDefined(target) && TandemNative.IsHost() {
    let shots = TandemShots.Of(weaponOwner.GetGame());
    if IsDefined(shots) {
      shots.Announce(weaponOwner, weapon, target, tbhCoefficient, requestedTriggerMode, rangedAttack);
    }
  }
  wrappedMethod(weaponOwner, weapon, timeStamp, tbhCoefficient, requestedTriggerMode, targetPosition, target, rangedAttack, maxSpreadOverride, aimingDelay, offset, shouldTrackTarget, predictionTime, posProviderOverride, muzzleOffset, weaponCustomEvent);
}
