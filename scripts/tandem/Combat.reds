// Fights against the host's NPCs (docs/architecture/combat.md, milestone 5).
//
// On a guest every NPC is a proxy of the host's. When our V hits one, the
// game's damage pipeline runs as usual (aim, weapon, perks, the proxy's
// armour: same record, same stats) up to the point where it would take the
// health away. There, the damage is computed without being applied
// (StatPoolsManager.ApplyDamage with forReal false, what the game itself
// does on a client) and sent to the host, which applies it to the real NPC;
// the health and the death come back (Npcs.reds). The hit still shows here at
// once: hit marker, damage numbers, the proxy's hit reaction. The skill XP
// the hit earns is queued here, as the game does when it applies damage.
//
// The other way round, one of the host's NPCs hitting a guest's puppet: the
// host's game runs the attacker's side of the hit (the NPC's attack, weapon,
// stats, range) and skips every stage of the target's side for the puppet
// (resistances, dodge, evasion, mitigation, the player multiplier, target
// modifiers and armour, block and deflect: the wraps at the end of this
// file). DealDamages sends what is left as PlayerHit to that player's game,
// which runs the hit on its own V (PlayerHitEvent) with the NPC's proxy as
// the attacker and skips the attacker's stages instead (ConvertDPSToHitDamage,
// CalculateSourceModifiers, ProcessEffectiveRange): each side is applied
// once, by the game that owns it (docs/research/engine-knowledge.md §13.12).
//
// What a hit does besides health (poise, non-lethal, the 1 HP cap, the hit
// reaction, wounds), the difficulty and area effects: Outcomes.reds.

