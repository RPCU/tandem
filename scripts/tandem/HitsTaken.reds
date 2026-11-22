// A hit a V took, shown on its puppet as the hit it was
// (docs/architecture/presence.md, "Hit reactions"). The V's own game ran
// the hit (its damage, block, armour: the authority table) and tells it
// (proto::HitTaken: who, the attack, where it landed, the way it went);
// every other game queues it on the puppet through the game's own damage
// pipeline, dealing nothing, so the hit reaction component picks the
// reaction as for any hit (HitReactionComponent.EvaluateHit: direction,
// body part, variation). A hit our own game ran on the puppet already (our
// V's under friendly fire, the swing of an NPC we run) is not shown twice.
// The fall of the V's health (Presence.reds, Hurt) stays the fallback for
// what is never told: damage over time, a fall, a lost message.

// A hit queued on a puppet, looked at again once the pipeline has run.
public class TandemQueuedHit {
  public let player: Uint32;
  public let due: Float;
  // The hit reaction component's last hit time before ours was queued.
  public let stamp: Float;
  // What we would twitch with, were the pipeline to play nothing.
  public let direction: Int32;
  public let intensity: Int32;
  public let part: Int32;
  public let by: String;
}

public class TandemHitsTaken {
  private let m_game: GameInstance;
  private let m_now: Float;
  // Hits we queued, until their pipeline has ended (DamageSystem's stages
  // know them by it: Combat.reds, Outcomes.reds).
  private let m_shown: array<ref<gameHitEvent>>;
  private let m_queued: array<ref<TandemQueuedHit>>;
  // Hits our own game ran on a puppet: by whom, on whose, when.
  private let m_nativeBy: array<EntityID>;
  private let m_nativeOn: array<Uint32>;
  private let m_nativeAt: array<Float>;
  // When a hit was last shown on each player's puppet (by us or natively).
  private let m_shownOn: array<Uint32>;
  private let m_shownAt: array<Float>;
  // Ours, told: at most one every 0.1 s (a shotgun's pellets are one hit
  // to look at).
  private let m_toldAt: Float;
  private let m_told: Int32;
  private let m_untold: Int32;
  private let m_byPipeline: Int32;
  private let m_byTwitch: Int32;
  private let m_leftToOurs: Int32;
  private let m_logged: array<CName>;

