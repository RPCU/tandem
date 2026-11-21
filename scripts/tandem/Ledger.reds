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

// A placed NPC's id, the same in every game (undefined: not placed, the
// random crowd): its entity's own static id, or, for a community's NPC,
// one made of what its entity stub says of it, its community's entity (the
// spawner, a static id) and its entry's name, as the game itself tells "that
// NPC" (NPCPuppet.CheckStubData, final.reds:125987). On 2.50 a community's
// NPC has a dynamic entity id of its own (Viktor: 895e01; 126 NPCs at
// kabuki-market and vik-clinic, none static, 2026-11-21). The NPCs of one
// entry share an id.
public abstract class TandemPlaced {
  public static func IdOf(npc: ref<ScriptedPuppet>) -> EntityID {
    let none = EntityID.FromHash(0ul);
    if !IsDefined(npc) || npc.IsCrowd() {
      return none;
    }
    let id = npc.GetEntityID();
    if EntityID.IsStatic(id) {
      return id;
    }
    let stub = TandemPlaced.StubOf(npc);
    if !IsDefined(stub) {
      return none;
    }
    let spawner = stub.GetSpawnerID();
    let entry = stub.GetOwnerCommunityEntryName();
    if !EntityID.IsDefined(spawner) || !EntityID.IsStatic(spawner) || !IsNameValid(entry) {
      return none;
    }
    let hash = EntityID.ToHash(spawner) * 31ul + TDBID.ToNumber(TDBID.Create(NameToString(entry)));
    // Never in the range of the ids the game hands out as it spawns.
    if hash < 4294967296ul {
      hash += 330712481792ul;
    }
    return EntityID.FromHash(hash);
  }

  private static func StubOf(npc: ref<ScriptedPuppet>) -> ref<gameEntityStubComponentPS> {
    let id = CreatePersistentID(npc.GetEntityID(), gameEntityStubComponentPS.GetPSComponentName());
    return GameInstance.GetPersistencySystem(npc.GetGame()).GetConstAccessToPSObject(id, n"gameEntityStubComponentPS") as gameEntityStubComponentPS;
  }