public abstract class TandemCombat {
  public static func System(game: GameInstance) -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
  }

  public static func ProxyNpc(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    let system = TandemCombat.System(game);
    return IsDefined(system) ? system.NpcOfProxy(target.GetEntityID()) : 0u;
  }

  public static func PuppetPlayer(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    let system = TandemCombat.System(game);
    return IsDefined(system) ? system.PlayerOfPuppet(target.GetEntityID()) : 0u;
  }

  // A hit on a remote player's puppet whose target's side is that player's
  // game's (every stage of it is skipped here): an NPC's we run (any game,
  // regions.md), the host's world's other than a proxy's (its devices,
  // turrets); on any game, our V's under friendly fire (Outcomes.reds).
  public static func OnPuppet(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    return TandemCombat.PuppetPlayer(game, hitEvent.target) != 0u && (TandemCombat.OursToSend(game, hitEvent) || TandemOutcomes.IsOurHit(game, hitEvent));
  }

  // A hit whose attacker's side this game computes: an NPC we run, or (the
  // host) anything of its world that is not a proxy of another game's NPC.
  public static func OursToSend(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    return TandemOwn.Hit(game, hitEvent) || (TandemNative.IsHost() && !TandemOwn.ProxyHit(game, hitEvent));
  }

  // A hit someone else's game computed the attacker's side of, run on our
  // V here: the host's NPC's (PlayerHitEvent), another V's area effect.
  public static func IsForwarded(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let system = TandemCombat.System(game);
    return IsDefined(system) && system.IsForwardedHit(hitEvent);
  }

  // Markers our game leaves on a target so that our V is paid its XP or
  // reward once (RPGManager.AwardExperienceFromQuickhack, final.reds:37296;
  // EffectExecutor_GivePlayerReward, :523580): each player's own business.
  // On the host's NPC they would take the same XP away from the host's V.
  public static func IsOwnMarker(effect: TweakDBID) -> Bool {
    return effect == t"BaseStatusEffect.QuickhackExpHarvested" || effect == t"BaseStatusEffect.UtilityGrenadeRewardHarvested";
  }

  public static func Flags(hitEvent: ref<gameHitEvent>) -> Int32 {
    let flags = 0;
    if hitEvent.attackData.HasFlag(hitFlag.WeakspotHit) || hitEvent.attackData.HasFlag(hitFlag.Headshot) {
      flags += 1;
    }
    if hitEvent.attackData.HasFlag(hitFlag.CriticalHit) {
      flags += 2;
    }
    if AttackData.IsMelee(hitEvent.attackData.GetAttackType()) {
      flags += 4;
    }
    if hitEvent.attackData.HasFlag(hitFlag.Kill) {
      flags += 8;
    }
    if hitEvent.attackData.HasFlag(hitFlag.IgnoreDifficulty) {
      flags += 16;
    }
    if hitEvent.attackData.HasFlag(hitFlag.CannotKillPlayer) {
      flags += 32;
    }
    return flags;
  }

  // ---- host: our NPCs' hits on a guest's puppet ----

  // Which hits on a puppet go to its player: an NPC's (or a device's, a
  // car's), not a V's (players do not hurt each other), and not the damage
  // ticks of a status effect on the puppet (the player's game runs its own
  // copy of the effect, from the attack that put it there).
  public static func ForwardsToPlayer(hitEvent: ref<gameHitEvent>, game: GameInstance) -> Bool {
    if AttackData.IsDoT(hitEvent.attackData) {
      return false;
    }
    // A device's (a turret, a mine, a barrel going up): the player's own
    // game runs its copy of the device against its own V (Devices.reds).
    if TandemDevices.IsDeviceHit(hitEvent) {
      return false;
    }
    let from = hitEvent.attackData.GetInstigator();
    return !IsDefined(from) || (!from.IsPlayer() && TandemCombat.PuppetPlayer(game, from) == 0u);
  }

  // The attacker's side of the hit, as our game computed it, to the player.
  public static func SendPlayerHit(game: GameInstance, victim: Uint32, hitEvent: ref<gameHitEvent>, pierce: Float) -> Void {
    // A player in another world (Elsewhere.reds) is not here to be hurt.
    if TandemElsewhere.IsElsewhere(game, victim) {
      TandemNative.Log(s"combat: hit on player \(victim)'s puppet not sent: they are in another world");
      return;
    }
    let from = hitEvent.attackData.GetInstigator();
    let system = TandemCombat.System(game);
    let npc = IsDefined(from) && IsDefined(system) ? system.SharedNpcOf(from.GetEntityID()) : 0u;
    let weapon = hitEvent.attackData.GetWeapon();
    let weaponId = TDBID.None();
    if IsDefined(weapon) {
      weaponId = ItemID.GetTDBID(weapon.GetItemID());
    }
    let attackId = TDBID.None();
    let definition = hitEvent.attackData.GetAttackDefinition();
    if IsDefined(definition) && IsDefined(definition.GetRecord()) {
      attackId = definition.GetRecord().GetID();
    }
    let values = hitEvent.attackComputed;
    let physical = values.GetAttackValue(gamedataDamageType.Physical);
    let thermal = values.GetAttackValue(gamedataDamageType.Thermal);
    let electric = values.GetAttackValue(gamedataDamageType.Electric);
    let chemical = values.GetAttackValue(gamedataDamageType.Chemical);
    let flags = TandemCombat.Flags(hitEvent);
    TandemNative.SendPlayerHit(victim, npc, physical, thermal, electric, chemical, EnumInt(hitEvent.attackData.GetAttackType()), flags, pierce, hitEvent.hitPosition, hitEvent.hitDirection, attackId, weaponId);
    TandemNative.LogDebug(s"combat: \(IsDefined(from) ? TDBID.ToStringDEBUG(GameObject.GetTDBID(from)) : "?") (npc \(npc)) hit player \(victim)'s puppet: \(physical) physical, \(thermal) thermal, \(electric) electric, \(chemical) chemical, type \(EnumInt(hitEvent.attackData.GetAttackType())), flags \(flags), sent to their game");
  }

  // ---- guest: the host's NPCs' hits on our V ----

  // The hit the host's NPC made, as a hit on our own V: the NPC's attack
  // (its record: hit reaction, status effects it applies, attack type) from
  // its proxy, with the damage the host's game computed for the attacker's
  // side. Our damage system then runs the target's side by our game's rules.
  public static func PlayerHitEvent(game: GameInstance, hit: TandemPlayerHit, proxy: ref<ScriptedPuppet>) -> ref<gameHitEvent> {
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return null;
    }
    let record = TandemCombat.AttackRecord(game, hit.attack, (hit.flags & 4) != 0);
    // Armour and the one-shot cap only apply with an attacking weapon: the
    // proxy's own if it holds one, else ours stands in (its armour
    // penetration is replaced by the host's: GetArmorPenetrationValue).
    let weapon: wref<WeaponObject>;
    if IsDefined(proxy) {
      weapon = ScriptedPuppet.GetActiveWeapon(proxy);
    }
    if !IsDefined(weapon) {
      weapon = GameObject.GetActiveWeapon(player);
    }
    // No proxy (the NPC is not shared with us): no attacker, our V as the
    // source (a hit with no source is dropped: CheckForQuickExit).
    let source: ref<GameObject> = player;
    let at = new Vector4(hit.x, hit.y, hit.z, 1.0);
    if IsDefined(proxy) {
      source = proxy;
      at = proxy.GetWorldPosition();
    }
    let context: AttackInitContext;
    context.record = record;
    context.instigator = proxy;
    context.source = source;
    context.weapon = weapon;
    let event = new gameHitEvent();
    event.attackData = new AttackData();
    event.target = player;
    event.hitPosition = new Vector4(hit.x, hit.y, hit.z, 1.0);
    event.hitDirection = new Vector4(hit.dx, hit.dy, hit.dz, 0.0);
    event.attackData.SetAttackDefinition(IAttack.Create(context));
    event.attackData.SetInstigator(proxy);
    event.attackData.SetSource(source);
    event.attackData.SetWeapon(weapon);
    event.attackData.SetAttackType(IntEnum<gamedataAttackType>(hit.attackType));
    event.attackData.SetAttackPosition(at);
    // The host's NPC was hostile to us: whatever our proxy's attitude, the
    // friendly-fire check (CheckForQuickExit) must not drop it.
    event.attackData.AddFlag(hitFlag.FriendlyFire, n"tandem_npc_hit");
    if (hit.flags & 1) != 0 {
      event.attackData.AddFlag(hitFlag.WeakspotHit, n"tandem_npc_hit");
    }
    if (hit.flags & 2) != 0 {
      event.attackData.AddFlag(hitFlag.CriticalHit, n"tandem_npc_hit");
    }
    if (hit.flags & 8) != 0 {
      event.attackData.AddFlag(hitFlag.Kill, n"tandem_npc_hit");
    }
    // The attack's own rules for its target (a story-mode exception, a brawl
    // that leaves 1 HP: ApplyDamageSingle's CannotKillPlayer).
    if (hit.flags & 16) != 0 {
      event.attackData.AddFlag(hitFlag.IgnoreDifficulty, n"tandem_npc_hit");
    }
    if (hit.flags & 32) != 0 {
      event.attackData.AddFlag(hitFlag.CannotKillPlayer, n"tandem_npc_hit");
    }
    event.attackComputed = new gameAttackComputed();
    event.attackComputed.SetAttackValue(hit.physical, gamedataDamageType.Physical);
    event.attackComputed.SetAttackValue(hit.thermal, gamedataDamageType.Thermal);
    event.attackComputed.SetAttackValue(hit.electric, gamedataDamageType.Electric);
    event.attackComputed.SetAttackValue(hit.chemical, gamedataDamageType.Chemical);
    return event;
  }

  // The armour penetration the host sent with a forwarded hit (it is the
  // NPC's weapon's, which this game may not have).
  public static func ForwardedPierce(game: GameInstance, hitEvent: ref<gameHitEvent>, out pierce: Float) -> Bool {
    let system = TandemCombat.System(game);
    return IsDefined(system) && system.ForwardedPierce(hitEvent, pierce);
  }

  // The attack a hit from another game names, as our game knows it. One
  // our game lacks (a mod's, or none from a mock player) is stood in by a
  // shove or a plain bullet, which only names the hit: the stand-in's own
  // multiplier for a V as target is not applied
  // (ProcessPlayerIncomingDamageMultiplier) and the damage stays at most
  // the sender's (CapForwarded). Each unknown record is logged once a
  // session (TandemNpcs.NoteStandIn).
  public static func AttackRecord(game: GameInstance, attack: TweakDBID, melee: Bool) -> ref<Attack_Record> {
    let record = TweakDBInterface.GetAttackRecord(attack);
    if IsDefined(record) {
      return record;
    }
    let standIn = melee ? t"Attacks.ForwardPush" : t"Attacks.REMOVE_BulletWithDamage";
    let system = TandemCombat.System(game);
    if IsDefined(system) && IsDefined(system.Npcs()) {
      system.Npcs().NoteStandIn(attack, standIn);
    }
    return TweakDBInterface.GetAttackRecord(standIn);
  }

  public static func IsStandIn(record: ref<Attack_Record>) -> Bool {
    if !IsDefined(record) {
      return false;
    }
    let id = record.GetID();
    return id == t"Attacks.ForwardPush" || id == t"Attacks.REMOVE_BulletWithDamage";
  }

  // An attack whose record sets a V's damage itself, from the V's health
  // (an explosion's fixed share, a grenade's per-difficulty damage:
  // ProcessPlayerFixedPercentageOverride, ProcessGrenadeExplosionDamageToPlayer,
  // final.reds 85107, 85250): our game's rule, whatever the sender computed.
  public static func SetsPlayerDamage(record: ref<Attack_Record>) -> Bool {
    let effect = record as Attack_GameEffect_Record;
    if !IsDefined(effect) {
      return false;
    }
    let flags = effect.HitFlags();
    return ArrayContains(flags, "OverridePlayerDamageWithFixedPercentage") || ArrayContains(flags, "PlayerGrenadeDamageOverride");
  }

  // A hit another game computed takes at most what it sent, per damage
  // type: our game's rules for our V (armour, resistances, block, perks, the
  // one-shot cap) may lower it, and raise it only where they mean to for a V
  // as target: the attack's own multiplier for players (a record we know),
  // the police's, an explosion's fixed share (SetsPlayerDamage). A stand-in
  // record's never (a 10-physical melee hit through Attacks.ForwardPush took
  // 392.96, 2026-11-21). The margin: 1 %, and the 1 point vanilla's
  // resistances and armour leave of each type. With no weapon our game
  // skips its one-shot cap (ProcessOneShotProtection, final.reds 85069):
  // applied here for an NPC's hit, as it would be with one.
  public static func CapForwarded(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Void {
    let system = TandemCombat.System(game);
    let sent: array<Float>;
    if !IsDefined(system) || !system.ForwardedSent(hitEvent, sent) {
      return;
    }
    let record = hitEvent.attackData.GetAttackDefinition().GetRecord();
    let standIn = TandemCombat.IsStandIn(record);
    if !standIn && TandemCombat.SetsPlayerDamage(record) {
      return;
    }
    let allowance = MaxF(PreventionSystem.GetDamageToPlayerMultiplier(game), 1.0);
    if IsDefined(record) && !standIn {
      allowance *= MaxF(record.PlayerIncomingDamageMultiplier(), 1.0);
    }
    let had = hitEvent.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
    let values = hitEvent.attackComputed.GetAttackValues();
    let total = 0.0;
    let i = 0;
    while i < ArraySize(values) {
      let most = 0.0;
      if i < ArraySize(sent) && sent[i] > 0.0 {
        most = MaxF(sent[i] * allowance * 1.01, 1.0);
      }
      values[i] = MinF(values[i], most);
      total += values[i];
      i += 1;
    }
    let instigator = hitEvent.attackData.GetInstigator();
    if !IsDefined(hitEvent.attackData.GetWeapon()) && hitEvent.target.IsPlayer() && TandemCombat.PuppetPlayer(game, instigator) == 0u {
      let cap = TandemCombat.OneShotCap(game, hitEvent.target);
      if cap > 0.0 && total > cap {
        let share = cap / total;
        i = 0;
        while i < ArraySize(values) {
          values[i] *= share;
          i += 1;
        }
      }
    }
    hitEvent.attackComputed.SetAttackValues(values);
    let now = hitEvent.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
    if now < had - 0.01 {
      TandemNative.LogDebug(s"combat: forwarded hit on our V capped: \(had) -> \(now) damage (\(standIn ? "a stand-in attack" : "the attack's own record"))");
    }
  }

  // The most one NPC hit may take from our V at the session's difficulty
  // (ProcessOneShotProtection): a share of its maximum health.
  public static func OneShotCap(game: GameInstance, target: ref<GameObject>) -> Float {
    let share = 0.0;
    switch GameInstance.GetStatsDataSystem(game).GetDifficulty() {
      case gameDifficulty.Story:
        share = TweakDBInterface.GetFloat(t"Constants.DamageSystem.maxPercentDamagePerHitStory", 0.0);
        break;
      case gameDifficulty.Easy:
        share = TweakDBInterface.GetFloat(t"Constants.DamageSystem.maxPercentDamagePerHitEasy", 0.0);
        break;
      case gameDifficulty.Hard:
        share = TweakDBInterface.GetFloat(t"Constants.DamageSystem.maxPercentDamagePerHitHard", 0.0);
        break;
      case gameDifficulty.VeryHard:
        share = TweakDBInterface.GetFloat(t"Constants.DamageSystem.maxPercentDamagePerHitVeryHard", 0.0);
        break;
    }
    let maxHealth = GameInstance.GetStatsSystem(game).GetStatValue(Cast<StatsObjectID>(target.GetEntityID()), gamedataStatType.Health);
    return maxHealth * share / 100.0;
  }

  public static func Health(game: GameInstance, target: wref<GameObject>) -> Float {
    return GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(target.GetEntityID()), gamedataStatPoolType.Health, false);
  }

  // ---- guest: our hits on a proxy ----

  // The skill XP of a hit on a proxy, queued as StatPoolsManager.DrainStatPool
  // does when damage is applied for real (final.reds 219283-219327): the
  // share of the target's health pool this hit drains, in percent. The proxy's
  // pool mirrors the host's NPC (Npcs.reds ApplyHealth). The queue is paid
  // when the proxy dies (Npcs.reds Die).
  public static func AwardDamageExperience(game: GameInstance, hitEvent: ref<gameHitEvent>, damage: Float) -> Float {
    let pools = GameInstance.GetStatPoolsSystem(game);
    let id = Cast<StatsObjectID>(hitEvent.target.GetEntityID());
    let percent = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, true);
    let points = pools.ToPoints(id, gamedataStatPoolType.Health, percent);
    if points <= 0.0 {
      points = 1.0;
    }
    let drained = MinF(damage * percent / points, percent);
    if drained > 0.0 {
      RPGManager.AwardExperienceFromDamage(hitEvent, drained);
    }
    return drained;
  }
}

