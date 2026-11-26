// Bodies at rest (docs/architecture/regions.md, "Corpses"; proto::CorpsePose).
//
// A death is its owner's word, and every game ragdolls its own copy: the
// same body came to rest another way on each screen, metres apart after a
// blast, a fall or a car. The owner's body is the truth: once it has
// settled (3 s after it fell and still for 1 s, or 8 s after), its owner reads its pose
// with the scene actors' read (engine/posestream.cpp) and sends it with the
// body's place and heading, once. Every game that shows it lays its copy
// there and draws it in that pose (the scene actors' write, held), for as
// long as the copy is there. A body whose pose cannot be read or drawn
// keeps its own ragdoll's, registered (`corpse pose`).

public class TandemCorpseWatch {
  public let npc: Uint32;
  public let id: EntityID;
  public let since: Float;
  public let stillSince: Float;
  public let last: Vector4;
  public let reading: Bool;
  public let readAt: Float;
}

public class TandemCorpseShown {
  public let npc: Uint32;
  public let id: EntityID;
  public let x: Float;
  public let y: Float;
  public let z: Float;
  public let yaw: Float;
  public let toldAt: Float;
  public let bound: Bool;
  // Let go to make room: back to its own ragdoll's pose for good.
  public let dropped: Bool;
}

public class TandemCorpses {
  private let m_game: GameInstance;
  private let m_watched: array<ref<TandemCorpseWatch>>;
  private let m_shown: array<ref<TandemCorpseShown>>;
  private let m_sent: Int32;
  private let m_drawn: Int32;
  private let m_placed: Int32;

  public static func Of(game: GameInstance) -> ref<TandemCorpses> {
    let presence = TandemPresenceSync.Of(game);
    return IsDefined(presence) ? presence.Corpses() : null;
  }

