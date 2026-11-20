// Enemy scaling (docs/architecture/npcs.md, "Enemy scaling"; the numbers and
// the count: core's tandem/scaling.hpp, through ProgressNatives.reds). A
// hostile NPC is tougher for each extra player near it: health ×(1 + 0.6·(n−1)),
// poise and the hit reaction thresholds ×(1 + 0.25·(n−1)), armour +10 % per
// extra player up to +20 %. The damage it deals is never touched: each V takes
// vanilla hits. Off with the host's tandem.ini `enemy_scaling = off`.
//
// The game that runs the NPC (the host, or its region's owner) counts the
// players near it twice a second (within 100 m, fighting it or its target;
// out beyond 130 m or 5 s after the fight; ScaleCount) and puts one
// Multiplier stat modifier per stat on it, the way the game's own difficulty
// and level scaling put theirs: a Multiplier multiplies the stat's final
// value, after every additive term (its level's curve, the difficulty's, its
// rarity's), so ours counts once. Plain AddModifier: never saved. When the
// count changes the NPC keeps its share of health and poise (no heal, no
// drop). It says the count (NpcScale) and every game that shows the NPC as
// a proxy puts the same modifiers on it: a guest's hit is computed against
// the proxy's armour, and its kill prediction against its health.
//
// The hit reaction thresholds (StaggerDamageThreshold, KnockdownDamageThreshold
// and their in-cover twins) are percentages of the total health the NPC's
// HitReactionComponent read when it was attached: our modifier on them makes
// the threshold in points the unscaled health's ×poise, whatever that
// component read (ScaleThreshold).

public class TandemScaled {
  public let npc: Uint32;
  public let id: EntityID;
  // The count its modifiers are for (1: none on), and the rule's health
  // then (a rule change puts them on again).
  public let players: Int32;
  public let health: Float;
  public let mods: array<ref<gameStatModifierData>>;
  // Owner: hostile to a V once (only those are scaled), and the count last said.
  public let hostile: Bool;
  public let sent: Int32;
}

public class TandemScaling {
  private let m_game: GameInstance;
  private let m_bodies: array<ref<TandemScaled>>;
  // The owners' word: how many players each of their NPCs is scaled for.
  private let m_words: array<Uint32>;
  private let m_wordCounts: array<Int32>;
  private let m_last: Float;
  private let m_lastProxies: Float;
  // For the report: NPCs scaled now, changes since the session's start.
  private let m_changes: Int32;
  private let m_told: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  // Owner: twice a second (TandemNpcs.Share asks each round).
  public func Due(now: Float) -> Bool {
    if now - this.m_last < 0.5 {
      return false;
    }
    this.m_last = now;
    return true;
  }

  private func Entry(npc: Uint32, id: EntityID) -> ref<TandemScaled> {
    for entry in this.m_bodies {
      if entry.npc == npc {
        if NotEquals(entry.id, id) {
          // A new body (spawned again in place): our modifiers went with the old one.
          entry.id = id;
          entry.players = 1;
          ArrayClear(entry.mods);
        }
        return entry;
      }
    }
    let entry = new TandemScaled();
    entry.npc = npc;
    entry.id = id;
    entry.players = 1;
    ArrayPush(this.m_bodies, entry);
    return entry;
  }

  private func Find(npc: Uint32) -> ref<TandemScaled> {
    for entry in this.m_bodies {
      if entry.npc == npc {
        return entry;
      }
    }
    return null;
  }

  // Hostile to one of the Vs, or fighting: an enemy. Civilians, vendors,
  // companions and allies are never scaled.
  public static func IsHostile(npc: ref<ScriptedPuppet>, watched: array<ref<GameObject>>) -> Bool {
    if NPCPuppet.IsInCombat(npc) {
      return true;
    }
    for v in watched {
      if IsDefined(v) && Equals(GameObject.GetAttitudeBetween(npc, v), EAIAttitude.AIA_Hostile) {
        return true;
      }
    }
    return false;
  }

