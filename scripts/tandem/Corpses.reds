// Bodies at rest (docs/architecture/regions.md, "Corpses"; proto::CorpsePose).
//
// A death is its owner's word, and every game ragdolls its own copy: the
// same body came to rest another way on each screen, metres apart after a
// blast, a fall or a car. The owner's body is the truth: once it has
// settled (3 s after it fell and still for 1 s, or 8 s after), its owner
// reads its ragdoll's bodies, as the game's cut multiplayer replicated a
// ragdoll (research engine/presence.md §13.162), and sends them with the
// body's place; a body that slides on more than 10 cm within 20 s of its
// fall is sent again (3 times at most). Every game that shows it sets its
// copy's ragdoll's bodies there, at rest, and lets it sleep: the copy lies
// as the owner's.
// A body that cannot be read or laid keeps its own fall's, registered
// (`corpse pose`).

public class TandemCorpseWatch {
  public let npc: Uint32;
  public let id: EntityID;
  public let since: Float;
  public let stillSince: Float;
  public let last: Vector4;
  // Sent (how many times), and when its drift was last measured.
  public let sends: Int32;
  public let checkedAt: Float;
}

public class TandemCorpseShown {
  public let npc: Uint32;
  public let toldAt: Float;
  // The copy laid (0: none yet), when, and how many times.
  public let id: EntityID;
  public let laidAt: Float;
  public let lays: Int32;
  public let measured: Bool;
}

public class TandemCorpses {
  private let m_game: GameInstance;
  private let m_watched: array<ref<TandemCorpseWatch>>;
  private let m_shown: array<ref<TandemCorpseShown>>;
  private let m_sent: Int32;
  private let m_laid: Int32;
  private let m_worst: Float;

  public static func Of(game: GameInstance) -> ref<TandemCorpses> {
    let presence = TandemPresenceSync.Of(game);
    return IsDefined(presence) ? presence.Corpses() : null;
  }

