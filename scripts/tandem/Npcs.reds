// One set of NPCs (docs/ARCHITECTURE.md, milestone 5). The host's game runs
// them; it announces each NPC near its V to the guests and sends where they
// all are ten times a second. A guest shows each one as a proxy: the same
// record and appearance with its brain off (TandemProxies.Pacify), moved by
// tandem's controller like a remote player.
//
// Fights: a guest's hit on a proxy goes to the host (Combat.reds), which
// applies it to the real NPC; health and death come back to every guest.

public class TandemNpcKnown {
  public let id: EntityID;
  public let npc: Uint32;
  public let seen: Bool;
  // Health last sent (fraction 0..1; -1 none yet), and whether we said it died.
  public let health: Float;
  public let dead: Bool;
  // The last guest who hit it, and when: its killer when the game names none.
  public let lastHitBy: Uint32;
  public let lastHitAt: Float;
}

public class TandemNpcProxy {
  public let npc: Uint32;
  public let id: EntityID;
  public let ready: Bool;
  // When the entity first existed: the controller waits for its rig to load.
  public let seenAt: Float;
  // The host's NPC died: the proxy is a corpse, no longer moved or hit.
  public let dead: Bool;
  // The host's health for it (fraction), applied once the proxy exists.
  public let health: Float;
}

public class TandemNpcs {
  private let m_game: GameInstance;
  private let m_known: array<ref<TandemNpcKnown>>;
  private let m_next: Uint32;
  private let m_last: Float;
  private let m_proxies: array<ref<TandemNpcProxy>>;
  // Guest: its own NPCs are never the world's (the host's proxies are), so
  // its crowd is turned off and what still spawns is hidden.
  private let m_quiet: Bool;
  private let m_lastQuiet: Float;
  private let m_hidden: Int32;
  private let m_hiddenIds: array<EntityID>;
  // Host: guests' status effects waiting for their puppet (the instigator).
  private let m_pendingEffects: array<TandemNpcEvent>;
  private let m_pendingSince: array<Float>;
  // Our player id in the session, and the game time (Tick).
  private let m_you: Uint32;
  private let m_now: Float;
  // Host: how far each NPC is in noticing each guest, gathered by Share and
  // sent every other round (5 Hz; Stealth.reds shows it on the guest).
  private let m_awareNpcs: array<Uint32>;
  private let m_awarePlayers: array<Uint32>;
  private let m_awareLevels: array<Float>;
  private let m_rounds: Int32;

