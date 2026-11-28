// Bosses (docs/architecture/bosses.md). A boss is pinned to the game that
// runs it (Regions.reds TandemRegionRules.Pinned: the host for the story's);
// every other game shows its proxy, whose own copy of the boss's component
// (AdamSmasherComponent, ChimeraComponent, OdaComponent... final.reds 2.50)
// still runs: its health listener crosses the same phase thresholds from
// the owner's health (NpcHealth), and its status effect handlers toggle the
// same hit shapes, plates, appearances and invulnerable weak spots, if the
// proxy wears the same effects. What its health does not say travels here:
//
// - The owner: once a tick, each boss it runs, the status effects its
//   scripts read (Mirrors: everything but what NpcStatus and the hits carry)
//   and each weak spot's health, said on change (BossState, kept for late
//   joiners).
// - A proxy: the effects that came and went since the owner's last word,
//   once the proxy's health is the one they were said at (0.5 s at most: its
//   own listener crosses the phase first, as on the owner), and its weak
//   spots no higher than the owner's (a broken one breaks, the game's own
//   way: DestroyWeakspot and Kill, as the bosses' components break theirs).
// - Both: the game's own boss bar on our HUD for a boss in combat near our
//   V, hostile to it, whoever it fights (vanilla shows it only for a boss
//   whose threats hold our V: BossHealthBarGameController, BossCombatNotifier).
// - The owner: what vanilla gives "the player" when a boss's scripts react
//   (a quickhack, a weak spot broken), given to the V that did it
//   (TryStartCombatWithTarget, as BossStealthComponent and SasquatchComponent
//   do for the local V).
//
// Tests (tools/game.sh cmd): "boss catalogue", "boss spawn RECORD|kind:K|
// name:S", "boss kill", "boss health P", "boss effect RECORD", "boss spot N",
// "boss report"; queries "boss bar|effects|spots|target|targets|phase|hits|
// health|state" (TestQuery.reds).

public class TandemBossBody {
  public let npc: Uint32;
  public let id: EntityID;
  // Our game runs it (else a proxy of another game's).
  public let own: Bool;
  public let record: TweakDBID;
  public let kind: String;
  public let seenAt: Float;
  public let dead: Bool;
  // Owner: what we said last, and when; the effects on it last tick (all of
  // them: a new one is what its scripts just reacted to).
  public let sentEffects: array<TweakDBID>;
  public let sentSpots: array<Int32>;
  public let sentAt: Float;
  public let lastEffects: array<TweakDBID>;
  // Owner: the last other V whose fire took a share of one of its weak
  // spots (WeakSpotHit), and when.
  public let spotBy: Uint32;
  public let spotAt: Float;
  // A proxy: the owner's word as last applied (the effects we put on or
  // found on it), the word waiting for its health, since when.
  public let worn: array<TweakDBID>;
  public let wornOnce: Bool;
  // Its owner has said a word on it (a new body of it wears it again).
  public let heard: Bool;
  public let pending: Bool;
  public let pendingSince: Float;
  public let wordHealth: Float;
  public let fellBack: Bool;
  // Both: its bar on our HUD (BossCombatNotifier said), and whom it fought:
  // seconds of its combat target per player (owner), hits our V took near it.
  public let barSaid: Bool;
  public let targetPlayers: array<Uint32>;
  public let targetSeconds: array<Float>;
  public let hitsOnUs: Int32;
}