// A boss's weak spots (WeakspotObject: a plate, a heat sink, a mech's
// vents), each an entity of its own on its NPC. Our fire on a proxy's
// breaks the proxy's on our screen as alone; the share of its health each
// hit took goes to the NPC's owner (WeakSpotHit), whose real weak spot
// takes the same: its destruction there sets its fact and moves the boss
// on (ScriptedWeakspotObject.OnWeakspotDestroy, final.reds:293345), on the
// game that runs the quest's NPC.
public abstract class TandemWeakSpots {
  // Its health, percent.
  public static func Health(game: GameInstance, spot: ref<GameObject>) -> Float {
    return GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(spot.GetEntityID()), gamedataStatPoolType.WeakspotHealth, true);
  }

  // Its NPC's weak spots, in the component's order.
  public static func Of(npc: ref<ScriptedPuppet>) -> array<wref<WeakspotObject>> {
    let spots: array<wref<WeakspotObject>>;
    let component = IsDefined(npc) ? npc.GetWeakspotComponent() : null;
    if IsDefined(component) {
      component.GetWeakspots(spots);
    }
    return spots;
  }

  // What a hit on a weak spot takes, in percent of its health, measured as
  // DealDamages would apply it (StatPoolsManager.ApplyDamage, not for real;
  // DrainStatPool's points to percent, final.reds 219136-219145).
  public static func Measure(game: GameInstance, hitEvent: ref<gameHitEvent>, spot: ref<WeakspotObject>) -> Float {
    let lost: array<SDamageDealt>;
    StatPoolsManager.ApplyDamage(hitEvent, false, lost);
    let points = 0.0;
    for dealt in lost {
      if Equals(dealt.affectedStatPool, gamedataStatPoolType.Health) {
        points += dealt.value;
      }
    }
    if points <= 0.0 {
      return 0.0;
    }
    return GameInstance.GetStatPoolsSystem(game).ToPercentage(Cast<StatsObjectID>(spot.GetEntityID()), gamedataStatPoolType.Health, points);
  }

  // Guest: our hit on a proxy's weak spot took `drain` percent of it.
  public static func Lost(spot: ref<WeakspotObject>, npc: Uint32, drain: Float) -> Void {
    let record = spot.GetRecord();
    let spots = TandemWeakSpots.Of(spot.GetOwner() as ScriptedPuppet);
    let index = -1;
    let i = 0;
    while i < ArraySize(spots) {
      if IsDefined(spots[i]) && spots[i].GetEntityID() == spot.GetEntityID() {
        index = i;
      }
      i += 1;
    }
    if drain <= 0.0 || index < 0 || !IsDefined(record) {
      // (npcs-weakspot) why a hit on a proxy's weak spot went nowhere.
      TandemNative.LogDebug(s"combat: our hit on weak spot \(index) of proxy \(npc) took nothing measured (\(drain)%, record \(IsDefined(record)))");
      return;
    }
    TandemFight.SendWeakSpot(npc, record.GetID(), index, drain);
    TandemNative.LogDebug(s"combat: our hit took \(drain)% of weak spot \(index) (\(TDBID.ToStringDEBUG(record.GetID()))) of proxy \(npc), for its owner");
  }

  // The NPC's owner: another V's fire on a weak spot of one of our NPCs (its
  // proxy there): the same share off ours, from that V's puppet. The one at
  // its index if it is of that record, else the first of that record still
  // whole.
  public static func Take(game: GameInstance, event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let body = npcs.Owns(event.npc) ? GameInstance.FindEntityByID(game, npcs.EntityOfNpc(event.npc)) as ScriptedPuppet : null;
    if !IsDefined(body) || body.IsDead() {
      return;
    }
    let spots = TandemWeakSpots.Of(body);
    let pick: wref<WeakspotObject>;
    let index = event.level;
    if index >= 0 && index < ArraySize(spots) && IsDefined(spots[index]) && IsDefined(spots[index].GetRecord()) && spots[index].GetRecord().GetID() == event.attack {
      pick = spots[index];
    } else {
      for candidate in spots {
        if !IsDefined(pick) && IsDefined(candidate) && IsDefined(candidate.GetRecord()) && candidate.GetRecord().GetID() == event.attack && !candidate.IsDead() {
          pick = candidate;
        }
      }
    }
    let what = TDBID.ToStringDEBUG(event.attack);
    if !IsDefined(pick) || pick.IsDead() || pick.IsInvulnerable() {
      TandemNative.Log(s"combat: player \(event.player)'s hit on weak spot \(index) (\(what)) of npc \(event.npc) dropped (none whole and open here)");
      return;
    }
    let by = TandemNpcs.PuppetOf(game, event.player);
    GameInstance.GetStatPoolsSystem(game).RequestChangingStatPoolValue(Cast<StatsObjectID>(pick.GetEntityID()), gamedataStatPoolType.WeakspotHealth, -event.damage, by, false, true);
    TandemNative.Log(s"combat: player \(event.player)'s hit took \(event.damage)% of weak spot \(index) (\(what)) of npc \(event.npc): \(TandemWeakSpots.Health(game, pick))% left");
  }

  // (test) tools/game.sh cmd "npc weakspot [N]": our V's hit of N (default
  // 40) on the first whole weak spot of the nearest proxy that takes hits
  // (a boss's plate shut in its phase does not: IsInvulnerable).
  public static func Test(game: GameInstance, proxy: ref<ScriptedPuppet>, amount: Float) -> Void {
    let player = GetPlayer(game);
    let target: wref<WeakspotObject>;
    let shut = 0;
    for candidate in TandemWeakSpots.Of(proxy) {
      if IsDefined(candidate) && !candidate.IsDead() {
        if candidate.IsInvulnerable() {
          shut += 1;
        } else if !IsDefined(target) {
          target = candidate;
        }
      }
    }
    if !IsDefined(player) || !IsDefined(target) {
      TandemNative.Log(s"npc weakspot: none open on the nearest proxy (\(shut) shut)");
      return;
    }
    let context: AttackInitContext;
    context.record = TweakDBInterface.GetAttackRecord(t"Attacks.VehicleImpact");
    context.instigator = player;
    context.source = player;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = target;
    hit.attackComputed = new gameAttackComputed();
    hit.attackComputed.SetAttackValue(amount, gamedataDamageType.Physical);
    hit.attackData.SetSource(player);
    hit.attackData.SetInstigator(player);
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    // A shot's hit on the spot gets this flag from the spot itself
    // (WeakspotObject.ProcessDamagePipeline, final.reds:293612), which a
    // queued hit skips; set here as a shot would have it (the hit took no
    // measured share without it, unproven why: npcs-weakspot).
    hit.attackData.AddFlag(hitFlag.WeakspotHit, n"tandem_test");
    GameInstance.GetDamageSystem(game).QueueHitEvent(hit, target);
    TandemNative.Log(s"npc weakspot: \(EntityID.ToDebugStringDecimal(target.GetEntityID())) of \(EntityID.ToDebugStringDecimal(proxy.GetEntityID())), \(TandemWeakSpots.Health(game, target))% whole");
  }
}

