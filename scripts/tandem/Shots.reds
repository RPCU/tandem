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
  // The proxies given the shot's weapon, and when: each given once, then
  // its spawn is waited for (giving again every frame removed the one still
  // spawning: RemoveItemFromSlot destroys a pending spawn). Per proxy: two
  // empty-handed ones firing at us each get theirs.
  private let m_givenTo: array<EntityID>;
  private let m_givenAt: array<Float>;
  // Guest: why the last shot is still waiting, and the shots fired.
  private let m_waitingFor: String;
  private let m_fired: Int32;
  // Of those, the smart guns' guided rounds (kShotAtTracked).
  private let m_guided: Int32;
  // Of those, the ones our game's time between hits let land (the rest
  // are aimed to miss).
  private let m_decidedHits: Int32;
  // Guest: the proxies that fired at us lately, and when: their bullets'
  // hits on our V are ours to apply (IsSimulatedHit), whatever flight time.
  private let m_recentBy: array<EntityID>;
  private let m_recentAt: array<Float>;

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
  // `tracked`: the AI fires a guided round (AIWeapon.Fire's
  // shouldTrackTarget: a smart weapon locked on the puppet, a missile rain):
  // the guest's proxy fires it guided at its V too.
  public func Announce(owner: wref<GameObject>, weapon: wref<WeaponObject>, target: ref<GameObject>, tbh: Float, trigger: gamedataTriggerMode, attack: TweakDBID, tracked: Bool) -> Void {
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
    // Whether it may land: the game's own time between hits, here on the
    // puppet, from the NPC's real line of sight (the guest's proxy has no
    // senses: there it reads 0, and every shot missed). 0 lets it land on
    // the guest's game, any other value has it aimed to miss there.
    // By a player's rules, as it would be at our own V: a puppet is no
    // player to the game's own (TandemShouldHitAsPlayer, below).
    let decided = MaxF(tbh, 1.0);
    let component = target.GetTargetShootComponent();
    let host = GetPlayer(this.m_game);
    if IsDefined(component) && IsDefined(host) {
      let group = 1.0;
      let cover = 1.0;
      if component.TandemShouldHitAsPlayer(owner, weapon, tbh, host, group, cover) {
        decided = 0.0;
      }
      this.Once(n"asPlayer", s"shots: shots at guests land by a V's time between hits (base \(TandemShots.Base(host)) for the puppet's \(TandemShots.Base(target)), with the group and cover coefficients)");
      if group != 1.0 || cover != 1.0 {
        this.Once(n"asPlayerFactors", s"shots: a shot at player \(guest): group x\(group), cover x\(cover) (a puppet had neither)");
      }
    } else {
      this.Once(n"noTbh", s"shots: player \(guest)'s puppet has no target shoot component: its game aims every shot to miss");
    }
    // proto::kShotAt*: 1 a smart weapon (its record's evolution), 2 guided.
    let flags = 0;
    if Equals(RPGManager.GetWeaponEvolution(weapon.GetItemID()), gamedataWeaponEvolution.Smart) {
      flags += 1;
    }
    if tracked {
      flags += 2;
      this.Once(n"announceGuided", s"shots: npc \(npc)'s smart rounds at player \(guest) are guided on their game (the host's AI tracks the target)");
    }
    TandemNative.SendShotAt(guest, npc, attack, ItemID.GetTDBID(weapon.GetItemID()), decided, EnumInt(trigger), factor, flags);
    ArrayPush(this.m_announcedBy, owner.GetEntityID());
    ArrayPush(this.m_announcedAt, guest);
    ArrayPush(this.m_announcedTime, this.Now());
    this.Once(n"announce", s"shots: npc \(npc)'s shots at player \(guest) are fired and decided on their game (first at speed \(factor))");
  }

  // The time between hits' base coefficient of a target (TBHsBaseCoefficient).
  public static func Base(target: ref<GameObject>) -> Float {
    return GameInstance.GetStatsSystem(target.GetGame()).GetStatValue(Cast<StatsObjectID>(target.GetEntityID()), gamedataStatType.TBHsBaseCoefficient);
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
        TandemShots.ShowHit(body, shooter, hit.player, new Vector4(hit.x, hit.y, hit.z, 1.0), new Vector4(hit.dx, hit.dy, hit.dz, 0.0));
      }
      this.Once(n"report", s"shots: player \(hit.player) reports npc \(hit.npc)'s shot hit their V (shown on the puppet: \(IsDefined(body) && IsDefined(shooter)))");
    }
  }

  // A bullet's hit on the puppet as the game shows one (its hit reaction,
  // sounds, effects), dealing nothing: the hit a ShotHit names (a game tells
  // its V's hits as HitTaken now, HitsTaken.reds; the simulated V still
  // says ShotHit), queued as any hit a V took, on the zone at its height.
  public static func ShowHit(body: ref<ScriptedPuppet>, shooter: ref<GameObject>, player: Uint32, at: Vector4, way: Vector4) -> Void {
    let hits = TandemHitsTaken.Of(body.GetGame());
    if !IsDefined(hits) {
      return;
    }
    let feet = body.GetWorldPosition();
    let offset = new Vector4(at.X - feet.X, at.Y - feet.Y, at.Z - feet.Z, 0.0);
    hits.Queue(body, shooter, TweakDBInterface.GetAttackRecord(t"Attacks.REMOVE_BulletWithDamage"), 10, at, way, TandemHitsTaken.ZoneAt(body, offset), 0, false);
    hits.NoteShown(player);
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
    // Weapons given a while ago: long in the proxy's hand, or it is gone.
    let g = 0;
    while g < ArraySize(this.m_givenAt) {
      if now - this.m_givenAt[g] > 10.0 || now < this.m_givenAt[g] {
        ArrayErase(this.m_givenTo, g);
        ArrayErase(this.m_givenAt, g);
      } else {
        g += 1;
      }
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
    // A shot named with an item that is no weapon (a crowd NPC's phone in
    // its hand: npcs-shoot-guest, 2026-11-22) would wait for an empty hand
    // for ever: a pistol stands in for it.
    let shotWeapon = shot.weapon;
    let item = TweakDBInterface.GetItemRecord(shotWeapon);
    let category = IsDefined(item) ? item.ItemCategory() : null;
    if TDBID.IsValid(shotWeapon) && (!IsDefined(category) || NotEquals(category.Type(), gamedataItemCategory.Weapon)) {
      shotWeapon = t"Items.Preset_Lexington_Default";
    }
    if !IsDefined(weapon) && TDBID.IsValid(shotWeapon) {
      if !this.m_handed {
        this.m_handed = true;
        TandemNative.LogDebug(s"shots: npc \(shot.npc)'s proxy has nothing in its hand: given \(TDBID.ToStringDEBUG(shotWeapon))\(shotWeapon != shot.weapon ? s" (for \(TDBID.ToStringDEBUG(shot.weapon)), no weapon)" : "")");
      }
      let given = ArrayFindFirst(this.m_givenTo, proxy.GetEntityID());
      if given < 0 || now - this.m_givenAt[given] > 2.0 || now < this.m_givenAt[given] {
        if given < 0 {
          ArrayPush(this.m_givenTo, proxy.GetEntityID());
          ArrayPush(this.m_givenAt, now);
        } else {
          this.m_givenAt[given] = now;
        }
        TandemWeapons.Hold(proxy, slot, shotWeapon);
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
    // A smart gun's round the host's AI tracks (kShotAtTracked): aimed at
    // the best of our V's targeting components and guided onto it, as
    // AIWeapon.Fire fires one with shouldTrackTarget (final.reds:173200).
    let component: wref<TargetingComponent>;
    if (shot.flags & 2) != 0 {
      // A player's is its primary one, as AIWeapon.Fire takes it for a
      // target that is the player (final.reds, `target.IsPlayer()`); the
      // targeting system's best found none on our V (2026-11-21).
      component = player.GetPrimaryTargetingComponent();
      if !IsDefined(component) {
        component = GameInstance.GetTargetingSystem(this.m_game).GetBestComponentOnTargetObject(weapon.GetWorldPosition(), weapon.GetWorldForward(), player, TargetComponentFilterType.Shooting);
      }
      if !IsDefined(component) {
        this.Once(n"guidedNoComponent", s"shots: npc \(shot.npc)'s guided round has no targeting component of our V to home on: fired plain");
      }
    }
    let guided = IsDefined(component);
    let aim = new Vector4(0.0, 0.0, 0.0, 0.0);
    let head = guided ? IPositionProvider.CreatePlacedComponentPositionProvider(component) : IPositionProvider.CreateSlotPositionProvider(player, n"Head");
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
    let miss = false;
    if IsDefined(target) {
      offset = target.HandleBeingShot(proxy, weapon, aim, maxSpread, shot.tbh, miss);
    }
    if !miss {
      this.m_decidedHits += 1;
    }
    let params: gameprojectileWeaponParams;
    params.hitPlaneOffset = offset;
    params.shootingOffset = stats.GetStatValue(weaponStats, gamedataStatType.ShootingOffsetAI);
    // A native struct local is not cleared by the script VM: the smart
    // fields are set on both paths.
    params.smartGunSpreadOnHitPlane = new Vector3(0.0, 0.0, 0.0);
    params.smartGunAccuracy = guided ? 1.0 : 0.0;
    params.smartGunIsProjectileGuided = guided;
    params.trackedTargetComponent = component;
    // A guided round the time between hits says must miss flies past our
    // V instead of homing into it (as AIWeapon.Fire has it).
    if guided && miss {
      gameprojectileWeaponParams.AddObjectToIgnoreCollisionWith(params, player.GetEntityID());
    }
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
    if guided {
      // From the component it homes on, the miss offset kept.
      let tracked = IPositionProvider.CreatePlacedComponentPositionProvider(component);
      tracked.SetWorldOffset(offset);
      params.targetPosition = aim + offset;
      weapon.AI_ShootAt(tracked, player, proxy, Cast<Uint16>(0), params, Cast<Uint8>(Max(perShot, 1)), 0.0, maxSpread);
      this.m_guided += 1;
      this.Once(n"guided", s"shots: npc \(shot.npc)'s guided round at us fired from its proxy (homing on our V's \(NameToString(component.GetName())), to land: \(!miss))");
    } else {
      weapon.AI_ShootAt(IPositionProvider.CreateStaticPositionProvider(at), null, proxy, Cast<Uint16>(0), params, Cast<Uint8>(Max(perShot, 1)), 0.0, maxSpread);
    }
    WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot);
    AnimationControllerComponent.PushEventToReplicate(weapon, n"Shoot");
    this.m_fired += 1;
    // The first five of each session, then every tenth.
    if this.m_fired <= 5 || this.m_fired % 10 == 0 {
      TandemNative.LogDebug(s"shots: npc \(shot.npc)'s shot at us fired from its proxy (\(this.m_fired) so far, \(this.m_guided) guided, \(this.m_decidedHits) let land by the time between hits, tbh \(shot.tbh); speed \(shot.factor), catching up x\(shot.boost) for \(shot.boostSeconds) s)");
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

  // A hit on our V from a proxy's shot (DealDamages, Combat.reds): our
  // game's decision. The others show it on our puppet as every hit our V
  // takes (HitTaken, HitsTaken.reds: no ShotHit from a game any more).
  public func Report(hitEvent: ref<gameHitEvent>) -> Void {
    if !IsDefined(hitEvent) || hitEvent.target != GetPlayer(this.m_game) {
      return;
    }
    let from = hitEvent.attackData.GetInstigator();
    let system = this.System();
    let npc = IsDefined(from) && IsDefined(system) ? system.NpcOfProxy(from.GetEntityID()) : 0u;
    if npc != 0u {
      this.Once(n"hit", s"shots: npc \(npc)'s shot hit our V (our game's decision)");
    }
  }

  public func End() -> Void {
    // A new session logs its firsts again.
    ArrayClear(this.m_logged);
    this.m_handed = false;
    this.m_fired = 0;
    this.m_guided = 0;
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
    ArrayClear(this.m_givenTo);
    ArrayClear(this.m_givenAt);
  }
}

// The host's call on whether its NPC's shot at a guest's puppet may land,
// by the game's rules for a shot at a player (TargetShootComponent
// .ShouldBeHit, final.reds:174004). A puppet is no player there: the group
// coefficient (several shooters at one V each hit it less often, from the
// recent hitters only a player's component records) and the cover one (a V
// behind cover is hit less) were 1, and the base coefficient and the
// difficulty's multiplier were the puppet's NPC record's. Here they are a
// player's: the host's own V's stats and package stand for the guest's
// (the same player record). Out: the group and cover coefficients used.
@addMethod(TargetShootComponent)
public final func TandemShouldHitAsPlayer(weaponOwner: ref<GameObject>, weapon: ref<WeaponObject>, coefficient: Float, player: ref<GameObject>, out group: Float, out cover: Float) -> Bool {
  let target = this.GetGameObject();
  let game = target.GetGame();
  let stats = GameInstance.GetStatsSystem(game);
  let source = weaponOwner.GetSourceShootComponent();
  group = 1.0;
  cover = 1.0;
  if !IsDefined(source) {
    return true;
  }
  let weaponId = ItemID.GetTDBID(weapon.GetItemID());
  if this.m_weaponTDBID != weaponId {
    this.m_weaponRecord = TweakDBInterface.GetWeaponItemRecord(weaponId);
    this.m_weaponTDBID = weaponId;
  }
  let puppet = weaponOwner as ScriptedPuppet;
  if IsDefined(puppet) && RPGManager.IsTechPierceEnabled(game, weaponOwner, weapon.GetItemID()) {
    let blocked = puppet.GetSenses().GetVisibilityTraceEndToAgentDist(target);
    if blocked > 0.0 && blocked < 1000000000.0 {
      return false;
    }
  }
  let visibility = this.GetVisibilityCoefficient(weaponOwner, weapon, target, coefficient);
  if coefficient > 0.0 && visibility <= 0.0 {
    return false;
  }
  let accuracy = stats.GetStatValue(Cast<StatsObjectID>(weaponOwner.GetEntityID()), gamedataStatType.Accuracy);
  let distance = this.GetDistanceCoefficient(weapon, target.GetWorldPosition());
  if accuracy == 0.0 || distance == 0.0 {
    return false;
  }
  let own = player.GetTargetShootComponent();
  let pack = IsDefined(own) ? own.GetPackageName() : this.GetPackageName();
  group = this.GetGroupCoefficient(weaponOwner);
  if Equals(RPGManager.GetItemType(weapon.GetItemID()), gamedataItemType.Wea_HeavyMachineGun) {
    group = LerpF(TweakDBInterface.GetFloat(TDBID.Create(pack + ".HMGGroupMultiplier"), 1.0), 1.0, group);
  }
  if !source.CanSeeSecondaryPointOfTarget(target) {
    let smart = IsDefined(this.m_weaponRecord) && IsDefined(this.m_weaponRecord.Evolution()) && Equals(this.m_weaponRecord.Evolution().Type(), gamedataWeaponEvolution.Smart);
    cover = TweakDBInterface.GetFloat(TDBID.Create(pack + (smart ? ".coverVsSmartWeaponsMultiplier" : ".coverVsNormalWeaponsMultiplier")), 1.0);
  }
  let field = ".normalModeMultiplier";
  switch GameInstance.GetStatsDataSystem(game).GetDifficulty() {
    case gameDifficulty.Story:
      field = ".storyModeMultiplier";
      break;
    case gameDifficulty.Easy:
      field = ".easyModeMultiplier";
      break;
    case gameDifficulty.VeryHard:
      field = ".hardModeMultiplier";
      break;
    default:
      break;
  }
  let between = TweakDBInterface.GetFloat(TDBID.Create(pack + field), 1.0)
    * stats.GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatType.TBHsBaseCoefficient)
    * stats.GetStatValue(Cast<StatsObjectID>(weaponOwner.GetEntityID()), gamedataStatType.TBHsBaseSourceMultiplierCoefficient)
    * (1.0 / accuracy) * distance * visibility * group * cover
    * source.GetLineOfSightTBHModifier(target);
  let now = EngineTime.ToFloat(GameInstance.GetSimTime(game));
  if now - this.GetLastHitTime() < between {
    return false;
  }
  this.SetLastHitTime(now);
  this.InsertRecentHitter(weaponOwner);
  return true;
}