public class TandemBosses {
  private let m_game: GameInstance;
  private let m_bodies: array<ref<TandemBossBody>>;
  private let m_lastScan: Float;
  private let m_lastSample: Float;
  private let m_now: Float;
  private let m_you: Uint32;
  // Our V's health at the last sample (percent; -1 none yet).
  private let m_ourHealth: Float;
  // Owners' words that came before their proxy's body (a late joiner's
  // records come with the spawn), and the health each was said at.
  private let m_early: array<Uint32>;
  private let m_earlyHealth: array<Float>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_ourHealth = -1.0;
  }

  public func End() -> Void {
    for body in this.m_bodies {
      if !body.own {
        TandemFight.ForgetBoss(body.npc);
      }
    }
    ArrayClear(this.m_bodies);
    ArrayClear(this.m_early);
    ArrayClear(this.m_earlyHealth);
  }

  // What a boss is to us: "boss" (NPCRarity Boss), "chimera", "cerberus"
  // (their NPC types, PL's bosses), "maxtac" (the bar only), "" none.
  public static func Kind(npc: ref<ScriptedPuppet>) -> String {
    if !IsDefined(npc) {
      return "";
    }
    if npc.IsBoss() {
      return "boss";
    }
    let type = npc.GetNPCType();
    if Equals(type, gamedataNPCType.Chimera) {
      return "chimera";
    }
    if Equals(type, gamedataNPCType.Cerberus) {
      return "cerberus";
    }
    if Equals(npc.GetNPCRarity(), gamedataNPCRarity.MaxTac) {
      return "maxtac";
    }
    return "";
  }

  // A status effect a boss's scripts read for its phases, mirrored on its
  // proxies: not what other messages carry (quickhacks, a breach's daemons,
  // what stops an upload: NpcStatus; uploads: NpcHackState), not a damage
  // tick's (its hits travel), not a death (NpcGone).
  public static func Mirrors(record: ref<StatusEffect_Record>) -> Bool {
    if !IsDefined(record) {
      return false;
    }
    let id = record.GetID();
    if id == t"AIQuickHackStatusEffect.BeingHacked" || id == t"AIQuickHackStatusEffect.BeingHackedWithTraceableHack" {
      return false;
    }
    if record.GameplayTagsContains(n"Quickhack") || TandemBreach.IsDaemonEffect(id) || TandemHacks.Interrupts(record) {
      return false;
    }
    let type = gamedataStatusEffectType.Misc;
    let typed = record.StatusEffectType();
    if IsDefined(typed) {
      type = typed.Type();
    }
    switch type {
      case gamedataStatusEffectType.Bleeding:
      case gamedataStatusEffectType.Burning:
      case gamedataStatusEffectType.Electrocuted:
      case gamedataStatusEffectType.Poisoned:
      case gamedataStatusEffectType.Kill:
      case gamedataStatusEffectType.SoftKill:
        return false;
      default:
        break;
    }
    return true;
  }

  private func Find(npc: Uint32) -> ref<TandemBossBody> {
    for body in this.m_bodies {
      if body.npc == npc {
        return body;
      }
    }
    return null;
  }

  private func Entity(body: ref<TandemBossBody>) -> ref<NPCPuppet> {
    return GameInstance.FindEntityByID(this.m_game, body.id) as NPCPuppet;
  }

  // ---- the tick (TandemNpcs.Tick, the game thread) ----

  public func Tick(npcs: ref<TandemNpcs>, now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_you = you;
    if now - this.m_lastScan >= 1.0 {
      this.m_lastScan = now;
      this.Scan(npcs);
    }
    this.TakeQueued(npcs);
    for body in this.m_bodies {
      if !body.dead {
        if body.own {
          this.Owner(body);
        } else if body.pending {
          this.Wear(body, npcs);
        }
      }
    }
    if now - this.m_lastSample >= 0.5 {
      this.m_lastSample = now;
      this.Sample();
    }
  }

  // The bosses our game runs and the proxies it shows, once a second.
  private func Scan(npcs: ref<TandemNpcs>) -> Void {
    let kept: array<ref<TandemBossBody>>;
    for known in npcs.Known() {
      if !known.dead {
        this.Keep(known.npc, known.id, true, kept);
      }
    }
    for proxy in npcs.Proxies() {
      if !proxy.dead && proxy.seenAt > 0.0 {
        this.Keep(proxy.npc, proxy.id, false, kept);
      }
    }
    // Gone (dead, handed over, out of range): said once, forgotten.
    for body in this.m_bodies {
      if !ArrayContains(kept, body) {
        TandemNative.Log(s"bosses: \(body.own ? "our boss" : "proxy") \(body.npc) \(TDBID.ToStringDEBUG(body.record)) gone\(body.dead ? " (died)" : "")");
        if !body.own {
          TandemFight.ForgetBoss(body.npc);
        }
      }
    }
    this.m_bodies = kept;
  }

  private func Keep(npc: Uint32, id: EntityID, own: Bool, out kept: array<ref<TandemBossBody>>) -> Void {
    let body = this.Find(npc);
    if IsDefined(body) && Equals(body.own, own) && body.id == id {
      if !body.dead {
        let entity = this.Entity(body);
        if IsDefined(entity) && entity.IsDead() {
          body.dead = true;
          TandemNative.Log(s"bosses: \(own ? "our boss" : "proxy") \(npc) \(TDBID.ToStringDEBUG(body.record)) died here");
        }
      }
      ArrayPush(kept, body);
      return;
    }
    let entity = GameInstance.FindEntityByID(this.m_game, id) as NPCPuppet;
    let kind = TandemBosses.Kind(entity);
    if Equals(kind, "") {
      return;
    }
    let again = IsDefined(body) && !own && body.heard;
    let fresh = IsDefined(body) ? body : new TandemBossBody();
    fresh.npc = npc;
    fresh.id = id;
    fresh.own = own;
    fresh.record = entity.GetRecordID();
    fresh.kind = kind;
    fresh.seenAt = this.m_now;
    fresh.dead = entity.IsDead();
    ArrayClear(fresh.worn);
    fresh.wornOnce = false;
    ArrayPush(kept, fresh);
    let spots = TandemWeakSpots.Of(entity);
    TandemNative.Log(s"bosses: \(own ? "our boss" : "proxy") \(npc) \(TDBID.ToStringDEBUG(fresh.record)) (\(kind), type \(EnumInt(entity.GetNPCType())), \(ArraySize(spots)) weak spots) \(own ? "shared" : "here"), health \(FloatToStringPrec(TandemBosses.Health(this.m_game, entity), 1))%");
    // A word that came before its body; none yet, its own component's
    // attach-time effects stay until the first.
    let early = ArrayFindFirst(this.m_early, npc);
    if !own && early >= 0 {
      fresh.pending = true;
      fresh.heard = true;
      fresh.pendingSince = this.m_now;
      fresh.wordHealth = this.m_earlyHealth[early];
      ArrayErase(this.m_early, early);
      ArrayErase(this.m_earlyHealth, early);
    } else if again {
      fresh.pending = true;
      fresh.pendingSince = this.m_now;
      fresh.wordHealth = -1.0;
    }
  }

  public static func Health(game: GameInstance, npc: ref<GameObject>) -> Float {
    return GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(npc.GetEntityID()), gamedataStatPoolType.Health, true);
  }

  // ---- the owner's side ----

  private func Owner(body: ref<TandemBossBody>) -> Void {
    let npc = this.Entity(body);
    if !IsDefined(npc) || Equals(body.kind, "maxtac") {
      return;
    }
    if npc.IsDead() {
      return;
    }
    let all: array<TweakDBID>;
    let mirrored: array<TweakDBID>;
    let applied = StatusEffectHelper.GetAppliedEffects(npc);
    for effect in applied {
      let record = effect.GetRecord();
      if IsDefined(record) {
        let id = record.GetID();
        if !ArrayContains(all, id) {
          ArrayPush(all, id);
          if TandemBosses.Mirrors(record) {
            ArrayPush(mirrored, id);
          }
          // (Not on the first pass: what it wore when found is not news.)
          if body.sentAt > 0.0 && !ArrayContains(body.lastEffects, id) {
            this.Reacted(body, npc, effect, record);
          }
        }
      }
    }
    body.lastEffects = all;
    let spots: array<Int32>;
    let ours = TandemWeakSpots.Of(npc);
    for spot in ours {
      ArrayPush(spots, IsDefined(spot) ? CeilF(TandemWeakSpots.Health(this.m_game, spot)) : -1);
    }
    // Said on change, and again every 2 s (the endpoint drops a repeat).
    let changed = !TandemBosses.SameSet(mirrored, body.sentEffects) || !TandemBosses.SameInts(spots, body.sentSpots);
    if changed || this.m_now - body.sentAt >= 2.0 {
      if changed {
        TandemNative.Log(s"bosses: our boss \(body.npc) now \(ArraySize(mirrored)) effect(s) \(TandemBosses.Names(mirrored)), weak spots \(TandemBosses.Ints(spots))");
      }
      body.sentEffects = mirrored;
      body.sentSpots = spots;
      body.sentAt = this.m_now;
      TandemFight.SendBossState(body.npc, TandemBosses.Health(this.m_game, npc) / 100.0, mirrored, spots);
    }
  }

  // A status effect that just came onto one of our bosses: what vanilla
  // then gives the local V, given to the V that caused it.
  private func Reacted(body: ref<TandemBossBody>, npc: ref<NPCPuppet>, effect: ref<StatusEffect>, record: ref<StatusEffect_Record>) -> Void {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(system) {
      return;
    }
    // A quickhack: BossStealthComponent starts combat with the local V
    // (final.reds 2.50, OnStatusEffectApplied). The hacker's puppet joins
    // its threats while it fights our V.
    if record.GameplayTagsContains(n"Quickhack") {
      let hacker = system.PlayerOfPuppet(effect.GetInstigatorEntityID());
      let puppet = hacker != 0u ? system.PuppetOf(hacker) : null;
      if IsDefined(puppet) && !NPCPuppet.IsInCombatWithTarget(npc, puppet) && AIActionHelper.TryStartCombatWithTarget(npc, puppet) {
        TandemNative.Log(s"bosses: our boss \(body.npc) quickhacked by player \(hacker) (\(TDBID.ToStringDEBUG(record.GetID()))): in combat with their V too");
      }
    }
    // A weak spot broken: SasquatchComponent drops her hammer and starts
    // combat with the local V; the V whose fire broke it is the one.
    if record.GameplayTagsContains(n"WeakspotDestruction") && body.spotBy != 0u && this.m_now - body.spotAt < 10.0 {
      let puppet = system.PuppetOf(body.spotBy);
      if IsDefined(puppet) && AIActionHelper.TryStartCombatWithTarget(npc, puppet) {
        TandemNative.Log(s"bosses: our boss \(body.npc)'s weak spot broken by player \(body.spotBy): in combat with their V");
      }
    }
  }

  // Owner: another V's fire took a share of a weak spot of our NPC
  // (TandemWeakSpots.Take).
  public func SpotTaken(npc: Uint32, player: Uint32) -> Void {
    let body = this.Find(npc);
    if IsDefined(body) && body.own {
      body.spotBy = player;
      body.spotAt = this.m_now;
    }
  }

  // ---- a proxy's side ----

  // The owner's word changed (TandemFightEvent kind 16: damage is the
  // health it was said at).
  public func Take(event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let body = this.Find(event.npc);
    if !IsDefined(body) {
      // Its body is not here yet: the next scan finds it, its word pending.
      let early = ArrayFindFirst(this.m_early, event.npc);
      if early >= 0 {
        this.m_earlyHealth[early] = event.damage;
      } else if ArraySize(this.m_early) < 32 {
        ArrayPush(this.m_early, event.npc);
        ArrayPush(this.m_earlyHealth, event.damage);
      }
      return;
    }
    if body.own {
      return;
    }
    body.pending = true;
    body.heard = true;
    body.pendingSince = this.m_now;
    body.wordHealth = event.damage;
    this.Wear(body, npcs);
  }

  // The owner's word on its proxy, once the proxy's health is the one it was
  // said at, or 0.5 s on.
  private func Wear(body: ref<TandemBossBody>, npcs: ref<TandemNpcs>) -> Void {
    let proxy = this.Entity(body);
    if !IsDefined(proxy) || proxy.IsDead() {
      return;
    }
    let health = TandemBosses.Health(this.m_game, proxy) / 100.0;
    if body.wordHealth >= 0.0 && AbsF(health - body.wordHealth) > 0.015 && this.m_now - body.pendingSince < 0.5 {
      return;
    }
    body.pending = false;
    let effects = TandemFight.BossEffects(body.npc);
    let spots = TandemFight.BossSpots(body.npc);
    // The first word: what the proxy's own component put on it at its
    // attach and the owner does not have goes (a late joiner's proxy starts
    // in phase 1 whatever the fight's).
    if !body.wornOnce {
      body.wornOnce = true;
      let own = StatusEffectHelper.GetAppliedEffects(proxy);
      for effect in own {
        let record = effect.GetRecord();
        if IsDefined(record) && TandemBosses.Mirrors(record) && !ArrayContains(body.worn, record.GetID()) {
          ArrayPush(body.worn, record.GetID());
        }
      }
    }
    let on = 0;
    let off = 0;
    let unknown = 0;
    for id in body.worn {
      if !ArrayContains(effects, id) && StatusEffectSystem.ObjectHasStatusEffect(proxy, id) {
        StatusEffectHelper.RemoveStatusEffect(proxy, id);
        off += 1;
      }
    }
    for id in effects {
      if !StatusEffectSystem.ObjectHasStatusEffect(proxy, id) {
        if IsDefined(TweakDBInterface.GetStatusEffectRecord(id)) {
          StatusEffectHelper.ApplyStatusEffect(proxy, id, proxy.GetEntityID());
          on += 1;
        } else {
          unknown += 1;
        }
      }
    }
    body.worn = effects;
    let broke = this.Spots(body, proxy, spots);
    if on + off > 0 || broke > 0 {
      TandemNative.Log(s"bosses: proxy \(body.npc) wears its owner's state: \(on) effect(s) on, \(off) off, \(broke) weak spot(s) lowered, health \(FloatToStringPrec(health * 100.0, 1))% (said at \(FloatToStringPrec(body.wordHealth * 100.0, 1))%)");
    }
    if unknown > 0 && !body.fellBack {
      body.fellBack = true;
      TandemDegraded.Fallback("boss state", s"npc \(body.npc): \(unknown) of its owner's effect records are unknown here: its proxy shows its own");
    }
  }

  // Its weak spots no higher than the owner's: a broken one broken the way
  // the bosses' own components break theirs.
  private func Spots(body: ref<TandemBossBody>, proxy: ref<NPCPuppet>, spots: array<Int32>) -> Int32 {
    let ours = TandemWeakSpots.Of(proxy);
    if ArraySize(spots) == 0 {
      return 0;
    }
    if ArraySize(ours) != ArraySize(spots) && !body.fellBack {
      body.fellBack = true;
      TandemDegraded.Fallback("boss state", s"npc \(body.npc): \(ArraySize(ours)) weak spots here, \(ArraySize(spots)) on its owner's: the ones past the shorter list are its own");
    }
    let lowered = 0;
    let i = 0;
    while i < ArraySize(ours) && i < ArraySize(spots) {
      let spot = ours[i];
      let said = spots[i];
      if IsDefined(spot) && said >= 0 && !spot.IsDead() {
        let here = TandemWeakSpots.Health(this.m_game, spot);
        if said == 0 {
          let scripted = spot as ScriptedWeakspotObject;
          if IsDefined(scripted) {
            scripted.DestroyWeakspot(proxy);
          }
          ScriptedWeakspotObject.Kill(spot);
          lowered += 1;
        } else if here > Cast<Float>(said) + 0.5 {
          GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(spot.GetEntityID()), gamedataStatPoolType.WeakspotHealth, Cast<Float>(said), null, true);
          lowered += 1;
        }
      }
      i += 1;
    }
    return lowered;
  }

  // ---- both: the bar, whom it fights, what reached our V ----

  private func Sample() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let health = TandemBosses.Health(this.m_game, player);
    let hurt = this.m_ourHealth >= 0.0 && health < this.m_ourHealth - 0.5;
    this.m_ourHealth = health;
    let nearest: ref<TandemBossBody>;
    let nearestAt = 60.0;
    for body in this.m_bodies {
      let npc = this.Entity(body);
      if IsDefined(npc) && !body.dead {
        let fighting = this.Fighting(body, npc);
        let distance = Vector4.Distance(npc.GetWorldPosition(), player.GetWorldPosition());
        this.Bar(body, npc, player, fighting && distance <= 100.0);
        if fighting && distance < nearestAt {
          nearest = body;
          nearestAt = distance;
        }
        if body.own && fighting && IsDefined(system) {
          this.CountTarget(body, npc, player, system);
          this.Paired(body, npc, system);
        }
      }
    }
    if hurt && IsDefined(nearest) {
      nearest.hitsOnUs += 1;
      TandemNative.LogDebug(s"bosses: our V hurt near boss \(nearest.npc) (\(nearest.hitsOnUs) time(s))");
    }
  }

  // In combat, as its owner runs it: ours by its own state, a proxy by the
  // high-level state its owner's NPC has (NpcAnims.reds sets it).
  private func Fighting(body: ref<TandemBossBody>, npc: ref<NPCPuppet>) -> Bool {
    if npc.IsDead() || ScriptedPuppet.IsDefeated(npc) {
      return false;
    }
    if body.own {
      return NPCPuppet.IsInCombat(npc);
    }
    return Equals(npc.GetHighLevelStateFromBlackboard(), gamedataNPCHighLevelState.Combat);
  }

  // The game's own boss bar: the event vanilla queues on the player when a
  // boss's threats hold it (BossHealthBarGameController.ReevaluateBossHealthBar),
  // queued for a boss fighting any V near ours and hostile to ours.
  private func Bar(body: ref<TandemBossBody>, npc: ref<NPCPuppet>, player: ref<PlayerPuppet>, near: Bool) -> Void {
    let wanted = near && Equals(GameObject.GetAttitudeBetween(npc, player), EAIAttitude.AIA_Hostile) && !StatusEffectSystem.ObjectHasStatusEffect(npc, t"Chimera.HideBossHealthBar");
    if Equals(wanted, body.barSaid) {
      return;
    }
    body.barSaid = wanted;
    let notice = new BossCombatNotifier();
    notice.bossEntity = npc;
    notice.combatEnded = !wanted;
    player.QueueEvent(notice);
    TandemNative.Log(s"bosses: boss bar for \(body.own ? "our boss" : "proxy") \(body.npc) \(TDBID.ToStringDEBUG(body.record)) \(wanted ? "asked on our HUD" : "let go (out of the fight)")");
  }

  // Owner: whom it is after now, half a second of it.
  private func CountTarget(body: ref<TandemBossBody>, npc: ref<NPCPuppet>, player: ref<PlayerPuppet>, system: ref<TandemSystem>) -> Void {
    let tracker = npc.GetTargetTrackerComponent();
    let threat: TrackedLocation;
    if !IsDefined(tracker) || !tracker.GetTopHostileThreat(false, threat) {
      return;
    }
    let target = threat.entity as GameObject;
    let who = 0u;
    if IsDefined(target) && target.GetEntityID() == player.GetEntityID() {
      who = this.m_you;
    } else if IsDefined(target) {
      who = system.PlayerOfPuppet(target.GetEntityID());
    }
    if who == 0u {
      return;
    }
    let i = 0;
    while i < ArraySize(body.targetPlayers) {
      if body.targetPlayers[i] == who {
        body.targetSeconds[i] += 0.5;
        return;
      }
      i += 1;
    }
    ArrayPush(body.targetPlayers, who);
    ArrayPush(body.targetSeconds, 0.5);
    TandemNative.Log(s"bosses: our boss \(body.npc) targets player \(who)");
  }

  // ---- what the boss's behaviour did that its proxies must do ----

  // The queues the hooks below fill (any thread), on our tick.
  private func TakeQueued(npcs: ref<TandemNpcs>) -> Void {
    let heard = TandemFight.TakeBossAiEvents();
    for event in heard {
      let npc = TandemHacks.NpcOf(this.m_game, event.entity);
      let body = npc != 0u ? this.Find(npc) : null;
      // Ours only: a proxy hears the events its owner sends it (NpcAnims).
      if IsDefined(body) && body.own && npcs.Owns(npc) {
        if npcs.Anims().AiEvent(npc, event.name) {
          TandemNative.Log(s"bosses: our boss \(npc)'s AI event \(NameToString(event.name)) to its proxies");
        }
      }
    }
    let kills = TandemFight.TakeBossKills();
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    for kill in kills {
      let npc = TandemHacks.NpcOf(this.m_game, kill.npc);
      let player = IsDefined(system) ? system.PlayerOfPuppet(kill.target) : 0u;
      if npc != 0u && player != 0u && npcs.Owns(npc) {
        TandemFight.SendScriptedKill(player, npc, new Vector4(kill.x, kill.y, kill.z, 1.0));
        TandemNative.Log(s"bosses: our npc \(npc)'s scripted kill of player \(player)'s V: sent to their game");
      }
    }
  }

  // A proxy's owner says its NPC's scripted kill takes our V
  // (TandemFightEvent kind 17): what it does to its target, done to ours on
  // our game (Cerberus's: our V sent where its owner's target went).
  public func Killed(event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let player = GetPlayer(this.m_game);
    let proxy = npcs.ProxyEntity(event.npc);
    if !IsDefined(player) || !EntityID.IsDefined(proxy) {
      TandemNative.Log(s"bosses: npc \(event.npc)'s scripted kill of our V dropped (no proxy of it here)");
      return;
    }
    let to = new Vector4(event.x, event.y, event.z, 1.0);
    let facing = new EulerAngles();
    facing.Yaw = player.GetWorldYaw();
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, facing);
    TandemNative.Log(s"bosses: npc \(event.npc)'s scripted kill sends our V to \(to.X) \(to.Y) \(to.Z), as its owner's game did its puppet");
  }

  // Owner: a paired animation of the boss with a V's puppet (a workspot both
  // are in, within 3 m: a grab). The puppet's V plays its own body on its
  // own game; nothing tells it to take the pair's other half (bosses.md,
  // limit "a paired grab"): said, once a fight.
  private func Paired(body: ref<TandemBossBody>, npc: ref<NPCPuppet>, system: ref<TandemSystem>) -> Void {
    if body.fellBack {
      return;
    }
    let spots = GameInstance.GetWorkspotSystem(this.m_game);
    if !spots.IsActorInWorkspot(npc) {
      return;
    }
    let players = system.PuppetPlayers();
    for p in players {
      let puppet = system.PuppetOf(p);
      if IsDefined(puppet) && Vector4.Distance(puppet.GetWorldPosition(), npc.GetWorldPosition()) < 3.0 && spots.IsActorInWorkspot(puppet) {
        body.fellBack = true;
        TandemDegraded.Fallback("boss state", s"npc \(body.npc) (\(TDBID.ToStringDEBUG(body.record))) plays a paired animation with player \(p)'s puppet: only on our game");
        return;
      }
    }
  }

  // A remote V's puppet these senses detect (at all) while it wears its
  // V's cloak (a `Cloak`-tagged camo record: Stealth.reds puts its V's on
  // it), as CerberusDetectionOpticalCamo asks of the local V. `log`: each
  // puppet said (the test, on the game thread only).
  // job thread safe: it reads the puppets' ids (Codeware's tag lookup locks), their status effects and the senses.
  public static func CloakedSeen(game: GameInstance, senses: ref<SenseComponent>, log: Bool) -> Bool {
    let seen = false;
    let puppets = GameInstance.GetDynamicEntitySystem().GetTaggedIDs(n"Tandem.Puppet");
    for id in puppets {
      let puppet = GameInstance.FindEntityByID(game, id) as GameObject;
      let cloaked = IsDefined(puppet) && StatusEffectSystem.ObjectHasStatusEffectWithTag(puppet, n"Cloak");
      let detection = senses.GetDetection(id);
      if log {
        TandemNative.Log(s"bosses: camo check: puppet \(EntityID.ToDebugStringDecimal(id)) cloaked \(cloaked), detected \(FloatToStringPrec(detection, 1))%");
      }
      if cloaked && detection >= 1.00 {
        seen = true;
      }
    }
    return seen;
  }

  // Any thread (the hooks below): an AI event a boss's own component
  // heard, queued for the tick (a native that only queues).
  public static func Heard(owner: ref<GameObject>, event: ref<AIEvent>) -> Void {
    if IsDefined(owner) && IsDefined(event) && TandemNative.IsStarted() {
      TandemFight.QueueBossAiEvent(owner.GetEntityID(), event.name);
    }
  }

  // ---- small helpers ----

  public static func SameSet(a: array<TweakDBID>, b: array<TweakDBID>) -> Bool {
    if ArraySize(a) != ArraySize(b) {
      return false;
    }
    for id in a {
      if !ArrayContains(b, id) {
        return false;
      }
    }
    return true;
  }

  public static func SameInts(a: array<Int32>, b: array<Int32>) -> Bool {
    if ArraySize(a) != ArraySize(b) {
      return false;
    }
    let i = 0;
    while i < ArraySize(a) {
      if a[i] != b[i] {
        return false;
      }
      i += 1;
    }
    return true;
  }

  public static func Names(ids: array<TweakDBID>) -> String {
    let out = "";
    for id in ids {
      out += (StrLen(out) > 0 ? " " : "") + TDBID.ToStringDEBUG(id);
    }
    return StrLen(out) > 0 ? out : "none";
  }

  public static func Ints(values: array<Int32>) -> String {
    let out = "";
    for value in values {
      out += (StrLen(out) > 0 ? "," : "") + IntToString(value);
    }
    return StrLen(out) > 0 ? out : "none";
  }

  // The boss nearest our V (any: ours or a proxy).
  private func Nearest() -> ref<TandemBossBody> {
    let player = GetPlayer(this.m_game);
    let best: ref<TandemBossBody>;
    let bestAt = 1000000.0;
    if !IsDefined(player) {
      return best;
    }
    for body in this.m_bodies {
      let npc = this.Entity(body);
      if IsDefined(npc) {
        let at = Vector4.Distance(npc.GetWorldPosition(), player.GetWorldPosition());
        if at < bestAt {
          best = body;
          bestAt = at;
        }
      }
    }
    return best;
  }

  // ---- tests ----

  // (test) TestQuery.reds "boss WHAT": the nearest boss's, for a scenario.
  // bar: "shown" or "hidden" (the game's boss bar on our HUD); effects: how
  // many mirrored effects it wears; spots: each weak spot's health; target:
  // the player its owner's top threat is (owner only; "none"); phase: the
  // highest phase effect it wears (1-3, 0 none); hits: how often our V was
  // hurt near it; health: its health percent; state: "alive", "dead", or
  // "none" (no boss).
  public func Query(what: String) -> String {
    if Equals(what, "bar") {
      return TandemBosses.BarShown() ? "shown" : "hidden";
    }
    let body = this.Nearest();
    if !IsDefined(body) {
      return "none";
    }
    let npc = this.Entity(body);
    if Equals(what, "state") {
      return body.dead || !IsDefined(npc) || npc.IsDead() ? "dead" : "alive";
    }
    if !IsDefined(npc) {
      return "none";
    }
    if Equals(what, "effects") {
      let n = 0;
      let applied = StatusEffectHelper.GetAppliedEffects(npc);
      for effect in applied {
        if TandemBosses.Mirrors(effect.GetRecord()) {
          n += 1;
        }
      }
      return IntToString(n);
    }
    if Equals(what, "spots") {
      let values: array<Int32>;
      let spots = TandemWeakSpots.Of(npc);
      for spot in spots {
        ArrayPush(values, IsDefined(spot) ? CeilF(TandemWeakSpots.Health(this.m_game, spot)) : -1);
      }
      return TandemBosses.Ints(values);
    }
    if Equals(what, "target") {
      let most = 0u;
      let longest = 0.0;
      let i = 0;
      while i < ArraySize(body.targetPlayers) {
        if body.targetSeconds[i] > longest {
          longest = body.targetSeconds[i];
          most = body.targetPlayers[i];
        }
        i += 1;
      }
      return most == 0u ? "none" : IntToString(Cast<Int32>(most));
    }
    if Equals(what, "targets") {
      let out = "";
      let i = 0;
      while i < ArraySize(body.targetPlayers) {
        out += (StrLen(out) > 0 ? "," : "") + s"\(body.targetPlayers[i]):\(FloatToStringPrec(body.targetSeconds[i], 1))";
        i += 1;
      }
      return StrLen(out) > 0 ? out : "none";
    }
    if Equals(what, "phase") {
      return IntToString(TandemBosses.Phase(npc));
    }
    if Equals(what, "hits") {
      return IntToString(body.hitsOnUs);
    }
    if Equals(what, "health") {
      return FloatToStringPrec(TandemBosses.Health(this.m_game, npc), 1);
    }
    return "unknown";
  }

  // Its phase as the bosses' components mark it (their PhaseN effects).
  public static func Phase(npc: ref<GameObject>) -> Int32 {
    let phase = 0;
    let applied = StatusEffectHelper.GetAppliedEffects(npc);
    for effect in applied {
      let record = effect.GetRecord();
      if IsDefined(record) {
        let name = TDBID.ToStringDEBUG(record.GetID());
        if StrEndsWith(name, ".Phase3") {
          phase = Max(phase, 3);
        } else if StrEndsWith(name, ".Phase2") {
          phase = Max(phase, 2);
        } else if StrEndsWith(name, ".Phase1") {
          phase = Max(phase, 1);
        }
      }
    }
    return phase;
  }

  // The game's boss bar is up on our HUD (its module's root, as
  // PartyHud.reds finds it).
  public static func BarShown() -> Bool {
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    if !IsDefined(layer) {
      return false;
    }
    let controllers = layer.GetGameControllers();
    for controller in controllers {
      let bar = controller as BossHealthBarGameController;
      if IsDefined(bar) {
        let root = bar.GetRootWidget();
        return IsDefined(root) && root.IsVisible();
      }
    }
    return false;
  }

  // "boss catalogue": every Character record that is a boss (rarity Boss,
  // or a Chimera or Cerberus type), with what the boss bar reads of it.
  // "boss spawn RECORD|kind:boss|kind:chimera|kind:cerberus|name:PART": it,
  // hostile to every V, 15 m ahead (TandemSystem.SpawnTestHostile). On the
  // nearest boss we run: "boss kill" (it dies), "boss health P" (P percent
  // left), "boss effect RECORD" (that status effect on it), "boss spot N"
  // (its weak spot N broken), "boss ai NAME" (its behaviour's AI event),
  // "boss scripted-kill" (its scripted kill of the first puppet), "boss
  // camo" (its camo check of each puppet). "boss cloak": our V cloaked.
  // "boss report": each boss.
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "report";
    let arg = ArraySize(words) > 2 ? words[2] : "";
    if Equals(verb, "catalogue") {
      TandemBosses.Catalogue("");
    } else if Equals(verb, "spawn") {
      let record = StrBeginsWith(arg, "kind:") || StrBeginsWith(arg, "name:") ? TandemBosses.Catalogue(arg) : TDBID.Create(arg);
      let character = TweakDBInterface.GetCharacterRecord(record);
      let player = GetPlayer(this.m_game);
      if !IsDefined(character) || !IsDefined(player) {
        TandemNative.Log(s"bosses: no such character record: \(arg)");
        return;
      }
      let forward = player.GetWorldForward();
      let at = player.GetWorldPosition();
      at.X += forward.X * 15.0;
      at.Y += forward.Y * 15.0;
      let id = system.SpawnTestHostile(record, at, player.GetWorldOrientation(), true);
      TandemNative.Log(s"bosses: spawning \(TDBID.ToStringDEBUG(record)) 15 m ahead at \(at.X) \(at.Y) \(at.Z) (\(EntityID.IsDefined(id)))");
    } else if Equals(verb, "cloak") {
      let player = GetPlayer(this.m_game);
      StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.OpticalCamoCoolPerkShort", player.GetEntityID());
      TandemNative.Log("bosses: our V cloaked (test)");
    } else if Equals(verb, "report") {
      this.Report();
    } else {
      let body = this.Nearest();
      let npc = IsDefined(body) && body.own ? this.Entity(body) : null;
      if !IsDefined(npc) {
        TandemNative.Log(s"bosses: \(verb): no boss of ours here");
        return;
      }
      if Equals(verb, "kill") {
        npc.Kill(GetPlayer(this.m_game), false, false);
        TandemNative.Log(s"bosses: our boss \(body.npc) killed (test)");
      } else if Equals(verb, "ai") {
        // As its behaviour sends one: to the entity, its components hear it.
        let event = new AIEvent();
        event.name = StringToName(arg);
        npc.QueueEvent(event);
        TandemNative.Log(s"bosses: our boss \(body.npc) sent the AI event \(arg) (test)");
      } else if Equals(verb, "scripted-kill") {
        // As CerberusRangedKillTask does to a puppet it targets: queued.
        let players = system.PuppetPlayers();
        let puppet = ArraySize(players) > 0 ? system.PuppetOf(players[0]) : null;
        if IsDefined(puppet) {
          TandemFight.QueueBossKill(npc.GetEntityID(), puppet.GetEntityID(), new Vector4(-2174.81, -2376.33, -192.68, 1.0));
          TandemNative.Log(s"bosses: our boss \(body.npc)'s scripted kill of player \(players[0])'s puppet (test)");
        } else {
          TandemNative.Log("bosses: scripted-kill: no puppet");
        }
      } else if Equals(verb, "camo") {
        let senses = npc.GetSensesComponent();
        if IsDefined(senses) {
          TandemNative.Log(s"bosses: camo check of our boss \(body.npc): a cloaked V seen \(TandemBosses.CloakedSeen(this.m_game, senses, true))");
        }
      } else if Equals(verb, "health") {
        let percent = StringToFloat(arg, 50.0);
        GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(npc.GetEntityID()), gamedataStatPoolType.Health, percent, GetPlayer(this.m_game), true);
        TandemNative.Log(s"bosses: our boss \(body.npc) set to \(FloatToStringPrec(percent, 1))% health (test)");
      } else if Equals(verb, "effect") {
        StatusEffectHelper.ApplyStatusEffect(npc, TDBID.Create(arg), npc.GetEntityID());
        TandemNative.Log(s"bosses: our boss \(body.npc) given \(arg) (test)");
      } else if Equals(verb, "spot") {
        let spots = TandemWeakSpots.Of(npc);
        let index = StringToInt(arg, 0);
        if index >= 0 && index < ArraySize(spots) && IsDefined(spots[index]) {
          GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(spots[index].GetEntityID()), gamedataStatPoolType.WeakspotHealth, 0.0, GetPlayer(this.m_game), true);
          TandemNative.Log(s"bosses: our boss \(body.npc)'s weak spot \(index) broken (test)");
        } else {
          TandemNative.Log(s"bosses: our boss \(body.npc) has no weak spot \(index) (\(ArraySize(spots)))");
        }
      } else {
        TandemNative.Log(s"bosses: unknown command \(command)");
      }
    }
  }

  // Logs each boss record (rarity Boss, or a Chimera's or Cerberus's type)
  // and returns, for "kind:K" (boss, chimera, cerberus), the first of that
  // kind, for "name:S" the first boss record whose id holds S, else the
  // first Character record that does ("" logs them all).
  public static func Catalogue(kind: String) -> TweakDBID {
    let named = StrBeginsWith(kind, "name:") ? StrLower(StrAfterFirst(kind, "name:")) : "";
    let wanted = StrBeginsWith(kind, "kind:") ? StrAfterFirst(kind, "kind:") : "";
    let any = TDBID.None();
    let first = TDBID.None();
    let n = 0;
    let records = TandemAttitudeRelations.RecordsOf("Character");
    for entry in records {
      let character = entry as Character_Record;
      if IsDefined(character) {
        let type = IsDefined(character.CharacterType()) ? character.CharacterType().Type() : gamedataNPCType.Invalid;
        let rarity = IsDefined(character.Rarity()) ? character.Rarity().Type() : gamedataNPCRarity.Invalid;
        let what = Equals(rarity, gamedataNPCRarity.Boss) ? "boss" : (Equals(type, gamedataNPCType.Chimera) ? "chimera" : (Equals(type, gamedataNPCType.Cerberus) ? "cerberus" : ""));
        let holds = StrLen(named) > 0 && StrContains(StrLower(TDBID.ToStringDEBUG(character.GetID())), named);
        if holds && Equals(what, "") && !TDBID.IsValid(any) {
          any = character.GetID();
        }
        if !Equals(what, "") {
          n += 1;
          if Equals(kind, "") {
            let thresholds = "";
            let i = 0;
            while i < character.GetBossHealthBarThresholdsCount() {
              thresholds += (StrLen(thresholds) > 0 ? "," : "") + FloatToStringPrec(character.GetBossHealthBarThresholdsItem(i), 0);
              i += 1;
            }
            TandemNative.Log(s"bosses: catalogue \(TDBID.ToStringDEBUG(character.GetID())) \(what) type \(EnumInt(type)) rarity \(EnumInt(rarity)) bar thresholds [\(thresholds)] name \"\(LocKeyToString(character.FullDisplayName()))\"");
          } else if !TDBID.IsValid(first) && (Equals(what, wanted) || holds) {
            first = character.GetID();
          }
        }
      }
    }
    if Equals(kind, "") {
      TandemNative.Log(s"bosses: catalogue: \(n) boss record(s)");
    }
    if !TDBID.IsValid(first) && TDBID.IsValid(any) {
      first = any;
    }
    if !Equals(kind, "") {
      TandemNative.Log(s"bosses: catalogue \(kind): \(TDBID.IsValid(first) ? TDBID.ToStringDEBUG(first) : "none")");
    }
    return first;
  }

  private func Report() -> Void {
    TandemNative.Log(s"bosses: \(ArraySize(this.m_bodies)) boss(es) here, our boss bar \(TandemBosses.BarShown() ? "shown" : "hidden")");
    for body in this.m_bodies {
      let npc = this.Entity(body);
      let health = IsDefined(npc) ? FloatToStringPrec(TandemBosses.Health(this.m_game, npc), 1) : "?";
      let targets = "";
      let i = 0;
      while i < ArraySize(body.targetPlayers) {
        targets += s" player \(body.targetPlayers[i]) \(FloatToStringPrec(body.targetSeconds[i], 1)) s";
        i += 1;
      }
      TandemNative.Log(s"bosses: \(body.own ? "our boss" : "proxy") \(body.npc) \(TDBID.ToStringDEBUG(body.record)) \(body.kind) \(body.dead ? "dead" : "alive") health \(health)% phase \(IsDefined(npc) ? TandemBosses.Phase(npc) : 0) bar \(body.barSaid ? "asked" : "not asked") hits on our V \(body.hitsOnUs) targets:\(StrLen(targets) > 0 ? targets : " none")");
    }
  }
}