  // ---- the owner's side ----

  // One of the NPCs our game runs, on a due round: counted, scaled, said.
  // watched: each player's V here (ours, the others' puppets; player p at
  // p - 1). hitBy / hitAt: the last guest who hit it and when; ownHitAt: our V.
  public func Own(npc: ref<ScriptedPuppet>, id: Uint32, watched: array<ref<GameObject>>, you: Uint32, hitBy: Uint32, hitAt: Float, ownHitAt: Float, now: Float) -> Void {
    let entry = this.Entry(id, npc.GetEntityID());
    if !entry.hostile {
      entry.hostile = TandemScaling.IsHostile(npc, watched);
    }
    let players = 1;
    if entry.hostile && TandemProgress.EnemyScaling() {
      let at = npc.GetWorldPosition();
      let distances: array<Float>;
      let engaged = 0;
      let bit = 1;
      let p = 1u;
      // Whom it is after.
      let target: ref<GameObject>;
      let tracker = npc.GetTargetTrackerComponent();
      let threat: TrackedLocation;
      if IsDefined(tracker) && tracker.GetTopHostileThreat(false, threat) {
        target = threat.entity as GameObject;
      }
      for v in watched {
        if IsDefined(v) {
          ArrayPush(distances, Vector4.Distance(at, v.GetWorldPosition()));
          let hit = p == you ? now - ownHitAt < 5.0 && ownHitAt > 0.0 : hitBy == p && now - hitAt < 5.0;
          if hit || target == v || NPCPuppet.IsInCombatWithTarget(npc, v) {
            engaged += bit;
          }
        } else {
          ArrayPush(distances, -1.0);
        }
        bit *= 2;
        p += 1u;
      }
      players = TandemProgress.ScaleCount(id, distances, engaged, now);
    }
    this.Apply(entry, npc, players);
    // Said once it is more than one, and on every change after.
    if players != entry.sent && (entry.sent != 0 || players > 1) {
      entry.sent = players;
      TandemProgress.SendNpcScale(id, players);
      this.Word(id, players);
    }
  }

  // ---- the proxies' side ----

