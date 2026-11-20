// The death ledger on the game's side (docs/architecture/regions.md §14):
// the placed NPCs that died in the session stay dead in every game, also
// where its player was not there. The session keeps them (TandemRegions:
// DiedBy, TakeDeaths, Bodies); Npcs.reds says which of our NPCs are placed
// and keeps our own copy of a dead one out of our world (Bury). Here: the
// rule for our own copy, and the bodies.
//
// A body: where a placed NPC another player killed lately fell, a copy of
// it is spawned and laid down, with no death played: what our V would find
// had it been there. It is ours (one of our spawned entities): looted as
// any corpse of our game, deleted when our V walks away, never saved.

public class TandemLedgerBody {
  // The placed NPC it stands for, and our copy.
  public let entity: EntityID;
  public let id: EntityID;
  public let knockedOut: Bool;
  // Since when its entity exists (0: not yet), and whether it lies there.
  public let seenAt: Float;
  public let laid: Bool;
}

public class TandemLedger {
  private let m_game: GameInstance;
  private let m_on: Bool;
  private let m_withBodies: Bool;
  private let m_last: Float;
  private let m_bodies: array<ref<TandemLedgerBody>>;
  // The placed NPCs we saw die as proxies: their corpse is here already.
  private let m_witnessed: array<EntityID>;
  private let m_buried: Int32;
  private let m_laid: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    // This game's own settings (tandem.ini), read once.
    this.m_on = NotEquals(TandemNative.Setting("death_ledger"), "off");
    this.m_withBodies = this.m_on && NotEquals(TandemNative.Setting("death_bodies"), "off");
  }

  public func On() -> Bool {
    return this.m_on;
  }

  // Our own copy of a placed NPC is to be kept out of our world: the session
  // says that entity's NPC died. Not a quest NPC of our own world unless the
  // host's game killed it (the story is the host's; a gig of ours may still
  // need the one a partner killed in its own world).
  public func Buries(npc: ref<ScriptedPuppet>) -> Bool {
    let id = npc.GetEntityID();
    if !this.m_on || !EntityID.IsStatic(id) {
      return false;
    }
    let by = TandemRegions.DiedBy(id);
    if by == 0u {
      return false;
    }
    return by == 1u || !npc.IsQuest();
  }

  public func NoteBuried(id: EntityID, by: Uint32) -> Void {
    this.m_buried += 1;
    TandemNative.Log(s"ledger: placed npc \(EntityID.ToDebugString(id)) died in player \(by)'s game: our copy is kept out of our world");
  }

  // We saw this placed NPC die here (its proxy is the corpse): no other body.
  public func Witness(entity: EntityID) -> Void {
    if EntityID.IsDefined(entity) && !ArrayContains(this.m_witnessed, entity) {
      if ArraySize(this.m_witnessed) >= 256 {
        ArrayErase(this.m_witnessed, 0);
      }
      ArrayPush(this.m_witnessed, entity);
    }
  }

  // One of our bodies (never an NPC of ours to share).
  public static func IsBody(id: EntityID) -> Bool {
    return EntityID.IsDynamic(id) && GameInstance.GetDynamicEntitySystem().IsTagged(id, n"Tandem.Body");
  }

  public func Tick(now: Float, you: Uint32) -> Void {
    if now - this.m_last < 1.0 {
      // A body just spawned is laid down as soon as it exists.
      this.Lay(now);
      return;
    }
    this.m_last = now;
    for death in TandemRegions.TakeDeaths() {
      TandemNative.Log(s"ledger: placed npc \(EntityID.ToDebugString(death.entity)) \(TDBID.ToStringDEBUG(death.record)) \(death.knockedOut ? "knocked out" : "died") in player \(death.by)'s game at (\(death.x), \(death.y), \(death.z))\(death.by == you ? " (ours)" : "")");
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || !this.m_withBodies || TandemElsewhere.AwayFromBody(this.m_game) {
      this.Clear();
      return;
    }
    let shown: array<EntityID>;
    for body in this.m_bodies {
      ArrayPush(shown, body.entity);
    }
    let here = player.GetWorldPosition();
    let wanted = TandemRegions.Bodies(here.X, here.Y, here.Z, shown);
    // The ones no longer wanted go (our V walked away, the death is old).
    let kept: array<ref<TandemLedgerBody>>;
    for body in this.m_bodies {
      let still = false;
      for death in wanted {
        if death.entity == body.entity {
          still = true;
        }
      }
      if still {
        ArrayPush(kept, body);
      } else {
        GameInstance.GetDynamicEntitySystem().DeleteEntity(body.id);
      }
    }
    this.m_bodies = kept;
    for death in wanted {
      if !ArrayContains(shown, death.entity) && !ArrayContains(this.m_witnessed, death.entity) && TDBID.IsValid(death.record) {
        this.Spawn(death);
      }
    }
    this.Lay(now);
  }

  private func Spawn(death: TandemDeath) -> Void {
    let half = death.yaw * 0.5;
    let spec = new DynamicEntitySpec();
    spec.recordID = death.record;
    if IsNameValid(death.appearance) {
      spec.appearanceName = death.appearance;
    }
    spec.position = new Vector4(death.x, death.y, death.z, 1.0);
    spec.orientation = new Quaternion(0.0, 0.0, SinF(half), CosF(half));
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Body");
    let body = new TandemLedgerBody();
    body.entity = death.entity;
    body.knockedOut = death.knockedOut;
    body.id = GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
    ArrayPush(this.m_bodies, body);
    TandemNative.Log(s"ledger: a body for placed npc \(EntityID.ToDebugString(death.entity)) (\(death.knockedOut ? "knocked out" : "dead") \(death.age) s ago, player \(death.by)'s) at (\(death.x), \(death.y), \(death.z))");
  }

  // Each body, once its entity exists: brain off at once, then laid down
  // with no death played (no kill is anyone's: no instigator).
  private func Lay(now: Float) -> Void {
    for body in this.m_bodies {
      if !body.laid {
        let npc = GameInstance.GetDynamicEntitySystem().GetEntity(body.id) as ScriptedPuppet;
        if IsDefined(npc) {
          if body.seenAt <= 0.0 {
            body.seenAt = now;
            TandemProxies.Pacify(npc);
          } else if now - body.seenAt >= 0.5 {
            body.laid = true;
            this.m_laid += 1;
            if body.knockedOut {
              StatusEffectHelper.ApplyStatusEffect(npc, t"BaseStatusEffect.Unconscious");
              npc.QueueEvent(CreateForceRagdollEvent(n"tandem_ledger_body"));
            } else {
              npc.Kill(null, true, false);
            }
          }
        }
      }
    }
  }

  private func Clear() -> Void {
    for body in this.m_bodies {
      GameInstance.GetDynamicEntitySystem().DeleteEntity(body.id);
    }
    ArrayClear(this.m_bodies);
  }

  // The session is over: the bodies go; what we kept out of our world stays
  // hidden until it streams out (Npcs.reds), and is alive again after.
  public func End() -> Void {
    this.Clear();
    ArrayClear(this.m_witnessed);
  }

  // (test, tools/game.sh cmd ledger)
  public func Report() -> String {
    let player = GetPlayer(this.m_game);
    let near = 0;
    if IsDefined(player) {
      let here = player.GetWorldPosition();
      near = TandemRegions.DeathsNear(here.X, here.Y, here.Z, 200.0);
    }
    return s"ledger: \(this.m_on ? "on" : "off"), bodies \(this.m_withBodies ? "on" : "off"), \(near) death(s) within 200 m, \(this.m_buried) of our copies kept out, \(ArraySize(this.m_bodies)) bodies shown (\(this.m_laid) laid so far), \(ArraySize(this.m_witnessed)) seen dying here";
  }
}