// ---- whom the bosses' own AI takes: every V, not only the local one ----

// Cerberus enters combat when the local player's detection passes 99.9
// (this condition true: not yet; final.reds 2.50 CerberusDetectionCombat):
// a remote V's puppet it has fully detected counts the same.
// job thread safe: it reads the NPC's senses and the puppets' ids (Codeware's tag lookup locks), and changes nothing.
@wrapMethod(CerberusDetectionCombat)
protected func Check(context: ScriptExecutionContext) -> AIbehaviorConditionOutcomes {
  let outcome = wrappedMethod(context);
  if NotEquals(outcome, AIbehaviorConditionOutcomes.True) || !TandemNative.IsStarted() {
    return outcome;
  }
  let owner = AIBehaviorScriptBase.GetPuppet(context);
  let senses = IsDefined(owner) ? owner.GetSensesComponent() : null;
  if !IsDefined(senses) || !owner.IsCerberus() {
    return outcome;
  }
  let puppets = GameInstance.GetDynamicEntitySystem().GetTaggedIDs(n"Tandem.Puppet");
  for id in puppets {
    if senses.GetDetection(id) > 99.90 {
      return AIbehaviorConditionOutcomes.False;
    }
  }
  return outcome;
}

// Smasher teleports around his target into "the player's" field of view,
// read from the active camera (final.reds 2.50
// SmasherFindTeleportPositionAroundTarget.IsInPlayersFOV): our V's. Around
// a remote V's puppet it is that V's view, read from its puppet.
// job thread safe: it reads positions and orientations only.
@wrapMethod(SmasherFindTeleportPositionAroundTarget)
private final func IsInPlayersFOV(context: ScriptExecutionContext, position: Vector4) -> Bool {
  let target = this.m_referenceTarget;
  if !TandemNative.IsStarted() || !IsDefined(target) || !TandemThreats.IsPuppet(target) {
    return wrappedMethod(context, position);
  }
  let eye = target.GetWorldPosition();
  eye.Z += 1.6;
  let direction = position - eye;
  let forward = target.GetWorldForward();
  let angle = Vector4.GetAngleDegAroundAxis(direction, forward, target.GetWorldUp());
  return AbsF(angle) < 30.0;
}

