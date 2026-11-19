// Grapples, takedowns, finishers and carried bodies in a shared world
// (docs/architecture/world.md, "The second player acts on the world too").
//
// The host's NPCs are the host's: a player who grabs, takes down, finishes,
// carries, throws or disposes of one plays its own side at once (its V's
// animation, on its proxy: native feel) and asks the host (NpcAction). The
// host validates it (the NPC is still there and alive, nobody else holds it,
// the finisher's health condition still holds), answers the asker
// (NpcActionReply) and plays the NPC's side on the real one with that
// player's puppet (the same takedown game effect, the same mount on the
// shoulder), then tells everyone who holds it (NpcHold) so every other game
// puts its proxy in the same grapple or on the same shoulder. A refused grab
// ends on the guest the way vanilla's failed grapples do: the NPC breaks free
// (GrappleBreakFreeDecisions). Vanilla references: final.reds, see
// docs/research/engine-knowledge.md §13.17.

public class TandemHeldNpc {
  public let npc: Uint32;
  // The holder (a player id), how (NpcHoldKind: 1 grappled, 2 carried,
  // 3 takedown, 4 finisher) and the animation's name.
  public let player: Uint32;
  public let how: Int32;
  public let name: CName;
  public let since: Float;
  // Host: placed in front of the holder each tick (the workspot did not take).
  public let followed: Bool;
  public let mounted: Bool;
  // Host: the outcome still owed when the animation ends (3 kill, 4 knock
  // out, 6 finisher kill, 0 none) and when; then the hold ends.
  public let outcome: Int32;
  public let endAt: Float;
  public let checked: Bool;
}