  // At most this many copies drawn in their owner's pose at once (the
  // scene actors' bodies share the slots: 16).
  public static func MaxDrawn() -> Int32 {
    return 6;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  // The end of the session: every copy back to its own ragdoll's pose.
  public func End() -> Void {
    for shown in this.m_shown {
      if shown.bound {
        TandemPresence.BindCorpse(shown.npc, null);
      }
    }
    for watch in this.m_watched {
      if watch.reading {
        TandemPresence.StopCorpseRead(watch.npc);
      }
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
      } else if watch.reading {
        TandemPresence.StopCorpseRead(npc);
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
      if !IsDefined(body) {
        if watch.reading {
          TandemPresence.StopCorpseRead(watch.npc);
        }
      } else if !watch.reading {
        let at = body.GetWorldPosition();
        if Vector4.Distance(at, watch.last) > 0.02 {
          watch.last = at;
          watch.stillSince = now;
        }
        // 3 s at least: the entity may stand still while its ragdoll falls.
        if (now - watch.since >= 3.0 && now - watch.stillSince >= 1.0) || now - watch.since >= 8.0 {
          TandemPresence.ReadCorpse(watch.npc, body);
          watch.reading = true;
          watch.readAt = now;
        }
        ArrayPush(kept, watch);
      } else if TandemPresence.SendCorpse(watch.npc, body.GetWorldPosition(), Deg2Rad(body.GetWorldYaw())) {
        this.m_sent += 1;
        let line = s"corpse: npc \(watch.npc) at rest after \(FloatToStringPrec(now - watch.since, 1)) s: its pose sent";
        if this.m_sent == 1 {
          TandemNative.Log(line + " (the first body)");
        } else {
          TandemNative.LogDebug(line);
        }
      } else if now - watch.readAt > 2.0 {
        TandemPresence.StopCorpseRead(watch.npc);
        TandemDegraded.Fallback("corpse pose", s"npc \(watch.npc): its body's pose could not be read; the other games' copies lie as their own ragdolls left them");
      } else {
        ArrayPush(kept, watch);
      }
    }
    this.m_watched = kept;
  }

  // ---- every other game: the bodies told, laid and drawn on our copies ----

  private func TickShown(now: Float, system: ref<TandemSystem>) -> Void {
    let told = TandemPresence.TakeCorpses();
    for corpse in told {
      this.Told(corpse, now);
    }
    if ArraySize(this.m_shown) == 0 {
      return;
    }
    let npcs = system.NpcsSync();
    let player = GetPlayer(this.m_game);
    let kept: array<ref<TandemCorpseShown>>;
    for shown in this.m_shown {
      let id = IsDefined(npcs) && !shown.dropped ? npcs.ProxyEntity(shown.npc) : EntityID.FromHash(0ul);
      let body = GameInstance.FindEntityByID(this.m_game, id) as ScriptedPuppet;
      if shown.dropped {
        // Let go for a nearer one: nothing more to do.
      } else if shown.bound && (!IsDefined(body) || id != shown.id) {
        // Its copy went (streamed out, a resync): nothing to draw any more.
        TandemPresence.BindCorpse(shown.npc, null);
      } else if shown.bound {
        ArrayPush(kept, shown);
      } else if IsDefined(body) {
        if this.Lay(shown, body, id, player) {
          ArrayPush(kept, shown);
        }
      } else if !shown.dropped && now - shown.toldAt < 5.0 {
        // Told before its copy is here (its spawn, its death): waited for.
        ArrayPush(kept, shown);
      }
    }
    this.m_shown = kept;
  }

  private func Told(corpse: TandemCorpse, now: Float) -> Void {
    for shown in this.m_shown {
      if shown.npc == corpse.npc {
        shown.x = corpse.x;
        shown.y = corpse.y;
        shown.z = corpse.z;
        shown.yaw = corpse.yaw;
        shown.toldAt = now;
        return;
      }
    }
    let shown = new TandemCorpseShown();
    shown.npc = corpse.npc;
    shown.id = EntityID.FromHash(0ul);
    shown.x = corpse.x;
    shown.y = corpse.y;
    shown.z = corpse.z;
    shown.yaw = corpse.yaw;
    shown.toldAt = now;
    ArrayPush(this.m_shown, shown);
  }

  // Our copy where the owner's body lies, in its pose. False: not drawn.
  private func Lay(shown: ref<TandemCorpseShown>, body: ref<ScriptedPuppet>, id: EntityID, player: ref<PlayerPuppet>) -> Bool {
    if !this.Room(player) {
      TandemDegraded.Fallback("corpse pose", s"npc \(shown.npc): \(TandemCorpses.MaxDrawn()) bodies already drawn in their owners' poses near us; this copy lies as its own ragdoll left it");
      return false;
    }
    let at = new Vector4(shown.x, shown.y, shown.z, 1.0);
    let apart = Vector4.Distance(body.GetWorldPosition(), at);
    if apart > 0.3 {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, TandemYawAngles(Rad2Deg(shown.yaw)));
      this.m_placed += 1;
    }
    TandemPresence.BindCorpse(shown.npc, body);
    shown.id = id;
    shown.bound = true;
    this.m_drawn += 1;
    let line = s"corpse: proxy \(shown.npc) drawn in its owner's rest pose (\(FloatToStringPrec(apart, 2)) m from where its owner's lies\(apart > 0.3 ? ": laid there" : ""))";
    if this.m_drawn == 1 {
      TandemNative.Log(line + " (the first body)");
    } else {
      TandemNative.LogDebug(line);
    }
    return true;
  }

  // A slot for one more drawn copy: one under the cap, or the farthest one
  // beyond 50 m let go (out of sight: back to its own ragdoll's pose).
  private func Room(player: ref<PlayerPuppet>) -> Bool {
    let drawn = 0;
    let farthest: ref<TandemCorpseShown>;
    let far = 50.0;
    for shown in this.m_shown {
      if shown.bound {
        drawn += 1;
        if IsDefined(player) {
          let distance = Vector4.Distance(player.GetWorldPosition(), new Vector4(shown.x, shown.y, shown.z, 1.0));
          if distance > far {
            far = distance;
            farthest = shown;
          }
        }
      }
    }
    if drawn < TandemCorpses.MaxDrawn() {
      return true;
    }
    if !IsDefined(farthest) {
      return false;
    }
    TandemPresence.BindCorpse(farthest.npc, null);
    farthest.bound = false;
    farthest.dropped = true;
    return true;
  }

  public func Report() -> Void {
    let bound = 0;
    for shown in this.m_shown {
      if shown.bound {
        bound += 1;
      }
    }
    TandemNative.Log(s"corpse: sent \(this.m_sent) (our NPCs' bodies), drawn \(this.m_drawn) (copies; \(this.m_placed) laid where their owner's lie), \(bound) drawn now, \(ArraySize(this.m_watched)) settling");
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }
}