@wrapMethod(DamageSystem)
private final func DealDamages(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  // A remote player's puppet never takes damage here: an NPC's owner sends
  // its hit to that player's game, any game its own V's hits under
  // friendly fire (Outcomes.reds); that game applies it to its V.
  let victim = TandemCombat.PuppetPlayer(game, hitEvent.target);
  if victim != 0u {
    if TandemCombat.OursToSend(game, hitEvent) && TandemCombat.ForwardsToPlayer(hitEvent, game) {
      TandemCombat.SendPlayerHit(game, victim, hitEvent, this.GetArmorPenetrationValue(hitEvent.attackData.GetWeapon(), hitEvent));
      // Our companions stand by that V (Reactions.reds).
      TandemReactions.RallyCompanions(game, hitEvent.attackData.GetInstigator(), hitEvent.target);
    } else if TandemOutcomes.HurtsPartner(game, hitEvent) {
      TandemOutcomes.SendAreaHit(game, victim, hitEvent);
    }
    return;
  }
  // Guest: a proxy attacks nobody (the host's NPC does, as PlayerHit).
  if TandemOutcomes.IsProxyComputed(game, hitEvent) {
    TandemNative.Log("combat: a proxy's own hit dropped (the host's NPC is the one that attacks)");
    return;
  }
  // Guest: the host's NPC's hit on our V, applied here like any other.
  if TandemCombat.IsForwarded(game, hitEvent) {
    let before = TandemCombat.Health(game, hitEvent.target);
    // Never more than the sender computed (a stand-in record's multipliers).
    TandemCombat.CapForwarded(game, hitEvent);
    wrappedMethod(hitEvent);
    let after = TandemCombat.Health(game, hitEvent.target);
    TandemNative.LogDebug(s"combat: npc hit applied to our V: \(hitEvent.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health)) damage, health \(before) -> \(after)");
    return;
  }
  // Our V's fire on a weak spot of a proxy (a boss's plate): it breaks on
  // our screen as alone, and what it lost goes to the NPC's owner, whose
  // real one takes it (its fact, the boss's next phase: TandemWeakSpots).
  let spot = hitEvent.target as WeakspotObject;
  let spotNpc = IsDefined(spot) ? TandemCombat.ProxyNpc(game, spot.GetOwner()) : 0u;
  let shooter = hitEvent.attackData.GetInstigator();
  if spotNpc != 0u && IsDefined(shooter) && shooter.IsPlayer() {
    let had = TandemWeakSpots.Health(game, spot);
    // The pool's change is a request the game applies later: read right
    // after, it is often unchanged. What the hit takes, measured first (not
    // applied), stands in then.
    let measured = TandemWeakSpots.Measure(game, hitEvent, spot);
    wrappedMethod(hitEvent);
    let drain = had - TandemWeakSpots.Health(game, spot);
    TandemWeakSpots.Lost(spot, spotNpc, drain > 0.0 ? drain : MinF(measured, had));
    return;
  }
  let npc = TandemCombat.ProxyNpc(game, hitEvent.target);
  if npc == 0u {
    // Our own V's hit on a shared NPC we run (whose kill it is: KillerOf).
    let own = hitEvent.attackData.GetInstigator();
    let system = TandemCombat.System(game);
    if IsDefined(own) && own.IsPlayer() && IsDefined(system) && IsDefined(system.Npcs()) {
      system.Npcs().NoteOwnHit(hitEvent.target.GetEntityID());
    }
    wrappedMethod(hitEvent);
    // Guest: a proxy's shot (Shots.reds) hit our V: the host shows it.
    let shots = TandemShots.Of(game);
    if IsDefined(shots) {
      shots.Report(hitEvent);
    }
    return;
  }
  // A proxy: what this hit would take, measured, not applied.
  let lost: array<SDamageDealt>;
  StatPoolsManager.ApplyDamage(hitEvent, false, lost);
  let damage = 0.0;
  for dealt in lost {
    if Equals(dealt.affectedStatPool, gamedataStatPoolType.Health) {
      damage += dealt.value;
    }
  }
  let instigator = hitEvent.attackData.GetInstigator();
  let outcomes = TandemOutcomes.Of(game);
  // A device's blast our V set off: the host's copy of it does the damage
  // to the real NPC (Devices.reds), ours shows it only.
  if damage > 0.0 && IsDefined(instigator) && instigator.IsPlayer() && IsDefined(outcomes) && !TandemDevices.IsDeviceHit(hitEvent) {
    // Sent when its pipeline ends, with the poise it took (Outcomes.reds).
    outcomes.Measured(hitEvent, npc, damage, TandemOutcomes.StrikeFlags(hitEvent, damage, TandemCombat.Health(game, hitEvent.target)));
    let xp = TandemCombat.AwardDamageExperience(game, hitEvent, damage);
    TandemNative.LogDebug(s"combat: hit on proxy \(npc) for \(damage), for the host (skill XP for \(xp)% of its health queued)");
    // It takes the last of the health the host gave this proxy: a kill, now,
    // as our own game would have it (Npcs.reds PredictKill).
    if damage >= TandemCombat.Health(game, hitEvent.target) - 0.01 {
      TandemCombat.System(game).PredictKill(npc);
    }
  }
  // The feedback the game gives for a hit (damage numbers, hit marker),
  // as SendDamageEvents would.
  let dealtEvent = new gameTargetDamageEvent();
  dealtEvent.target = hitEvent.target;
  dealtEvent.attackData = hitEvent.attackData;
  dealtEvent.hitPosition = hitEvent.hitPosition;
  dealtEvent.hitDirection = hitEvent.hitDirection;
  dealtEvent.hitRepresentationResult = hitEvent.hitRepresentationResult;
  dealtEvent.damage = damage;
  if IsDefined(instigator) {
    instigator.QueueEvent(dealtEvent);
  }
}

