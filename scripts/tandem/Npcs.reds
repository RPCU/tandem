// One set of NPCs (docs/architecture/npcs.md, milestone 5; per-player regions:
// docs/architecture/regions.md). Every game runs the NPCs around its own V
// (m_known): it announces each one to the players linked with it and sends
// where they all are ten times a second. The others show each one as a
// proxy (m_proxies): the same record and appearance with its brain off
// (TandemProxies.Pacify), moved by tandem's controller like a remote player.
// "The host" in the comments below reads "the NPC's owner": the host's game
// was the only owner before regions.
//
// Fights: a hit on a proxy goes to its owner (Combat.reds), which applies it
// to the real NPC with its whole outcome (Outcomes.reds); health, poise,
// wounds, knock-outs and death come back to everyone who sees it.
//
// An NPC changes owner when its owner's V leaves it with another player
// near (Regions.reds: Give, Take): the giver's body becomes a proxy in
// place, the receiver's proxy wakes up.

public class TandemNpcKnown {
  public let id: EntityID;
  public let npc: Uint32;
  public let seen: Bool;
  // Health last sent (fraction 0..1; -1 none yet), with whether our game
  // cannot kill it now (1: Protected), and whether we said it died.
  public let health: Float;
  public let protect: Int32;
  public let dead: Bool;
  // Poise last sent (fraction; -1 none yet), and whether it was knocked out.
  public let poise: Float;
  public let down: Bool;
  // The last guest who hit it, and when: its killer when the game names none.
  public let lastHitBy: Uint32;
  public let lastHitAt: Float;
  // When our own V last hit it (KillerOf: a guest's later hit or takedown
  // wins over the game naming our V).
  public let lastOwnHitAt: Float;
  // The vehicle carrying our V it was seen seated in, and the seat, as
  // sent (NpcSeat): Panam at the wheel of the quest car we ride in.
  public let seatVehicle: EntityID;
  public let seat: CName;
  // Regions. A proxy we took over: one of our spawned entities (deleted when
  // it leaves the shared world), when its brain was switched on (0: checked
  // already), and until when a body spawned again for it may be missing.
  public let managed: Bool;
  public let wokeAt: Float;
  public let pendingUntil: Float;
  public let record: TweakDBID;
  public let appearance: CName;
  // Whose vehicle that is, as sent: a player, or a shared world vehicle
  // (TandemStreet.SeatOwner).
  public let seatOwner: Uint32;
  // Its level (Levels.reds): the round it was last worked out in, our
  // modifier on it, and its level as last told to the guests.
  public let levelGen: Int32;
  public let levelSeen: Bool;
  public let levelMod: ref<gameStatModifierData>;
  public let levelShift: Float;
  public let levelSent: Float;
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
  // Its owner's game cannot kill it now (NpcHealth's kNpcProtected): no hit
  // of ours is a kill (PredictKill).
  public let protect: Bool;
  // Regions: its body is the game's own entity (an NPC of ours we handed
  // over): never deleted, hidden when its proxy goes, woken up when the
  // session ends.
  public let native: Bool;
  // Our modifier on it for the host's NPC's level (Levels.reds).
  public let levelMod: ref<gameStatModifierData>;
  public let levelShift: Float;
}

// A proxy taken out of its seat, deleted a moment later (a body is deleted
// once it is no longer mounted, as System.reds does for puppets).
public class TandemNpcLeaving {
  public let id: EntityID;
  public let at: Float;
}

// A proxy that came a moment ago: which of our own NPCs is the same person
// is decided once its owner's word on what it is has had time to come
// (TandemNpcs.MergeTwins).
public class TandemTwinPending {
  public let npc: Uint32;
  public let owner: Uint32;
  public let record: TweakDBID;
  public let at: Vector4;
  public let since: Float;
}

// What another game computed for a hit it forwarded to our V, per damage
// type (gameAttackComputed's order): our game's rules may lower it, never
// raise it (Combat.reds CapForwarded).
public class TandemSentDamage {
  public let values: array<Float>;
}

public class TandemNpcs {
  private let m_game: GameInstance;
  private let m_known: array<ref<TandemNpcKnown>>;
  private let m_next: Uint32;
  private let m_last: Float;
  private let m_proxies: array<ref<TandemNpcProxy>>;
  // Regions: our V is inside a higher-ranked linked player's region: that
  // game populates the streets we both see, so our crowd is turned off and
  // what still spawns there is hidden. Alone in our region, our crowd is on.
    private let m_quiet: Bool;
  // Since when a linked player that outranks us has been unplaced (0: none
  // is), and whether we now take it to be with us.
  private let m_unplacedSince: Float;
  private let m_coveredSince: Float;
  private let m_assumeTogether: Bool;
  private let m_lastQuiet: Float;
  // The overlap: this game's setting (read once), the share of our crowd we
  // keep now, the null areas we keep (by the player whose region each
  // covers), and the NPCs left to their vehicle's fate.
  private let m_overlap: String;
  private let m_overlapRead: Bool;
  private let m_share: Float;
  private let m_nullPlayers: array<Uint32>;
  private let m_nullIds: array<Uint64>;
  private let m_nullLogged: Bool;
  private let m_riders: Int32;
  private let m_hidden: Int32;
  private let m_hiddenIds: array<EntityID>;
  // Host: guests' status effects waiting for their puppet (the instigator).
  private let m_pendingEffects: array<TandemNpcEvent>;
  private let m_pendingSince: array<Float>;
  // Our player id in the session, and the game time (Tick).
  private let m_you: Uint32;
  private let m_now: Float;
  // Guest: the host's NPCs' hits on our V queued in our damage system, with
  // the armour penetration and the damage the host sent for each
  // (Combat.reds).
  private let m_forwarded: array<ref<gameHitEvent>>;
  private let m_forwardedPierce: array<Float>;
  private let m_forwardedSent: array<ref<TandemSentDamage>>;
  // The attack records other games named that ours lacks, each logged once
  // a session (Combat.reds AttackRecord).
  private let m_standIns: array<TweakDBID>;
  // Guest: the host's NPCs' melee hits on our V waiting for their proxy to
  // be drawn landing them (TandemFight.MeleeHold): the hit, its armour
  // penetration, when it runs. And whether the first was logged.
  private let m_held: array<ref<gameHitEvent>>;
  private let m_heldPierce: array<Float>;
  private let m_heldUntil: array<Float>;
  private let m_heldLogged: Bool;
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
  // Each V's level, the session's, and our NPCs' or proxies' (Levels.reds).
  private let m_levels: ref<TandemLevels>;
  // Enemies tougher for each extra player near them (Scaling.reds).
  private let m_scaling: ref<TandemScaling>;
  // The session's dead (Ledger.reds), and the proxies whose twin among our
  // own NPCs is still to be found.
  private let m_ledger: ref<TandemLedger>;
  private let m_twins: array<ref<TandemTwinPending>>;

  public func Ledger() -> ref<TandemLedger> {
    return this.m_ledger;
  }

  public func Levels() -> ref<TandemLevels> {
    return this.m_levels;
  }

  public func Scaling() -> ref<TandemScaling> {
    return this.m_scaling;
  }

  // An owner's word: how many players one of its NPCs is scaled for, on its
  // proxy if it is there.
  public func SetScale(npc: Uint32, players: Int32) -> Void {
    this.m_scaling.Word(npc, players);
    let proxy = this.FindProxy(npc);
    if IsDefined(proxy) && proxy.seenAt > 0.0 && !proxy.dead {
      let body = this.ProxyBody(proxy.id) as ScriptedPuppet;
      if IsDefined(body) {
        this.m_scaling.Proxy(npc, body);
      }
    }
  }

  // An NPC's level as its owner says (the host, or a guest for an NPC of its
  // own, regions.md), on our proxy of it if it is there.
  public func SetLevel(npc: Uint32, level: Float) -> Void {
    this.m_levels.OnNpc(npc, level);
    let proxy = this.FindProxy(npc);
    if IsDefined(proxy) && proxy.seenAt > 0.0 {
      let body = GameInstance.GetDynamicEntitySystem().GetEntity(proxy.id) as ScriptedPuppet;
      if IsDefined(body) {
        this.m_levels.Proxy(proxy, body);
        this.ApplyHealth(proxy);
      }
    }
  }

  // Host: the NPCs we share (Scenes.reds asks which of them act in a scene).
  public func Known() -> array<ref<TandemNpcKnown>> {
    return this.m_known;
  }

  public static func Range() -> Float {
    return 60.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_next = 1u;
    this.m_anims = new TandemNpcAnims();
    this.m_anims.Start(game, this);
    this.m_levels = new TandemLevels();
    this.m_levels.Start(game);
    this.m_scaling = new TandemScaling();
    this.m_scaling.Start(game);
    this.m_ledger = new TandemLedger();
    this.m_ledger.Start(game);
    this.m_share = 1.0;
  }