  public static func Of(game: GameInstance) -> ref<TandemHitsTaken> {
    let presence = TandemPresenceSync.Of(game);
    return IsDefined(presence) ? presence.HitsTaken() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func End() -> Void {
    ArrayClear(this.m_shown);
    ArrayClear(this.m_queued);
    ArrayClear(this.m_nativeBy);
    ArrayClear(this.m_nativeOn);
    ArrayClear(this.m_nativeAt);
    ArrayClear(this.m_shownOn);
    ArrayClear(this.m_shownAt);
    ArrayClear(this.m_logged);
    this.m_toldAt = 0.0;
  }

  private func Once(key: CName, text: String) -> Void {
    if !ArrayContains(this.m_logged, key) {
      ArrayPush(this.m_logged, key);
      TandemNative.Log(text);
    }
  }

  // ---- our V's side ----

  // The damage pipeline of a hit ended (DamageSystem.ProcessPipeline,
  // Combat.reds): one that took health of our V is told to the others.
  public static func Ours(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Void {
    let hits = TandemHitsTaken.Of(game);
    if IsDefined(hits) {
      hits.Tell(hitEvent);
    }
  }

  private func Tell(hitEvent: ref<gameHitEvent>) -> Void {
    let player = GetPlayer(this.m_game);
    let system = TandemCombat.System(this.m_game);
    if !IsDefined(player) || !IsDefined(system) || system.You() == 0u || !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) || hitEvent.target != player {
      return;
    }
    let data = hitEvent.attackData;
    // What took no health (blocked, dodged, immune) shows no hit; damage
    // over time has no place nor way: the health's fall says it.
    if !data.HasFlag(hitFlag.SuccessfulAttack) || data.HasFlag(hitFlag.DealNoDamage) || AttackData.IsDoT(data) {
      return;
    }
    if this.m_now - this.m_toldAt < 0.1 {
      this.m_untold += 1;
      return;
    }
    this.m_toldAt = this.m_now;
    // Who: an NPC the session shares (ours or a proxy), another V's puppet.
    let by = 0;
    let attacker = 0u;
    let from = data.GetInstigator();
    if IsDefined(from) {
      let id = from.GetEntityID();
      let npc = system.NpcOf(id);
      let other = system.PlayerOfPuppet(id);
      if npc != 0u {
        by = 1;
        attacker = npc;
      } else if other != 0u {
        by = 2;
        attacker = other;
      }
    }
    let attack = TDBID.None();
    let definition = data.GetAttackDefinition();
    if IsDefined(definition) && IsDefined(definition.GetRecord()) {
      attack = definition.GetRecord().GetID();
    }
    let type = data.GetAttackType();
    let flags = 0;
    if AttackData.IsMelee(type) {
      flags += 1;
    }
    if StatusEffectSystem.ObjectHasStatusEffectOfType(player, gamedataStatusEffectType.Stagger) {
      flags += 2;
    }
    if StatusEffectSystem.ObjectHasStatusEffectOfType(player, gamedataStatusEffectType.Knockdown) {
      flags += 4;
    }
    if data.HasFlag(hitFlag.CriticalHit) || data.HasFlag(hitFlag.Headshot) {
      flags += 8;
    }
    // The hit shape it landed on (a forwarded hit has none: the others
    // place it by its height).
    let zone = -1;
    let shape = -1;
    let shapes = hitEvent.hitRepresentationResult.hitShapes;
    if ArraySize(shapes) > 0 {
      let first = shapes[0];
      let user = first.userData as HitShapeUserDataBase;
      if IsDefined(user) {
        zone = EnumInt(user.m_hitReactionZone);
        shape = EnumInt(user.m_hitShapeType);
      }
    }
    let whole = GameInstance.GetStatsSystem(this.m_game).GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatType.Health);
    let took = hitEvent.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
    let share = whole > 0.0 ? ClampF(took / whole, 0.0, 1.0) : 0.0;
    let at = hitEvent.hitPosition;
    let feet = player.GetWorldPosition();
    let offset = new Vector4(at.X - feet.X, at.Y - feet.Y, at.Z - feet.Z, 0.0);
    TandemPresence.SendHitTaken(by, attacker, attack, EnumInt(type), flags, zone, shape, share, offset, hitEvent.hitDirection);
    this.m_told += 1;
    let who = TandemHitsTaken.Who(by, attacker);
    this.Once(n"told", s"hit taken: our V's first, by \(who), told to the others (said once; cmd presence report counts them)");
    TandemNative.LogDebug(s"hit taken: our V's, by \(who) (attack \(TDBID.ToStringDEBUG(attack)), type \(EnumInt(type)), zone \(zone), \(Cast<Int32>(share * 100.0)) % of its health, flags \(flags)): told to the others");
  }

  private static func Who(by: Int32, attacker: Uint32) -> String {
    if by == 1 {
      return s"npc \(attacker)";
    }
    if by == 2 {
      return s"player \(attacker)";
    }
    return "nobody the others know";
  }

  // ---- the others' side: what the damage pipeline asks ----

  // A hit we queued on a puppet (Queue): its player's game ran it; here it
  // only reacts (no stage of ours drops it, none changes it, no status
  // effect of the attack is applied).
  public static func IsShown(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let hits = TandemHitsTaken.Of(game);
    return IsDefined(hits) && ArrayContains(hits.m_shown, hitEvent);
  }

