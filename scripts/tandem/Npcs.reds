// One set of NPCs (docs/ARCHITECTURE.md, milestone 5). The host's game runs
// them; it announces each NPC near its V to the guests and sends where they
// all are ten times a second. A guest shows each one as a proxy: the same
// record and appearance with its brain off (TandemProxies.Pacify), moved by
// tandem's controller like a remote player.
//
// Fights: a guest's hit on a proxy goes to the host (Combat.reds), which
// applies it to the real NPC with its whole outcome (Outcomes.reds); health,
// poise, wounds, knock-outs and death come back to every guest.

public class TandemNpcKnown {
  public let id: EntityID;
  public let npc: Uint32;
  public let seen: Bool;
  // Health last sent (fraction 0..1; -1 none yet), and whether we said it died.
  public let health: Float;
  public let dead: Bool;
  // Poise last sent (fraction; -1 none yet), and whether it was knocked out.
  public let poise: Float;
  public let down: Bool;
  // The last guest who hit it, and when: its killer when the game names none.
  public let lastHitBy: Uint32;
  public let lastHitAt: Float;
  // The vehicle carrying our V it was seen seated in, and the seat, as
  // sent (NpcSeat): Panam at the wheel of the quest car we ride in.
  public let seatVehicle: EntityID;
  public let seat: CName;
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
  // The host's poise for it (fraction, -1 none yet); knocked out (a
  // non-lethal hit: its body lies there, still to be hit).
  public let poise: Float;
  public let down: Bool;
  // Consistency checks (Check): since when the host has no such NPC, and
  // since when its body is gone (0: not so).
  public let orphanSince: Float;
  public let goneSince: Float;
  // Where the host says it sits (NpcSeat: whose vehicle, which seat; owner
  // 0: on foot), whether that is still to do, and where it sits here.
  public let seatOwner: Uint32;
  public let seatSlot: CName;
  public let seatChanged: Bool;
  public let seatRetryAt: Float;
  public let seatedIn: EntityID;
  public let seatedSlot: CName;
  // Out of a seat: our controller goes back on at this time.
  public let reattachAt: Float;
  // What it was spawned from (to bring it back, see Reconcile).
  public let record: TweakDBID;
  public let appearance: CName;
  // Our own hit killed it here before the host said so (PredictKill): when,
  // and when the host's health for it said it lives (0: no such doubt).
  public let predictedAt: Float;
  public let doubtAt: Float;
}

// A proxy taken out of its seat, deleted a moment later (a body is deleted
// once it is no longer mounted, as System.reds does for puppets).
public class TandemNpcLeaving {
  public let id: EntityID;
  public let at: Float;
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
  // Guest: the host's NPCs' hits on our V queued in our damage system, with
  // the armour penetration the host sent for each (Combat.reds).
  private let m_forwarded: array<ref<gameHitEvent>>;
  private let m_forwardedPierce: array<Float>;
  // Host: how far each NPC is in noticing each guest, gathered by Share and
  // sent every other round (5 Hz; Stealth.reds shows it on the guest).
  private let m_awareNpcs: array<Uint32>;
  private let m_awarePlayers: array<Uint32>;
  private let m_awareLevels: array<Float>;
  private let m_rounds: Int32;
  // Guest: proxies on their way out of a seat.
  private let m_leaving: array<ref<TandemNpcLeaving>>;
  // How they animate (NpcAnims.reds): sampled here, followed on proxies.
  private let m_anims: ref<TandemNpcAnims>;
  // Guest: the consistency checks (Check): when they last ran, and the
  // NPCs the host shares that we do not show, since when.
  private let m_lastCheck: Float;
  private let m_missingIds: array<Uint32>;
  private let m_missingSince: array<Float>;

