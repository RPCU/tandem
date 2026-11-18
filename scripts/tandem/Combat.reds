// Fights against the host's NPCs (docs/ARCHITECTURE.md, milestone 5).
//
// On a guest every NPC is a proxy of the host's. When our V hits one, the
// game's damage pipeline runs as usual (aim, weapon, perks, the proxy's
// armour: same record, same stats) up to the point where it would take the
// health away. There, the damage is computed without being applied
// (StatPoolsManager.ApplyDamage with forReal false, what the game itself
// does on a client) and sent to the host, which applies it to the real NPC;
// the health and the death come back (Npcs.reds). The hit still shows here
// at once: hit marker, damage numbers, the proxy's hit reaction.

public abstract class TandemCombat {
  public static func ProxyNpc(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.NpcOfProxy(target.GetEntityID()) : 0u;
  }

  public static func PuppetPlayer(game: GameInstance, target: wref<GameObject>) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.PlayerOfPuppet(target.GetEntityID()) : 0u;
  }

  public static func IsGuestEffectTick(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    if !TandemNative.IsHost() {
      return false;
    }
    if !AttackData.IsDoT(hitEvent.attackData) {
      return false;
    }
    let instigator = hitEvent.attackData.GetInstigator();
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    let guest = IsDefined(instigator) && IsDefined(system) ? system.PlayerOfPuppet(instigator.GetEntityID()) : 0u;
    if guest != 0u {
      TandemNative.Log(s"combat: player \(guest)'s effect tick on \(EntityID.ToDebugStringDecimal(hitEvent.target.GetEntityID())) left to their game");
    }
    return guest != 0u;
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
    return flags;
  }
}

@wrapMethod(DamageSystem)
private final func DealDamages(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  // Host: the damage ticks of a status effect a guest put on our NPC are
  // the guest's to compute (its perks); they arrive as hits.
  if TandemCombat.IsGuestEffectTick(game, hitEvent) {
    return;
  }
  // Host: a remote player's puppet never takes damage here; the hit is
  // theirs (milestone 5: NPCs hitting guests).
  let victim = TandemCombat.PuppetPlayer(game, hitEvent.target);
  if victim != 0u {
    let measured: array<SDamageDealt>;
    StatPoolsManager.ApplyDamage(hitEvent, false, measured);
    let total = 0.0;
    for dealt in measured {
      total += dealt.value;
    }
    let from = hitEvent.attackData.GetInstigator();
    TandemNative.Log(s"combat: \(IsDefined(from) ? TDBID.ToStringDEBUG((from as ScriptedPuppet).GetRecordID()) : "?") hit player \(victim)'s puppet for \(total) (type \(EnumInt(hitEvent.attackData.GetAttackType())), physical \(hitEvent.attackComputed.GetAttackValue(gamedataDamageType.Physical)))");
    return;
  }
  let npc = TandemCombat.ProxyNpc(game, hitEvent.target);
  if npc == 0u {
    wrappedMethod(hitEvent);
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
  if damage > 0.0 && IsDefined(instigator) && instigator.IsPlayer() {
    TandemNative.SendNpcHit(npc, damage, TandemCombat.Flags(hitEvent), hitEvent.hitPosition, hitEvent.hitDirection);
    TandemNative.Log(s"combat: hit on proxy \(npc) for \(damage) sent to the host");
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
// the target's health to its minimum here. On a proxy, the host does it.
@wrapMethod(DamageSystem)
private final func ProcessInstantKill(hitEvent: ref<gameHitEvent>) {
  let npc = TandemCombat.ProxyNpc(GetGameInstance(), hitEvent.target);
  if npc == 0u || hitEvent.projectionPipeline || !hitEvent.attackData.HasFlag(hitFlag.Kill) {
    wrappedMethod(hitEvent);
    return;
  }
  hitEvent.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_proxy");
  hitEvent.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_proxy");
  let instigator = hitEvent.attackData.GetInstigator();
  if IsDefined(instigator) && instigator.IsPlayer() {
    TandemNative.SendNpcHit(npc, 0.0, TandemCombat.Flags(hitEvent), hitEvent.hitPosition, hitEvent.hitDirection);
    TandemNative.Log(s"combat: instant kill on proxy \(npc) sent to the host");
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
// on to DealDamages, where they are measured for that player's game and
// never applied (above).
@wrapMethod(DamageSystem)
private final func InvulnerabilityCheck(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  if TandemCombat.PuppetPlayer(GetGameInstance(), hitEvent.target) != 0u {
    return;
  }
  wrappedMethod(hitEvent, cache);
}

// (diagnosis, milestone 5) does any hit on a puppet reach the damage system?
@wrapMethod(DamageSystem)
private final func ProcessPipeline(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  let victim = TandemCombat.PuppetPlayer(GetGameInstance(), hitEvent.target);
  if victim != 0u {
    TandemNative.Log(s"combat: a hit on player \(victim)'s puppet enters the pipeline (type \(EnumInt(hitEvent.attackData.GetAttackType())))");
  }
  wrappedMethod(hitEvent, cache);
}