// Instant kills (finishers, the Kill flag) skip DealDamages: the game sets
// the target's health to its minimum here. On a proxy, the host does it; on
// a guest's puppet (host), that player's game does.
@wrapMethod(DamageSystem)
private final func ProcessInstantKill(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  if !hitEvent.projectionPipeline && hitEvent.attackData.HasFlag(hitFlag.Kill) && TandemCombat.OnPuppet(game, hitEvent) {
    hitEvent.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_puppet");
    hitEvent.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_puppet");
    if TandemCombat.ForwardsToPlayer(hitEvent, game) {
      TandemCombat.SendPlayerHit(game, TandemCombat.PuppetPlayer(game, hitEvent.target), hitEvent, this.GetArmorPenetrationValue(hitEvent.attackData.GetWeapon(), hitEvent));
    }
    return;
  }
  let npc = TandemCombat.ProxyNpc(game, hitEvent.target);
  if npc == 0u || hitEvent.projectionPipeline || !hitEvent.attackData.HasFlag(hitFlag.Kill) {
    wrappedMethod(hitEvent);
    return;
  }
  hitEvent.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_proxy");
  hitEvent.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_proxy");
  let instigator = hitEvent.attackData.GetInstigator();
  let outcomes = TandemOutcomes.Of(game);
  if IsDefined(instigator) && instigator.IsPlayer() && IsDefined(outcomes) {
    outcomes.Measured(hitEvent, npc, 0.0, TandemOutcomes.StrikeFlags(hitEvent, 0.0, 0.0) | 8);
    TandemNative.Log(s"combat: instant kill on proxy \(npc), for the host");
    TandemCombat.System(game).PredictKill(npc);
  }
}