// ---- the AI events a boss's behaviour sends its own components ----
// (final.reds 2.50: Oda's blades and stealth, Kurt's knife and pistol
// looks and his scanner, Cerberus's scans and eye flares, a scanner's
// sweeps.) A proxy's behaviour is off: its owner's events reach it with
// the NPC's animation states (NpcAnims.reds), queued here on the owner.

// job thread safe: it calls the wrapped handler and queues the event's name (TandemBosses.Heard: a native that only queues).
@wrapMethod(OdaComponent)
protected cb func OnAIEvent(aiEvent: ref<AIEvent>) -> Bool {
  let result = wrappedMethod(aiEvent);
  TandemBosses.Heard(this.GetOwner(), aiEvent);
  return result;
}

// job thread safe: it calls the wrapped handler and queues the event's name (TandemBosses.Heard: a native that only queues).
@wrapMethod(KurtzBossComponent)
protected cb func OnAIEvent(aiEvent: ref<AIEvent>) -> Bool {
  let result = wrappedMethod(aiEvent);
  TandemBosses.Heard(this.GetOwner(), aiEvent);
  return result;
}

// job thread safe: it calls the wrapped handler and queues the event's name (TandemBosses.Heard: a native that only queues).
@wrapMethod(KurtzComponent)
protected cb func OnAIEvent(aiEvent: ref<AIEvent>) -> Bool {
  let result = wrappedMethod(aiEvent);
  TandemBosses.Heard(this.GetOwner(), aiEvent);
  return result;
}

