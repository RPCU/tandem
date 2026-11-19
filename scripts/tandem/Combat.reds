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
  // game's (every stage of it is skipped here): on the host, an NPC's; on
  // any game, our V's area effect (Outcomes.reds).
  public static func OnPuppet(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    return TandemCombat.PuppetPlayer(game, hitEvent.target) != 0u && (TandemNative.IsHost() || TandemOwn.Hit(game, hitEvent) || TandemOutcomes.IsOurArea(game, hitEvent));
  }

  // A hit someone else's game computed the attacker's side of, run on our
  // V here: the host's NPC's (PlayerHitEvent), another V's area effect.
  public static func IsForwarded(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let system = TandemCombat.System(game);
    return IsDefined(system) && system.IsForwardedHit(hitEvent);
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
    TandemNative.Log(s"combat: \(IsDefined(from) ? TDBID.ToStringDEBUG(GameObject.GetTDBID(from)) : "?") (npc \(npc)) hit player \(victim)'s puppet: \(physical) physical, \(thermal) thermal, \(electric) electric, \(chemical) chemical, type \(EnumInt(hitEvent.attackData.GetAttackType())), flags \(flags), sent to their game");
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
    let melee = (hit.flags & 4) != 0;
    let record = TweakDBInterface.GetAttackRecord(hit.attack);
    if !IsDefined(record) {
      record = TweakDBInterface.GetAttackRecord(melee ? t"Attacks.ForwardPush" : t"Attacks.REMOVE_BulletWithDamage");
    }
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

@wrapMethod(DamageSystem)
private final func DealDamages(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  // A remote player's puppet never takes damage here: the host sends an
  // NPC's hit to that player's game, any game its own V's area effects
  // (Outcomes.reds); that game applies it to its V.
  let victim = TandemCombat.PuppetPlayer(game, hitEvent.target);
  if victim != 0u {
    if (TandemNative.IsHost() || TandemOwn.Hit(game, hitEvent)) && TandemCombat.ForwardsToPlayer(hitEvent, game) {
      TandemCombat.SendPlayerHit(game, victim, hitEvent, this.GetArmorPenetrationValue(hitEvent.attackData.GetWeapon(), hitEvent));
    } else if TandemOutcomes.IsOurArea(game, hitEvent) {
      TandemOutcomes.SendAreaHit(game, victim, hitEvent);
    }
    return;
  }
  // Guest: a proxy attacks nobody (the host's NPC does, as PlayerHit).
  if TandemOutcomes.IsProxyComputed(game, hitEvent) {
    TandemNative.Log("combat: a proxy's own hit on our V dropped (the host's NPC is the one that attacks)");
    return;
  }
  // Guest: the host's NPC's hit on our V, applied here like any other.
  if TandemCombat.IsForwarded(game, hitEvent) {
    let before = TandemCombat.Health(game, hitEvent.target);
    wrappedMethod(hitEvent);
    let after = TandemCombat.Health(game, hitEvent.target);
    TandemNative.Log(s"combat: npc hit applied to our V: \(hitEvent.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health)) damage, health \(before) -> \(after)");
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
    TandemNative.Log(s"combat: hit on proxy \(npc) for \(damage), for the host (skill XP for \(xp)% of its health queued)");
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
  let result = wrappedMethod(evt);
  // A V is never crippled or dismembered (Outcomes.reds KeepsLimbs).
  if IsDefined(evt.staticData) && TandemOutcomes.IsLimbEffect(evt.staticData) && TandemOutcomes.KeepsLimbs(this, s"status effect \(TDBID.ToStringDEBUG(evt.staticData.GetID()))") {
    StatusEffectHelper.RemoveStatusEffect(this, evt.staticData.GetID());
    return result;
  }
  if evt.isNewApplication && !evt.isAppliedOnSpawn && IsDefined(evt.staticData) {
    let player = GetPlayer(this.GetGame());
    if IsDefined(player) && evt.instigatorEntityID == player.GetEntityID() {
      let npc = TandemCombat.ProxyNpc(this.GetGame(), this);
      if npc != 0u {
        let ids: array<TweakDBID>;
        ArrayPush(ids, evt.staticData.GetID());
        TandemNative.SendNpcEffect(npc, ids);
        TandemNative.Log(s"combat: status effect \(TDBID.ToStringDEBUG(evt.staticData.GetID())) on proxy \(npc) sent to the host");
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
  // Host: what a guest's game computed already (its effects' ticks and
  // explosions, its car copy's collisions) arrives as strikes.
  if TandemOutcomes.IsGuestComputed(game, hitEvent) {
    TandemNative.Log(s"combat: a guest's own hit on \(EntityID.ToDebugStringDecimal(hitEvent.target.GetEntityID())) left to their game (type \(EnumInt(hitEvent.attackData.GetAttackType())))");
    return;
  }
  let victim = TandemCombat.PuppetPlayer(game, hitEvent.target);
  if victim != 0u {
    TandemNative.Log(s"combat: a hit on player \(victim)'s puppet enters the pipeline (type \(EnumInt(hitEvent.attackData.GetAttackType())))");
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
  let system = TandemCombat.System(game);
  if IsDefined(system) && system.IsForwardedHit(hitEvent) {
    if !hitEvent.attackData.HasFlag(hitFlag.SuccessfulAttack) {
      TandemNative.Log(s"combat: npc hit on our V dealt no damage (no-damage flag \(hitEvent.attackData.HasFlag(hitFlag.DealNoDamage)), evaded \(hitEvent.attackData.HasFlag(hitFlag.WasEvaded)), blocked \(hitEvent.attackData.WasBlocked()), deflected \(hitEvent.attackData.WasDeflectedAny()))");
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
  if TandemCombat.OnPuppet(GetGameInstance(), hitEvent) {
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