public class TandemTakedowns extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_now: Float;
  private let m_held: array<ref<TandemHeldNpc>>;
  // Guest: grabs the host refused (the grapple breaks free), and proxies
  // whose controller comes back at a given time.
  private let m_refused: array<Uint32>;
  private let m_unholdAt: array<Float>;
  private let m_unholdNpc: array<Uint32>;
  // Guest: deaths the host announced while our takedown still plays.
  private let m_deferredNpc: array<Uint32>;
  private let m_deferredKiller: array<Uint32>;
  private let m_deferredAt: array<Float>;
  private let m_flushing: Bool;
  // Host: disposed bodies announced as gone for good once in the container.
  private let m_disposedNpc: array<Uint32>;
  private let m_disposedBy: array<Uint32>;
  private let m_disposedAt: array<Float>;
  // Host: NPC reactions that vanilla gives "the player" run for another V
  // (Reactions.reds): our V's second chance and gag are off meanwhile.
  private let m_suppressAvoid: Bool;

  public static func Get(game: GameInstance) -> ref<TandemTakedowns> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemTakedowns") as TandemTakedowns;
  }

  // ---- shared helpers ----

  private func System() -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"TandemSystem") as TandemSystem;
  }

  private func Npcs() -> ref<TandemNpcs> {
    let system = this.System();
    return IsDefined(system) ? system.NpcsSync() : null;
  }

  // The session id of a shared NPC (host: ours; guest: a proxy, dead or not).
  public func NpcOf(target: wref<GameObject>) -> Uint32 {
    let npcs = this.Npcs();
    if !IsDefined(target) || !IsDefined(npcs) || this.m_you == 0u {
      return 0u;
    }
    return npcs.NpcOf(target.GetEntityID());
  }

  public static func ProxyEntity(game: GameInstance, npc: Uint32) -> EntityID {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    let npcs = IsDefined(system) ? system.NpcsSync() : null;
    let none = EntityID.FromHash(0ul);
    return IsDefined(npcs) ? npcs.EntityOf(npc) : none;
  }

  private func BodyOf(npc: Uint32) -> ref<NPCPuppet> {
    let npcs = this.Npcs();
    if !IsDefined(npcs) {
      return null;
    }
    return GameInstance.FindEntityByID(this.GetGameInstance(), npcs.EntityOf(npc)) as NPCPuppet;
  }

  private func PuppetOf(player: Uint32) -> ref<GameObject> {
    let system = this.System();
    return IsDefined(system) ? system.PuppetOf(player) : null;
  }

  private func Find(npc: Uint32) -> ref<TandemHeldNpc> {
    for held in this.m_held {
      if held.npc == npc {
        return held;
      }
    }
    return null;
  }

  private func Forget(npc: Uint32) -> Void {
    let kept: array<ref<TandemHeldNpc>>;
    for held in this.m_held {
      if held.npc != npc {
        ArrayPush(kept, held);
      }
    }
    this.m_held = kept;
  }

  private func Hold(npc: Uint32, player: Uint32, how: Int32, name: CName) -> ref<TandemHeldNpc> {
    let held = this.Find(npc);
    if !IsDefined(held) {
      held = new TandemHeldNpc();
      held.npc = npc;
      ArrayPush(this.m_held, held);
    }
    held.player = player;
    held.how = how;
    held.name = name;
    held.since = this.m_now;
    held.outcome = 0;
    held.endAt = 0.0;
    held.checked = false;
    return held;
  }

  public static func KindName(kind: Int32) -> String {
    switch kind {
      case 1: return "grab";
      case 2: return "release";
      case 3: return "takedown";
      case 4: return "knockout";
      case 5: return "animate";
      case 6: return "finisher";
      case 7: return "pick up";
      case 8: return "drop";
      case 9: return "throw";
      case 10: return "dispose";
    }
    return s"action \(kind)";
  }

  // Our V's side of a takedown on a shared NPC, seen by the wraps below. A
  // guest asks the host; the host, which owns the NPC, tells everyone.
  public func OurAction(actor: wref<GameObject>, target: wref<GameObject>, kind: Int32, name: CName, record: TweakDBID, entity: EntityID, amount: Float, direction: Vector4) -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(actor) || !IsDefined(player) || actor != player {
      return;
    }
    let npc = this.NpcOf(target);
    if npc == 0u {
      return;
    }
    if this.Owns(npc) {
      this.HostOwnAction(npc, kind, name);
      return;
    }
    TandemNative.SendNpcAction(npc, kind, name, record, entity, amount, direction);
    TandemNative.Log(s"takedowns: our \(TandemTakedowns.KindName(kind)) on \(npc) sent to the host");
    let puppet = TandemNative.NpcPuppetId(npc);
    if kind == 1 || kind == 3 || kind == 4 || kind == 5 || kind == 6 || kind == 7 || kind == 9 || kind == 10 {
      // Our V places the proxy now (the grapple, the shoulder, the throw).
      TandemNative.HoldPuppet(puppet, true);
      this.Hold(npc, this.m_you, kind == 7 ? 2 : (kind == 1 ? 1 : (kind == 6 ? 4 : 3)), name);
    } else {
      // Let go: the host's position takes over again once it has landed.
      this.Forget(npc);
      this.UnholdLater(npc, 1.5);
    }
  }

  private func UnholdLater(npc: Uint32, delay: Float) -> Void {
    ArrayPush(this.m_unholdNpc, npc);
    ArrayPush(this.m_unholdAt, this.m_now + delay);
  }

  // Guest: the host refused this grab; the grapple's break-free decision
  // asks (GrappleBreakFreeDecisions below).
  public func Refused(target: wref<GameObject>) -> Bool {
    let npc = this.NpcOf(target);
    return npc != 0u && ArrayContains(this.m_refused, npc);
  }

  public func ClearRefused(target: wref<GameObject>) -> Bool {
    let npc = this.NpcOf(target);
    if npc != 0u && ArrayContains(this.m_refused, npc) {
      ArrayRemove(this.m_refused, npc);
      return true;
    }
    return false;
  }

  // Someone holds or animates this NPC now (Outcomes.reds: a knock-out
  // then leaves laying it down to the takedown).
  public func Holds(npc: Uint32) -> Bool {
    return IsDefined(this.Find(npc));
  }

  public func IsOurs(target: wref<GameObject>, how: Int32) -> Bool {
    let held = this.Find(this.NpcOf(target));
    return IsDefined(held) && held.player == this.m_you && held.how == how;
  }

  // Guest: the host's NPC died while our own takedown of its proxy still
  // plays: the proxy dies the game's way once the animation is over
  // (TandemNpcs.Die asks).
  public func DefersDeath(npc: Uint32, killer: Uint32) -> Bool {
    if this.m_flushing || this.Owns(npc) {
      return false;
    }
    let held = this.Find(npc);
    if !IsDefined(held) || held.player != this.m_you || this.m_now - held.since > 6.0 {
      return false;
    }
    ArrayPush(this.m_deferredNpc, npc);
    ArrayPush(this.m_deferredKiller, killer);
    ArrayPush(this.m_deferredAt, held.since + 6.0);
    return true;
  }

  public func SuppressAvoid(on: Bool) -> Void {
    this.m_suppressAvoid = on;
  }

  public func AvoidSuppressed() -> Bool {
    return this.m_suppressAvoid;
  }

  // ---- every frame ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    if you == 0u {
      if this.m_you != 0u {
        this.EndSession();
      }
      return;
    }
    this.m_you = you;
    for event in TandemNative.TakeNpcActions() {
      if event.kind == 1 {
        this.OnRequest(event);
      } else if event.kind == 2 {
        this.OnReply(event);
      } else if event.kind == 3 {
        this.OnHold(event);
      } else if event.kind == 4 {
        TandemReactions.OnCue(this.GetGameInstance(), event.npc, event.action);
      }
    }
    // The NPCs we run, then the proxies we show (each skips the other's).
    this.HostTick();
    this.GuestTick();
  }

  // We run this shared NPC (regions): its owner's part is ours.
  private func Owns(npc: Uint32) -> Bool {
    let npcs = this.Npcs();
    return IsDefined(npcs) && npcs.Owns(npc);
  }

  private func EndSession() -> Void {
    for held in this.m_held {
      TandemNative.HoldPuppet(TandemNative.NpcPuppetId(held.npc), false);
    }
    for npc in this.m_unholdNpc {
      TandemNative.HoldPuppet(TandemNative.NpcPuppetId(npc), false);
    }
    ArrayClear(this.m_held);
    ArrayClear(this.m_refused);
    ArrayClear(this.m_unholdNpc);
    ArrayClear(this.m_unholdAt);
    ArrayClear(this.m_deferredNpc);
    ArrayClear(this.m_deferredKiller);
    ArrayClear(this.m_deferredAt);
    ArrayClear(this.m_disposedNpc);
    ArrayClear(this.m_disposedBy);
    ArrayClear(this.m_disposedAt);
    this.m_suppressAvoid = false;
    this.m_you = 0u;
  }

  private func GuestTick() -> Void {
    let npcs: array<Uint32>;
    let at: array<Float>;
    let i = 0;
    while i < ArraySize(this.m_unholdNpc) {
      if this.m_now >= this.m_unholdAt[i] {
        if !IsDefined(this.Find(this.m_unholdNpc[i])) {
          TandemNative.HoldPuppet(TandemNative.NpcPuppetId(this.m_unholdNpc[i]), false);
        }
      } else {
        ArrayPush(npcs, this.m_unholdNpc[i]);
        ArrayPush(at, this.m_unholdAt[i]);
      }
      i += 1;
    }
    this.m_unholdNpc = npcs;
    this.m_unholdAt = at;
    // Our own takedowns and finishers end on their own: the host's NpcHold
    // and death follow.
    let kept: array<ref<TandemHeldNpc>>;
    for held in this.m_held {
      if this.Owns(held.npc) {
        ArrayPush(kept, held); // ours: HostTick's
      } else if !EntityID.IsDefined(TandemTakedowns.ProxyEntity(this.GetGameInstance(), held.npc)) {
        // Its proxy is gone (disposed of, out of range).
        TandemNative.HoldPuppet(TandemNative.NpcPuppetId(held.npc), false);
      } else if held.player == this.m_you && (held.how == 3 || held.how == 4) && this.m_now - held.since > 6.0 {
        this.UnholdLater(held.npc, 0.0);
      } else {
        ArrayPush(kept, held);
      }
    }
    this.m_held = kept;
    // Deaths that waited for our animation.
    let dn: array<Uint32>;
    let dk: array<Uint32>;
    let da: array<Float>;
    i = 0;
    while i < ArraySize(this.m_deferredNpc) {
      if this.m_now >= this.m_deferredAt[i] {
        let tracked = this.Npcs();
        if IsDefined(tracked) {
          this.m_flushing = true;
          tracked.Die(this.m_deferredNpc[i], this.m_deferredKiller[i]);
          this.m_flushing = false;
        }
      } else {
        ArrayPush(dn, this.m_deferredNpc[i]);
        ArrayPush(dk, this.m_deferredKiller[i]);
        ArrayPush(da, this.m_deferredAt[i]);
      }
      i += 1;
    }
    this.m_deferredNpc = dn;
    this.m_deferredKiller = dk;
    this.m_deferredAt = da;
  }

  // ---- host ----

  private func HostTick() -> Void {
    let kept: array<ref<TandemHeldNpc>>;
    for held in this.m_held {
      // A proxy's hold is its owner's to end (GuestTick).
      if !this.Owns(held.npc) || this.HostKeep(held) {
        ArrayPush(kept, held);
      }
    }
    this.m_held = kept;
    let dn: array<Uint32>;
    let db: array<Uint32>;
    let da: array<Float>;
    let i = 0;
    while i < ArraySize(this.m_disposedNpc) {
      if this.m_now >= this.m_disposedAt[i] {
        TandemNative.SendNpcGone(this.m_disposedNpc[i], 2, this.m_disposedBy[i]);
        TandemNative.Log(s"takedowns: \(this.m_disposedNpc[i]) disposed of: gone for everyone");
      } else {
        ArrayPush(dn, this.m_disposedNpc[i]);
        ArrayPush(db, this.m_disposedBy[i]);
        ArrayPush(da, this.m_disposedAt[i]);
      }
      i += 1;
    }
    this.m_disposedNpc = dn;
    this.m_disposedBy = db;
    this.m_disposedAt = da;
  }

  // One held NPC on the host: still held? Follows its holder, gets the
  // outcome its animation owes, is let go when its holder is gone.
  private func HostKeep(held: ref<TandemHeldNpc>) -> Bool {
    let body = this.BodyOf(held.npc);
    if !IsDefined(body) {
      return false;
    }
    if held.player == this.m_you {
      // Our own V: vanilla runs it; the hold ends with the animation.
      if held.endAt > 0.0 && this.m_now >= held.endAt {
        this.Announce(held.npc, 0u, 0, n"None");
        return false;
      }
      return true;
    }
    let puppet = this.PuppetOf(held.player);
    if !IsDefined(puppet) {
      // The holder left (or crashed): the NPC is free again.
      this.Free(body, held, true);
      this.Announce(held.npc, 0u, 0, n"None");
      TandemNative.Log(s"takedowns: \(held.npc) let go: player \(held.player) is gone");
      return false;
    }
    if held.how == 1 && !held.checked && this.m_now - held.since > 0.5 {
      held.checked = true;
      if !ScriptedPuppet.IsBeingGrappled(body) {
        // The grapple effect did not take with a puppet: keep the NPC still
        // and in its arms ourselves.
        StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.Grappled", puppet.GetEntityID());
        held.followed = true;
        TandemNative.Log(s"takedowns: \(held.npc) grappled by player \(held.player) (fallback: held in front of the puppet)");
      } else {
        TandemNative.Log(s"takedowns: \(held.npc) grappled by player \(held.player) (workspot)");
      }
    }
    if held.followed && held.how == 1 {
      let forward = puppet.GetWorldForward();
      let at = puppet.GetWorldPosition();
      at.X += forward.X * 0.6;
      at.Y += forward.Y * 0.6;
      GameInstance.GetTeleportationFacility(this.GetGameInstance()).Teleport(body, at, TandemYawAngles(puppet.GetWorldYaw()));
    }
    if held.how >= 3 {
      // Its death, by the effect or the outcome, is this player's kill.
      this.NoteActor(held.npc, held.player);
    }
    if held.endAt > 0.0 && this.m_now >= held.endAt {
      this.Outcome(body, held, puppet);
      this.Free(body, held, false);
      this.Announce(held.npc, 0u, 0, n"None");
      return false;
    }
    return true;
  }

  // What a takedown or finisher owes once its animation is over, if the
  // game's own effect did not do it (it may not, with a puppet): the kill or
  // the knock-out, credited to the player who did it.
  private func Outcome(body: ref<NPCPuppet>, held: ref<TandemHeldNpc>, puppet: ref<GameObject>) -> Void {
    if body.IsDead() {
      return;
    }
    if held.outcome == 3 || held.outcome == 6 {
      body.Kill(puppet, false, false);
      TandemNative.Log(s"takedowns: \(held.npc) killed by player \(held.player) (outcome)");
    } else if held.outcome == 4 && !ScriptedPuppet.IsDefeated(body) && !ScriptedPuppet.IsUnconscious(body) {
      StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.Unconscious", puppet.GetEntityID());
      TandemNative.Log(s"takedowns: \(held.npc) knocked out by player \(held.player) (outcome)");
    }
  }

  // The player whose takedown, finisher or throw this NPC dies of: its
  // killer when the game names none (a puppet as the takedown's activator
  // is not recorded as the killer: "killer: player 0" in game).
  private func NoteActor(npc: Uint32, player: Uint32) -> Void {
    let npcs = this.Npcs();
    if IsDefined(npcs) {
      npcs.NoteActor(npc, player);
    }
  }

  private func Announce(npc: Uint32, player: Uint32, how: Int32, name: CName) -> Void {
    TandemNative.SendNpcHold(npc, player, how, name);
  }

  // Our own V acting on our own NPC: vanilla does all of it; the others
  // only need to know who holds it.
  private func HostOwnAction(npc: Uint32, kind: Int32, name: CName) -> Void {
    if kind == 1 {
      this.Hold(npc, this.m_you, 1, name);
      this.Announce(npc, this.m_you, 1, name);
    } else if kind == 3 || kind == 4 || kind == 5 || kind == 6 {
      let how = kind == 6 ? 4 : 3;
      let held = this.Hold(npc, this.m_you, how, name);
      held.endAt = this.m_now + (kind == 6 ? 5.0 : 4.0);
      this.Announce(npc, this.m_you, how, name);
    } else if kind == 7 {
      this.Hold(npc, this.m_you, 2, name);
      this.Announce(npc, this.m_you, 2, name);
    } else {
      this.Forget(npc);
      this.Announce(npc, 0u, 0, n"None");
      if kind == 10 {
        this.DisposedLater(npc, this.m_you);
      }
    }
    TandemNative.Log(s"takedowns: our \(TandemTakedowns.KindName(kind)) on \(npc) shown to the guests");
  }

  private func DisposedLater(npc: Uint32, player: Uint32) -> Void {
    ArrayPush(this.m_disposedNpc, npc);
    ArrayPush(this.m_disposedBy, player);
    ArrayPush(this.m_disposedAt, this.m_now + 3.0);
  }

  // A guest's request: is it still possible here, where the NPC lives?
  private func OnRequest(event: TandemNpcAction) -> Void {
    let body = this.BodyOf(event.npc);
    let puppet = this.PuppetOf(event.player);
    let held = this.Find(event.npc);
    let kind = event.action;
    let reason = 0;
    if !IsDefined(body) || !IsDefined(puppet) {
      reason = 1;
    } else if IsDefined(held) && held.player != event.player && kind != 2 {
      reason = 2;
    } else if kind == 1 && (body.IsDead() || ScriptedPuppet.IsDefeated(body) || ScriptedPuppet.IsUnconscious(body)) {
      reason = 1;
    } else if (kind == 3 || kind == 4 || kind == 5 || kind == 6) && body.IsDead() {
      reason = 1;
    } else if (kind == 1 || kind == 7) && Vector4.Distance(body.GetWorldPosition(), puppet.GetWorldPosition()) > 6.0 {
      reason = 5;
    } else if kind == 6 && !this.FinisherHolds(body, event.amount) {
      reason = 3;
    } else if kind == 7 && !body.IsDead() && !ScriptedPuppet.IsDefeated(body) && !ScriptedPuppet.IsUnconscious(body) && !IsDefined(held) {
      // Only a body (or one we are holding) is carried.
      reason = 3;
    } else if (kind == 8 || kind == 9) && (!IsDefined(held) || held.player != event.player) {
      reason = 4;
    }
    TandemNative.SendNpcActionReply(event.player, event.npc, kind, reason == 0, reason);
    let answer = "accepted";
    if reason != 0 {
      answer = s"refused (reason \(reason))";
    }
    TandemNative.Log(s"takedowns: player \(event.player) asks \(TandemTakedowns.KindName(kind)) on \(event.npc): \(answer)");
    if reason == 0 {
      // Its death or knock-out is this player's (TandemNpcs.KillerOf).
      let npcs = this.Npcs();
      if IsDefined(npcs) {
        npcs.NoteHitBy(event.npc, event.player);
      }
      this.Apply(body, puppet, event);
    }
  }

  // The finisher's condition still holds here: its health is no higher than
  // the guest saw (1 % steps and a round trip behind, so a margin), or its
  // guard is broken.
  private func FinisherHolds(body: ref<NPCPuppet>, seen: Float) -> Bool {
    if StatusEffectSystem.ObjectHasStatusEffect(body, t"BaseStatusEffect.FinisherActiveStatusEffect") {
      return true;
    }
    let health = GameInstance.GetStatPoolsSystem(this.GetGameInstance()).GetStatPoolValue(Cast<StatsObjectID>(body.GetEntityID()), gamedataStatPoolType.Health, true) / 100.0;
    return health <= seen + 0.05;
  }

  // The NPC's side, played on the real one with the guest's puppet: what
  // vanilla does with its own V (final.reds: TakedownGrapplePreyEvents,
  // SelectSyncedAnimationAndExecuteAction, FinisherAttackEvents,
  // CarriedObjectEvents, ThrowEvents, DisposalDevice).
  private func Apply(body: ref<NPCPuppet>, puppet: ref<GameObject>, event: TandemNpcAction) -> Void {
    let kind = event.action;
    let game = this.GetGameInstance();
    if kind == 1 {
      this.Hold(event.npc, event.player, 1, event.name);
      TakedownGameEffectHelper.FillTakedownData(puppet, puppet, body, n"takedowns", event.name, "");
      this.Announce(event.npc, event.player, 1, event.name);
    } else if kind == 3 || kind == 4 || kind == 5 {
      let held = this.Hold(event.npc, event.player, 3, event.name);
      held.outcome = kind == 5 ? 0 : kind;
      held.endAt = this.m_now + 4.0;
      this.NoteActor(event.npc, event.player);
      if kind == 4 {
        // Knocked out the way vanilla ends a grapple with V down
        // (TakedownUnmountPreyEvents): the status itself. The setUnconscious
        // effect run with a puppet (plus takedown_defeat) was followed by a
        // stopped game thread in the first in-game run (2026-11-18).
        StatusEffectHelper.RemoveStatusEffect(body, t"BaseStatusEffect.Grappled");
        StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.Unconscious", puppet.GetEntityID());
      } else {
        if kind == 3 {
          body.SetMyKiller(puppet);
        }
        // The kill effect takes with a puppet (it killed the NPC in game).
        TakedownGameEffectHelper.FillTakedownData(puppet, puppet, body, n"takedowns", event.name, "");
      }
      this.Announce(event.npc, event.player, 3, event.name);
      TandemNative.Log(s"takedowns: \(event.npc) taken down by player \(event.player) (\(NameToString(event.name)))");
    } else if kind == 6 {
      let held = this.Hold(event.npc, event.player, 4, event.name);
      held.outcome = 6;
      held.endAt = this.m_now + 5.0;
      this.NoteActor(event.npc, event.player);
      if Equals(event.name, n"tandem_blunt_hold") {
        // The blunt "hold" finisher throws and kills at once (final.reds
        // TriggerNewPerkFinisherBluntHold).
        StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.ThrownNPC");
        body.Kill(puppet, true, true);
      } else {
        TandemTakedowns.PlayFinisher(game, puppet, body, event.name);
        body.MarkForDeath();
      }
      this.Announce(event.npc, event.player, 4, event.name);
      TandemNative.Log(s"takedowns: \(event.npc) finished by player \(event.player)");
    } else if kind == 2 {
      let held = this.Find(event.npc);
      if IsDefined(held) && held.player == event.player {
        this.Free(body, held, true);
        this.Forget(event.npc);
        this.Announce(event.npc, 0u, 0, n"None");
        TandemNative.Log(s"takedowns: \(event.npc) broke free of player \(event.player)");
      }
    } else if kind == 7 {
      let held = this.Find(event.npc);
      if IsDefined(held) {
        this.Free(body, held, false);
      }
      held = this.Hold(event.npc, event.player, 2, event.name);
      TandemTakedowns.Mount(game, body, puppet);
      held.mounted = true;
      this.Announce(event.npc, event.player, 2, event.name);
      TandemNative.Log(s"takedowns: \(event.npc) carried by player \(event.player)");
    } else if kind == 8 || kind == 9 {
      let held = this.Find(event.npc);
      if IsDefined(held) {
        this.Free(body, held, kind == 8);
      }
      this.Forget(event.npc);
      if kind == 9 {
        this.NoteActor(event.npc, event.player);
        TandemTakedowns.Throw(game, body, puppet, new Vector4(event.dx, event.dy, event.dz, 0.0));
      }
      this.Announce(event.npc, 0u, 0, n"None");
      TandemNative.Log(s"takedowns: \(event.npc) \(kind == 9 ? "thrown" : "dropped") by player \(event.player)");
    } else if kind == 10 {
      let held = this.Find(event.npc);
      if IsDefined(held) {
        this.Free(body, held, false);
      }
      this.Forget(event.npc);
      this.NoteActor(event.npc, event.player);
      TandemTakedowns.Dispose(body, puppet);
      this.Announce(event.npc, 0u, 0, n"None");
      this.DisposedLater(event.npc, event.player);
      TandemNative.Log(s"takedowns: \(event.npc) disposed of by player \(event.player)");
    }
  }

  // Out of whatever held it: the grapple's status and workspot, the
  // carrier's shoulder. `alive`: it fights back (vanilla's break free).
  private func Free(body: ref<NPCPuppet>, held: ref<TandemHeldNpc>, alive: Bool) -> Void {
    let game = this.GetGameInstance();
    if held.mounted {
      TandemTakedowns.Unmount(game, body);
      held.mounted = false;
      if alive || body.IsDead() || ScriptedPuppet.IsUnconscious(body) {
        body.QueueEvent(CreateForceRagdollEvent(n"tandem_drop"));
      }
    }
    if held.how == 1 {
      StatusEffectHelper.RemoveStatusEffect(body, t"BaseStatusEffect.Grappled");
      if alive && !body.IsDead() {
        AIActionHelper.ClearWorkspotCommand(body, true);
        GameInstance.GetWorkspotSystem(game).SendFastExitSignal(body);
        let puppet = this.PuppetOf(held.player);
        if IsDefined(puppet) {
          TargetTrackingExtension.InjectThreat(body, puppet);
        }
      }
    }
    held.followed = false;
  }

  // ---- what both roles play on an NPC with a V that is not ours ----

  public static func PlayFinisher(game: GameInstance, instigator: ref<GameObject>, target: ref<GameObject>, name: CName) -> Bool {
    let effect = GameInstance.GetGameEffectSystem(game).CreateEffectStatic(n"playFinisher", name, instigator);
    if !IsDefined(effect) {
      return false;
    }
    EffectData.SetVector(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.position, target.GetWorldPosition());
    EffectData.SetEntity(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.entity, target);
    effect.Run();
    return true;
  }

  // On the carrier's left shoulder, as CarriedObjectEvents.OnEnter mounts
  // a body on V, with the carried pose.
  public static func Mount(game: GameInstance, body: ref<NPCPuppet>, carrier: ref<GameObject>) -> Void {
    let info: MountingInfo;
    info.childId = body.GetEntityID();
    info.parentId = carrier.GetEntityID();
    info.slotId.id = n"leftShoulder";
    let request = new MountingRequest();
    request.lowLevelMountingInfo = info;
    GameInstance.GetMountingFacility(game).Mount(request);
    body.MountingStartDisableComponents();
    let mounting = new AnimFeature_Mounting();
    mounting.mountingState = 2;
    AnimationControllerComponent.ApplyFeature(body, n"Mounting", mounting);
    let carry = new AnimFeature_Carry();
    carry.state = 2;
    carry.useBothHands = true;
    carry.isCarryActive = true;
    AnimationControllerComponent.ApplyFeature(body, n"Carry", carry);
  }

  public static func Unmount(game: GameInstance, body: ref<NPCPuppet>) -> Void {
    let info: MountingInfo;
    info.childId = body.GetEntityID();
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = info;
    GameInstance.GetMountingFacility(game).Unmount(request);
    body.MountingEndEnableComponents();
    let carry = new AnimFeature_Carry();
    AnimationControllerComponent.ApplyFeature(body, n"Carry", carry);
    let mounting = new AnimFeature_Mounting();
    AnimationControllerComponent.ApplyFeature(body, n"Mounting", mounting);
  }

  // ThrowEvents: the throw kills, ragdolls and pushes the body.
  public static func Throw(game: GameInstance, body: ref<NPCPuppet>, thrower: ref<GameObject>, impulse: Vector4) -> Void {
    if !body.IsDead() {
      body.Kill(thrower, true, true);
      StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.ForceKillFromRagdoll");
    }
    StatusEffectHelper.ApplyStatusEffect(body, t"BaseStatusEffect.ThrownNPC");
    body.SetDisableRagdoll(false, true);
    body.QueueEvent(CreateForceRagdollNoPowerPoseEvent(n"tandem_throw"));
    GameInstance.GetDelaySystem(game).DelayEvent(body, CreateRagdollApplyImpulseEvent(body.GetWorldPosition(), impulse, 10000.0), 0.1);
  }

  // DisposalDevice.OnDisposeBody / OnWorkspotFinished: dead, marked
  // disposed, unseen, hidden for good. (The device's own state, its door and
  // light, stays the guest's: TODO with device sync.)
  public static func Dispose(body: ref<NPCPuppet>, by: ref<GameObject>) -> Void {
    if !body.IsDead() {
      body.Kill(by, true, true);
    }
    NPCPuppet.SetNPCDisposedFact(body);
    body.GetPuppetStateBlackboard().SetBool(GetAllBlackboardDefs().PuppetState.IsBodyDisposed, true, true);
    let visible = body.GetVisibleObjectComponent();
    if IsDefined(visible) {
      visible.SetVisibleObjectTypeInvalid();
    }
    body.HideIrreversibly();
  }

  // ---- guest ----

  private func OnReply(event: TandemNpcAction) -> Void {
    let kind = event.action;
    if event.accepted {
      TandemNative.Log(s"takedowns: host accepted our \(TandemTakedowns.KindName(kind)) on \(event.npc)");
      return;
    }
    TandemNative.Log(s"takedowns: host refused our \(TandemTakedowns.KindName(kind)) on \(event.npc) (reason \(event.reason))");
    let proxy = this.BodyOf(event.npc);
    if kind == 1 {
      // The grapple breaks free, as vanilla's failed grapples do.
      if !ArrayContains(this.m_refused, event.npc) {
        ArrayPush(this.m_refused, event.npc);
      }
    } else if kind == 7 {
      // Someone else has it: put it down (vanilla's forced drop).
      let drop = new PSMPostponedParameterBool();
      drop.id = n"forceDropBody";
      drop.value = true;
      GetPlayer(this.GetGameInstance()).QueueEvent(drop);
    } else if (kind == 3 || kind == 4 || kind == 5 || kind == 6) && IsDefined(proxy) && !proxy.IsDead() {
      GameInstance.GetWorkspotSystem(this.GetGameInstance()).SendFastExitSignal(proxy);
    }
    let held = this.Find(event.npc);
    if IsDefined(held) && held.player == this.m_you {
      this.Forget(event.npc);
    }
    this.UnholdLater(event.npc, 2.0);
  }

  // The host says who holds one of its NPCs: our proxy of it goes with that
  // V (a puppet here) the way the real one does.
  private func OnHold(event: TandemNpcAction) -> Void {
    if event.player == this.m_you {
      return; // ours: our V already plays it
    }
    if this.Owns(event.npc) {
      return; // an NPC we run: HostKeep frees it when its holder is gone
    }
    let game = this.GetGameInstance();
    let proxy = this.BodyOf(event.npc);
    let held = this.Find(event.npc);
    if event.player == 0u {
      if IsDefined(held) && IsDefined(proxy) {
        if held.mounted {
          TandemTakedowns.Unmount(game, proxy);
          proxy.QueueEvent(CreateForceRagdollEvent(n"tandem_drop"));
        } else if held.how == 1 && !proxy.IsDead() {
          GameInstance.GetWorkspotSystem(game).SendFastExitSignal(proxy);
        }
        TandemTakedowns.Interactions(proxy, true);
      }
      if IsDefined(held) && held.player != this.m_you {
        this.Forget(event.npc);
        this.UnholdLater(event.npc, 1.0);
      }
      TandemNative.Log(s"takedowns: \(event.npc) released");
      return;
    }
    let puppet = this.PuppetOf(event.player);
    held = this.Hold(event.npc, event.player, event.action, event.name);
    if !IsDefined(proxy) || !IsDefined(puppet) {
      TandemNative.Log(s"takedowns: \(event.npc) held by player \(event.player) (no proxy or puppet here yet)");
      return;
    }
    TandemNative.HoldPuppet(TandemNative.NpcPuppetId(event.npc), true);
    TandemTakedowns.Interactions(proxy, false);
    if event.action == 2 {
      TandemTakedowns.Mount(game, proxy, puppet);
      held.mounted = true;
    } else if event.action == 4 {
      TandemTakedowns.PlayFinisher(game, puppet, proxy, event.name);
    } else if IsNameValid(event.name) {
      TakedownGameEffectHelper.FillTakedownData(puppet, puppet, proxy, n"takedowns", event.name, "");
    }
    TandemNative.Log(s"takedowns: \(event.npc) held by player \(event.player) (how \(event.action))");
  }

  // Nobody else grabs, takes down or finishes what another player holds.
  public static func Interactions(proxy: ref<ScriptedPuppet>, on: Bool) -> Void {
    proxy.EnableInteraction(n"Grapple", on);
    proxy.EnableInteraction(n"TakedownLayer", on);
    proxy.EnableInteraction(n"AerialTakedown", on);
    proxy.EnableInteraction(n"NewPerkFinisherLayer", on);
    proxy.EnableInteraction(n"BossTakedownLayer", on);
    proxy.EnableInteraction(n"MassiveTargetTakedownLayer", on);
  }

  // (test) tools/game.sh cmd takedown grab|kill|knockout|release|finisher|
  // pickup|drop|throw|list: our V's request on the nearest proxy, without
  // the animation (guest), or the list of holds.
  public func Command(command: String) -> Void {
    if Equals(command, "takedown list") {
      TandemNative.Log(s"takedowns: \(ArraySize(this.m_held)) held");
      for held in this.m_held {
        TandemNative.Log(s"takedowns: \(held.npc) by player \(held.player), how \(held.how)");
      }
      return;
    }
    let player = GetPlayer(this.GetGameInstance());
    let npcs = this.Npcs();
    let target = IsDefined(npcs) ? npcs.NearestProxy(player.GetWorldPosition()) : null;
    if !IsDefined(target) {
      TandemNative.Log("takedown: no proxy");
      return;
    }
    let what = StrAfterFirst(command, "takedown ");
    let kind = 0;
    let name = n"None";
    if Equals(what, "grab") {
      kind = 1;
      name = n"GrappleTarget";
    } else if Equals(what, "release") {
      kind = 2;
      name = n"BreakFree";
    } else if Equals(what, "kill") {
      kind = 3;
      name = n"kill";
    } else if Equals(what, "knockout") {
      kind = 4;
      name = n"setUnconscious";
    } else if Equals(what, "finisher") {
      kind = 6;
      name = n"finisher_default";
    } else if Equals(what, "pickup") {
      kind = 7;
    } else if Equals(what, "drop") {
      kind = 8;
    } else if Equals(what, "throw") {
      kind = 9;
    }
    if kind == 0 {
      TandemNative.Log(s"takedown: unknown \(what)");
      return;
    }
    let none = TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    let forward = player.GetWorldForward();
    let health = GameInstance.GetStatPoolsSystem(this.GetGameInstance()).GetStatPoolValue(Cast<StatsObjectID>(target.GetEntityID()), gamedataStatPoolType.Health, true) / 100.0;
    this.OurAction(player, target, kind, name, none, noEntity, health, new Vector4(forward.X * 100.0, forward.Y * 100.0, 20.0, 0.0));
  }
}