  // The recorder, solo (System.reds RecordTick): the NPCs around our V as a
  // host shares them, nothing else (no hits to take, no crowd change).
  public func RecordTick(now: Float) -> Void {
    this.m_you = 0u;
    this.m_now = now;
    this.m_anims.Events(now, 0u);
    if now - this.m_last >= 0.1 {
      this.m_last = now;
      this.Share();
    }
  }

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_you = you;
    this.m_now = now;
    this.m_levels.Tick(now);
    // Who runs which NPC first (regions): a hand-over moves an NPC between
    // the two lists below.
    this.TakeRegionEvents(now);
    // The owner's side: the NPCs our game runs.
    this.TakeHits();
    // What the game changed on our NPCs' animation since the last frame.
    this.m_anims.Events(now, you);
    if now - this.m_last >= 0.1 {
      this.m_last = now;
      this.UpdateQuiet();
      this.Share();
    }
    // The proxies' side: the NPCs the players linked with us run.
    this.Follow(now);
    this.m_scaling.Proxies(this, now);
    this.m_ledger.Tick(now, you);
    this.TakePlayerHits();
    if now - this.m_lastCheck >= 1.0 {
      this.m_lastCheck = now;
      this.Check(now);
    }
    let outcomes = TandemOutcomes.Of(this.m_game);
    if IsDefined(outcomes) {
      outcomes.Tick(now, you, this);
    }
  }

  // A new id of our own range: our player id minus one in the top 8 bits
  // (tandem/regions.hpp MakeNpcId; the host's are as they always were, and
  // so are the recorder's, solo: m_you 0).
  private func NewId() -> Uint32 {
    let slot = this.m_you > 1u ? this.m_you - 1u : 0u;
    let id = slot * 16777216u + this.m_next;
    this.m_next += 1u;
    return id;
  }

  // The body of a proxy: one of our spawned entities, or (handed over in
  // place) the game's own.
  public func ProxyBody(id: EntityID) -> ref<Entity> {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(id);
    if !IsDefined(body) {
      body = GameInstance.FindEntityByID(this.m_game, id);
    }
    return body;
  }

  // A proxy leaves: deleted when it is ours; a body of the game's own (an
  // NPC of ours we handed over) is hidden for good instead: it belongs to
  // another player's world now.
  private func Dispose(proxy: ref<TandemNpcProxy>) -> Void {
    if proxy.native {
      let body = GameInstance.FindEntityByID(this.m_game, proxy.id) as ScriptedPuppet;
      if IsDefined(body) && !ArrayContains(this.m_hiddenIds, proxy.id) {
        body.HideIrreversibly();
        ArrayPush(this.m_hiddenIds, proxy.id);
      }
    } else {
      TandemEntities.Delete(proxy.id);
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
  // A melee hit waits until its proxy is drawn landing it (the host decided
  // it a network trip and an interpolation delay ahead of what we see), so
  // a block, a parry or a dodge is timed against the swing on our screen,
  // as in single player (docs/architecture/combat-coverage.md).
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
        let hold = (hit.flags & 4) != 0 && IsDefined(proxy) ? TandemFight.MeleeHold(hit.npc) : 0.0;
        if hold > 0.005 && ArraySize(this.m_held) < 16 {
          ArrayPush(this.m_held, event);
          ArrayPush(this.m_heldPierce, hit.pierce);
          ArrayPush(this.m_heldUntil, this.m_now + hold);
          if !this.m_heldLogged {
            this.m_heldLogged = true;
            TandemNative.Log(s"npcs: npc \(hit.npc)'s melee hit on our V held \(hold * 1000.0) ms, until its proxy lands it");
          }
        } else {
          this.RunPlayerHit(damage, event, hit.pierce);
        }
        TandemNative.Log(s"npcs: hit by npc \(hit.npc)\(IsDefined(proxy) ? "" : " (no proxy)"): \(hit.physical) physical, \(hit.thermal) thermal, \(hit.electric) electric, \(hit.chemical) chemical, type \(hit.attackType), attack \(TDBID.ToStringDEBUG(hit.attack))");
      }
    }
    let i = 0;
    while i < ArraySize(this.m_held) {
      if this.m_now >= this.m_heldUntil[i] {
        this.RunPlayerHit(damage, this.m_held[i], this.m_heldPierce[i]);
        ArrayErase(this.m_held, i);
        ArrayErase(this.m_heldPierce, i);
        ArrayErase(this.m_heldUntil, i);
      } else {
        i += 1;
      }
    }
  }

  private func RunPlayerHit(damage: ref<DamageSystem>, event: ref<gameHitEvent>, pierce: Float) -> Void {
    this.Forward(event, pierce);
    damage.QueueHitEvent(event, event.target);
  }

  public func IsForwarded(hit: ref<gameHitEvent>) -> Bool {
    return ArrayContains(this.m_forwarded, hit);
  }

  // The damage the sender computed, as the hit was queued here.
  public func ForwardedSent(hit: ref<gameHitEvent>, out sent: array<Float>) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_forwarded) {
      if this.m_forwarded[i] == hit {
        sent = this.m_forwardedSent[i].values;
        return true;
      }
      i += 1;
    }
    return false;
  }

  public func NoteStandIn(attack: TweakDBID, standIn: TweakDBID) -> Void {
    if ArrayContains(this.m_standIns, attack) || ArraySize(this.m_standIns) >= 64 {
      return;
    }
    ArrayPush(this.m_standIns, attack);
    TandemNative.Log(s"combat: attack \(TDBID.ToStringDEBUG(attack)) unknown here, stood in by \(TDBID.ToStringDEBUG(standIn)) (its damage: at most the sender's)");
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

  // A hit another game computed, about to be queued on our V: the host's
  // NPC's, another V's area effect (Outcomes.reds). The newest few: an entry
  // leaves when its pipeline ends.
  public func Forward(hit: ref<gameHitEvent>, pierce: Float) -> Void {
    while ArraySize(this.m_forwarded) >= 16 {
      ArrayErase(this.m_forwarded, 0);
      ArrayErase(this.m_forwardedPierce, 0);
      ArrayErase(this.m_forwardedSent, 0);
    }
    let sent = new TandemSentDamage();
    sent.values = hit.attackComputed.GetAttackValues();
    ArrayPush(this.m_forwarded, hit);
    ArrayPush(this.m_forwardedPierce, pierce);
    ArrayPush(this.m_forwardedSent, sent);
  }

  public func Forget(hit: ref<gameHitEvent>) -> Void {
    let i = 0;
    while i < ArraySize(this.m_forwarded) {
      if this.m_forwarded[i] == hit {
        ArrayErase(this.m_forwarded, i);
        ArrayErase(this.m_forwardedPierce, i);
        ArrayErase(this.m_forwardedSent, i);
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
      if proxy.native {
        // Ours before we handed it over, and nobody runs it any more: back
        // to our own game.
        let own = GameInstance.FindEntityByID(this.m_game, proxy.id) as ScriptedPuppet;
        if IsDefined(own) && !proxy.dead {
          TandemProxies.Wake(own);
        }
      } else {
        TandemEntities.Delete(proxy.id);
      }
    }
    for leaving in this.m_leaving {
      TandemEntities.Delete(leaving.id);
    }
    ArrayClear(this.m_leaving);
    ArrayClear(this.m_proxies);
    // Host: our NPCs are scaled to our V again, and for one player.
    for known in this.m_known {
      this.m_levels.Release(known, known.id);
    }
    this.m_scaling.End();
    this.m_ledger.End();
    ArrayClear(this.m_twins);
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
    let body = this.ProxyBody(proxy.id) as GameObject;
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
    this.m_levels.End();
    this.m_scaling.End();
    ArrayClear(this.m_hiddenIds);
    if this.m_quiet || this.m_share < 0.95 {
      GameInstance.GetCommunitySystem(this.m_game).ResetDensityModifier();
      this.m_quiet = false;
    }
    this.m_share = 1.0;
    this.UpdateNullAreas(false);
  }

  public func Hidden() -> Int32 {
    return this.m_hidden;
  }

  // Our own crowd is off (our V deep in a linked player's region): no save
  // meanwhile (Saves.reds).
  public func Quiet() -> Bool {
    return this.m_quiet;
  }

  // ---- regions: our crowd, and what we hold back ----

  // Our own crowd where a higher-ranked linked player's region covers the
  // world (its game populates what we both see; regions.md §6). Deep inside
  // it (45 m of its V, back beyond 60 m) our crowd is off. Around it, by
  // this game's `overlap` setting: "density", the crowd's density follows
  // the part of our region no cover overlaps; "area", a crowd null area is
  // kept over each cover (pedestrians and traffic spawn nowhere in it); "off",
  // nothing more. Alone in our region our game is the single-player game.
  private func UpdateQuiet() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if !this.m_overlapRead {
      this.m_overlapRead = true;
      this.m_overlap = TandemNative.Setting("overlap");
    }
    let here = player.GetWorldPosition();
    let range = this.m_quiet ? TandemRegionRules.Radius() : TandemRegionRules.QuietIn();
    let unplaced = false;
    let by = TandemRegionRules.CoveredBy(here, range, unplaced);
    // A player that outranks us and has said nothing of where it is: we
    // wait three seconds, then take it to be with us (a host that sends no
    // state, as before regions; a mock host standing idle).
    if unplaced {
      if this.m_unplacedSince <= 0.0 {
        this.m_unplacedSince = this.m_now;
      }
    } else {
      this.m_unplacedSince = 0.0;
    }
    this.m_assumeTogether = unplaced && this.m_now - this.m_unplacedSince >= 3.0;
    if unplaced && !this.m_assumeTogether {
      return;
    }
    let crowd = GameInstance.GetCommunitySystem(this.m_game);
    // Covered for a second before the crowd goes: at a join the others'
    // places are not all known yet (a guest 1 km away was covered for 0.14 s,
    // its street emptied and filled again: regions-apart).
    if by == 0u || this.m_quiet {
      this.m_coveredSince = 0.0;
    } else if this.m_coveredSince <= 0.0 || this.m_now < this.m_coveredSince {
      this.m_coveredSince = this.m_now;
    }
    if by != 0u && !this.m_quiet && this.m_now - this.m_coveredSince >= 1.0 {
      this.m_quiet = true;
      this.m_share = 0.0;
      crowd.ChangeDensityModifier(0.0);
      TandemNative.Log(s"npcs: guest, own crowd off (inside player \(by)'s region)");
    } else if by == 0u && this.m_quiet {
      this.m_quiet = false;
      this.m_share = 1.0;
      crowd.ResetDensityModifier();
      TandemNative.Log("npcs: own region, own crowd on");
    }
    // (Also deep inside: whether density 0 stops traffic too is not known.)
    if Equals(this.m_overlap, "area") {
      this.UpdateNullAreas(true);
    }
    if this.m_quiet {
      return;
    }
    if Equals(this.m_overlap, "density") {
      // Thinner as the two regions overlap more (tenths: no change on
      // every step of a walk).
      let share = TandemRegions.CrowdShare(here.X, here.Y, here.Z, false);
      if share > 0.0 && AbsF(share - this.m_share) >= 0.05 {
        this.m_share = share;
        if share >= 0.95 {
          crowd.ResetDensityModifier();
        } else {
          crowd.ChangeDensityModifier(share);
        }
        TandemNative.Log(s"npcs: own crowd at \(share) (a higher-ranked player's region overlaps ours)");
      }
    }
  }

  // Seated in a vehicle of our world that we do not share (a traffic car
  // held back inside a cover, Traffic.reds): it follows its vehicle.
  private func RidesUnshared(npc: ref<ScriptedPuppet>) -> Bool {
    let ride: wref<VehicleObject>;
    if !VehicleComponent.GetVehicle(this.m_game, npc, ride) || !IsDefined(ride) {
      return false;
    }
    let traffic = TandemTraffic.Of(this.m_game);
    return !IsDefined(traffic) || traffic.IdOfBody(ride.GetEntityID()) == 0u;
  }

  // A crowd null area over each cover, as the plugin's plan says (it
  // follows each V in steps and renews an area before it runs out: an area
  // lasts 12 s by itself, should we stop). Not saved.
  private func UpdateNullAreas(on: Bool) -> Void {
    let steps = TandemRegions.NullSteps(this.m_now, on);
    if ArraySize(steps) == 0 {
      return;
    }
    let crowd = GameInstance.GetCommunitySystem(this.m_game);
    for step in steps {
      let i = ArrayFindFirst(this.m_nullPlayers, step.player);
      if i >= 0 {
        crowd.DisableCrowdNullArea(this.m_nullIds[i]);
        ArrayErase(this.m_nullPlayers, i);
        ArrayErase(this.m_nullIds, i);
      }
      if step.place {
        let box: Box;
        box.Min = new Vector4(-60.0, -60.0, -40.0, 1.0);
        box.Max = new Vector4(60.0, 60.0, 40.0, 1.0);
        let at: WorldTransform; // assigned on every path
        WorldTransform.SetIdentity(at);
        WorldTransform.SetPosition(at, new Vector4(step.x, step.y, step.z, 1.0));
        let id = crowd.EnableDynamicCrowdNullArea(box, at, false, 12.0);
        ArrayPush(this.m_nullPlayers, step.player);
        ArrayPush(this.m_nullIds, id);
        if !this.m_nullLogged {
          this.m_nullLogged = true;
          TandemNative.Log(s"npcs: crowd null area \(id) over player \(step.player)'s region at (\(step.x), \(step.y), \(step.z))");
        }
      } else {
        this.m_nullLogged = false;
        TandemNative.Log(s"npcs: crowd null area over player \(step.player)'s region removed");
      }
    }
  }

  // (test) what our game holds back now.
  public func OverlapReport() -> String {
    let covers = TandemRegions.Covers();
    return s"overlap: \(this.m_overlap), \(ArraySize(covers)) cover(s), own crowd \(this.m_quiet ? "off" : s"at \(this.m_share)"), \(ArraySize(this.m_nullIds)) null area(s), \(this.m_hidden) NPC(s) hidden so far, \(this.m_riders) rider(s) left to their vehicle";
  }

  // An NPC of ours that we do not add to the shared world: brain off and out
  // of sight for good (a new spawn inside a higher-ranked player's region,
  // or the twin of an NPC that player runs).
  // Our own NPCs we hide (Vendors.reds: our vendor behind its owner's proxy).
  public func HiddenIds() -> array<EntityID> {
    return this.m_hiddenIds;
  }

  private func Suppress(npc: ref<ScriptedPuppet>) -> Void {
    let id = npc.GetEntityID();
    if ArrayContains(this.m_hiddenIds, id) {
      return;
    }
    // A vendor's shop stays its own V's (Vendors.reds): what it goes by is
    // read now, while it stands there.
    let vendors = TandemVendors.Of(this.m_game);
    if IsDefined(vendors) {
      vendors.Keep(npc);
    }
    TandemProxies.Pacify(npc);
    npc.HideIrreversibly();
    ArrayPush(this.m_hiddenIds, id);
    this.m_hidden += 1;
  }

  // Proxies of a higher-ranked player's NPCs came where NPCs of ours stand:
  // a placed NPC (a vendor at its stall, a guard at its post) is in both
  // games. Ours gives way, the proxy stands in its place. Which of ours is
  // the same person (tandem/twins.hpp): the one of the static id its owner
  // named, else the nearest of the same record with a dynamic id, one of
  // ours to one proxy (two identical guards side by side are two pairs).
  // Decided for all the proxies of a batch together, a moment after they
  // came (their owner's word on what they are comes by another queue).
  private func MergeTwins(now: Float) -> Void {
    if ArraySize(this.m_twins) == 0 {
      return;
    }
    let due: array<ref<TandemTwinPending>>;
    let waiting: array<ref<TandemTwinPending>>;
    for pending in this.m_twins {
      if now - pending.since >= 0.3 {
        ArrayPush(due, pending);
      } else {
        ArrayPush(waiting, pending);
      }
    }
    if ArraySize(due) == 0 {
      return;
    }
    this.m_twins = waiting;
    let theirRecords: array<TweakDBID>;
    let theirPlaces: array<Vector4>;
    let theirEntities: array<EntityID>;
    for pending in due {
      ArrayPush(theirRecords, pending.record);
      ArrayPush(theirPlaces, pending.at);
      ArrayPush(theirEntities, TandemRegions.PlacedOf(pending.npc));
    }
    let candidates: array<ref<TandemNpcKnown>>;
    let bodies: array<ref<ScriptedPuppet>>;
    let ourRecords: array<TweakDBID>;
    let ourPlaces: array<Vector4>;
    let ourEntities: array<EntityID>;
    let ourBusy: array<Bool>;
    let none = EntityID.FromHash(0ul);
    for known in this.m_known {
      if !known.dead && !known.down && !known.managed {
        let own = GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet;
        if IsDefined(own) {
          ArrayPush(candidates, known);
          ArrayPush(bodies, own);
          ArrayPush(ourRecords, own.GetRecordID());
          ArrayPush(ourPlaces, own.GetWorldPosition());
          ArrayPush(ourEntities, TandemPlaced.IdOf(own));
          ArrayPush(ourBusy, TandemRegionRules.Engaged(this.m_game, own));
        }
      }
    }
    if ArraySize(candidates) == 0 {
      return;
    }
    let pairs = TandemRegions.MatchTwins(theirRecords, theirPlaces, theirEntities, ourRecords, ourPlaces, ourEntities, ourBusy);
    let i = 0;
    while i < ArraySize(pairs) && i < ArraySize(due) {
      let ours = pairs[i];
      if ours >= 0 && ours < ArraySize(candidates) {
        let known = candidates[ours];
        TandemNative.SendNpcGone(known.npc, 0, 0u);
        this.m_anims.Forget(known.npc);
        this.Suppress(bodies[ours]);
        this.ForgetKnown(known.npc);
        TandemNative.Log(s"regions: our npc \(known.npc) is the twin of player \(due[i].owner)'s \(due[i].npc) (\(EntityID.IsDefined(theirEntities[i]) ? "the same placed NPC" : "by record and place")): ours gives way");
      }
      i += 1;
    }
  }

  // A new NPC of ours that another game already runs: the placed NPC of
  // that entity is shown here as a proxy (alive or dead). One owner per NPC:
  // ours is not added to the shared world.
  private func IsTwinOfProxy(id: EntityID) -> Bool {
    if !EntityID.IsDefined(id) {
      return false;
    }
    for proxy in this.m_proxies {
      if !proxy.native && TandemRegions.PlacedOf(proxy.npc) == id {
        return true;
      }
    }
    return false;
  }

  // Our own copy of a vendor the session says died elsewhere (Vendors.reds):
  // out of the shared world if we announced it, then out of sight for good.
  // False when there is no living copy of ours to hide.
  public func Bury(id: EntityID) -> Bool {
    let own = GameInstance.FindEntityByID(this.m_game, id) as ScriptedPuppet;
    if !IsDefined(own) || own.IsDead() || this.IsProxyEntity(id) {
      return false;
    }
    let known = this.Find(id);
    if IsDefined(known) {
      if known.dead || known.down {
        return false; // the one that died is ours
      }
      TandemNative.SendNpcGone(known.npc, 0, 0u);
      this.m_anims.Forget(known.npc);
      this.ForgetKnown(known.npc);
    }
    // A body that streamed in again is a new one: hidden again.
    ArrayRemove(this.m_hiddenIds, id);
    this.Suppress(own);
    return true;
  }

  private func ForgetKnown(npc: Uint32) -> Void {
    let kept: array<ref<TandemNpcKnown>>;
    for known in this.m_known {
      if known.npc != npc {
        ArrayPush(kept, known);
      }
    }
    this.m_known = kept;
  }

  // ---- the owner's side ----

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
    // Enemy scaling, twice a second, in a session only (not the recorder's).
    let scaling = this.m_you != 0u && this.m_scaling.Due(this.m_now);
    // Our V away from its body in another world (Elsewhere.reds): the NPCs
    // around it are that world's, none is shared, and the shared ones go.
    // Out to where an NPC leaves the shared world: one we run stays ours
    // until then (a hand-over is asked before, regions.hpp kLose).
    let around = player.GetNPCsAroundObject(TandemRegionRules.Lose());
    if TandemElsewhere.AwayFromBody(this.m_game) {
      ArrayClear(around);
    }
    let here = player.GetWorldPosition();
    // Inside another world (cyberspace, a braindance) the NPCs are that
    // world's: none is hidden, as before.
    let hides = !riding && !TandemElsewhere.Inside(this.m_game);
    // The session's dead near us, if any (Ledger.reds).
    let riders = 0;
    let ledger = hides && this.m_you != 0u && this.m_ledger.On() && TandemRegions.DeathsNear(here.X, here.Y, here.Z, 2000.0) > 0;
    for npc in around {
      let id = npc.GetEntityID();
      let known = this.Find(id);
      // The same dead stay dead: our own copy of a placed NPC that died in
      // the session (in another game, or here before it streamed out) is
      // kept out of our world. Not one lying here, nor one of ours in a fight.
      if ledger && !npc.IsDead() && !ScriptedPuppet.IsUnconscious(npc) && !ScriptedPuppet.IsDefeated(npc) && (!IsDefined(known) || !NPCPuppet.IsInCombat(npc)) && this.m_ledger.Buries(npc) {
        if this.Bury(id) {
          let died = TandemPlaced.IdOf(npc);
          this.m_ledger.NoteBuried(died, id, TandemRegions.DiedBy(died));
        }
        known = this.Find(id);
      }
      // New to us: ours to add to the shared world when it is in our region
      // (the radius), is not a remote player's puppet, a proxy or one we
      // hold back, and no higher-ranked linked player's region covers it.
      let ours = IsDefined(known);
      if !ours && ArraySize(ids) < 64 && !riding && !npc.IsDead() && !this.IsPuppet(id) && !this.IsProxyEntity(id) && !TandemLedger.IsBody(id) && !ArrayContains(this.m_hiddenIds, id) && Vector4.Distance(npc.GetWorldPosition(), here) <= TandemNpcs.Range() {
        let unplaced = false;
        let by = hides ? TandemRegionRules.CoveredBy(npc.GetWorldPosition(), TandemRegionRules.Radius(), unplaced) : 0u;
        if unplaced && !this.m_assumeTogether {
          // We do not know yet whether a higher-ranked player's region
          // covers it: neither announced nor held back this round.
        } else if by != 0u && this.RidesUnshared(npc) {
          // A driver or a passenger follows its vehicle (Traffic.reds holds
          // the car back, and shares it once it is out of the cover): never
          // hidden at the wheel, not announced while in there.
          riders += 1;
        } else if by != 0u && !TandemRegionRules.Engaged(this.m_game, npc) {
          this.Suppress(npc);
        } else if TandemVendors.IsBuried(npc) {
          // Our copy of a vendor the session says died (Vendors.reds).
          this.Suppress(npc);
        } else if hides && this.IsTwinOfProxy(TandemPlaced.IdOf(npc)) {
          // Another game runs this placed NPC already (its proxy is here).
          this.Suppress(npc);
          TandemNative.Log(s"regions: our own \(EntityID.ToDebugString(id)) is a placed NPC another game runs: held back");
        } else {
          known = new TandemNpcKnown();
          known.id = id;
          known.npc = this.NewId();
          known.health = -1.0;
          known.poise = -1.0;
          known.record = npc.GetRecordID();
          known.appearance = npc.GetCurrentAppearanceName();
          ArrayPush(this.m_known, known);
          TandemNative.SendNpcSpawn(known.npc, npc.GetRecordID(), npc.GetCurrentAppearanceName(), npc.GetWorldPosition(), npc.GetWorldOrientation(), TandemMods.NpcHint(npc));
          // A vendor placed in the world says its entity: a partner whose
          // game shows it opens its own vendor of it (Vendors.reds).
          TandemVendors.Announce(known.npc, npc);
          // Placed in the world (never the random crowd): the same id in
          // every game (TandemPlaced). Its twin there is known by it, and
          // its death is the session's to remember (Ledger.reds).
          let placed = TandemPlaced.IdOf(npc);
          if EntityID.IsDefined(placed) {
            TandemRegions.Placed(known.npc, placed);
          }
          // A quest or story NPC, a police unit: ours for good.
          if TandemRegionRules.Pinned(npc) {
            TandemRegions.Pin(known.npc);
          }
          ours = true;
        }
      }
      // One we run stays ours past what one message carries (64): it is
      // left out of this round's states, not dropped.
      if ours && !riding && !npc.IsDead() {
        known.seen = true;
      }
      // The dead are handled below.
      if ours && ArraySize(ids) < 64 && !riding && !npc.IsDead() {
        // Taken over a moment ago: did its brain come back?
        if known.wokeAt > 0.0 && this.m_now - known.wokeAt >= 1.5 {
          known.wokeAt = 0.0;
          if !TandemProxies.Awake(npc) {
            this.Respawn(known, npc);
          }
        }
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
        let protect = TandemNpcs.Protected(npc) ? 1 : 0;
        if AbsF(health - known.health) >= 0.01 || protect != known.protect {
          if protect != known.protect {
            TandemNative.Log(s"npcs: \(known.npc) \(protect == 1 ? "cannot die now (immortal or invulnerable)" : "can die again")");
          }
          known.health = health;
          known.protect = protect;
          TandemNative.SendNpcHealth(known.npc, health, protect);
        }
        // Scaled to the session's level, and its level told (Levels.reds).
        this.m_levels.Host(known, npc);
        // Tougher for each extra player near it, said to all (Scaling.reds).
        if scaling {
          this.m_scaling.Own(npc, known.npc, watched, this.m_you, known.lastHitBy, known.lastHitAt, known.lastOwnHitAt, this.m_now);
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
          if by != this.m_you {
            this.PayAssist(player, known.id);
          }
        }
      }
    }
    // Not listed: dead (a corpse stays known, reported dead once, until it
    // goes), or gone.
    let kept: array<ref<TandemNpcKnown>>;
    for known in this.m_known {
      if known.seen || known.pendingUntil > this.m_now {
        ArrayPush(kept, known);
      } else {
        let body = GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet;
        if IsDefined(body) && body.IsDead() && Vector4.Distance(body.GetWorldPosition(), player.GetWorldPosition()) < TandemNpcs.Range() + 20.0 {
          if !known.dead {
            known.dead = true;
            let killer = this.KillerOf(body, known, player);
            TandemNative.SendNpcGone(known.npc, 1, killer);
            this.m_anims.Forget(known.npc);
            this.m_scaling.Forget(known.npc);
            TandemNative.Log(s"npcs: \(known.npc) died (killer: player \(killer))");
            // A guest's kill we helped with: our share of the skill XP is
            // paid as a guest's is (Die), and does not stay queued in our
            // save (the game only pays it on our own kill).
            if killer != this.m_you {
              this.PayAssist(player, known.id);
            }
            // Its quest and important items reach the guests' corpses (Loot.reds).
            TandemLoot.HostCorpse(this.m_game, known.id, known.npc);
          }
          ArrayPush(kept, known);
        } else {
          TandemNative.SendNpcGone(known.npc, 0, 0u);
          this.m_anims.Forget(known.npc);
          // A proxy we took over is one of our spawned entities: the game
          // would never stream it out.
          if known.managed {
            TandemEntities.Delete(known.id);
          }
          // No longer shared: scaled to our V again, for one player.
          this.m_levels.Release(known, known.id);
          this.m_scaling.Release(known.npc);
        }
      }
    }
    this.m_known = kept;
    this.m_riders = riders;
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
          TandemNative.SendNpcSeat(known.npc, known.seatOwner, slot);
          TandemNative.LogDebug(s"npcs: \(known.npc) moved to \(NameToString(slot))");
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
      known.seatOwner = this.m_you;
      TandemNative.SendNpcSeat(known.npc, this.m_you, slot);
      TandemNative.Log(s"npcs: \(known.npc) sits in \(NameToString(slot)) of our vehicle");
      return;
    }
    // In a world vehicle we share (Traffic.reds): a driver, a passenger, an
    // officer in a police car. Seated in its proxy on the other games.
    let traffic = TandemTraffic.Of(this.m_game);
    let ride: wref<VehicleObject>;
    if IsDefined(traffic) && VehicleComponent.GetVehicle(this.m_game, npc, ride) && IsDefined(ride) {
      let car = traffic.IdOfBody(ride.GetEntityID());
      if car != 0u {
        VehicleComponent.GetMountedSlotName(this.m_game, npc, slot);
        known.seatVehicle = ride.GetEntityID();
        known.seat = slot;
        known.seatOwner = TandemStreet.SeatOwner(car);
        TandemNative.SendNpcSeat(known.npc, known.seatOwner, slot);
      }
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
  // or not, on their own (docs/architecture/stealth.md, each player is detected on their
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
        // The game names our V for deaths it cannot place (a guest's
        // takedown or strike finished it): a guest's hit after our V's last
        // one is the kill.
        if known.lastHitBy != 0u && this.m_now - known.lastHitAt < 8.0 && known.lastHitAt > known.lastOwnHitAt {
          return known.lastHitBy;
        }
        return this.m_you;
      }
      let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
      let who = IsDefined(system) ? system.PlayerOfPuppet(killer.GetEntityID()) : 0u;
      if who != 0u {
        return who;
      }
    }
    return known.lastHitBy != 0u && this.m_now - known.lastHitAt < 8.0 ? known.lastHitBy : 0u;
  }

  // Status effects other players' hits put on our NPCs (new ones come with
  // the NPC events, in Follow). Their hits themselves are strikes:
  // Outcomes.reds.
  private func TakeHits() -> Void {
    // Effects that waited for their player's puppet.
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
  }

  // A status effect a guest's hit put on our NPC: its behaviour here (stun,
  // panic, burning), from the guest's puppet, whose damage ticks we leave
  // to the guest (Combat.reds). False: their puppet is not here yet. The
  // Relic's marks act on the dead (Outcomes.reds IsRelicMark); a second
  // chance's memory wipe is a request (Reactions.reds DodgedOutOfSight).
  private func ApplyEffect(event: TandemNpcEvent) -> Bool {
    let known = this.FindNpc(event.npc);
    let body = IsDefined(known) ? GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet : null;
    if !IsDefined(body) || (body.IsDead() && !TandemOutcomes.IsRelicMark(event.record)) {
      return true; // nothing left to apply it to
    }
    // A guest's own XP marker (an older build sent them): not ours to wear.
    // An effect our game lacks (a mod's; said on screen): none.
    if TandemCombat.IsOwnMarker(event.record) || !TDBID.IsValid(event.record) {
      return true;
    }
    // A guest's quickhack landing on an actor of our V's scene, during it:
    // refused like its upload (Hacks.reds, Restrictions.reds' backstop).
    let record = TweakDBInterface.GetStatusEffectRecord(event.record);
    if IsDefined(record) && record.GameplayTagsContains(n"Quickhack")
      && TandemHacks.SceneRefuses(this.m_game, body, event.npc, event.player, event.record) {
      return true;
    }
    let instigator = TandemNpcs.PuppetOf(this.m_game, event.player);
    if !IsDefined(instigator) {
      return false;
    }
    if event.record == t"BaseStatusEffect.MemoryWipeOnDodge" && !TandemReactions.DodgedOutOfSight(body, instigator, event.npc, event.player) {
      return true;
    }
    StatusEffectHelper.ApplyStatusEffect(body, event.record, instigator.GetEntityID());
    TandemNative.LogDebug(s"npcs: \(event.npc) gets \(TDBID.ToStringDEBUG(event.record)) from player \(event.player)");
    return true;
  }

  // Our game cannot kill this NPC now: god mode (a quest's immortality or
  // invulnerability: GetImmortality, final.reds:87656) or its IsInvulnerable
  // stat (as the weak spots read it, final.reds:306570). Its health floors
  // or stays, so a guest's hit that takes the last of its proxy's is none.
  public static func Protected(npc: ref<ScriptedPuppet>) -> Bool {
    let mode = gameGodModeType.Mortal;
    if GetImmortality(npc, mode) {
      return true;
    }
    return GameInstance.GetStatsSystem(npc.GetGame()).GetStatValue(Cast<StatsObjectID>(npc.GetEntityID()), gamedataStatType.IsInvulnerable) > 0.0;
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
      if event.kind == 5 {
        // On an NPC we run: another player's hit put a status effect on it.
        if !this.ApplyEffect(event) {
          ArrayPush(this.m_pendingEffects, event);
          ArrayPush(this.m_pendingSince, this.m_now);
        }
      } else if event.kind == 4 {
        let proxy = this.FindProxy(event.npc);
        if IsDefined(proxy) {
          proxy.health = event.amount;
          proxy.protect = (event.flags & 1) != 0;
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
        let at = new Vector4(event.x, event.y, event.z, 1.0);
        let player = GetPlayer(this.m_game);
        if IsDefined(this.FindProxy(event.npc)) {
          TandemNative.Log(s"consistency: spawn of npc \(event.npc) ignored, its proxy exists");
        } else if IsDefined(this.FindNpc(event.npc)) {
          // One we run (handed over to us before its announcement came).
        } else if IsDefined(player) && Vector4.Distance(player.GetWorldPosition(), at) > 250.0 {
          // Too far to show (we just joined and the host does not know yet
          // where we are): the checks spawn it if it is still ours to show.
        } else {
          // Its owner as the session has it; until it says, the game that
          // announced it (the top bits of its id).
          let owner = TandemRegions.OwnerOf(event.npc);
          if owner == 0u {
            owner = event.npc / 16777216u + 1u;
          }
          // Its twin among our own NPCs gives way (MergeTwins, below).
          if TandemRegionRules.Outranks(owner, this.m_you) && ArraySize(this.m_twins) < 64 {
            let pending = new TandemTwinPending();
            pending.npc = event.npc;
            pending.owner = owner;
            pending.record = event.record;
            pending.at = at;
            pending.since = now;
            ArrayPush(this.m_twins, pending);
          }
          this.SpawnProxy(event.npc, event.record, event.appearance, at, new Quaternion(event.i, event.j, event.k, event.r));
        }
      } else if event.kind == 2 {
        this.Remove(event.npc);
      }
    }
    this.MergeTwins(now);
    this.Reconcile(now);
    // Brain off and our controller on, as soon as each proxy exists.
    for proxy in this.m_proxies {
      if !proxy.ready {
        let npc = this.ProxyBody(proxy.id) as ScriptedPuppet;
        if IsDefined(npc) && proxy.seenAt <= 0.0 {
          proxy.seenAt = now;
          // Brain off at once: a proxy never acts on its own.
          TandemProxies.Pacify(npc);
          // The host's NPC's level and scale first: its health is a share
          // of the maximum they give.
          this.m_levels.Proxy(proxy, npc);
          this.m_scaling.Proxy(proxy.npc, npc);
          this.ApplyHealth(proxy);
          this.ApplyPoise(proxy);
        }
        // The controller once its body has had two seconds to load.
        if IsDefined(npc) && proxy.seenAt > 0.0 && now - proxy.seenAt >= 2.0 {
          // An appearance its owner's game has and ours lacks (an NPC look
          // pack): the body wears another (the template's own): said once.
          if IsNameValid(proxy.appearance) && NotEquals(npc.GetCurrentAppearanceName(), proxy.appearance) {
            // Its owner: the announcer, in the id's top 8 bits (regions::Announcer).
            TandemModsNative.ModNote(TandemMods.CharacterLook(), proxy.appearance, 0, proxy.npc / 16777216u + 1u);
            TandemNative.Log(s"npcs: proxy \(proxy.npc) asked for look \(NameToString(proxy.appearance)), wears \(NameToString(npc.GetCurrentAppearanceName()))");
            proxy.appearance = npc.GetCurrentAppearanceName();
          }
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
      let body = this.ProxyBody(proxy.id);
      if IsDefined(body) {
        TandemNative.AttachController(TandemNative.NpcPuppetId(proxy.npc), body);
      }
      proxy.reattachAt = 0.0;
    }
    if !proxy.seatChanged || now < proxy.seatRetryAt {
      return;
    }
    let npc = this.ProxyBody(proxy.id) as GameObject;
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
    // A seat in a shared world vehicle: its proxy here (Traffic.reds).
    let street = TandemStreet.IsStreetSeat(proxy.seatOwner);
    let traffic = TandemTraffic.Of(game);
    if street && IsDefined(traffic) {
      carId = traffic.BodyOf(TandemStreet.CarOfSeat(proxy.seatOwner));
    }
    let car = TandemTraffic.Body(game, carId);
    let slot = TandemVehicles.FreeSlot(game, car, proxy.seatSlot, proxy.id);
    if !IsDefined(car) || !IsNameValid(slot) {
      // No copy yet (it spawns with the player's seat), or every seat taken.
      proxy.seatRetryAt = now + 0.5;
      return;
    }
    TandemVehicles.Seat(game, npc, car, slot);
    if street && IsDefined(traffic) {
      traffic.MarkSeated(TandemStreet.CarOfSeat(proxy.seatOwner));
    }
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
        TandemEntities.Delete(leaving.id);
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
    return this.ProxyBody(proxy.id) as ScriptedPuppet;
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

  // ---- guest: consistency (docs/architecture/consistency.md, Consistency) ----

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
      // (A corpse or an unconscious body stays where it fell: the host's
      // record lets go of a knocked-out NPC too, its body is ours to keep.)
      if !proxy.dead && !proxy.down && !TandemSync.IsNpcShared(proxy.npc) {
        if proxy.orphanSince <= 0.0 {
          proxy.orphanSince = now;
        } else if now - proxy.orphanSince >= 2.0 {
          TandemNative.Log(s"consistency: proxy \(proxy.npc) has no NPC on the host, removed");
          keep = false;
        }
      } else {
        proxy.orphanSince = 0.0;
      }
      if keep && proxy.seenAt > 0.0 && !IsDefined(this.ProxyBody(proxy.id)) {
        if proxy.goneSince <= 0.0 {
          proxy.goneSince = now;
        } else if now - proxy.goneSince >= 3.0 {
          TandemNative.Log(s"consistency: proxy \(proxy.npc) lost its body\(proxy.dead ? ", corpse dropped" : ", spawned again")");
          keep = false;
          // (A body of the game's own that streamed out comes back as one
          // of ours.)
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
        this.Dispose(proxy);
      }
    }
    this.m_proxies = kept;
    // What the players linked with us share and we do not show.
    let missing: array<Uint32>;
    let since: array<Float>;
    for npc in TandemSync.SharedNpcs() {
      if !IsDefined(this.FindProxy(npc.npc)) && !IsDefined(this.FindNpc(npc.npc)) {
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
    let npc = this.ProxyBody(proxy.id) as ScriptedPuppet;
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
    let npc = this.ProxyBody(proxy.id) as ScriptedPuppet;
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
    this.m_ledger.Witness(TandemRegions.PlacedOf(npc));
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = this.ProxyBody(proxy.id) as ScriptedPuppet;
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
        // The defeat's own reward (level XP, street cred, its bounty), as
        // the game pays a knock-out (RewardKiller, final.reds:21851): the
        // queue above is only the skill XP. Once per body (m_awarded).
        let downed = body as NPCPuppet;
        if IsDefined(downed) {
          BountyManager.CompleteBounty(downed);
        }
      } else {
        let pay = new ProcessQueuedCombatExperience();
        pay.Set(player, proxy.id);
        GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PlayerDevelopmentSystem").QueueRequest(pay);
      }
    }
    TandemNative.Log(s"npcs: proxy \(npc) knocked out\(by == this.m_you ? ", by us" : "")");
  }

  // The skill XP our own hits on a body queued, paid when another player
  // finished it (an assist: a co-op choice, coop-perks.md §2.4).
  private func PayAssist(player: ref<GameObject>, body: EntityID) -> Void {
    if !IsDefined(player) {
      return;
    }
    let pay = new ProcessQueuedCombatExperience();
    pay.Set(player, body);
    GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PlayerDevelopmentSystem").QueueRequest(pay);
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

  // Host: our own V hit one of our shared NPCs (Combat.reds).
  public func NoteOwnHit(id: EntityID) -> Void {
    let known = this.Find(id);
    if IsDefined(known) {
      known.lastOwnHitAt = this.m_now;
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
    // Its corpse is here: the ledger shows no other body for it.
    this.m_ledger.Witness(TandemRegions.PlacedOf(npc));
    this.m_anims.Drop(npc);
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = this.ProxyBody(proxy.id) as ScriptedPuppet;
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
      let npc = this.ProxyBody(proxy.id) as ScriptedPuppet;
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

  // A shared NPC's session id by its entity, dead or alive (one we run, or
  // a proxy), 0: none; and back (Takedowns.reds).
  public func NpcOf(id: EntityID) -> Uint32 {
    let known = this.Find(id);
    if IsDefined(known) {
      return known.npc;
    }
    for proxy in this.m_proxies {
      if proxy.id == id {
        return proxy.npc;
      }
    }
    return 0u;
  }

  public func EntityOf(npc: Uint32) -> EntityID {
    let known = this.FindNpc(npc);
    if IsDefined(known) {
      return known.id;
    }
    let proxy = this.FindProxy(npc);
    if IsDefined(proxy) {
      return proxy.id;
    }
    let none = EntityID.FromHash(0ul);
    return none;
  }

  // Regions: we run this shared NPC (false: a proxy, or none), by its
  // session id or its entity. What "TandemNative.IsHost()" used to stand for
  // wherever an NPC is concerned.
  public func Owns(npc: Uint32) -> Bool {
    return npc != 0u && IsDefined(this.FindNpc(npc));
  }

  public func OwnsEntity(id: EntityID) -> Bool {
    return IsDefined(this.Find(id));
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

  // Quickhacks (Hacks.reds): the entity of a live session NPC (the real
  // one when we run it, else its proxy), and the other way round.
  public func EntityOfNpc(npc: Uint32) -> EntityID {
    let known = this.FindNpc(npc);
    if IsDefined(known) {
      return known.id;
    }
    let proxy = this.FindProxy(npc);
    return IsDefined(proxy) && !proxy.dead ? proxy.id : EntityID();
  }

  public func NpcOfEntity(id: EntityID) -> Uint32 {
    let known = this.Find(id);
    if IsDefined(known) {
      return known.dead ? 0u : known.npc;
    }
    return this.NpcOfProxy(id);
  }

  private func Remove(npc: Uint32) -> Void {
    let kept: array<ref<TandemNpcProxy>>;
    this.m_levels.Forget(npc);
    this.m_scaling.Forget(npc);
    for proxy in this.m_proxies {
      if proxy.npc == npc {
        this.m_anims.Drop(npc);
        TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
        let body = this.ProxyBody(proxy.id) as GameObject;
        if proxy.native {
          this.Dispose(proxy);
        } else if EntityID.IsDefined(proxy.seatedIn) && IsDefined(body) {
          // Out of its seat first, deleted once off it (ProcessLeaving).
          TandemVehicles.Unseat(this.m_game, body, proxy.seatedIn, proxy.seatedSlot);
          let leaving = new TandemNpcLeaving();
          leaving.id = proxy.id;
          leaving.at = this.m_now;
          ArrayPush(this.m_leaving, leaving);
        } else if !proxy.dead || !TandemLoot.LeaveCorpse(this.m_game, proxy.id) {
          // A corpse leaves what it still holds as a loot bag (Loot.reds).
          TandemEntities.Delete(proxy.id);
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
  // proxy is brought back (Reconcile). One its owner's game cannot kill now
  // (a quest's immortal, a boss between phases: Protected) is never killed
  // here: its owner's health comes back instead.
  public func PredictKill(npc: Uint32) -> Void {
    let proxy = this.FindProxy(npc);
    if !IsDefined(proxy) || proxy.dead || TandemTakedowns.Get(this.m_game).DefersDeath(npc, this.m_you) {
      return;
    }
    if proxy.protect {
      TandemNative.LogDebug(s"npcs: proxy \(npc) takes our lethal hit but cannot die on its owner's game: no kill here");
      return;
    }
    proxy.dead = true;
    this.m_ledger.Witness(TandemRegions.PlacedOf(npc));
    proxy.predictedAt = this.m_now;
    proxy.doubtAt = 0.0;
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    let body = this.ProxyBody(proxy.id) as ScriptedPuppet;
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

  // A kill we saw that the host did not: 1.5 s after it with no death from
  // it (its health above zero since, or no word at all: an immortal NPC at
  // its floor sends none), the proxy is spawned again where it fell. (What
  // the kill gave our V stays given.)
  private func Reconcile(now: Float) -> Void {
    for proxy in this.m_proxies {
      if proxy.predictedAt > 0.0 && now - proxy.predictedAt > 1.5 {
        let why = proxy.doubtAt > 0.0 ? "its health says it lives" : "no word of its death in 1.5 s";
        let entities = GameInstance.GetDynamicEntitySystem();
        let old = this.ProxyBody(proxy.id) as GameObject;
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
        this.Dispose(proxy);
        proxy.native = false;
        proxy.id = entities.CreateEntity(spec);
        proxy.ready = false;
        proxy.seenAt = 0.0;
        proxy.dead = false;
        // A new body: our level modifier went with the old one.
        proxy.levelMod = null;
        proxy.levelShift = 0.0;
        proxy.predictedAt = 0.0;
        proxy.doubtAt = 0.0;
        TandemNative.Log(s"npcs: proxy \(proxy.npc) lives on the host (\(why)): our kill was not one, brought back");
      }
    }
  }

  // Guest: the host's NPCs as shown here (Stealth.reds: their detection of us).
  public func Proxies() -> array<ref<TandemNpcProxy>> {
    return this.m_proxies;
  }

  // (Time.reds) The live NPCs this game runs, and their entities in the
  // same order: their owner slows them inside a time bubble.
  public func TimeIds() -> array<Uint32> {
    let out: array<Uint32>;
    for known in this.m_known {
      if !known.dead {
        ArrayPush(out, known.npc);
      }
    }
    return out;
  }

  public func TimeEntities() -> array<EntityID> {
    let out: array<EntityID>;
    for known in this.m_known {
      if !known.dead {
        ArrayPush(out, known.id);
      }
    }
    return out;
  }

  // The live proxies this game shows, and their entities in the same order:
  // animated at the speed their owner's bubble gives them (Police.reds: which
  // of them are officers that see our V).
  public func ProxyIds() -> array<Uint32> {
    let out: array<Uint32>;
    for proxy in this.m_proxies {
      if !proxy.dead {
        ArrayPush(out, proxy.npc);
      }
    }
    return out;
  }

  public func ProxyEntities() -> array<EntityID> {
    let out: array<EntityID>;
    for proxy in this.m_proxies {
      if !proxy.dead {
        ArrayPush(out, proxy.id);
      }
    }
    return out;
  }

  // The shared NPCs here: the ones we run and the proxies we show.
  public func Count() -> Int32 {
    return ArraySize(this.m_known) + ArraySize(this.m_proxies);
  }

  public func OwnedCount() -> Int32 {
    return ArraySize(this.m_known);
  }

  public func ProxyCount() -> Int32 {
    return ArraySize(this.m_proxies);
  }

  public func Clear() -> Void {
    for proxy in this.m_proxies {
      TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(proxy.npc));
      this.Unseat(proxy);
      this.Dispose(proxy);
    }
    ArrayClear(this.m_proxies);
    ArrayClear(this.m_known);
    this.m_anims.Clear();
  }

  // ---- regions: hand-overs (docs/architecture/regions.md §4) ----

  private func TakeRegionEvents(now: Float) -> Void {
    for event in TandemRegions.TakeEvents() {
      if event.kind == 1 {
        this.Give(event.npc, event.player);
      } else if event.kind == 2 {
        this.Take(event);
      } else if event.kind == 3 && event.was == this.m_you {
        this.Lost(event.npc, event.player);
      }
    }
  }

  // The host asks us to hand one of our NPCs over to `to` (our V is leaving
  // it, theirs is near). Its state is read, its brain switched off, and its
  // body stays here as the proxy of what is now their NPC: nothing pops.
  private func Give(npc: Uint32, to: Uint32) -> Void {
    let none: array<TweakDBID>;
    let noTimes: array<Float>;
    let noStacks: array<Int32>;
    let known = this.FindNpc(npc);
    let body = IsDefined(known) ? GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet : null;
    if !IsDefined(known) || !IsDefined(body) {
      // Nothing of it here to read (it streamed out with us): they take it
      // as their proxy shows it (flag 8: blind).
      this.ForgetKnown(npc);
      this.m_anims.Forget(npc);
      TandemRegions.Give(npc, to, 0.0, 0.0, 0.0, 0.0, -1.0, -1.0, 8, 0u, 0, 0, none, noTimes, noStacks);
      TandemNative.Log(s"regions: handover out: npc \(npc) to player \(to), no body here (blind)");
      return;
    }
    // Ours for now: a corpse or a body knocked out stays where it fell; a
    // quest NPC, one held, in a takedown or seated is not to be moved; nor
    // while our V is in another world.
    let takedowns = TandemTakedowns.Get(this.m_game);
    let pinned = TandemRegionRules.Pinned(body);
    if TandemRegions.Holds(this.m_you) || known.dead || known.down || body.IsDead() || pinned || EntityID.IsDefined(known.seatVehicle) || (IsDefined(takedowns) && takedowns.Holds(npc)) || TandemElsewhere.AwayFromBody(this.m_game) {
      if pinned {
        TandemRegions.Pin(npc);
      }
      TandemRegions.Refuse(npc);
      TandemNative.Log(s"regions: handover of npc \(npc) to player \(to) refused\(pinned ? " (pinned)" : "")");
      return;
    }
    let id = known.id;
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let health = pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Health, true) / 100.0;
    let poise = pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Poise, true) / 100.0;
    let flags = 0;
    if NPCPuppet.IsInCombat(body) {
      flags += 1;
    }
    if ScriptedPuppet.IsDefeated(body) {
      flags += 2;
    }
    // Whom it fights (the new owner first) and whom it is hostile to.
    let player = GetPlayer(this.m_game);
    let watched = this.Watched(player);
    let target = 0u;
    let hostileTo = 0;
    let p = 1u;
    let bit = 1;
    for v in watched {
      if IsDefined(v) {
        if NPCPuppet.IsInCombatWithTarget(body, v) && (target == 0u || p == to) {
          target = p;
        }
        if Equals(GameObject.GetAttitudeBetween(body, v), EAIAttitude.AIA_Hostile) {
          hostileTo += bit;
        }
      }
      p += 1u;
      bit *= 2;
    }
    // Its status effects that run out (burning, poison, a stun): the ones
    // with time left, as many as a message carries.
    let effects: array<TweakDBID>;
    let remaining: array<Float>;
    let stacks: array<Int32>;
    for effect in StatusEffectHelper.GetAppliedEffects(body) {
      let left = effect.GetRemainingDuration();
      let record = effect.GetRecord();
      if left > 0.1 && IsDefined(record) && ArraySize(effects) < 8 {
        ArrayPush(effects, record.GetID());
        ArrayPush(remaining, left);
        ArrayPush(stacks, Cast<Int32>(effect.GetStackCount()));
      }
    }
    let at = body.GetWorldPosition();
    let highLevel = EnumInt(body.GetHighLevelStateFromBlackboard());
    // Brain off; our controller moves it from the new owner's states.
    TandemProxies.Pacify(body);
    let proxy = new TandemNpcProxy();
    proxy.npc = npc;
    proxy.id = id;
    proxy.native = !known.managed;
    proxy.health = health;
    proxy.poise = poise;
    proxy.record = body.GetRecordID();
    proxy.appearance = body.GetCurrentAppearanceName();
    // Its rig is loaded: the controller goes on at the next pass.
    proxy.seenAt = this.m_now - 2.0;
    ArrayPush(this.m_proxies, proxy);
    this.ForgetKnown(npc);
    this.m_anims.Forget(npc);
    // Its modifiers stay on the body: the count we said is the new owner's
    // until it says its own.
    this.m_scaling.Handed(npc);
    TandemRegions.Give(npc, to, at.X, at.Y, at.Z, Deg2Rad(body.GetWorldYaw()), health, poise, flags, target, hostileTo, highLevel, effects, remaining, stacks);
    TandemNative.Log(s"regions: handover out: npc \(npc) to player \(to), health \(health), effects \(ArraySize(effects)), target player \(target)");
  }

  // test reset: the NPCs we run that another player announced (handed over
  // or inherited from a mock player, a chaser among them) are deleted: they
  // outlive the scenario and, nobody else up, kill our V at the test field.
  // The scan reports them gone.
  public func DeleteInheritedForTests() -> Int32 {
    let entities = GameInstance.GetDynamicEntitySystem();
    let deleted = 0;
    for known in this.m_known {
      let announcer = known.npc / 16777216u + 1u;
      if this.m_you != 0u && announcer != this.m_you && EntityID.IsDefined(known.id) {
        TandemEntities.Delete(known.id);
        deleted += 1;
      }
    }
    return deleted;
  }

  // An NPC we showed as a proxy is ours to run from now (handed over, or
  // inherited from an owner that is gone): its brain is switched back on and
  // it gets the state that came with it. Same body: nothing pops.
  private func Take(event: TandemRegionEvent) -> Void {
    let npc = event.npc;
    let proxy = this.FindProxy(npc);
    let body = IsDefined(proxy) ? this.ProxyBody(proxy.id) as ScriptedPuppet : null;
    if !IsDefined(proxy) || !IsDefined(body) || proxy.dead || body.IsDead() || TandemElsewhere.AwayFromBody(this.m_game) {
      // Nothing here to wake up: it goes for everyone rather than stay with
      // an owner that does not run it.
      if IsDefined(proxy) {
        this.Remove(npc);
      }
      TandemRegions.Adopt(npc, false);
      TandemNative.Log(s"regions: handover in: npc \(npc) from player \(event.player) refused, no live proxy here");
      return;
    }
    TandemNative.UnregisterPuppet(TandemNative.NpcPuppetId(npc));
    // A rider of a shared vehicle handed to us with it (traffic.md, "Who
    // owns an area") stays in its seat: it drives our car now. Any other
    // seat is the giver's vehicle's: out of it.
    let traffic = TandemTraffic.Of(this.m_game);
    let rides = EntityID.IsDefined(proxy.seatedIn) && TandemStreet.IsStreetSeat(proxy.seatOwner) && IsDefined(traffic) && traffic.Mine(TandemStreet.CarOfSeat(proxy.seatOwner));
    if !rides {
      this.Unseat(proxy);
    }
    this.m_anims.Drop(npc);
    let known = new TandemNpcKnown();
    if rides {
      known.seatVehicle = proxy.seatedIn;
      known.seat = proxy.seatedSlot;
      known.seatOwner = proxy.seatOwner;
    }
    known.id = proxy.id;
    known.npc = npc;
    known.health = -1.0;
    known.poise = -1.0;
    known.seen = true;
    known.down = proxy.down;
    known.managed = !proxy.native;
    known.wokeAt = this.m_now;
    known.record = proxy.record;
    known.appearance = proxy.appearance;
    ArrayPush(this.m_known, known);
    let kept: array<ref<TandemNpcProxy>>;
    for other in this.m_proxies {
      if other.npc != npc {
        ArrayPush(kept, other);
      }
    }
    this.m_proxies = kept;
    TandemProxies.Wake(body);
    // Ours to count from now: the modifiers on the body stay until our own
    // count differs, and the share of health that came with it is kept.
    this.m_scaling.Handed(npc);
    this.Restore(body, event);
    TandemRegions.Adopt(npc, true);
    TandemNative.Log(s"regions: handover in: npc \(npc) from player \(event.player) health \(event.health) effects \(event.effects) target player \(event.target)\((event.flags & 16) != 0 ? " (inherited)" : "")");
  }

  // What came with an NPC taken over, put on its body here: health, guard,
  // the status effects with the time they had left, whom it is hostile to
  // and whom it fights (as tandem's test ganger is set on a puppet,
  // System.reds). What its behaviour tree was doing is not carried: it
  // decides again from its target.
  private func Restore(body: ref<ScriptedPuppet>, event: TandemRegionEvent) -> Void {
    let id = body.GetEntityID();
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    if event.health >= 0.0 {
      pools.RequestSettingStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Health, MaxF(event.health * 100.0, 0.1), null, true);
    }
    if event.poise >= 0.0 {
      pools.RequestSettingStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Poise, event.poise * 100.0, null, true);
    }
    let effects = GameInstance.GetStatusEffectSystem(this.m_game);
    for effect in TandemRegions.EffectsOf(event.npc) {
      // One our game lacks (a mod's) comes as none: skipped.
      if TDBID.IsValid(effect.record) && !StatusEffectSystem.ObjectHasStatusEffect(body, effect.record) {
        StatusEffectHelper.ApplyStatusEffect(body, effect.record);
      }
      if TDBID.IsValid(effect.record) && effect.remaining > 0.0 {
        effects.SetStatusEffectRemainingDuration(id, effect.record, effect.remaining);
      }
    }
    let player = GetPlayer(this.m_game);
    let watched = this.Watched(player);
    let p = 1u;
    let bit = 1;
    for v in watched {
      if IsDefined(v) && (event.hostileTo & bit) != 0 {
        GameObject.ChangeAttitudeToHostile(body, v);
      }
      if IsDefined(v) && p == event.target {
        GameObject.ChangeAttitudeToHostile(body, v);
        TargetTrackingExtension.InjectThreat(body, v);
      }
      p += 1u;
      bit *= 2;
    }
  }

  // A proxy we took over whose brain did not come back (its AI component
  // stays off): spawned again in place from its record, the old body deleted
  // in the same frame. One frame of swap instead of a frozen NPC.
  private func Respawn(known: ref<TandemNpcKnown>, old: ref<ScriptedPuppet>) -> Void {
    if !known.managed || !TDBID.IsValid(known.record) {
      TandemNative.Log(s"regions: npc \(known.npc) did not wake up (its own body: left as it is)");
      return;
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let health = pools.GetStatPoolValue(Cast<StatsObjectID>(known.id), gamedataStatPoolType.Health, true);
    let spec = new DynamicEntitySpec();
    spec.recordID = known.record;
    spec.appearanceName = known.appearance;
    spec.position = old.GetWorldPosition();
    spec.orientation = old.GetWorldOrientation();
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Adopted");
    TandemEntities.Delete(known.id);
    known.id = entities.CreateEntity(spec);
    known.pendingUntil = this.m_now + 3.0;
    known.health = health / 100.0;
    TandemNative.Log(s"regions: npc \(known.npc) did not wake up: spawned again in place");
  }

  // An NPC we ran is no longer ours without our giving it (our world was
  // rebuilt, or the session let go of it). Someone runs it now: its body, if
  // it is still here, becomes their proxy. Nobody does: it is ours alone
  // again, and is announced afresh if it is still around.
  private func Lost(npc: Uint32, owner: Uint32) -> Void {
    let known = this.FindNpc(npc);
    if !IsDefined(known) {
      return;
    }
    let body = GameInstance.FindEntityByID(this.m_game, known.id) as ScriptedPuppet;
    this.ForgetKnown(npc);
    this.m_anims.Forget(npc);
    if owner != 0u {
      this.m_scaling.Handed(npc);
    } else {
      this.m_scaling.Release(npc);
    }
    if owner != 0u && IsDefined(body) && !body.IsDead() {
      TandemProxies.Pacify(body);
      let proxy = new TandemNpcProxy();
      proxy.npc = npc;
      proxy.id = known.id;
      proxy.native = !known.managed;
      proxy.health = -1.0;
      proxy.poise = -1.0;
      proxy.record = body.GetRecordID();
      proxy.appearance = body.GetCurrentAppearanceName();
      proxy.seenAt = this.m_now - 2.0;
      // Still seated (a driver handed over with its vehicle): it stays in
      // its seat, which is the proxy's seat now.
      if EntityID.IsDefined(known.seatVehicle) && VehicleComponent.IsMountedToProvidedVehicle(this.m_game, known.id, GameInstance.FindEntityByID(this.m_game, known.seatVehicle) as VehicleObject) {
        proxy.seatedIn = known.seatVehicle;
        proxy.seatedSlot = known.seat;
        proxy.seatOwner = known.seatOwner;
        proxy.seatSlot = known.seat;
      }
      ArrayPush(this.m_proxies, proxy);
    }
    TandemNative.Log(s"regions: npc \(npc) is no longer ours (now player \(owner)'s)");
  }

  public func LedgerReport() -> String {
    return this.m_ledger.Report();
  }

  public func AnimsReport() -> String {
    return this.m_anims.Report();
  }

  // (test) Host: an NPC in a workspot near V to share (NpcAnims.Seek).
  public func SeekWorkspot(range: Float) -> Bool {
    return this.m_anims.Seek(range);
  }
}