  public static func Range() -> Float {
    return 60.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_next = 1u;
    this.m_anims = new TandemNpcAnims();
    this.m_anims.Start(game, this);
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
      // Not while our V is in another world (Elsewhere.reds): the NPCs
      // around it are that world's, and hiding is for good.
      // On the NCART our train is our own (Transit.reds): its passengers
      // stay, as the host's stay on its train.
      if now - this.m_lastQuiet >= 0.5 && !TandemElsewhere.Inside(this.m_game) && !this.OnMetro() {
        this.m_lastQuiet = now;
        this.HideOwn();
      }
      this.Follow(now);
      this.TakePlayerHits();
      if now - this.m_lastCheck >= 1.0 {
        this.m_lastCheck = now;
        this.Check(now);
      }
    }
    let outcomes = TandemOutcomes.Of(this.m_game);
    if IsDefined(outcomes) {
      outcomes.Tick(now, you, this);
    }
  }

  // Our V rides the NCART: every game runs its own train, passengers and
  // all, so those NPCs are not shared (the host's would float along the
  // line on the guests' screens, with no train under them).
  private func OnMetro() -> Bool {
    let player = GetPlayer(this.m_game);
    let car: wref<VehicleObject>;
    return IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, car) && TandemTransit.IsMetro(this.m_game, player, car);
  }

  // ---- guest: the host's NPCs hitting our V ----

  // Each hit runs on our V through our own damage system (the target's side
  // of the hit by our game's rules), from the NPC's proxy when we have it.
  private func TakePlayerHits() -> Void {
    let damage = GameInstance.GetDamageSystem(this.m_game);
    for hit in TandemNative.TakePlayerHits() {
      let proxy: ref<ScriptedPuppet>;
      let known = hit.npc != 0u ? this.FindProxy(hit.npc) : null;
      if IsDefined(known) {
        proxy = GameInstance.GetDynamicEntitySystem().GetEntity(known.id) as ScriptedPuppet;
      }
      let event = TandemCombat.PlayerHitEvent(this.m_game, hit, proxy);
      if IsDefined(event) {
        // The newest few: an entry leaves when its pipeline ends.
        while ArraySize(this.m_forwarded) >= 16 {
          ArrayErase(this.m_forwarded, 0);
          ArrayErase(this.m_forwardedPierce, 0);
        }
        ArrayPush(this.m_forwarded, event);
        ArrayPush(this.m_forwardedPierce, hit.pierce);
        damage.QueueHitEvent(event, event.target);
        TandemNative.Log(s"npcs: hit by npc \(hit.npc)\(IsDefined(proxy) ? "" : " (no proxy)"): \(hit.physical) physical, \(hit.thermal) thermal, \(hit.electric) electric, \(hit.chemical) chemical, type \(hit.attackType), attack \(TDBID.ToStringDEBUG(hit.attack))");
      }
    }
  }

  public func IsForwarded(hit: ref<gameHitEvent>) -> Bool {
    return ArrayContains(this.m_forwarded, hit);
  }

  public func ForwardedPierce(hit: ref<gameHitEvent>, out pierce: Float) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_forwarded) {
      if this.m_forwarded[i] == hit {
        pierce = this.m_forwardedPierce[i];
        return true;
      }
      i += 1;
    }
    return false;
  }

  // Another V's area effect on our V (Outcomes.reds), run like the host's.
  public func Forward(hit: ref<gameHitEvent>, pierce: Float) -> Void {
    while ArraySize(this.m_forwarded) >= 16 {
      ArrayErase(this.m_forwarded, 0);
      ArrayErase(this.m_forwardedPierce, 0);
    }
    ArrayPush(this.m_forwarded, hit);
    ArrayPush(this.m_forwardedPierce, pierce);
  }

  public func Forget(hit: ref<gameHitEvent>) -> Void {
    let i = 0;
    while i < ArraySize(this.m_forwarded) {
      if this.m_forwarded[i] == hit {
        ArrayErase(this.m_forwarded, i);
        ArrayErase(this.m_forwardedPierce, i);
        return;
      }
      i += 1;
    }
  }

  // The session is over: the host's NPCs go, our crowd comes back. (Our own
  // NPCs hidden meanwhile stay hidden until they stream out.)
  public func End() -> Void {
    for proxy in this.m_proxies {
      TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
      this.Unseat(proxy);
      GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
    }
    for leaving in this.m_leaving {
      GameInstance.GetDynamicEntitySystem().DeleteEntity(leaving.id);
    }
    ArrayClear(this.m_leaving);
    ArrayClear(this.m_proxies);
    ArrayClear(this.m_known);
    this.m_anims.Clear();
    ArrayClear(this.m_missingIds);
    ArrayClear(this.m_missingSince);
    this.Stop();
  }

  // A proxy out of its seat at once (the session is over). Deleting a body
  // just taken out of a car copy was not seen to hurt (§13.6: the vehicle's
  // deletion does); seated proxies only exist with a ride going on.
  private func Unseat(proxy: ref<TandemNpcProxy>) -> Void {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as GameObject;
    if EntityID.IsDefined(proxy.seatedIn) && IsDefined(body) {
      TandemVehicles.Unseat(this.m_game, body, proxy.seatedIn, proxy.seatedSlot);
    }
    let none = EntityID.FromHash(0ul);
    proxy.seatedIn = none;
  }

  // The game's crowd back as it was (leaving the session), and our difficulty.
  public func Stop() -> Void {
    let system = TandemCombat.System(this.m_game);
    if IsDefined(system) {
      system.Outcomes().End();
    }
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
    let carrier = this.Carrier(player);
    ArrayClear(this.m_awareNpcs);
    ArrayClear(this.m_awarePlayers);
    ArrayClear(this.m_awareLevels);
    // On the NCART none are listed: the ones known go (NpcGone, out of range).
    let riding = this.OnMetro();
    let sampling = this.m_anims.Due(this.m_now);
    // Our V away from its body in another world (Elsewhere.reds): the NPCs
    // around it are that world's, none is shared, and the shared ones go.
    let around = player.GetNPCsAroundObject(TandemNpcs.Range());
    if TandemElsewhere.AwayFromBody(this.m_game) {
      ArrayClear(around);
    }
    for npc in around {
      let id = npc.GetEntityID();
      // The remote players' puppets are not the host's world (cars are not
      // listed, the host has no proxies); the dead are handled below.
      if ArraySize(ids) < 64 && !riding && !this.IsPuppet(id) && !npc.IsDead() {
        let known = this.Find(id);
        if !IsDefined(known) {
          known = new TandemNpcKnown();
          known.id = id;
          known.npc = this.m_next;
          known.health = -1.0;
          known.poise = -1.0;
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
        if sampling {
          this.m_anims.Sample(npc, known.npc, this.m_you);
        }
        let health = pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Health, true) / 100.0;
        if AbsF(health - known.health) >= 0.01 {
          known.health = health;
          TandemNative.SendNpcHealth(known.npc, health);
        }
        this.ShareSeat(known, npc, carrier);
        // Its guard, as its health (a guest's finisher checks the proxy's).
        let poise = pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Poise, true) / 100.0;
        if AbsF(poise - known.poise) >= 0.05 || (poise <= 0.0 && known.poise > 0.0) {
          known.poise = poise;
          TandemFight.SendPoise(known.npc, poise);
        }
        // Knocked out (a non-lethal hit): the body stays, and may still die.
        if !known.down && ScriptedPuppet.IsUnconscious(npc) {
          known.down = true;
          let by = this.KillerOf(npc, known, player);
          TandemNative.SendNpcGone(known.npc, 3, by);
          TandemNative.Log(s"npcs: \(known.npc) knocked out (by player \(by))");
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
            this.m_anims.Forget(known.npc);
            TandemNative.Log(s"npcs: \(known.npc) died (killer: player \(killer))");
            // Its quest and important items reach the guests' corpses (Loot.reds).
            TandemLoot.HostCorpse(this.m_game, known.id, known.npc);
          }
          ArrayPush(kept, known);
        } else {
          TandemNative.SendNpcGone(known.npc, 0, 0u);
          this.m_anims.Forget(known.npc);
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

  // The vehicle carrying our V that our game runs (our car, a ride, the
  // quest car a scene drives; not another player's copy): the one the
  // guests have a copy of, under our player id.
  private func Carrier(player: ref<GameObject>) -> ref<VehicleObject> {
    let car: wref<VehicleObject>;
    if !VehicleComponent.GetVehicle(this.m_game, player, car) || !IsDefined(car) || TandemTransit.IsMetro(this.m_game, player, car) {
      return null;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(system) || system.CarOwnerOf(car.GetEntityID()) != 0u {
      return null;
    }
    return car;
  }

  // An NPC seated in the vehicle carrying our V (Panam at the wheel, Judy
  // beside us): the guests seat its proxy in their copy. It stays seated
  // there while it sits in that vehicle here, our V in it or not (the copy
  // is parked where we left it, and so is the car here).
  private func ShareSeat(known: ref<TandemNpcKnown>, npc: ref<ScriptedPuppet>, carrier: ref<VehicleObject>) -> Void {
    let none = EntityID.FromHash(0ul);
    let slot = n"None";
    if EntityID.IsDefined(known.seatVehicle) {
      let was = GameInstance.FindEntityByID(this.m_game, known.seatVehicle) as VehicleObject;
      if IsDefined(was) && VehicleComponent.IsMountedToProvidedVehicle(this.m_game, known.id, was) {
        VehicleComponent.GetMountedSlotName(this.m_game, npc, slot);
        if !Equals(slot, known.seat) {
          known.seat = slot;
          TandemNative.SendNpcSeat(known.npc, this.m_you, slot);
          TandemNative.Log(s"npcs: \(known.npc) moved to \(NameToString(slot))");
        }
        return;
      }
      known.seatVehicle = none;
      known.seat = n"None";
      TandemNative.SendNpcSeat(known.npc, 0u, n"None");
      TandemNative.Log(s"npcs: \(known.npc) got out");
    }
    if IsDefined(carrier) && VehicleComponent.IsMountedToProvidedVehicle(this.m_game, known.id, carrier) {
      VehicleComponent.GetMountedSlotName(this.m_game, npc, slot);
      known.seatVehicle = carrier.GetEntityID();
      known.seat = slot;
      TandemNative.SendNpcSeat(known.npc, this.m_you, slot);
      TandemNative.Log(s"npcs: \(known.npc) sits in \(NameToString(slot)) of our vehicle");
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
  // Host: a player acted on this NPC (Takedowns.reds): its killer if it
  // dies within three seconds and the game names none.
  public func NoteActor(npc: Uint32, player: Uint32) -> Void {
    let known = this.FindNpc(npc);
    if IsDefined(known) {
      known.lastHitBy = player;
      known.lastHitAt = this.m_now;
    }
  }

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

  // Status effects guests' hits put on our NPCs. (Their hits themselves are
  // strikes: Outcomes.reds.)
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
          if proxy.predictedAt > 0.0 && event.amount > 0.0 {
            proxy.doubtAt = now;
          }
        }
      } else if event.kind == 2 && event.reason == 1 {
        this.Die(event.npc, event.player);
      } else if event.kind == 6 {
        let seated = this.FindProxy(event.npc);
        if IsDefined(seated) {
          seated.seatOwner = event.player;
          seated.seatSlot = event.appearance;
          seated.seatChanged = true;
          seated.seatRetryAt = 0.0;
        }
      } else if event.kind == 2 && event.reason == 3 {
        this.KnockOut(event.npc, event.player);
      } else if event.kind == 1 {
        // Never two proxies of one NPC (a spawn after the checks put it
        // back already).
        if IsDefined(this.FindProxy(event.npc)) {
          TandemNative.Log(s"consistency: spawn of npc \(event.npc) ignored, its proxy exists");
        } else {
          this.SpawnProxy(event.npc, event.record, event.appearance, new Vector4(event.x, event.y, event.z, 1.0), new Quaternion(event.i, event.j, event.k, event.r));
        }
      } else {
        this.Remove(event.npc);
      }
    }
    this.Reconcile(now);
    // Brain off and our controller on, as soon as each proxy exists.
    for proxy in this.m_proxies {
      if !proxy.ready {
        let npc = entities.GetEntity(proxy.id) as ScriptedPuppet;
        if IsDefined(npc) && proxy.seenAt <= 0.0 {
          proxy.seenAt = now;
          // Brain off at once: a proxy never acts on its own.
          TandemProxies.Pacify(npc);
          this.ApplyHealth(proxy);
          this.ApplyPoise(proxy);
        }
        // The controller once its body has had two seconds to load.
        if IsDefined(npc) && proxy.seenAt > 0.0 && now - proxy.seenAt >= 2.0 {
          let puppet = TandemNative.NpcPuppetId(proxy.npc);
          TandemNative.RegisterPuppet(puppet, proxy.id);
          proxy.ready = TandemNative.AttachController(puppet, npc);
        }
      } else if !proxy.dead {
        this.FollowSeat(proxy, now);
      }
    }
    this.ProcessLeaving(now);
    this.m_anims.Follow(now, this.m_you);
  }

  // A proxy into the seat the host says (in the copy of the vehicle
  // carrying that player), or out of it, once it is ready (its controller
  // on: mounting takes it off, getting out puts it back).
  private func FollowSeat(proxy: ref<TandemNpcProxy>, now: Float) -> Void {
    let game = this.m_game;
    let entities = GameInstance.GetDynamicEntitySystem();
    if proxy.reattachAt > 0.0 && now >= proxy.reattachAt && !GameInstance.GetMountingFacility(game).IsMountedToAnything(proxy.id) {
      let body = entities.GetEntity(proxy.id);
      if IsDefined(body) {
        TandemNative.AttachController(TandemNative.NpcPuppetId(proxy.npc), body);
      }
      proxy.reattachAt = 0.0;
    }
    if !proxy.seatChanged || now < proxy.seatRetryAt {
      return;
    }
    let npc = entities.GetEntity(proxy.id) as GameObject;
    if !IsDefined(npc) {
      return;
    }
    let none = EntityID.FromHash(0ul);
    if EntityID.IsDefined(proxy.seatedIn) {
      TandemVehicles.Unseat(game, npc, proxy.seatedIn, proxy.seatedSlot);
      proxy.seatedIn = none;
      proxy.reattachAt = now + 0.5;
      TandemNative.Log(s"npcs: proxy \(proxy.npc) out of its seat");
    }
    if proxy.seatOwner == 0u {
      proxy.seatChanged = false;
      return;
    }
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    let carId = IsDefined(system) ? system.CarOf(proxy.seatOwner) : none;
    let car = entities.GetEntity(carId) as VehicleObject;
    let slot = TandemVehicles.FreeSlot(game, car, proxy.seatSlot, proxy.id);
    if !IsDefined(car) || !IsNameValid(slot) {
      // No copy yet (it spawns with the player's seat), or every seat taken.
      proxy.seatRetryAt = now + 0.5;
      return;
    }
    TandemVehicles.Seat(game, npc, car, slot);
    proxy.seatedIn = carId;
    proxy.seatedSlot = slot;
    proxy.seatChanged = false;
    proxy.reattachAt = 0.0;
    TandemNative.Log(s"npcs: proxy \(proxy.npc) seated in \(NameToString(slot)) of player \(proxy.seatOwner)'s vehicle");
  }

  // Proxies taken out of a seat: deleted once off it.
  private func ProcessLeaving(now: Float) -> Void {
    if ArraySize(this.m_leaving) == 0 {
      return;
    }
    let kept: array<ref<TandemNpcLeaving>>;
    for leaving in this.m_leaving {
      if now - leaving.at >= 0.5 || (now - leaving.at >= 0.1 && !GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(leaving.id)) {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(leaving.id);
      } else {
        ArrayPush(kept, leaving);
      }
    }
    this.m_leaving = kept;
  }

  // Guest: a proxy that is alive and moved by our controller (null: none).
  public func ReadyProxy(npc: Uint32) -> ref<ScriptedPuppet> {
    let proxy = this.FindProxy(npc);
    if !IsDefined(proxy) || !proxy.ready || proxy.dead {
      return null;
    }
    return GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
  }

  // The proxy entity of a host NPC, alive or dead (Loot.reds).
  public func ProxyEntity(npc: Uint32) -> EntityID {
    let proxy = this.FindProxy(npc);
    let none = EntityID.FromHash(0ul);
    return IsDefined(proxy) ? proxy.id : none;
  }

  private func SpawnProxy(npc: Uint32, record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion) -> Void {
    let spec = new DynamicEntitySpec();
    spec.recordID = record;
    spec.appearanceName = appearance;
    spec.position = position;
    spec.orientation = orientation;
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Proxy");
    let proxy = new TandemNpcProxy();
    proxy.npc = npc;
    proxy.health = -1.0;
    proxy.poise = -1.0;
    proxy.record = record;
    proxy.appearance = appearance;
    proxy.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    ArrayPush(this.m_proxies, proxy);
  }

  // ---- guest: consistency (docs/ARCHITECTURE.md, Consistency) ----

  // Our proxies against the session's record of what the host shares (the
  // events that build them can be lost to a full queue, a loading screen,
  // a body streamed out): a proxy whose NPC the host no longer has goes, one
  // the host has and we lack is spawned, one whose body is gone comes back.
  // Each waits 2 s first, in case the event that fixes it is on its way.
  private func Check(now: Float) -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    let kept: array<ref<TandemNpcProxy>>;
    let lost: array<Uint32>;
    for proxy in this.m_proxies {
      let keep = true;
      if !proxy.dead && !TandemSync.IsNpcShared(proxy.npc) {
        if proxy.orphanSince <= 0.0 {
          proxy.orphanSince = now;
        } else if now - proxy.orphanSince >= 2.0 {
          TandemNative.Log(s"consistency: proxy \(proxy.npc) has no NPC on the host, removed");
          keep = false;
        }
      } else {
        proxy.orphanSince = 0.0;
      }
      if keep && proxy.seenAt > 0.0 && !IsDefined(entities.GetEntity(proxy.id)) {
        if proxy.goneSince <= 0.0 {
          proxy.goneSince = now;
        } else if now - proxy.goneSince >= 3.0 {
          TandemNative.Log(s"consistency: proxy \(proxy.npc) lost its body\(proxy.dead ? ", corpse dropped" : ", spawned again")");
          keep = false;
          if !proxy.dead {
            ArrayPush(lost, proxy.npc);
          }
        }
      } else {
        proxy.goneSince = 0.0;
      }
      if keep {
        ArrayPush(kept, proxy);
      } else {
        TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
        entities.DeleteEntity(proxy.id);
      }
    }
    this.m_proxies = kept;
    // What the host shares and we do not show.
    let missing: array<Uint32>;
    let since: array<Float>;
    for npc in TandemSync.SharedNpcs() {
      if !IsDefined(this.FindProxy(npc.npc)) {
        let first = this.MissingSince(npc.npc, now);
        if ArrayContains(lost, npc.npc) || now - first >= 2.0 {
          let half = npc.yaw * 0.5;
          this.SpawnProxy(npc.npc, npc.record, npc.appearance, new Vector4(npc.x, npc.y, npc.z, 1.0), new Quaternion(0.0, 0.0, SinF(half), CosF(half)));
          if !ArrayContains(lost, npc.npc) {
            TandemNative.Log(s"consistency: proxy \(npc.npc) was missing, spawned from the session's record");
          }
        } else {
          ArrayPush(missing, npc.npc);
          ArrayPush(since, first);
        }
      }
    }
    this.m_missingIds = missing;
    this.m_missingSince = since;
  }

  // Since when the host's NPC has had no proxy here (now: from now).
  private func MissingSince(npc: Uint32, now: Float) -> Float {
    let i = 0;
    while i < ArraySize(this.m_missingIds) {
      if this.m_missingIds[i] == npc {
        return this.m_missingSince[i];
      }
      i += 1;
    }
    return now;
  }

  // (tools/game.sh cmd npc count) the host's NPCs the session's record has,
  // as the plugin counts them and as the list the checks walk has them (a
  // local: an intrinsic on a call's result is not to be trusted).
  public func SharedCount() -> Int32 {
    return TandemSync.SharedNpcCount();
  }

  public func SharedListed() -> Int32 {
    let npcs = TandemSync.SharedNpcs();
    return ArraySize(npcs);
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

  // The host's poise on the proxy (Outcomes.reds): its own finisher and
  // guard checks for our V then see the host's guard.
  public func SetPoise(npc: Uint32, poise: Float) -> Void {
    let proxy = this.FindProxy(npc);
    if IsDefined(proxy) {
      proxy.poise = poise;
      this.ApplyPoise(proxy);
    }
  }

  private func ApplyPoise(proxy: ref<TandemNpcProxy>) -> Void {
    if proxy.poise < 0.0 || proxy.dead {
      return;
    }
    let npc = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    if IsDefined(npc) {
      GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(proxy.id), gamedataStatPoolType.Poise, proxy.poise * 100.0, null, true);
    }
  }

  // The host's NPC was knocked out (a non-lethal hit): the proxy goes down
  // unconscious, its body there to be hit (a lethal hit then kills it on
  // the host, NpcGone 1). Whoever knocked it out is paid the skill XP
  // queued, as for a kill; everyone else their assist.
  private func KnockOut(npc: Uint32, by: Uint32) -> Void {
    let proxy = this.FindProxy(npc);
    if !IsDefined(proxy) || proxy.dead || proxy.down {
      return;
    }
    proxy.down = true;
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    if IsDefined(body) {
      StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.Unconscious");
      // A takedown still playing on it (Takedowns.reds) lays it down itself.
      let takedowns = TandemTakedowns.Get(this.m_game);
      if !IsDefined(takedowns) || !takedowns.Holds(npc) {
        body.QueueEvent(CreateForceRagdollEvent(n"tandem_knocked_out"));
      }
    }
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      if by != 0u && by == this.m_you {
        let neutralized = new TargetNeutraliziedEvent();
        neutralized.type = ENeutralizeType.Unconscious;
        neutralized.targetID = proxy.id;
        player.QueueEvent(neutralized);
      } else {
        let pay = new ProcessQueuedCombatExperience();
        pay.Set(player, proxy.id);
        GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PlayerDevelopmentSystem").QueueRequest(pay);
      }
    }
    TandemNative.Log(s"npcs: proxy \(npc) knocked out\(by == this.m_you ? ", by us" : "")");
  }

  // Host: a guest's strike landed on one of our NPCs (its killer, if no
  // other is named).
  public func NoteHitBy(npc: Uint32, player: Uint32) -> Void {
    let known = this.FindNpc(npc);
    if IsDefined(known) {
      known.lastHitBy = player;
      known.lastHitAt = this.m_now;
    }
  }

  // Guest: any proxy, alive, down or dead (Outcomes.reds: their wounds and
  // hits are the host's).
  public func IsProxyEntity(id: EntityID) -> Bool {
    for proxy in this.m_proxies {
      if proxy.id == id {
        return true;
      }
    }
    return false;
  }

  public func AnyProxy(npc: Uint32) -> EntityID {
    let proxy = this.FindProxy(npc);
    return IsDefined(proxy) ? proxy.id : EntityID();
  }

  // The host's NPC died: so does the proxy, with the game's own death (its
  // animation, ragdoll, sounds), and it stays as a corpse until the host's
  // goes.
  public func Die(npc: Uint32, killer: Uint32) -> Void {
    // Our own takedown of it still plays: it dies after (Takedowns.reds).
    if TandemTakedowns.Get(this.m_game).DefersDeath(npc, killer) {
      return;
    }
    let proxy = this.FindProxy(npc);
    if IsDefined(proxy) && proxy.predictedAt > 0.0 {
      // The kill our own game saw first (PredictKill): the host agrees.
      proxy.predictedAt = 0.0;
      proxy.doubtAt = 0.0;
      TandemNative.Log(s"npcs: proxy \(npc) died on the host too (killer: player \(killer))");
      return;
    }
    if !IsDefined(proxy) || proxy.dead {
      return;
    }
    proxy.dead = true;
    this.m_anims.Drop(npc);
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    // Our kill: the game credits our V (on-kill perks, its own feedback) and
    // pays the skill XP our hits on it queued (Combat.reds): RewardKiller
    // sends our V TargetNeutraliziedEvent, which processes the queue for this
    // entity (final.reds 21851-21871, 45336-45346).
    let ours = killer != 0u && killer == this.m_you;
    if IsDefined(body) {
      body.Kill(ours ? GetPlayer(this.m_game) : null, false, false);
      // Its death animation or ragdoll is the host's NPC's (a brain-off
      // body plays none of its own: HitReactions.reds).
      let reactions = TandemReactionsSync.Of(this.m_game);
      if IsDefined(reactions) {
        reactions.Died(npc);
      }
      // Its loot is ours to roll, as any corpse's (Loot.reds).
      TandemLoot.Corpse(this.m_game, proxy.id);
    }
    // Someone else's kill we helped with: our share is paid too (a co-op
    // choice, coop-perks.md §2.4), and the queue does not keep it forever.
    let player = GetPlayer(this.m_game);
    if !ours && IsDefined(player) {
      let pay = new ProcessQueuedCombatExperience();
      pay.Set(player, proxy.id);
      GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PlayerDevelopmentSystem").QueueRequest(pay);
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

  // A shared NPC's session id by its entity, dead or alive (host: ours;
  // guest: a proxy), 0: none; and back (Takedowns.reds).
  public func NpcOf(id: EntityID) -> Uint32 {
    if TandemNative.IsHost() {
      let known = this.Find(id);
      return IsDefined(known) ? known.npc : 0u;
    }
    for proxy in this.m_proxies {
      if proxy.id == id {
        return proxy.npc;
      }
    }
    return 0u;
  }

  public func EntityOf(npc: Uint32) -> EntityID {
    if TandemNative.IsHost() {
      let known = this.FindNpc(npc);
      if IsDefined(known) {
        return known.id;
      }
    } else {
      let proxy = this.FindProxy(npc);
      if IsDefined(proxy) {
        return proxy.id;
      }
    }
    let none = EntityID.FromHash(0ul);
    return none;
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

  // Quickhacks (Hacks.reds): the entity of a session NPC (host: the real
  // one; guest: its live proxy), and the other way round.
  public func EntityOfNpc(npc: Uint32) -> EntityID {
    if TandemNative.IsHost() {
      let known = this.FindNpc(npc);
      return IsDefined(known) ? known.id : EntityID();
    }
    let proxy = this.FindProxy(npc);
    return IsDefined(proxy) && !proxy.dead ? proxy.id : EntityID();
  }

  public func NpcOfEntity(id: EntityID) -> Uint32 {
    if !TandemNative.IsHost() {
      return this.NpcOfProxy(id);
    }
    let known = this.Find(id);
    return IsDefined(known) && !known.dead ? known.npc : 0u;
  }

  private func Remove(npc: Uint32) -> Void {
    let kept: array<ref<TandemNpcProxy>>;
    for proxy in this.m_proxies {
      if proxy.npc == npc {
        this.m_anims.Drop(npc);
        TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
        let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as GameObject;
        if EntityID.IsDefined(proxy.seatedIn) && IsDefined(body) {
          // Out of its seat first, deleted once off it (ProcessLeaving).
          TandemVehicles.Unseat(this.m_game, body, proxy.seatedIn, proxy.seatedSlot);
          let leaving = new TandemNpcLeaving();
          leaving.id = proxy.id;
          leaving.at = this.m_now;
          ArrayPush(this.m_leaving, leaving);
        } else if !proxy.dead || !TandemLoot.LeaveCorpse(this.m_game, proxy.id) {
          // A corpse leaves what it still holds as a loot bag (Loot.reds).
          GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
        }
      } else {
        ArrayPush(kept, proxy);
      }
    }
    this.m_proxies = kept;
  }

  // Guest: our own hit takes the last of a proxy's health (Combat.reds).
  // It dies here at once, by our V, as in single player: on-kill perks, a
  // Sandevistan's kill extension and the skill XP fire at the hit, not a
  // round trip to the host later. The host's word follows (Die); if it
  // says the NPC lives instead (its health was above what we knew), the
  // proxy is brought back (Reconcile).
  public func PredictKill(npc: Uint32) -> Void {
    let proxy = this.FindProxy(npc);
    if !IsDefined(proxy) || proxy.dead || TandemTakedowns.Get(this.m_game).DefersDeath(npc, this.m_you) {
      return;
    }
    proxy.dead = true;
    proxy.predictedAt = this.m_now;
    proxy.doubtAt = 0.0;
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
    if IsDefined(body) {
      body.Kill(GetPlayer(this.m_game), false, false);
      let reactions = TandemReactionsSync.Of(this.m_game);
      if IsDefined(reactions) {
        reactions.Died(npc);
      }
      // Its loot is ours to roll, as for a death the host told us of (Die):
      // Die returns early for a kill we saw first (Loot.reds).
      TandemLoot.Corpse(this.m_game, proxy.id);
    }
    TandemNative.Log(s"npcs: proxy \(npc) died by our hit (ahead of the host)");
  }

  // A kill we saw that the host did not: 1.5 s after it, with the host's
  // health for the NPC above zero since and no death from it, the proxy is
  // spawned again where it fell. (What the kill gave our V stays given.)
  private func Reconcile(now: Float) -> Void {
    for proxy in this.m_proxies {
      if proxy.predictedAt > 0.0 && proxy.doubtAt > 0.0 && now - proxy.predictedAt > 1.5 {
        let entities = GameInstance.GetDynamicEntitySystem();
        let old = entities.GetEntity(proxy.id) as GameObject;
        let spec = new DynamicEntitySpec();
        spec.recordID = proxy.record;
        spec.appearanceName = proxy.appearance;
        if IsDefined(old) {
          spec.position = old.GetWorldPosition();
          spec.orientation = old.GetWorldOrientation();
        } else {
          spec.position = GetPlayer(this.m_game).GetWorldPosition();
        }
        spec.alwaysSpawned = true;
        spec.persistState = false;
        spec.persistSpawn = false;
        spec.spawnInView = true;
        spec.active = true;
        ArrayPush(spec.tags, n"Tandem.Proxy");
        entities.DeleteEntity(proxy.id);
        proxy.id = entities.CreateEntity(spec);
        proxy.ready = false;
        proxy.seenAt = 0.0;
        proxy.dead = false;
        proxy.predictedAt = 0.0;
        proxy.doubtAt = 0.0;
        TandemNative.Log(s"npcs: proxy \(proxy.npc) lives on the host: our kill was not one, brought back");
      }
    }
  }

  // Guest: the host's NPCs as shown here (Stealth.reds: their detection of us).
  public func Proxies() -> array<ref<TandemNpcProxy>> {
    return this.m_proxies;
  }

  // (Time.reds) The live NPCs this game shares (host: the ones it lists;
  // guest: its proxies), and their entities in the same order.
  public func TimeIds() -> array<Uint32> {
    let out: array<Uint32>;
    if TandemNative.IsHost() {
      for known in this.m_known {
        if !known.dead {
          ArrayPush(out, known.npc);
        }
      }
    } else {
      for proxy in this.m_proxies {
        if !proxy.dead {
          ArrayPush(out, proxy.npc);
        }
      }
    }
    return out;
  }

  public func TimeEntities() -> array<EntityID> {
    let out: array<EntityID>;
    if TandemNative.IsHost() {
      for known in this.m_known {
        if !known.dead {
          ArrayPush(out, known.id);
        }
      }
    } else {
      for proxy in this.m_proxies {
        if !proxy.dead {
          ArrayPush(out, proxy.id);
        }
      }
    }
    return out;
  }

  public func Count() -> Int32 {
    return TandemNative.IsHost() ? ArraySize(this.m_known) : ArraySize(this.m_proxies);
  }

  public func Clear() -> Void {
    for proxy in this.m_proxies {
      TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
      this.Unseat(proxy);
      GameInstance.GetDynamicEntitySystem().DeleteEntity(proxy.id);
    }
    ArrayClear(this.m_proxies);
    ArrayClear(this.m_known);
    this.m_anims.Clear();
  }

  public func AnimsReport() -> String {
    return this.m_anims.Report();
  }

  // (test) Host: an NPC in a workspot near V to share (NpcAnims.Seek).
  public func SeekWorkspot(range: Float) -> Bool {
    return this.m_anims.Seek(range);
  }
}