  // (test) What IdOf went by, for the logs.
  public static func Why(npc: ref<ScriptedPuppet>) -> String {
    if !IsDefined(npc) {
      return "no npc";
    }
    // The crowd's: by its record, or by its crowd member component (what
    // its stub says is logged all the same: a stall's seller is the crowd's).
    let crowd = "";
    if npc.IsCrowd() {
      let record = npc.GetRecord();
      crowd = IsDefined(record) && record.IsCrowd() ? "the crowd's (its record), " : "the crowd's (in the crowd), ";
    }
    if EntityID.IsStatic(npc.GetEntityID()) {
      return s"\(crowd)its own static id";
    }
    let stub = TandemPlaced.StubOf(npc);
    if !IsDefined(stub) {
      return s"\(crowd)no entity stub";
    }
    let spawner = stub.GetSpawnerID();
    return s"\(crowd)community \(TandemNative.EntityHex(spawner)) (static \(EntityID.IsStatic(spawner))) entry \(NameToString(stub.GetOwnerCommunityEntryName()))";
  }
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
  // The placed NPCs we keep out, and which of ours stands for each.
  private let m_keptIds: array<EntityID>;
  private let m_keptOwn: array<EntityID>;
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
    if !this.m_on {
      return false;
    }
    let id = TandemPlaced.IdOf(npc);
    if !EntityID.IsDefined(id) {
      return false;
    }
    let by = TandemRegions.DiedBy(id);
    if by == 0u {
      return false;
    }
    // One death, one of ours: a community's entry may post several NPCs
    // under one name, and the others of it live on.
    let i = 0;
    while i < ArraySize(this.m_keptIds) {
      if this.m_keptIds[i] == id && this.m_keptOwn[i] != npc.GetEntityID() && IsDefined(GameInstance.FindEntityByID(this.m_game, this.m_keptOwn[i])) {
        return false;
      }
      i += 1;
    }
    return by == 1u || !TandemLedger.QuestNpc(npc);
  }

  // A quest's NPC: its record's quest affiliation (a gig's, a side job's,
  // the story's), there from the start, or the runtime mark (IsQuest: a
  // quest's SetAsQuestImportantEvent, which can come after the NPC first
  // streams in, when it would be buried already).
  public static func QuestNpc(npc: ref<ScriptedPuppet>) -> Bool {
    if npc.IsQuest() {
      return true;
    }
    let record = TweakDBInterface.GetCharacterRecord(npc.GetRecordID());
    if !IsDefined(record) || !IsDefined(record.Quest()) {
      return false;
    }
    let affiliation = record.Quest().Type();
    return NotEquals(affiliation, gamedataNPCQuestAffiliation.General) && NotEquals(affiliation, gamedataNPCQuestAffiliation.Invalid) && NotEquals(affiliation, gamedataNPCQuestAffiliation.Count);
  }

  public func NoteBuried(id: EntityID, own: EntityID, by: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_keptIds) && this.m_keptIds[i] != id {
      i += 1;
    }
    if i < ArraySize(this.m_keptIds) {
      this.m_keptOwn[i] = own;
    } else {
      if ArraySize(this.m_keptIds) >= 256 {
        ArrayErase(this.m_keptIds, 0);
        ArrayErase(this.m_keptOwn, 0);
      }
      ArrayPush(this.m_keptIds, id);
      ArrayPush(this.m_keptOwn, own);
    }
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

  // (test, tools/game.sh cmd "ledger near" | "ledger goto") the nearest
  // placed NPC of our own world within 200 m that the ledger could keep out
  // (placed: TandemPlaced.IdOf; alive, not a quest's, not a proxy;
  // ledger-guest's stand-in for "a placed NPC"); goto: our V before it,
  // facing it. `entity` is the id every game knows it by.
  public func Command(command: String, npcs: ref<TandemNpcs>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      TandemNative.Log("ledger: no V");
      return;
    }
    let here = player.GetWorldPosition();
    let best: ref<NPCPuppet>;
    let bestAt = 200.0;
    let shop: ref<NPCPuppet>;
    let shopAt = 200.0;
    let seen = 0;
    let placed = 0;
    // What the spot has instead, when it has none: the NPCs that are not
    // the crowd's, the vendors, and the nearest of the former.
    let posted = 0;
    let vendors = 0;
    let other: ref<NPCPuppet>;
    let otherAt = 201.0;
    for body in player.GetNPCsAroundObject(200.0) {
      if IsDefined(body) && !body.IsDead() {
        seen += 1;
        let id = body.GetEntityID();
        vendors += body.IsVendor() ? 1 : 0;
        if !body.IsCrowd() {
          posted += 1;
          let off = Vector4.Distance(here, body.GetWorldPosition());
          if off < otherAt {
            other = body;
            otherAt = off;
          }
        }
        if EntityID.IsDefined(TandemPlaced.IdOf(body)) && !(IsDefined(npcs) && npcs.IsProxyEntity(id)) {
          placed += 1;
          let far = Vector4.Distance(here, body.GetWorldPosition());
          if !TandemLedger.QuestNpc(body) {
            // A vendor only where nobody else stands: ours is hidden once
            // its twin dies, and vendor-guest needs it (Viktor, 2026-11-21).
            if body.IsVendor() {
              if far <= shopAt {
                shop = body;
                shopAt = far;
              }
            } else if far <= bestAt {
              best = body;
              bestAt = far;
            }
          }
        }
      }
    }
    if !IsDefined(best) {
      best = shop;
      bestAt = shopAt;
    }
    if !IsDefined(best) {
      let nearest = IsDefined(other) ? s"record \(TDBID.ToStringDEBUG(other.GetRecordID())) entity \(TandemNative.EntityHex(other.GetEntityID())) at \(otherAt) m, static \(EntityID.IsStatic(other.GetEntityID())), \(TandemPlaced.Why(other)), a quest's \(TandemLedger.QuestNpc(other))" : "none";
      TandemNative.Log(s"ledger: no placed npc of our own world within 200 m (\(seen) npcs, \(placed) placed; \(posted) not the crowd's, \(vendors) vendors; nearest not the crowd's: \(nearest))");
      return;
    }
    let at = best.GetWorldPosition();
    if StrContains(command, "goto") {
      let ahead = best.GetWorldForward();
      ahead.Z = 0.0;
      let to = at + Vector4.Normalize(ahead) * 1.5;
      to.W = 1.0;
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, Vector4.ToRotation(at - to));
    }
    TandemNative.Log(s"ledger: near placed npc record \(TDBID.ToStringDEBUG(best.GetRecordID())) entity \(TandemNative.EntityHex(TandemPlaced.IdOf(best))) at \(at.X) \(at.Y) \(at.Z) (\(bestAt) m; \(seen) npcs, \(placed) placed; \(TandemPlaced.Why(best)))");
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