  // Bodies told and remembered at most (the oldest forgotten first): a copy
  // streamed in again is laid again.
  public static func MaxShown() -> Int32 {
    return 64;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  // The end of the session: every body forgotten (the copies go with it).
  public func End() -> Void {
    for shown in this.m_shown {
      TandemPresence.ForgetCorpse(shown.npc);
    }
    ArrayClear(this.m_shown);
    ArrayClear(this.m_watched);
  }

  // ---- the owner: one of our shared NPCs died or was knocked out ----

  public static func Watch(game: GameInstance, npc: Uint32, id: EntityID) -> Void {
    let corpses = TandemCorpses.Of(game);
    if IsDefined(corpses) {
      corpses.Add(npc, id);
    }
  }

  public func Add(npc: Uint32, id: EntityID) -> Void {
    let now = this.Now();
    let kept: array<ref<TandemCorpseWatch>>;
    for watch in this.m_watched {
      if watch.npc != npc {
        ArrayPush(kept, watch);
      }
    }
    this.m_watched = kept;
    let watch = new TandemCorpseWatch();
    watch.npc = npc;
    watch.id = id;
    watch.since = now;
    watch.stillSince = now;
    watch.last = new Vector4(0.0, 0.0, 0.0, 0.0);
    ArrayPush(this.m_watched, watch);
  }

  public func Tick(system: ref<TandemSystem>) -> Void {
    let now = this.Now();
    this.TickOwner(now);
    this.TickShown(now, system);
  }

  private func TickOwner(now: Float) -> Void {
    if ArraySize(this.m_watched) == 0 {
      return;
    }
    let kept: array<ref<TandemCorpseWatch>>;
    for watch in this.m_watched {
      let body = GameInstance.FindEntityByID(this.m_game, watch.id) as ScriptedPuppet;
      if !IsDefined(body) || now - watch.since > 20.0 {
        // Gone, or long at rest: no more watched.
        TandemPresence.ForgetCorpse(watch.npc);
      } else if watch.sends == 0 {
        let at = body.GetWorldPosition();
        if Vector4.Distance(at, watch.last) > 0.02 {
          watch.last = at;
          watch.stillSince = now;
        }
        // 3 s at least: the entity may stand still while its ragdoll falls.
        if (now - watch.since >= 3.0 && now - watch.stillSince >= 1.0) || now - watch.since >= 8.0 {
          if this.Send(watch, body, now) {
            ArrayPush(kept, watch);
          }
        } else {
          ArrayPush(kept, watch);
        }
      } else if now - watch.checkedAt >= 1.0 {
        watch.checkedAt = now;
        let drift = TandemPresence.CorpseDrift(watch.npc, body);
        if drift > 0.1 && watch.sends < 4 {
          TandemNative.LogDebug(s"corpse: npc \(watch.npc) slid \(FloatToStringPrec(drift, 2)) m after it was sent: sent again");
          this.Send(watch, body, now);
        }
        ArrayPush(kept, watch);
      } else {
        ArrayPush(kept, watch);
      }
    }
    this.m_watched = kept;
  }

  private func Send(watch: ref<TandemCorpseWatch>, body: ref<ScriptedPuppet>, now: Float) -> Bool {
    let at = body.GetWorldPosition();
    let why = TandemPresence.SendCorpse(watch.npc, body, at, Deg2Rad(body.GetWorldYaw()));
    if NotEquals(why, "") {
      TandemDegraded.Fallback("corpse pose", s"npc \(watch.npc): its body could not be read (\(why)); the other games' copies lie as their own falls left them");
      return false;
    }
    watch.sends += 1;
    watch.checkedAt = now;
    if watch.sends > 1 {
      return true;
    }
    this.m_sent += 1;
    let line = s"corpse: npc \(watch.npc) at rest after \(FloatToStringPrec(now - watch.since, 1)) s: its bodies sent";
    if this.m_sent == 1 {
      TandemNative.Log(line + " (the first body)");
    } else {
      TandemNative.LogDebug(line);
    }
    return true;
  }

  // ---- every other game: the bodies told, laid on our copies ----

  private func TickShown(now: Float, system: ref<TandemSystem>) -> Void {
    let told = TandemPresence.TakeCorpses();
    for corpse in told {
      this.Told(corpse.npc, now);
    }
    if ArraySize(this.m_shown) == 0 {
      return;
    }
    let npcs = system.NpcsSync();
    if !IsDefined(npcs) {
      return;
    }
    for shown in this.m_shown {
      let id = npcs.ProxyEntity(shown.npc);
      let body = GameInstance.FindEntityByID(this.m_game, id) as ScriptedPuppet;
      if IsDefined(body) {
        this.Show(shown, body, id, now);
      }
    }
  }

  private func Told(npc: Uint32, now: Float) -> Void {
    for shown in this.m_shown {
      if shown.npc == npc {
        // Told again: laid again.
        shown.toldAt = now;
        shown.id = EntityID.FromHash(0ul);
        return;
      }
    }
    if ArraySize(this.m_shown) >= TandemCorpses.MaxShown() {
      TandemPresence.ForgetCorpse(this.m_shown[0].npc);
      ArrayErase(this.m_shown, 0);
    }
    let shown = new TandemCorpseShown();
    shown.npc = npc;
    shown.toldAt = now;
    shown.id = EntityID.FromHash(0ul);
    ArrayPush(this.m_shown, shown);
  }

  // A copy laid when it comes (or comes again), laid once more 1 s after
  // (its own fall may still have been moving it), then let sleep; 2 s
  // after the first, how far it lies from its owner's is measured once.
  private func Show(shown: ref<TandemCorpseShown>, body: ref<ScriptedPuppet>, id: EntityID, now: Float) -> Void {
    if id != shown.id {
      shown.id = id;
      shown.lays = 0;
      shown.measured = false;
      if !this.Lay(shown, body, now) {
        // Not laid: left as its own fall left it, for good.
        shown.lays = 2;
        shown.measured = true;
      }
      return;
    }
    if shown.lays == 1 && now - shown.laidAt >= 1.0 {
      this.Lay(shown, body, now);
      body.QueueEvent(new RagdollPutToSleepEvent());
    } else if !shown.measured && shown.lays >= 2 && now - shown.laidAt >= 1.0 {
      shown.measured = true;
      let apart = TandemPresence.CorpseApart(shown.npc, body);
      if apart > this.m_worst {
        this.m_worst = apart;
      }
      let line = s"corpse: proxy \(shown.npc) lies as its owner's: its bodies within \(FloatToStringPrec(apart, 2)) m of the owner's";
      if this.m_laid == 1 {
        TandemNative.Log(line + " (the first body)");
      } else {
        TandemNative.LogDebug(line);
      }
    }
  }

  private func Lay(shown: ref<TandemCorpseShown>, body: ref<ScriptedPuppet>, now: Float) -> Bool {
    let why = TandemPresence.LayCorpse(shown.npc, body);
    if NotEquals(why, "") {
      TandemDegraded.Fallback("corpse pose", s"npc \(shown.npc): its copy could not be laid as its owner's body (\(why)); it lies as its own fall left it");
      return false;
    }
    if shown.lays == 0 {
      this.m_laid += 1;
      let line = s"corpse: proxy \(shown.npc) laid as its owner's body (its ragdoll's bodies set, at rest)";
      if this.m_laid == 1 {
        TandemNative.Log(line + " (the first body)");
      } else {
        TandemNative.LogDebug(line);
      }
    }
    shown.lays += 1;
    shown.laidAt = now;
    return true;
  }

  // The counts, then every copy here: how far its ragdoll's bodies lie now
  // from its owner's last word, read from the ragdoll itself.
  public func Report() -> Void {
    TandemNative.Log(s"corpse: sent \(this.m_sent) (our NPCs' bodies), laid \(this.m_laid) (copies; the worst \(FloatToStringPrec(this.m_worst, 2)) m from its owner's when measured), \(ArraySize(this.m_shown)) told, \(ArraySize(this.m_watched)) watched");
    let system = TandemSystem.Of(this.m_game);
    let npcs = IsDefined(system) ? system.NpcsSync() : null;
    if !IsDefined(npcs) {
      return;
    }
    for shown in this.m_shown {
      let id = npcs.ProxyEntity(shown.npc);
      let body = GameInstance.FindEntityByID(this.m_game, id) as ScriptedPuppet;
      if IsDefined(body) {
        TandemNative.Log(s"corpse: proxy \(shown.npc) now \(FloatToStringPrec(TandemPresence.CorpseApart(shown.npc, body), 2)) m from its owner's body (laid \(shown.lays) times)");
      } else {
        TandemNative.Log(s"corpse: proxy \(shown.npc) has no copy here");
      }
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }
}

// (test) "test query corpses kill": the test gangers ("npc spawn") die, each
// thrown its own way (a ragdoll with an impulse) by our V: bodies for two
// games to compare. "test query corpses": the report.
public class TandemCorpsesTest {
  public static func Command(game: GameInstance, command: String) -> Void {
    if !StrBeginsWith(command, "test query corpses kill") {
      let corpses = TandemCorpses.Of(game);
      if IsDefined(corpses) {
        corpses.Report();
      }
      return;
    }
    let ids = GameInstance.GetDynamicEntitySystem().GetTaggedIDs(n"Tandem.Test");
    let player = GetPlayer(game);
    let n = 0;
    for id in ids {
      let npc = GameInstance.FindEntityByID(game, id) as NPCPuppet;
      if IsDefined(npc) && !npc.IsDead() {
        npc.Kill(player, false, false);
        let at = npc.GetWorldPosition();
        let angle = Cast<Float>(n) * 2.1;
        let way = new Vector4(CosF(angle) * 6.0, SinF(angle) * 6.0, 3.0, 0.0);
        npc.QueueEvent(CreateForceRagdollEvent(n"tandem_test"));
        GameInstance.GetDelaySystem(game).DelayEvent(npc, CreateRagdollApplyImpulseEvent(at, way, 5.0), 0.1, false);
        n += 1;
      }
    }
    TandemNative.Log(s"corpse test: \(n) ganger(s) killed and thrown");
  }
}