// job thread safe: it calls the wrapped handler and queues the event's name (TandemBosses.Heard: a native that only queues).
@wrapMethod(CerberusComponent)
protected cb func OnAIEvent(aiEvent: ref<AIEvent>) -> Bool {
  let result = wrappedMethod(aiEvent);
  TandemBosses.Heard(this.GetOwner(), aiEvent);
  return result;
}

// job thread safe: it calls the wrapped handler and queues the event's name (TandemBosses.Heard: a native that only queues).
@wrapMethod(ScannerControlComponent)
protected cb func OnAIEvent(aiEvent: ref<AIEvent>) -> Bool {
  let result = wrappedMethod(aiEvent);
  TandemBosses.Heard(this.GetOwner(), aiEvent);
  return result;
}

// Cerberus's scripted kill teleports its combat target to one place
// (final.reds 2.50 CerberusRangedKillTask.Activate: -2174.81, -2376.33,
// -192.68). A remote V's puppet moved there is put back by its V's own
// stream: its V's game does it to its V instead (ScriptedKill).
// job thread safe: it queues the kill (a native that only queues) or calls the wrapped method.
@wrapMethod(CerberusRangedKillTask)
protected func Activate(context: ScriptExecutionContext) -> Void {
  let target = ScriptExecutionContext.GetArgumentObject(context, n"CombatTarget");
  let owner = ScriptExecutionContext.GetOwner(context);
  if !TandemNative.IsStarted() || !IsDefined(target) || !IsDefined(owner) || !TandemThreats.IsPuppet(target) {
    wrappedMethod(context);
    return;
  }
  TandemFight.QueueBossKill(owner.GetEntityID(), target.GetEntityID(), new Vector4(-2174.81, -2376.33, -192.68, 1.0));
}

// Cerberus reacts to the local player cloaked and detected
// (CerberusDetectionOpticalCamo): a remote V's puppet wears its V's camo
// record (Stealth.reds), so one cloaked and detected counts the same.
// job thread safe: it reads the NPC's senses, the puppets' ids (Codeware's tag lookup locks) and their status effects, and changes nothing.
@wrapMethod(CerberusDetectionOpticalCamo)
protected func Check(context: ScriptExecutionContext) -> AIbehaviorConditionOutcomes {
  let outcome = wrappedMethod(context);
  if Equals(outcome, AIbehaviorConditionOutcomes.True) || !TandemNative.IsStarted() {
    return outcome;
  }
  let owner = AIBehaviorScriptBase.GetPuppet(context);
  let senses = IsDefined(owner) ? owner.GetSensesComponent() : null;
  if !IsDefined(senses) || !owner.IsCerberus() {
    return outcome;
  }
  return TandemBosses.CloakedSeen(owner.GetGame(), senses, false) ? AIbehaviorConditionOutcomes.True : outcome;
}