  public static func Range() -> Float {
    return 60.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_next = 1u;
  }

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_you = you;
    this.m_now = now;
    if TandemNative.IsHost() {
      this.TakeHits();
      if now - this.m_last >= 0.1 {
        this.m_last = now;
        this.Share();
      }
    } else {
      if !this.m_quiet {
        this.m_quiet = true;
        GameInstance.GetCommunitySystem(this.m_game).ChangeDensityModifier(0.0);
        TandemNative.Log("npcs: guest, own crowd off");
      }
      if now - this.m_lastQuiet >= 0.5 {
        this.m_lastQuiet = now;
        this.HideOwn();
      }
      this.Follow(now);
    }
  }

  // The session is over: the host's NPCs go, our crowd comes back. (Our own
  // NPCs hidden meanwhile stay hidden until they stream out.)
  public func End() -> Void {
    for proxy in this.m_proxies {
      TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
      GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
    }
    ArrayClear(this.m_proxies);
    ArrayClear(this.m_known);
    this.Stop();
  }

  // The game's crowd back as it was (leaving the session).
  public func Stop() -> Void {
    ArrayClear(this.m_hiddenIds);
    if this.m_quiet {
      GameInstance.GetCommunitySystem(this.m_game).ResetDensityModifier();
      this.m_quiet = false;
    }
  }

  public func Hidden() -> Int32 {
    return this.m_hidden;
  }

  // ---- guest ----

  // Our own NPCs near V (encounters, communities, what the crowd spawned
  // before it was off): brain off and out of sight for good. The host's
  // version of each one comes as a proxy.
  private func HideOwn() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    for npc in player.GetNPCsAroundObject(TandemNpcs.Range()) {
      let id = npc.GetEntityID();
      if !entities.IsManaged(id) && !ArrayContains(this.m_hiddenIds, id) {
        TandemProxies.Pacify(npc);
        npc.HideIrreversibly();
        ArrayPush(this.m_hiddenIds, id);
        this.m_hidden += 1;
      }
    }
  }

  // ---- host ----

  private func Share() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    for known in this.m_known {
      known.seen = false;
    }
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let orientations: array<Quaternion>;
    let detected: array<Int32>;
    let watched = this.Watched(player);
    ArrayClear(this.m_awareNpcs);
    ArrayClear(this.m_awarePlayers);
    ArrayClear(this.m_awareLevels);
    for npc in player.GetNPCsAroundObject(TandemNpcs.Range()) {
      let id = npc.GetEntityID();
      // The remote players' puppets are not the host's world (cars are not
      // listed, the host has no proxies); the dead are handled below.
      if ArraySize(ids) < 64 && !this.IsPuppet(id) && !npc.IsDead() {
        let known = this.Find(id);
        if !IsDefined(known) {
          known = new TandemNpcKnown();
          known.id = id;
          known.npc = this.m_next;
          known.health = -1.0;
          this.m_next += 1u;
          ArrayPush(this.m_known, known);
          TandemNative.SendNpcSpawn(known.npc, npc.GetRecordID(), npc.GetCurrentAppearanceName(), npc.GetWorldPosition(), npc.GetWorldOrientation());
        }
        known.seen = true;
        ArrayPush(ids, known.npc);
        ArrayPush(positions, npc.GetWorldPosition());
        ArrayPush(orientations, npc.GetWorldOrientation());
        let mask = this.Detected(npc, watched);
        ArrayPush(detected, mask);
        this.Notice(known.npc, npc, watched, mask);
        let health = pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Health, true) / 100.0;
        if AbsF(health - known.health) >= 0.01 {
          known.health = health;
          TandemNative.SendNpcHealth(known.npc, health);
        }
      }
    }
    // Not listed: dead (a corpse stays known, reported dead once, until it
    // goes), or gone.
    let kept: array<ref<TandemNpcKnown>>;
    for known in this.m_known {
      if known.seen {
        ArrayPush(kept, known);
      } else {
        let body = GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet;
        if IsDefined(body) && body.IsDead() && Vector4.Distance(body.GetWorldPosition(), player.GetWorldPosition()) < TandemNpcs.Range() + 20.0 {
          if !known.dead {
            known.dead = true;
            let killer = this.KillerOf(body, known, player);
            TandemNative.SendNpcGone(known.npc, 1, killer);
            TandemNative.Log(s"npcs: \(known.npc) died (killer: player \(killer))");
          }
          ArrayPush(kept, known);
        } else {
          TandemNative.SendNpcGone(known.npc, 0, 0u);
        }
      }
    }
    this.m_known = kept;
    TandemNative.SendNpcStates(ids, positions, orientations, detected);
    this.m_rounds += 1;
    if this.m_rounds % 2 == 0 {
      TandemNative.SendNpcAwareness(this.m_awareNpcs, this.m_awarePlayers, this.m_awareLevels);
    }
  }

  // How far this NPC is in noticing each guest's V (its detection meter,
  // 0..1; full once it has detected them), for that guest's stealth meter.
  // Our own V's is our game's own business.
  private func Notice(id: Uint32, npc: ref<ScriptedPuppet>, watched: array<ref<GameObject>>, mask: Int32) -> Void {
    let senses = npc.GetSensesComponent();
    if !IsDefined(senses) {
      return;
    }
    let p = 1u;
    let bit = 1;
    for v in watched {
      if IsDefined(v) && p != this.m_you {
        let level = (mask & bit) != 0 ? 1.0 : ClampF(senses.GetDetection(v.GetEntityID()) / 100.0, 0.0, 1.0);
        if level >= 0.01 {
          ArrayPush(this.m_awareNpcs, id);
          ArrayPush(this.m_awarePlayers, p);
          ArrayPush(this.m_awareLevels, level);
        }
      }
      p += 1u;
      bit *= 2;
    }
  }

  private func IsPuppet(id: EntityID) -> Bool {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) && system.PlayerOfPuppet(id) != 0u;
  }

  // Every V in our world, by player id: ours (m_you) and the remote
  // players' puppets. Index i is player i + 1.
  private func Watched(player: ref<GameObject>) -> array<ref<GameObject>> {
    let out: array<ref<GameObject>>;
    let p = 1u;
    while p <= 8u {
      ArrayPush(out, p == this.m_you ? player : TandemNpcs.PuppetOf(this.m_game, p));
      p += 1u;
    }
    return out;
  }

  // Which Vs this NPC has detected (bit player - 1): each player is seen,
  // or not, on their own (ARCHITECTURE, each player is detected on their
  // own). Full detection meter, or in combat with them as a hostile threat
  // (NPCPuppet.IsInCombatWithTarget: what keeps a V in combat once the NPC
  // has lost sight of them and searches). A guest enters and leaves combat
  // on this (Stealth.reds).
  private func Detected(npc: ref<ScriptedPuppet>, watched: array<ref<GameObject>>) -> Int32 {
    let mask = 0;
    let senses = npc.GetSensesComponent();
    let bit = 1;
    for v in watched {
      if IsDefined(v) && ((IsDefined(senses) && senses.GetDetection(v.GetEntityID()) >= 100.0) || NPCPuppet.IsInCombatWithTarget(npc, v)) {
        mask += bit;
      }
      bit *= 2;
    }
    return mask;
  }

  // The player whose hit killed it: the game's killer when it is a V, else
  // the last guest who hit it in the last three seconds, else none.
  private func KillerOf(body: ref<ScriptedPuppet>, known: ref<TandemNpcKnown>, player: ref<GameObject>) -> Uint32 {
    let killer = body.GetKiller() as GameObject;
    if IsDefined(killer) {
      if killer == player {
        return this.m_you;
      }
      let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
      let who = IsDefined(system) ? system.PlayerOfPuppet(killer.GetEntityID()) : 0u;
      if who != 0u {
        return who;
      }
    }
    return known.lastHitBy != 0u && this.m_now - known.lastHitAt < 3.0 ? known.lastHitBy : 0u;
  }

  // Guests' hits on our NPCs: the damage their game computed, applied here
  // where the NPC lives. The instigator is the guest's puppet, so the NPC
  // turns on them.
  private func TakeHits() -> Void {
    // Effects that waited for their guest's puppet.
    let waiting = this.m_pendingEffects;
    let since = this.m_pendingSince;
    ArrayClear(this.m_pendingEffects);
    ArrayClear(this.m_pendingSince);
    let i = 0;
    while i < ArraySize(waiting) {
      if !this.ApplyEffect(waiting[i]) {
        if this.m_now - since[i] < 10.0 {
          ArrayPush(this.m_pendingEffects, waiting[i]);
          ArrayPush(this.m_pendingSince, since[i]);
        } else {
          TandemNative.Log(s"npcs: player \(waiting[i].player)'s effect on \(waiting[i].npc) dropped (no puppet)");
        }
      }
      i += 1;
    }
    for event in TandemNative.TakeNpcEvents() {
      if event.kind == 5 {
        if !this.ApplyEffect(event) {
          ArrayPush(this.m_pendingEffects, event);
          ArrayPush(this.m_pendingSince, this.m_now);
        }
      } else if event.kind == 3 {
        let known = this.FindNpc(event.npc);
        let body = IsDefined(known) ? GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet : null;
        if IsDefined(body) && !body.IsDead() {
          known.lastHitBy = event.player;
          known.lastHitAt = this.m_now;
          let instigator = TandemNpcs.PuppetOf(this.m_game, event.player);
          let pools = GameInstance.GetStatPoolsSystem(this.m_game);
          if (event.flags & 8) != 0 {
            // An instant kill (finisher, Kill flag), as the game does it.
            pools.RequestSettingStatPoolMinValue(Cast<StatsObjectID>(known.id), gamedataStatPoolType.Health, instigator);
          } else {
            pools.RequestChangingStatPoolValue(Cast<StatsObjectID>(known.id), gamedataStatPoolType.Health, -event.amount, instigator, false, false);
          }
          TandemNpcs.React(body, instigator, event);
          TandemNative.Log(s"npcs: \(event.npc) hit by player \(event.player) for \(event.amount) (flags \(event.flags))");
        }
      }
    }
  }

  // A status effect a guest's hit put on our NPC: its behaviour here (stun,
  // panic, burning), from the guest's puppet, whose damage ticks we leave
  // to the guest (Combat.reds). False: their puppet is not here yet.
  private func ApplyEffect(event: TandemNpcEvent) -> Bool {
    let known = this.FindNpc(event.npc);
    let body = IsDefined(known) ? GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet : null;
    if !IsDefined(body) || body.IsDead() {
      return true; // nothing left to apply it to
    }
    let instigator = TandemNpcs.PuppetOf(this.m_game, event.player);
    if !IsDefined(instigator) {
      return false;
    }
    StatusEffectHelper.ApplyStatusEffect(body, event.record, instigator.GetEntityID());
    TandemNative.Log(s"npcs: \(event.npc) gets \(TDBID.ToStringDEBUG(event.record)) from player \(event.player)");
    return true;
  }

  // The NPC reacts to a guest's hit as to one of ours: its hit animation,
  // sounds and effects, and the hit stimulus that turns it on whoever hit
  // it. The damage is already applied (exactly what the guest's game
  // computed): this hit deals none.
  public static func React(body: ref<ScriptedPuppet>, instigator: ref<GameObject>, event: TandemNpcEvent) -> Void {
    if !IsDefined(instigator) || body.IsDead() {
      return;
    }
    let context: AttackInitContext;
    context.record = TweakDBInterface.GetAttackRecord((event.flags & 4) != 0 ? t"Attacks.ForwardPush" : t"Attacks.REMOVE_BulletWithDamage");
    context.instigator = instigator;
    context.source = instigator;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = body;
    hit.hitPosition = new Vector4(event.x, event.y, event.z, 1.0);
    hit.hitDirection = new Vector4(event.dx, event.dy, event.dz, 0.0);
    hit.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_guest_hit");
    hit.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_guest_hit");
    hit.attackData.SetSource(instigator);
    hit.attackData.SetInstigator(instigator);
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    GameInstance.GetDamageSystem(body.GetGame()).QueueHitEvent(hit, body);
  }

  // A remote player's puppet (the instigator of their hits).
  public static func PuppetOf(game: GameInstance, player: Uint32) -> ref<GameObject> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.PuppetOf(player) : null;
  }

  private func FindNpc(npc: Uint32) -> ref<TandemNpcKnown> {
    for known in this.m_known {
      if known.npc == npc {
        return known;
      }
    }
    return null;
  }

  private func Find(id: EntityID) -> ref<TandemNpcKnown> {
    for known in this.m_known {
      if known.id == id {
        return known;
      }
    }
    return null;
  }

  private func Follow(now: Float) -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    for event in TandemNative.TakeNpcEvents() {
      if event.kind == 4 {
        let proxy = this.FindProxy(event.npc);
        if IsDefined(proxy) {
          proxy.health = event.amount;
          this.ApplyHealth(proxy);
        }
      } else if event.kind == 2 && event.reason == 1 {
        this.Die(event.npc, event.player);
      } else if event.kind == 1 {
        let spec = new DynamicEntitySpec();
        spec.recordID = event.record;
        spec.appearanceName = event.appearance;
        spec.position = new Vector4(event.x, event.y, event.z, 1.0);
        spec.orientation = new Quaternion(event.i, event.j, event.k, event.r);
        spec.alwaysSpawned = true;
        spec.persistState = false;
        spec.persistSpawn = false;
        spec.spawnInView = true;
        spec.active = true;
        ArrayPush(spec.tags, n"Tandem.Proxy");
        let proxy = new TandemNpcProxy();
        proxy.npc = event.npc;
        proxy.health = -1.0;
        proxy.id = entities.CreateEntity(spec);
        ArrayPush(this.m_proxies, proxy);
      } else {
        this.Remove(event.npc);
      }
    }
    // Brain off and our controller on, as soon as each proxy exists.
    for proxy in this.m_proxies {
      if !proxy.ready {
        let npc = entities.GetEntity(proxy.id) as ScriptedPuppet;
        if IsDefined(npc) && proxy.seenAt <= 0.0 {
          proxy.seenAt = now;
          // Brain off at once: a proxy never acts on its own.
          TandemProxies.Pacify(npc);
          this.ApplyHealth(proxy);
        }
        // The controller once its body has had two seconds to load.
        if IsDefined(npc) && proxy.seenAt > 0.0 && now - proxy.seenAt >= 2.0 {
          let puppet = TandemNative.NpcPuppetId(proxy.npc);
          TandemNative.RegisterPuppet(puppet, proxy.id);
          proxy.ready = TandemNative.AttachController(puppet, npc);
        }
      }
    }
  }

  private func FindProxy(npc: Uint32) -> ref<TandemNpcProxy> {
    for proxy in this.m_proxies {
      if proxy.npc == npc {
        return proxy;
      }
    }
    return null;
  }

  // The host's health on the proxy (its bar, its look when hurt). Local
  // damage never reaches proxies (Combat.reds), so this is all they take.
  private func ApplyHealth(proxy: ref<TandemNpcProxy>) -> Void {
    if proxy.health < 0.0 || proxy.dead {
      return;
    }
    let npc = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    if IsDefined(npc) {
      GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(proxy.id), gamedataStatPoolType.Health, MaxF(proxy.health * 100.0, 0.1), null, true);
    }
  }

  // The host's NPC died: so does the proxy, with the game's own death (its
  // animation, ragdoll, sounds), and it stays as a corpse until the host's
  // goes.
  private func Die(npc: Uint32, killer: Uint32) -> Void {
    let proxy = this.FindProxy(npc);
    if !IsDefined(proxy) || proxy.dead {
      return;
    }
    proxy.dead = true;
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    // Our kill: the game credits our V (on-kill perks, XP, its own feedback).
    let ours = killer != 0u && killer == this.m_you;
    if IsDefined(body) {
      body.Kill(ours ? GetPlayer(this.m_game) : null, false, false);
    }
    TandemNative.Log(s"npcs: proxy \(npc) died\(ours ? ", our kill" : "")");
  }

  // (test) The nearest live proxy to a point that V may hit (the game
  // ignores hits on friendly NPCs: FriendlyFireIgnored).
  public func NearestProxy(at: Vector4) -> ref<ScriptedPuppet> {
    let best: ref<ScriptedPuppet>;
    let bestDistance = 1000000.0;
    let player = GetPlayer(this.m_game);
    for proxy in this.m_proxies {
      let npc = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
      if !proxy.dead && IsDefined(npc) && NotEquals(GameObject.GetAttitudeBetween(npc, player), EAIAttitude.AIA_Friendly) {
        let distance = Vector4.Distance(npc.GetWorldPosition(), at);
        if distance < bestDistance {
          bestDistance = distance;
          best = npc;
        }
      }
    }
    return best;
  }

  // The NPC a proxy stands for (0: not a live proxy). Combat.reds sends hits
  // on it to the host instead of applying them.
  public func NpcOfProxy(id: EntityID) -> Uint32 {
    for proxy in this.m_proxies {
      if proxy.id == id && !proxy.dead {
        return proxy.npc;
      }
    }
    return 0u;
  }

  private func Remove(npc: Uint32) -> Void {
    let kept: array<ref<TandemNpcProxy>>;
    for proxy in this.m_proxies {
      if proxy.npc == npc {
        TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
        GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
      } else {
        ArrayPush(kept, proxy);
      }
    }
    this.m_proxies = kept;
  }

  // Guest: the host's NPCs as shown here (Stealth.reds: their detection of us).
  public func Proxies() -> array<ref<TandemNpcProxy>> {
    return this.m_proxies;
  }

  public func Count() -> Int32 {
    return TandemNative.IsHost() ? ArraySize(this.m_known) : ArraySize(this.m_proxies);
  }

  public func Clear() -> Void {
    for proxy in this.m_proxies {
      TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
      GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
    }
    ArrayClear(this.m_proxies);
    ArrayClear(this.m_known);
  }
}