  // An owner's word (TandemFightEvent kind 12): kept for a proxy not there yet.
  public func Word(npc: Uint32, players: Int32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_words) {
      if this.m_words[i] == npc {
        this.m_wordCounts[i] = players;
        return;
      }
      i += 1;
    }
    ArrayPush(this.m_words, npc);
    ArrayPush(this.m_wordCounts, players);
  }

  public func WordOf(npc: Uint32) -> Int32 {
    let i = 0;
    while i < ArraySize(this.m_words) {
      if this.m_words[i] == npc {
        return this.m_wordCounts[i];
      }
      i += 1;
    }
    return 1;
  }

  // A proxy, once its body exists and on every pass: the owner's count on it.
  public func Proxy(npc: Uint32, body: ref<ScriptedPuppet>) -> Void {
    this.Apply(this.Entry(npc, body.GetEntityID()), body, this.WordOf(npc));
  }

  // Each proxy again once a second (a word that came before its body, a rule
  // that changed).
  public func Proxies(npcs: ref<TandemNpcs>, now: Float) -> Void {
    if now - this.m_lastProxies < 1.0 {
      return;
    }
    this.m_lastProxies = now;
    for proxy in npcs.Proxies() {
      if !proxy.dead && proxy.seenAt > 0.0 {
        let body = npcs.ProxyBody(proxy.id) as ScriptedPuppet;
        if IsDefined(body) && !body.IsDead() {
          this.Proxy(proxy.npc, body);
        }
      }
    }
  }

  // ---- both ----

  // The modifiers for `players` on its body, its shares of health and poise kept.
  private func Apply(entry: ref<TandemScaled>, body: ref<ScriptedPuppet>, players: Int32) -> Void {
    if body.IsDead() {
      return;
    }
    let rule = TandemProgress.EnemyScaling();
    let health = TandemProgress.EnemyScalingHealth();
    let wanted = rule ? Max(players, 1) : 1;
    if wanted == entry.players && (wanted == 1 || AbsF(health - entry.health) < 0.001) {
      return;
    }
    let id = Cast<StatsObjectID>(body.GetEntityID());
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    // Its shares now, put back once the maximums have moved.
    let healthShare = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, true);
    let poiseShare = pools.IsStatPoolAdded(id, gamedataStatPoolType.Poise) ? pools.GetStatPoolValue(id, gamedataStatPoolType.Poise, true) : -1.0;
    let before = pools.GetStatPoolMaxPointValue(id, gamedataStatPoolType.Health);
    this.Strip(entry, stats, id);
    let base = stats.GetStatValue(id, gamedataStatType.Health);
    let multiplier = TandemProgress.ScaleHealth(wanted);
    let poise = TandemProgress.ScalePoise(wanted);
    let armor = TandemProgress.ScaleArmor(wanted);
    let threshold = 1.0;
    if wanted > 1 {
      let reactions = body.GetHitReactionComponent();
      let seen = IsDefined(reactions) ? reactions.TandemTotalHealth() : 0.0;
      threshold = TandemProgress.ScaleThreshold(wanted, base, seen);
      this.Put(entry, stats, id, gamedataStatType.Health, multiplier);
      this.Put(entry, stats, id, gamedataStatType.Poise, poise);
      this.Put(entry, stats, id, gamedataStatType.Armor, armor);
      this.Put(entry, stats, id, gamedataStatType.StaggerDamageThreshold, threshold);
      this.Put(entry, stats, id, gamedataStatType.StaggerDamageThresholdInCover, threshold);
      this.Put(entry, stats, id, gamedataStatType.KnockdownDamageThreshold, threshold);
      this.Put(entry, stats, id, gamedataStatType.KnockdownDamageThresholdInCover, threshold);
    }
    if healthShare > 0.0 {
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, healthShare, null, true);
    }
    if poiseShare >= 0.0 {
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Poise, poiseShare, null, true);
    }
    let was = entry.players;
    entry.players = wanted;
    entry.health = health;
    this.m_changes += 1;
    TandemNative.Log(s"scaling: npc \(entry.npc) \(TDBID.ToStringDEBUG(body.GetRecordID()))\(body.IsBoss() ? " (boss)" : "") rarity \(EnumInt(body.GetNPCRarity())): \(was) -> \(wanted) players, health x\(multiplier) (max \(before) -> \(pools.GetStatPoolMaxPointValue(id, gamedataStatPoolType.Health)), \(healthShare) % kept), poise x\(poise), armor x\(armor), thresholds x\(threshold)");
    if wanted > 1 && !this.m_told {
      this.m_told = true;
      TandemCoop.Notice(this.m_game, "Scaling-On");
    }
  }

  private func Put(entry: ref<TandemScaled>, stats: ref<StatsSystem>, id: StatsObjectID, stat: gamedataStatType, value: Float) -> Void {
    if AbsF(value - 1.0) < 0.0001 {
      return;
    }
    let modifier = RPGManager.CreateStatModifier(stat, gameStatModifierType.Multiplier, value);
    if stats.AddModifier(id, modifier) {
      ArrayPush(entry.mods, modifier);
    }
  }

  private func Strip(entry: ref<TandemScaled>, stats: ref<StatsSystem>, id: StatsObjectID) -> Void {
    for modifier in entry.mods {
      stats.RemoveModifier(id, modifier);
    }
    ArrayClear(entry.mods);
  }

  // Our modifiers off a body that lives on (no longer shared, the session
  // is over), its shares kept; nothing of it is kept.
  public func Release(npc: Uint32) -> Void {
    let entry = this.Find(npc);
    if IsDefined(entry) {
      let body = GameInstance.FindEntityByID(this.m_game, entry.id) as ScriptedPuppet;
      if IsDefined(body) && !body.IsDead() && ArraySize(entry.mods) > 0 {
        this.Apply(entry, body, 1);
      }
    }
    this.Forget(npc);
  }

  // It died, went or its proxy was removed: nothing is kept (a corpse keeps
  // its modifiers: nothing reads them).
  public func Forget(npc: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_bodies) {
      if this.m_bodies[i].npc == npc {
        ArrayErase(this.m_bodies, i);
      } else {
        i += 1;
      }
    }
    i = 0;
    while i < ArraySize(this.m_words) {
      if this.m_words[i] == npc {
        ArrayErase(this.m_words, i);
        ArrayErase(this.m_wordCounts, i);
      } else {
        i += 1;
      }
    }
    TandemProgress.ForgetScale(npc);
  }

  // An NPC changed hands (Npcs.reds Give, Lost, Take): the count it was said
  // for stays its word until its new owner says its own; ours to say again
  // when it comes back.
  public func Handed(npc: Uint32) -> Void {
    let entry = this.Find(npc);
    if IsDefined(entry) {
      entry.sent = 0;
    }
    TandemProgress.ForgetScale(npc);
  }

  // The session is over: every NPC as its own game made it.
  public func End() -> Void {
    let npcs: array<Uint32>;
    for entry in this.m_bodies {
      ArrayPush(npcs, entry.npc);
    }
    for npc in npcs {
      this.Release(npc);
    }
    ArrayClear(this.m_bodies);
    ArrayClear(this.m_words);
    ArrayClear(this.m_wordCounts);
    TandemProgress.ClearScales();
    this.m_told = false;
  }

  // Bodies with our modifiers on now.
  public func On() -> Int32 {
    let n = 0;
    for entry in this.m_bodies {
      if ArraySize(entry.mods) > 0 {
        n += 1;
      }
    }
    return n;
  }

  // tools/game.sh cmd npc scale: the rule, and each NPC we scale or show.
  public func Report() -> Void {
    TandemNative.Log(s"scaling: rule \(TandemProgress.EnemyScaling() ? "on" : "off"), health +\(TandemProgress.EnemyScalingHealth()) per extra player; \(ArraySize(this.m_bodies)) NPCs known, \(this.On()) scaled, \(this.m_changes) changes, \(ArraySize(this.m_words)) owners' words");
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let stats = GameInstance.GetStatsSystem(this.m_game);
    for entry in this.m_bodies {
      let body = GameInstance.FindEntityByID(this.m_game, entry.id) as ScriptedPuppet;
      if IsDefined(body) {
        let id = Cast<StatsObjectID>(entry.id);
        let reactions = body.GetHitReactionComponent();
        TandemNative.Log(s"scaling: npc \(entry.npc) \(TDBID.ToStringDEBUG(body.GetRecordID())) for \(entry.players) players (said \(entry.sent), word \(this.WordOf(entry.npc))), hostile \(entry.hostile), health \(pools.GetStatPoolValue(id, gamedataStatPoolType.Health, false)) of \(pools.GetStatPoolMaxPointValue(id, gamedataStatPoolType.Health)), poise \(pools.GetStatPoolValue(id, gamedataStatPoolType.Poise, false)) of \(pools.GetStatPoolMaxPointValue(id, gamedataStatPoolType.Poise)) (stat \(stats.GetStatValue(id, gamedataStatType.Poise))), armor \(stats.GetStatValue(id, gamedataStatType.Armor)), stagger \(stats.GetStatValue(id, gamedataStatType.StaggerDamageThreshold)) % of \(IsDefined(reactions) ? reactions.TandemTotalHealth() : 0.0), \(ArraySize(entry.mods)) modifiers");
      }
    }
  }
}

// The total health the hit reaction thresholds are a percentage of, as the
// component read it (GetDBParameters, once after the NPC is attached).
@addMethod(HitReactionComponent)
public func TandemTotalHealth() -> Float {
  return this.m_totalHealth;
}