// Status effects our V puts on a proxy, decided by our game's rules
// (application chance, buildup, our perks, the target's immunities), and
// whatever put them (hits, quickhacks): they stay on the proxy (their look,
// their damage ticks, which reach the host as hits) and go to the host for
// what they do to the real NPC. The status effect system applies them a
// frame after the hit, so they are caught here, not in the damage pipeline.
@wrapMethod(NPCPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  TandemNative.HookRan(n"Combat.NPCPuppet.OnStatusEffectApplied");
  // The NPC's owner: another V's one-punch slow motion stays theirs, and a
  // V's way out of combat lets only that V go (Outcomes.reds, Stealth.reds:
  // the Relic).
  if TandemOutcomes.IsGuestsOnePunch(this, evt) {
    let outcomes = TandemOutcomes.Of(this.GetGame());
    if IsDefined(outcomes) {
      outcomes.DropOnePunchSlowMo();
    }
  }
  let others = TandemStealth.OthersHunted(this, evt);
  let result = wrappedMethod(evt);
  TandemStealth.HuntAgain(this, others);
  // A V is never crippled or dismembered (Outcomes.reds KeepsLimbs).
  if IsDefined(evt.staticData) && TandemOutcomes.IsLimbEffect(evt.staticData) && TandemOutcomes.KeepsLimbs(this, s"status effect \(TDBID.ToStringDEBUG(evt.staticData.GetID()))") {
    StatusEffectHelper.RemoveStatusEffect(this, evt.staticData.GetID());
    return result;
  }
  // A breach's daemons are run by the NPC's owner from the breach itself
  // (Breach.reds): never forwarded from a proxy as well.
  if evt.isNewApplication && !evt.isAppliedOnSpawn && IsDefined(evt.staticData) && !TandemCombat.IsOwnMarker(evt.staticData.GetID()) && !TandemBreach.IsDaemonEffect(evt.staticData.GetID()) {
    let player = GetPlayer(this.GetGame());
    // The Relic's arm attacks mark their target with no instigator
    // (Outcomes.reds IsRelicMark): ours all the same, and on a proxy our
    // hit has killed already too (the marks act on the dead).
    let relic = TandemOutcomes.IsRelicMark(evt.staticData.GetID()) && TandemOwn.IsProxy(this.GetGame(), this.GetEntityID());
    if IsDefined(player) && (evt.instigatorEntityID == player.GetEntityID() || relic) {
      let npc = relic ? TandemOutcomes.ProxyEvenDead(this.GetGame(), this) : TandemCombat.ProxyNpc(this.GetGame(), this);
      if npc != 0u {
        let ids: array<TweakDBID>;
        ArrayPush(ids, evt.staticData.GetID());
        TandemNative.SendNpcEffect(npc, ids);
        TandemNative.LogDebug(s"combat: status effect \(TDBID.ToStringDEBUG(evt.staticData.GetID())) on proxy \(npc) sent to its owner");
      }
    }
  }
  return result;
}