// ---- the vanilla paths that move, animate or kill an NPC (our V's side) ----

// Every takedown runs its NPC side through this game effect (grapple start,
// kill, knock-out, aerial, boss, massive target): final.reds 252966, called
// from TakedownGrapplePreyEvents.OnEnter (535235) and
// LocomotionTakedownEvents.SelectSyncedAnimationAndExecuteAction (285240).
@wrapMethod(TakedownGameEffectHelper)
public final static func FillTakedownData(executionOwner: wref<GameObject>, activator: wref<GameObject>, target: wref<GameObject>, effectName: CName, effectTag: CName, opt statusEffect: String) -> Bool {
  let result = wrappedMethod(executionOwner, activator, target, effectName, effectTag, statusEffect);
  if IsDefined(activator) && activator.IsPlayer() && IsNameValid(effectTag) && Equals(effectName, n"takedowns") {
    let tag = NameToString(effectTag);
    let kind = 5;
    if Equals(effectTag, n"GrappleTarget") || Equals(effectTag, n"GrappleTargetCover") || Equals(effectTag, n"GrappleTargetPlayerCrouch") {
      kind = 1;
    } else if Equals(effectTag, n"kill") {
      kind = 3;
    } else if StrBeginsWith(tag, "setUnconscious") {
      kind = 4;
    } else if Equals(effectTag, n"GrappleFailed") || Equals(effectTag, n"BreakFree") {
      kind = 0; // the break free sends the release
    }
    if kind != 0 {
      let none = TDBID.None();
      let noEntity = EntityID.FromHash(0ul);
      TandemTakedowns.Get(activator.GetGame()).OurAction(activator, target, kind, effectTag, none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
    }
  }
  return result;
}

// A grab the host refused breaks free at once: vanilla's own end for a
// failed grapple (final.reds 537741).
@wrapMethod(GrappleBreakFreeDecisions)
protected const final func EnterCondition(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if TandemTakedowns.Get(scriptInterface.GetGame()).Refused(this.stateMachineInitData.target) {
    return true;
  }
  return wrappedMethod(stateContext, scriptInterface);
}

// The grapple ended with the NPC alive (it broke free, final.reds 537827).
@wrapMethod(GrappleBreakFreeEvents)
public func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let takedowns = TandemTakedowns.Get(scriptInterface.GetGame());
  if !takedowns.ClearRefused(this.stateMachineInitData.target) {
    let none = TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    takedowns.OurAction(scriptInterface.executionOwner, this.stateMachineInitData.target, 2, n"BreakFree", none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
}

// Let go of a grabbed NPC without a takedown (final.reds 538977).
@wrapMethod(TakedownReleasePreyEvents)
public func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let takedowns = TandemTakedowns.Get(scriptInterface.GetGame());
  if takedowns.IsOurs(this.stateMachineInitData.target, 1) {
    let none = TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    takedowns.OurAction(scriptInterface.executionOwner, this.stateMachineInitData.target, 2, n"ReleasePrey", none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
}

// Finishers play the "playFinisher" game effect on the target
// (final.reds 250132); the kill itself reaches the host as a lethal hit.
@wrapMethod(FinisherAttackEvents)
public final func PlayFinisherGameEffect(const targetPuppet: ref<GameObject>, instigator: ref<GameObject>, const hasFromFront: Bool, const hasFromBack: Bool) -> Bool {
  let played = wrappedMethod(targetPuppet, instigator, hasFromFront, hasFromBack);
  if played && IsDefined(instigator) && instigator.IsPlayer() {
    let finisherName = n"None";
    this.GetFinisherNameBasedOnWeapon(targetPuppet, instigator, hasFromFront, hasFromBack, finisherName);
    let weapon = GameObject.GetActiveWeapon(instigator);
    let record = IsDefined(weapon) ? ItemID.GetTDBID(weapon.GetItemID()) : TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    let health = GameInstance.GetStatPoolsSystem(instigator.GetGame()).GetStatPoolValue(Cast<StatsObjectID>(targetPuppet.GetEntityID()), gamedataStatPoolType.Health, true) / 100.0;
    TandemTakedowns.Get(instigator.GetGame()).OurAction(instigator, targetPuppet, 6, finisherName, record, noEntity, health, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
  return played;
}

// The blunt "hold" finisher kills on the spot (final.reds 22433).
@wrapMethod(ScriptedPuppet)
private final func TriggerNewPerkFinisherBluntHold(playerPuppet: ref<PlayerPuppet>, npcPuppet: ref<NPCPuppet>) -> Void {
  wrappedMethod(playerPuppet, npcPuppet);
  let none = TDBID.None();
  let noEntity = EntityID.FromHash(0ul);
  TandemTakedowns.Get(this.GetGame()).OurAction(playerPuppet, npcPuppet, 6, n"tandem_blunt_hold", none, noEntity, 1.0, new Vector4(0.0, 0.0, 0.0, 0.0));
}

// Carrying: the body's CarriedObject state machine (owner: the body,
// executionOwner: our V), final.reds 531436 (pick up), 533304 (drop),
// 536937 (release), 534389 (throw).
@wrapMethod(PickUpEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let none = TDBID.None();
  let noEntity = EntityID.FromHash(0ul);
  TandemTakedowns.Get(scriptInterface.GetGame()).OurAction(scriptInterface.executionOwner, scriptInterface.owner, 7, n"None", none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
}

@wrapMethod(DropEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let takedowns = TandemTakedowns.Get(scriptInterface.GetGame());
  if takedowns.IsOurs(scriptInterface.owner, 2) {
    let none = TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    takedowns.OurAction(scriptInterface.executionOwner, scriptInterface.owner, 8, n"None", none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
}

@wrapMethod(ReleaseEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let takedowns = TandemTakedowns.Get(scriptInterface.GetGame());
  if takedowns.IsOurs(scriptInterface.owner, 2) {
    let none = TDBID.None();
    let noEntity = EntityID.FromHash(0ul);
    takedowns.OurAction(scriptInterface.executionOwner, scriptInterface.owner, 8, n"None", none, noEntity, 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
}

@wrapMethod(ThrowEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let forward = Transform.GetForward(scriptInterface.GetCameraWorldTransform());
  let impulse = forward * this.GetStaticFloatParameterDefault("ragdollMaxImpulse", 100.0);
  let none = TDBID.None();
  let noEntity = EntityID.FromHash(0ul);
  TandemTakedowns.Get(scriptInterface.GetGame()).OurAction(scriptInterface.executionOwner, scriptInterface.owner, 9, n"None", none, noEntity, 0.0, impulse);
}

// Disposal containers (final.reds 210049, 210105, 210112): the container
// is the host's; the body goes into it on every game.
@wrapMethod(DisposalDevice)
protected cb func OnDisposeBody(evt: ref<DisposeBody>) -> Bool {
  let result = wrappedMethod(evt);
  TandemDisposal.Sent(this, this.m_npcBody, n"None");
  return result;
}

@wrapMethod(DisposalDevice)
protected cb func OnTakedownAndDisposeBody(evt: ref<TakedownAndDisposeBody>) -> Bool {
  let result = wrappedMethod(evt);
  TandemDisposal.Sent(this, this.m_npcBody, n"kill");
  return result;
}

@wrapMethod(DisposalDevice)
protected cb func OnNonlethalTakedownAndDisposeBody(evt: ref<NonlethalTakedownAndDisposeBody>) -> Bool {
  let result = wrappedMethod(evt);
  TandemDisposal.Sent(this, this.m_npcBody, n"setUnconscious");
  return result;
}

public abstract class TandemDisposal {
  public static func Sent(device: ref<DisposalDevice>, body: wref<NPCPuppet>, how: CName) -> Void {
    let game = device.GetGame();
    let none = TDBID.None();
    TandemTakedowns.Get(game).OurAction(GetPlayer(game), body, 10, how, none, device.GetEntityID(), 0.0, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
}