  public static func Forget(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Void {
    let hits = TandemHitsTaken.Of(game);
    if IsDefined(hits) {
      ArrayRemove(hits.m_shown, hitEvent);
    }
  }

  // A hit of our own game's on a player's puppet enters the pipeline: its
  // reaction is the game's own, here and now. That V's game will tell the
  // same hit: not shown again.
  public static func Native(game: GameInstance, victim: Uint32, hitEvent: ref<gameHitEvent>) -> Void {
    let hits = TandemHitsTaken.Of(game);
    let from = IsDefined(hitEvent.attackData) ? hitEvent.attackData.GetInstigator() : null;
    if !IsDefined(hits) || !IsDefined(from) || hitEvent.attackData.HasFlag(hitFlag.DealNoDamage) {
      return;
    }
    ArrayPush(hits.m_nativeBy, from.GetEntityID());
    ArrayPush(hits.m_nativeOn, victim);
    ArrayPush(hits.m_nativeAt, hits.m_now);
    hits.NoteShown(victim);
  }

  private func WasNative(player: Uint32, by: EntityID) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_nativeAt) {
      if this.m_nativeOn[i] == player && this.m_nativeBy[i] == by {
        ArrayErase(this.m_nativeBy, i);
        ArrayErase(this.m_nativeOn, i);
        ArrayErase(this.m_nativeAt, i);
        return true;
      }
      i += 1;
    }
    return false;
  }

  // A hit was shown on this player's puppet just now (Shots.reds too).
  public func NoteShown(player: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_shownOn) {
      if this.m_shownOn[i] == player {
        this.m_shownAt[i] = this.m_now;
        return;
      }
      i += 1;
    }
    ArrayPush(this.m_shownOn, player);
    ArrayPush(this.m_shownAt, this.m_now);
  }

  // A hit was shown on this player's puppet since `since` (Presence.reds:
  // the fall of its health is then that hit's, not one more flinch).
  public func ShownSince(player: Uint32, since: Float) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_shownOn) {
      if this.m_shownOn[i] == player {
        return this.m_shownAt[i] >= since;
      }
      i += 1;
    }
    return false;
  }

  // ---- the others' side: every tick ----

  public func Tick(now: Float, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    for hit in TandemPresence.TakeHitsTaken() {
      this.Show(hit, system);
    }
    // A native hit its V's game never told (blocked, dodged) is forgotten.
    while ArraySize(this.m_nativeAt) > 0 && now - this.m_nativeAt[0] > 1.5 {
      ArrayErase(this.m_nativeBy, 0);
      ArrayErase(this.m_nativeOn, 0);
      ArrayErase(this.m_nativeAt, 0);
    }
    let i = 0;
    while i < ArraySize(this.m_queued) {
      let queued = this.m_queued[i];
      if now >= queued.due {
        ArrayErase(this.m_queued, i);
        this.Played(queued, system);
      } else {
        i += 1;
      }
    }
  }

  private func Show(hit: TandemHitTaken, system: ref<TandemSystem>) -> Void {
    let body = system.PuppetOf(hit.player) as ScriptedPuppet;
    if !IsDefined(body) || TandemVisibility.IsHidden(this.m_game, hit.player) {
      return;
    }
    let who = TandemHitsTaken.Who(hit.by, hit.attacker);
    // Our own V's hit (friendly fire): our game showed it as it landed.
    if hit.by == 2 && hit.attacker == system.You() {
      this.m_leftToOurs += 1;
      this.NoteShown(hit.player);
      TandemNative.LogDebug(s"hit taken: player \(hit.player)'s, by our V: our own game showed it as it landed");
      return;
    }
    let attacker: ref<GameObject>;
    if hit.by == 1 {
      attacker = GameInstance.FindEntityByID(this.m_game, system.NpcEntity(hit.attacker)) as GameObject;
    } else if hit.by == 2 {
      attacker = system.PuppetOf(hit.attacker);
    }
    // The swing of an NPC we run, landed on the puppet in our own game.
    if IsDefined(attacker) && this.WasNative(hit.player, attacker.GetEntityID()) {
      this.m_leftToOurs += 1;
      this.NoteShown(hit.player);
      TandemNative.LogDebug(s"hit taken: player \(hit.player)'s, by \(who): our own game showed it as it landed");
      return;
    }
    let way = new Vector4(hit.dx, hit.dy, hit.dz, 0.0);
    let offset = new Vector4(hit.x, hit.y, hit.z, 0.0);
    let zone = hit.zone >= 0 && hit.zone <= 9 ? hit.zone : TandemHitsTaken.ZoneAt(body, offset);
    let direction = TandemHitsTaken.DirectionOf(body, way);
    let part = TandemHitsTaken.PartOf(zone);
    // How hard, as the fall of its health would have said (Presence.reds),
    // and at least medium for one that staggered it or knocked it down.
    let intensity = hit.share >= 0.2 ? 2 : (hit.share >= 0.08 ? 1 : 0);
    if (hit.flags & 6) != 0 && intensity < 1 {
      intensity = 1;
    }
    let strength = intensity >= 2 ? 1.0 : (intensity == 1 ? 0.6 : 0.25);
    // The flinch over the streamed pose, the way the hit went.
    TandemPresence.Flinch(hit.player, strength, way, body.GetWorldForward());
    this.NoteShown(hit.player);
    if !IsDefined(attacker) {
      // Nobody both games know hit it: the pipeline needs a source. The
      // body's own twitch, from where and on what part the hit says.
      TandemReactionsSync.Twitch(body, direction, intensity, part);
      this.m_byTwitch += 1;
      TandemNative.LogDebug(s"hit taken: player \(hit.player)'s puppet twitches (direction \(direction), part \(part), intensity \(intensity)): by \(who), no source for the game's pipeline");
      return;
    }
    let component = body.GetHitReactionComponent();
    let queued = new TandemQueuedHit();
    queued.player = hit.player;
    queued.due = this.m_now + 0.15;
    queued.stamp = IsDefined(component) ? component.GetLastHitTimeStamp() : -1.0;
    queued.direction = direction;
    queued.intensity = intensity;
    queued.part = part;
    queued.by = who;
    ArrayPush(this.m_queued, queued);
    let melee = (hit.flags & 1) != 0;
    let at = body.GetWorldPosition();
    at.X += hit.x;
    at.Y += hit.y;
    at.Z += hit.z;
    this.Queue(body, attacker, TandemCombat.AttackRecord(this.m_game, hit.attack, melee), hit.attackType, at, way, zone, hit.shape, (hit.flags & 8) != 0);
  }

  // 0.15 s after a hit was queued: what the game's pipeline made of it.
  // The hit reaction component stamps the hits it reacted to; a reaction
  // other than a twitch is a task of the AI a puppet does not run, and one
  // the component let pass (its own pacing of hits) plays nothing: the
  // body's own twitch then, with the direction and part the game chose.
  private func Played(queued: ref<TandemQueuedHit>, system: ref<TandemSystem>) -> Void {
    let body = system.PuppetOf(queued.player) as ScriptedPuppet;
    let component = IsDefined(body) ? body.GetHitReactionComponent() : null;
    if !IsDefined(component) {
      return;
    }
    let data = component.GetHitReactionData();
    let reacted = component.GetLastHitTimeStamp() != queued.stamp && IsDefined(data);
    if reacted && data.hitType == 1 {
      this.m_byPipeline += 1;
      this.Once(n"pipeline", s"hit taken: player \(queued.player)'s puppet reacts by the game's pipeline (twitch, direction \(data.hitDirection), part \(data.hitBodyPart), variation \(data.animVariation); said once)");
      TandemNative.LogDebug(s"hit taken: player \(queued.player)'s puppet reacts by the game's pipeline (twitch, direction \(data.hitDirection), part \(data.hitBodyPart), intensity \(data.hitIntensity)): by \(queued.by)");
      return;
    }
    if reacted {
      TandemReactionsSync.Twitch(body, data.hitDirection, data.hitIntensity, data.hitBodyPart);
      this.m_byTwitch += 1;
      TandemNative.LogDebug(s"hit taken: player \(queued.player)'s puppet twitches (direction \(data.hitDirection), part \(data.hitBodyPart)): the game's pipeline chose reaction \(data.hitType), an AI task a puppet does not run; by \(queued.by)");
      return;
    }
    TandemReactionsSync.Twitch(body, queued.direction, queued.intensity, queued.part);
    this.m_byTwitch += 1;
    this.Once(n"nothing", s"hit taken: player \(queued.player)'s puppet twitches (direction \(queued.direction), part \(queued.part)): the game's pipeline played nothing for its hit by \(queued.by) (said once)");
    TandemNative.LogDebug(s"hit taken: player \(queued.player)'s puppet twitches (direction \(queued.direction), part \(queued.part)): the game's pipeline played nothing; by \(queued.by)");
  }

  // The hit on the puppet as the game runs one (DamageSystem.QueueHitEvent:
  // its hit reaction, sounds, effects), dealing nothing: the attack, its
  // type, its source, and a hit shape (the hit reaction needs one:
  // HitReactionComponent.GetBodyPart) of the zone it landed on.
  public func Queue(body: ref<ScriptedPuppet>, attacker: ref<GameObject>, record: ref<Attack_Record>, attackType: Int32, at: Vector4, way: Vector4, zone: Int32, shapeType: Int32, critical: Bool) -> Void {
    let weapon = GameObject.GetActiveWeapon(attacker);
    let context: AttackInitContext;
    context.record = record;
    context.instigator = attacker;
    context.source = attacker;
    context.weapon = weapon;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = body;
    hit.hitPosition = at;
    hit.hitDirection = way;
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    hit.attackData.SetInstigator(attacker);
    hit.attackData.SetSource(attacker);
    if IsDefined(weapon) {
      hit.attackData.SetWeapon(weapon);
    }
    hit.attackData.SetAttackType(IntEnum<gamedataAttackType>(attackType));
    hit.attackData.SetAttackPosition(attacker.GetWorldPosition());
    hit.attackData.AddFlag(hitFlag.DealNoDamage, n"tandem_hit_taken");
    hit.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_hit_taken");
    // Its V's game found it a hit: our attitude check must not drop it.
    hit.attackData.AddFlag(hitFlag.FriendlyFire, n"tandem_hit_taken");
    if critical {
      hit.attackData.AddFlag(hitFlag.CriticalHit, n"tandem_hit_taken");
    }
    hit.attackComputed = new gameAttackComputed();
    let data = new HitShapeUserDataBase();
    data.m_hitReactionZone = IntEnum<EHitReactionZone>(zone);
    data.m_dismembermentPart = IntEnum<EAIDismembermentBodyPart>(-1);
    data.m_hitShapeType = IntEnum<EHitShapeType>(shapeType >= 0 ? shapeType : 0);
    data.m_hitShapeTag = zone == 0 ? n"Head" : n"Body";
    let landed: HitShapeResult;
    landed.hitPositionEnter = at;
    landed.hitPositionExit = at;
    let shape: HitShapeData;
    shape.userData = data;
    shape.result = landed;
    let shapes: HitRepresentationQueryResult;
    ArrayPush(shapes.hitShapes, shape);
    hit.hitRepresentationResult = shapes;
    ArrayPush(this.m_shown, hit);
    GameInstance.GetDamageSystem(this.m_game).QueueHitEvent(hit, body);
  }

  // The zone of a body a hit landed on, by where it landed from the body's
  // position (EHitReactionZone): the head from 1.5 m up, the chest from
  // 1.1 m, the abdomen from 0.85 m, the legs under it, the left or the
  // right by its side of the body.
  public static func ZoneAt(body: ref<GameObject>, offset: Vector4) -> Int32 {
    let right = body.GetWorldRight();
    let side = offset.X * right.X + offset.Y * right.Y;
    if offset.Z >= 1.5 {
      return 0;
    }
    if offset.Z >= 1.1 {
      return side < 0.0 ? 1 : 4;
    }
    if offset.Z >= 0.85 {
      return 7;
    }
    return side < 0.0 ? 8 : 9;
  }

  // The hit direction the animation takes, as the game computes it from a
  // hit's way (GameObject.GetLocalAngleForDirectionInInt, 0 stored as 4:
  // HitReactionComponent.StoreHitData); no way: from the front.
  public static func DirectionOf(body: ref<GameObject>, way: Vector4) -> Int32 {
    if Vector4.Length(way) < 0.01 {
      return 3;
    }
    let direction = GameObject.GetLocalAngleForDirectionInInt(way, body);
    return direction == 0 ? 4 : direction;
  }

  // EHitReactionZone to the animation's body part (EAIHitBodyPart), as
  // HitReactionComponent.ReactionZoneEnumToBodyPartID.
  public static func PartOf(zone: Int32) -> Int32 {
    if zone == 0 {
      return 1;
    }
    if zone >= 1 && zone <= 3 {
      return 2;
    }
    if zone >= 4 && zone <= 6 {
      return 3;
    }
    if zone == 8 {
      return 5;
    }
    if zone == 9 {
      return 6;
    }
    return 4;
  }

  // ---- developer commands (tools/game.sh cmd presence hit ...) ----

  // presence hit [npc]: every puppet takes a bullet on its left leg from
  // its left, as if its V's game had told it: by nobody known (the body's
  // twitch), or `npc`: by the first NPC of the session here (the game's
  // pipeline).
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let npc = 0u;
    let npcs = system.NpcsSync();
    if Equals(command, "npc") && IsDefined(npcs) {
      let proxies = npcs.Proxies();
      if ArraySize(proxies) > 0 {
        let first = proxies[0];
        npc = first.npc;
      }
    }
    let count = 0;
    for player in system.PuppetPlayers() {
      let body = system.PuppetOf(player);
      if IsDefined(body) {
        let right = body.GetWorldRight();
        let hit: TandemHitTaken;
        hit.player = player;
        hit.by = npc != 0u ? 1 : 0;
        hit.attacker = npc;
        hit.attack = TDBID.None();
        hit.attackType = 10;
        hit.flags = 0;
        hit.zone = 8;
        hit.shape = 0;
        hit.share = 0.1;
        hit.x = -0.15 * right.X;
        hit.y = -0.15 * right.Y;
        hit.z = 0.5;
        hit.dx = right.X;
        hit.dy = right.Y;
        hit.dz = 0.0;
        this.Show(hit, system);
        count += 1;
      }
    }
    TandemNative.Log(s"presence: a hit on the left leg, from the left, on \(count) puppet(s), by \(TandemHitsTaken.Who(npc != 0u ? 1 : 0, npc))");
  }

  public func Report() -> Void {
    TandemNative.Log(s"hit taken: ours told \(this.m_told) (\(this.m_untold) within 0.1 s of another, not told); the others' shown by the game's pipeline \(this.m_byPipeline), by the body's twitch \(this.m_byTwitch), left to our own game's hit \(this.m_leftToOurs); waiting \(ArraySize(this.m_queued))");
  }
}