// Host: a remote player's puppet is invulnerable here (its record's), so
// the game would end each hit on it now, as "no damage". Its hits must go
// on to DealDamages, where they are sent to that player's game (above).
@wrapMethod(DamageSystem)
private final func InvulnerabilityCheck(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  if TandemCombat.PuppetPlayer(GetGameInstance(), hitEvent.target) != 0u {
    return;
  }
  wrappedMethod(hitEvent, cache);
}

// Every hit on a puppet, and the end of each forwarded hit on our V (the
// hits that never reached DealDamages: dodged, evaded, blocked, immune).
@wrapMethod(DamageSystem)
private final func ProcessPipeline(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  let game = GetGameInstance();
  // Host: a shot at a guest's puppet that the guest's game fires and decides
  // (Shots.reds): never a second hit from here.
  let shots = TandemShots.Of(game);
  if IsDefined(shots) && shots.SimulatedByGuest(hitEvent) {
    return;
  }
  // Any game: a bullet drawn here for someone else's shot (Presence.reds).
  // Its shooter's game decided what it hit.
  let presence = TandemPresenceSync.Of(game);
  if IsDefined(presence) && presence.Cosmetic(hitEvent) {
    return;
  }
  // Our V's bullet hit this after a ricochet: the others draw its bounce.
  TandemPresenceSync.OurRicochet(game, hitEvent);
  // A duel is its starter's: a hit on a fist fight's opponent from a V
  // that is not in it is dropped (Contests.reds).
  if TandemContests.Watched(game, hitEvent) {
    return;
  }
  // What another V's game computed already (its effects' ticks and
  // explosions, its thrown objects' copies, a guest's car copy's
  // collisions) arrives as strikes or AreaHit.
  if TandemOutcomes.IsRemoteComputed(game, hitEvent) {
    TandemNative.LogDebug(s"combat: another V's own hit on \(EntityID.ToDebugStringDecimal(hitEvent.target.GetEntityID())) left to their game (type \(EnumInt(hitEvent.attackData.GetAttackType())))");
    return;
  }
  let victim = TandemCombat.PuppetPlayer(game, hitEvent.target);
  if victim != 0u {
    TandemNative.LogDebug(s"combat: a hit on player \(victim)'s puppet enters the pipeline (type \(EnumInt(hitEvent.attackData.GetAttackType())))");
  }
  // Guest: a hit on a proxy goes to the host once its pipeline has ended,
  // with the poise PostProcess took.
  let outcomes = TandemOutcomes.Of(game);
  let proxy = TandemCombat.ProxyNpc(game, hitEvent.target) != 0u;
  let poise = proxy && IsDefined(outcomes) ? outcomes.Began(hitEvent) : 0.0;
  wrappedMethod(hitEvent, cache);
  if proxy && IsDefined(outcomes) {
    outcomes.Ended(hitEvent, poise);
  }
  if IsDefined(outcomes) {
    outcomes.Forget(hitEvent);
  }
  // A proxy's attack on our V we blocked or parried: the NPC recoils in its
  // owner's game (Outcomes.reds).
  TandemOutcomes.ReportDefence(game, hitEvent);
  let system = TandemCombat.System(game);
  if IsDefined(system) && system.IsForwardedHit(hitEvent) {
    if !hitEvent.attackData.HasFlag(hitFlag.SuccessfulAttack) {
      TandemNative.LogDebug(s"combat: npc hit on our V dealt no damage (no-damage flag \(hitEvent.attackData.HasFlag(hitFlag.DealNoDamage)), evaded \(hitEvent.attackData.HasFlag(hitFlag.WasEvaded)), blocked \(hitEvent.attackData.WasBlocked()), deflected \(hitEvent.attackData.WasDeflectedAny()))");
    }
    system.ForgetForwardedHit(hitEvent);
  }
}

