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
  private let m_waiting: array<TandemShotAt>;
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
  private let m_handed: Bool;
  // The proxy last given the shot's weapon, and when: given once, then its
  // spawn is waited for (giving again every frame removed the one still
  // spawning: RemoveItemFromSlot destroys a pending spawn).
  private let m_givenTo: EntityID;
  private let m_givenAt: Float;
  // Guest: why the last shot is still waiting, and the shots fired.
  private let m_waitingFor: String;
  private let m_fired: Int32;
  // Guest: the proxies that fired at us lately, and when: their bullets'
  // hits on our V are ours to apply (IsSimulatedHit), whatever flight time.
  private let m_recentBy: array<EntityID>;
  private let m_recentAt: array<Float>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_givenTo = EntityID.FromHash(0ul);
    this.m_givenAt = 0.0;
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

  // Every frame of the session (TandemTime.Update).
  public func Tick() -> Void {
    let now = this.Now();
    // The NPCs we run fire at other Vs; the proxies we show fire at ours.
    this.Prune(now);
    this.ShowReports();
    this.FireIncoming(now);
    this.FinishCatchUps(now);
  }

  // ---- host ----

  // AIWeapon.Fire is about to run on the host (NpcAnims.reds' wrap): at a
  // guest's puppet, the shot goes to that guest's game instead of deciding
  // the hit here. The host still fires it, for its own view (the flash, the
  // sound, the NPC's ammo and timing): its hit on the puppet is dropped.
  public func Announce(owner: wref<GameObject>, weapon: wref<WeaponObject>, target: ref<GameObject>, tbh: Float, trigger: gamedataTriggerMode, attack: TweakDBID) -> Void {
    if !IsDefined(owner) || !IsDefined(weapon) || !IsDefined(target) || !TandemOwn.Entity(this.m_game, owner.GetEntityID()) {
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
    TandemNative.SendShotAt(guest, npc, attack, ItemID.GetTDBID(weapon.GetItemID()), tbh, EnumInt(trigger), factor);
    ArrayPush(this.m_announcedBy, owner.GetEntityID());
    ArrayPush(this.m_announcedAt, guest);
    ArrayPush(this.m_announcedTime, this.Now());
    this.Once(n"announce", s"shots: npc \(npc)'s shots at player \(guest) are fired and decided on their game (first at speed \(factor))");
  }

  // A hit on a guest's puppet from a shot that guest's game simulates: not
  // decided here (ProcessPipeline, Combat.reds). Our own report-driven hit
  // reaction carries DealNoDamage and passes.
  public func SimulatedByGuest(hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hitEvent) || !IsDefined(hitEvent.target) || hitEvent.attackData.HasFlag(hitFlag.DealNoDamage) {
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
      if IsDefined(body) && IsDefined(shooter) && !body.IsDead() {
        TandemShots.ShowHit(body, shooter, new Vector4(hit.x, hit.y, hit.z, 1.0), new Vector4(hit.dx, hit.dy, hit.dz, 0.0));
      }
      this.Once(n"report", s"shots: player \(hit.player) reports npc \(hit.npc)'s shot hit their V (shown on the puppet: \(IsDefined(body) && IsDefined(shooter)))");
    }
  }

  // A bullet's hit on the puppet as the game shows one (its hit reaction,
  // sounds, effects), dealing nothing: no damage, no floater. Our own
  // pipeline passes it (it carries DealNoDamage: SimulatedByGuest).
  public static func ShowHit(body: ref<ScriptedPuppet>, shooter: ref<GameObject>, at: Vector4, way: Vector4) -> Void {
    let context: AttackInitContext;
    context.record = TweakDBInterface.GetAttackRecord(t"Attacks.REMOVE_BulletWithDamage");
    context.instigator = shooter;
    context.source = shooter;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = body;
    hit.hitPosition = at;
    hit.hitDirection = way;
    hit.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_shot_report");
    hit.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_shot_report");
    hit.attackData.SetSource(shooter);
    hit.attackData.SetInstigator(shooter);
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    GameInstance.GetDamageSystem(body.GetGame()).QueueHitEvent(hit, body);
  }

  // ---- guest ----

  private func FireIncoming(now: Float) -> Void {
    for shot in TandemNative.TakeShotsAt() {
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
        // Its proxy is spawning (bodies take a couple of seconds) or its
        // weapon is being put in its hand: up to 3 s.
        if now - since[i] < 3.0 {
          ArrayPush(this.m_waiting, waiting[i]);
          ArrayPush(this.m_waitingSince, since[i]);
        } else {
          TandemNative.Log(s"shots: npc \(waiting[i].npc)'s shot at us lost (waited 3 s: \(this.m_waitingFor))");
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
  private func Fire(shot: TandemShotAt, now: Float) -> Bool {
    let system = this.System();
    let npcs = IsDefined(system) ? system.NpcsSync() : null;
    let player = GetPlayer(this.m_game);
    if !IsDefined(npcs) || !IsDefined(player) {
      this.m_waitingFor = "no session";
      return false;
    }
    let proxy = GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(shot.npc)) as ScriptedPuppet;
    if !IsDefined(proxy) {
      this.m_waitingFor = "no proxy";
      return false;
    }
    let slot = t"AttachmentSlots.WeaponRight";
    let weapon = GameInstance.GetTransactionSystem(this.m_game).GetItemInSlot(proxy, slot) as WeaponObject;
    // NPC anims (NpcAnims.reds) puts the host's weapon in the proxy's hand
    // from the NPC's own animation state: that one is used as it is (never
    // swapped here, the two would undo each other). Only an empty hand gets
    // the shot's weapon.
    if !IsDefined(weapon) && TDBID.IsValid(shot.weapon) {
      if !this.m_handed {
        this.m_handed = true;
        TandemNative.LogDebug(s"shots: npc \(shot.npc)'s proxy has nothing in its hand: given \(TDBID.ToStringDEBUG(shot.weapon))");
      }
      if proxy.GetEntityID() != this.m_givenTo || now - this.m_givenAt > 2.0 || now < this.m_givenAt {
        this.m_givenTo = proxy.GetEntityID();
        this.m_givenAt = now;
        TandemWeapons.Hold(proxy, slot, shot.weapon);
      }
      this.m_waitingFor = "an empty hand";
      return false;
    }
    if !IsDefined(weapon) {
      this.m_waitingFor = "no weapon given";
      return false;
    }
    if !weapon.IsAttached() {
      this.m_waitingFor = s"its \(TDBID.ToStringDEBUG(ItemID.GetTDBID(weapon.GetItemID()))) not attached";
      return false;
    }
    // The host's NPC is shooting at us: so is its proxy here, whatever its
    // group says (a friendly attacker's hits are dropped, CheckForQuickExit).
    let agent = proxy.GetAttitudeAgent();
    if IsDefined(agent) && IsDefined(player.GetAttitudeAgent()) && NotEquals(agent.GetAttitudeTowards(player.GetAttitudeAgent()), EAIAttitude.AIA_Hostile) {
      agent.SetAttitudeTowards(player.GetAttitudeAgent(), EAIAttitude.AIA_Hostile);
    }
    let aim = new Vector4(0.0, 0.0, 0.0, 0.0);
    let head = IPositionProvider.CreateSlotPositionProvider(player, n"Head");
    if !IsDefined(head) || !head.CalculatePosition(aim) {
      aim = player.GetWorldPosition();
      aim.Z += 1.6;
    }
    aim.Z -= 0.15;
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let weaponStats = Cast<StatsObjectID>(weapon.GetEntityID());
    let maxSpread = stats.GetStatValue(weaponStats, gamedataStatType.SpreadMaxAI);
    let offset = new Vector4(0.0, 0.0, 0.0, 0.0);
    let target = player.GetTargetShootComponent();
    if IsDefined(target) {
      offset = target.HandleBeingShot(proxy, weapon, aim, maxSpread, shot.tbh);
    }
    let params: gameprojectileWeaponParams;
    params.hitPlaneOffset = offset;
    params.shootingOffset = stats.GetStatValue(weaponStats, gamedataStatType.ShootingOffsetAI);
    // Aimed at our V: the other Vs' puppets here do not catch it (on the
    // host's game the NPC's shot at us passed them by too).
    for other in system.PuppetPlayers() {
      let puppet = system.PuppetOf(other);
      if IsDefined(puppet) {
        gameprojectileWeaponParams.AddObjectToIgnoreCollisionWith(params, puppet.GetEntityID());
      }
    }
    let perShot = Cast<Int32>(stats.GetStatValue(weaponStats, gamedataStatType.ProjectilesPerShot));
    let at: WorldPosition; // assigned on every path: filled whole by WorldPosition.SetVector4 at once
    WorldPosition.SetVector4(at, aim + offset);
    ArrayPush(this.m_firedBy, proxy.GetEntityID());
    ArrayPush(this.m_firedFactor, shot.factor);
    ArrayPush(this.m_firedBoost, shot.boost);
    ArrayPush(this.m_firedBoostSeconds, shot.boostSeconds);
    ArrayPush(this.m_firedTime, now);
    let recent = ArrayFindFirst(this.m_recentBy, proxy.GetEntityID());
    if recent >= 0 {
      this.m_recentAt[recent] = now;
    } else {
      ArrayPush(this.m_recentBy, proxy.GetEntityID());
      ArrayPush(this.m_recentAt, now);
    }
    // The attack it fires: the host's, as a slowed one is (Time.reds).
    AIWeapon.SetAttackBasedOnTimeDilation(proxy, weapon, shot.attack);
    weapon.AI_ShootAt(IPositionProvider.CreateStaticPositionProvider(at), null, proxy, Cast<Uint16>(0), params, Cast<Uint8>(Max(perShot, 1)), 0.0, maxSpread);
    WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot);
    AnimationControllerComponent.PushEventToReplicate(weapon, n"Shoot");
    this.m_fired += 1;
    // The first of each session, then every tenth.
    if this.m_fired == 1 || this.m_fired % 10 == 0 {
      TandemNative.LogDebug(s"shots: npc \(shot.npc)'s shot at us fired from its proxy (\(this.m_fired) so far; speed \(shot.factor), catching up x\(shot.boost) for \(shot.boostSeconds) s)");
    }
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
    if !IsDefined(bullet) || !IsDefined(shooter) {
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

  // A hit on our V from a shot this game fired from a proxy (any shot of a
  // proxy that fired at us in the last 3 s: a slowed bullet flies long):
  // ours to apply, not a proxy's own hit to drop (TandemOutcomes
  // .IsProxyComputed).
  public func IsSimulatedHit(hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) {
      return false;
    }
    if !AttackData.IsRangedOrDirect(hitEvent.attackData.GetAttackType()) {
      return false;
    }
    let from = hitEvent.attackData.GetInstigator();
    if !IsDefined(from) {
      return false;
    }
    let now = this.Now();
    let i = 0;
    while i < ArraySize(this.m_recentBy) {
      if now - this.m_recentAt[i] > 3.0 {
        ArrayErase(this.m_recentBy, i);
        ArrayErase(this.m_recentAt, i);
      } else if this.m_recentBy[i] == from.GetEntityID() {
        return true;
      } else {
        i += 1;
      }
    }
    return false;
  }

  // A hit on our V from a proxy's shot (DealDamages, Combat.reds): the host
  // shows it on our puppet.
  public func Report(hitEvent: ref<gameHitEvent>) -> Void {
    if !IsDefined(hitEvent) || hitEvent.target != GetPlayer(this.m_game) {
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
    // A new session logs its firsts again.
    ArrayClear(this.m_logged);
    this.m_handed = false;
    this.m_fired = 0;
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
    ArrayClear(this.m_recentBy);
    ArrayClear(this.m_recentAt);
  }
}