// ---- host: the target's side of a hit on a puppet is its player's game's ----
// (final.reds 84603-84618 ModifyHitData, 84914 Process, 86213 and 86390.)

@wrapMethod(DamageSystem)
private final func ProcessDamageReduction(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func ProcessDodge(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func ProcessEvasion(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func ProcessMitigation(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func ProcessPlayerIncomingDamageMultiplier(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  if TandemCombat.OnPuppet(game, hitEvent) {
    return;
  }
  // Guest: a stand-in attack (TandemCombat.AttackRecord) is not the one the
  // sender's NPC made: its multiplier for a V as target is not applied, the
  // police's is (final.reds 84831).
  if TandemCombat.IsForwarded(game, hitEvent) && TandemCombat.IsStandIn(hitEvent.attackData.GetAttackDefinition().GetRecord()) {
    if IsDefined(hitEvent.target as PlayerPuppet) {
      hitEvent.attackComputed.MultAttackValue(PreventionSystem.GetDamageToPlayerMultiplier(game));
    }
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func CalculateTargetModifiers(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func ProcessBlockAndDeflect(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

// ---- guest: the attacker's side of a forwarded hit is the host's ----

@wrapMethod(DamageSystem)
private final func ConvertDPSToHitDamage(hitEvent: ref<gameHitEvent>) {
  let outcomes = TandemOutcomes.Of(GetGameInstance());
  if TandemCombat.IsForwarded(GetGameInstance(), hitEvent) || (IsDefined(outcomes) && outcomes.IsStrike(hitEvent)) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func CalculateSourceModifiers(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  if TandemCombat.IsForwarded(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent, cache);
}

@wrapMethod(DamageSystem)
public final func ProcessEffectiveRange(hitEvent: ref<gameHitEvent>) {
  if TandemCombat.IsForwarded(GetGameInstance(), hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
protected final func GetArmorPenetrationValue(weapon: ref<WeaponObject>, hitEvent: ref<gameHitEvent>) -> Float {
  let pierce = 0.0;
  if TandemCombat.ForwardedPierce(GetGameInstance(), hitEvent, pierce) {
    return pierce;
  }
  return wrappedMethod(weapon, hitEvent);
}
