// Hit outcomes (docs/architecture/combat.md, "Game rules hold for every hit"): a hit
// carries its whole outcome, not just health, and each part of it is decided
// by one game only.
//
// Guest: our hit on a proxy is measured as before (Combat.reds DealDamages)
// and kept until its pipeline ends, where the poise it took is known
// (DamageSystem.PostProcess drains it for player attackers); then it goes to
// the host as a strike: health, poise, the rules that bound it (non-lethal,
// leave 1 HP, the attack's minimum health), the attack and the hit shape it
// landed on.
//
// Host: a strike becomes a hit of the guest's puppet on the real NPC, run
// through our damage pipeline for what the NPC does about it (its hit
// reaction by body part, weak spot and strength, wounds and dismemberment,
// sounds, the damage history that names its killer and knocks it out
// instead of killing when the hit was non-lethal), with every stage that
// would compute damage again skipped (the wraps at the end): the pools take
// exactly what the guest's game computed (TandemFight.StrikeLoss).
//
// Both ways: what the host's NPC lost (limbs, poise, consciousness) shows on
// every proxy; the session plays at the host's difficulty; an area effect
// (an explosion, a grenade) is computed on the attacker's game only, each
// NPC's share sent as a strike and each other V's as an AreaHit to that V's
// game; a proxy never deals damage on a guest, a guest's puppet none on the
// host (its game computed it already).

public class TandemStrike {
  public let npc: Uint32;
  public let player: Uint32;
  public let damage: Float;
  public let poise: Float;
  public let minHealth: Float;
  public let flags: Int32;
  public let attackType: Int32;
  public let zone: Int32;
  public let part: Int32;
  public let shape: Int32;
  public let attack: TweakDBID;
  public let position: Vector4;
  public let direction: Vector4;
  // Guest: the proxy's poise (points) when the hit's pipeline began.
  public let poiseBefore: Float;
  // Host: the guest's puppet, whose hit this is.
  public let instigator: wref<GameObject>;
  // Guest: seen by a frame tick already.
  public let stale: Bool;
}

public class TandemOutcomes {
  private let m_game: GameInstance;
  // Our strikes on proxies waiting for their pipeline to end (player 0),
  // and the hits made of other players' strikes on the NPCs we run, in our
  // damage system now (player: whose).
  private let m_hits: array<ref<gameHitEvent>>;
  private let m_strikes: array<ref<TandemStrike>>;
  // Host: the difficulty last sent (-1 none), when we last looked.
  private let m_sentLevel: Int32;
  private let m_lastLevelCheck: Float;
  // Guest: our own difficulty, while the host's is on.
  private let m_ownLevel: gameDifficulty;
  private let m_haveOwn: Bool;
  // Guest: a host wound being put on a proxy (not to be suppressed).
  private let m_replaying: Bool;
  // Host: one-punch slow motions on their way to our V for guests' punches
  // (Relic), until when.
  private let m_slowMoDrops: Int32;
  private let m_slowMoDropUntil: Float;
  // Host: the rules last sent (-1 none). Thrown objects (Throws.reds).
  private let m_sentRules: Int32;
  private let m_throws: ref<TandemThrows>;

  // In a session only (null outside: single player is left alone).
  public static func Of(game: GameInstance) -> ref<TandemOutcomes> {
    let system = TandemCombat.System(game);
    return IsDefined(system) && system.You() != 0u ? system.Outcomes() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_sentLevel = -1;
    this.m_sentRules = -1;
    this.m_throws = new TandemThrows();
    this.m_throws.Start(game);
  }

  public func Throws() -> ref<TandemThrows> {
    return this.m_throws;
  }

  // Every frame in a session (TandemNpcs.Tick).
  public func Tick(now: Float, you: Uint32, npcs: ref<TandemNpcs>) -> Void {
    if TandemNative.IsHost() {
      if now - this.m_lastLevelCheck >= 2.0 {
        this.m_lastLevelCheck = now;
        let level = EnumInt(GameInstance.GetStatsDataSystem(this.m_game).GetDifficulty());
        if level != this.m_sentLevel {
          this.m_sentLevel = level;
          TandemFight.SendDifficulty(level);
          TandemNative.Log(s"outcomes: the session plays at our difficulty \(level)");
        }
        // The session's rules, tandem.ini's (friendly_fire, enemy_scaling,
        // enemy_scaling_health).
        let rules = TandemFight.ConfiguredRules();
        if rules != this.m_sentRules {
          this.m_sentRules = rules;
          TandemFight.SendRules(rules);
          TandemNative.Log(s"outcomes: the session's rules: friendly fire \((rules & 1) != 0 ? "on" : "off"), enemy scaling \(TandemOutcomes.ScalingText(rules & 2, (rules / 256) % 256))");
        }
      }
    }
    // A strike of ours on a proxy whose pipeline has not ended by the next
    // frame (it should not happen): sent as it is.
    let i = 0;
    while i < ArraySize(this.m_strikes) {
      if this.m_strikes[i].player != 0u {
        i += 1;
      } else if this.m_strikes[i].stale {
        this.SendStrike(this.m_strikes[i], 0.0);
        ArrayErase(this.m_strikes, i);
        ArrayErase(this.m_hits, i);
      } else {
        this.m_strikes[i].stale = true;
        i += 1;
      }
    }
    this.Throws().Tick();
    for event in TandemFight.TakeEvents() {
      switch event.kind {
        case 1:
          this.TakeStrike(event, npcs);
          break;
        case 2:
          this.Wound(event, you, npcs);
          break;
        case 3:
          npcs.SetPoise(event.npc, event.poise);
          break;
        case 4:
          this.FollowDifficulty(event.level);
          break;
        case 5:
          this.TakeAreaHit(event, npcs);
          break;
        case 6:
          this.Throws().Arrive(event);
          break;
        case 7:
          this.Throws().Ended(event);
          break;
        case 8:
          TandemNative.Log(s"outcomes: the host's rules: friendly fire \((event.flags & 1) != 0 ? "on" : "off"), enemy scaling \(TandemOutcomes.ScalingText(event.flags & 2, event.level))");
          break;
        case 9:
          this.TakeDefended(event, npcs);
          break;
        case 10:
          npcs.Levels().OnSession(event.level, Cast<Int32>(event.player));
          break;
        case 11:
          // Its owner's word on its level (any game, regions.md): on our
          // proxy of it; one we run has ours already.
          if !npcs.Owns(event.npc) {
            npcs.SetLevel(event.npc, event.damage);
          }
          break;
        case 12:
          npcs.SetScale(event.npc, Cast<Int32>(event.player));
          break;
        case 13:
          this.Throws().OnPath(event);
          break;
        case 14:
          TandemWeakSpots.Take(this.m_game, event, npcs);
          break;
        case 15:
          TandemHacks.Traced(this.m_game, event.npc, event.player, npcs);
          break;
        default:
          break;
      }
    }
  }

  // "on, health +60 % per extra player" or "off", for the log.
  public static func ScalingText(on: Int32, health: Int32) -> String {
    if on == 0 {
      return "off";
    }
    return s"on, health +\(health) % per extra player";
  }

  // Guest: the host's difficulty is on (ours waits): no save may carry it
  // (TandemSystem.IsSavingLocked).
  public func FollowsHost() -> Bool {
    return this.m_haveOwn;
  }

  // test reset: the difficulty and the rules are sent and said again, the
  // throws' once-only lines too (scenarios read them, a session keeps them).
  public func ResayForTests() -> Void {
    this.m_sentLevel = -1;
    this.m_sentRules = -1;
    if IsDefined(this.m_throws) {
      this.m_throws.ResayForTests();
    }
  }

  // The session is over: our own difficulty back.
  public func End() -> Void {
    if this.m_haveOwn {
      GameInstance.GetStatsDataSystem(this.m_game).SetDifficulty(this.m_ownLevel);
      TandemNative.Log(s"outcomes: our own difficulty \(EnumInt(this.m_ownLevel)) back");
      this.m_haveOwn = false;
    }
    this.m_sentLevel = -1;
    this.m_sentRules = -1;
    ArrayClear(this.m_hits);
    ArrayClear(this.m_strikes);
    if IsDefined(this.m_throws) {
      this.m_throws.End();
    }
  }

  // ---- difficulty (coop-perks.md §2.8) ----

  // Guest: the host's difficulty, for everything our game scales by it
  // (one-shot protection, explosions' fixed share, grenades: the target's
  // side of the hits the host forwards; NPC damage itself is scaled on the
  // host, the attacker's side).
  private func FollowDifficulty(level: Int32) -> Void {
    let stats = GameInstance.GetStatsDataSystem(this.m_game);
    let current = stats.GetDifficulty();
    if EnumInt(current) == level {
      return;
    }
    if !this.m_haveOwn {
      this.m_ownLevel = current;
      this.m_haveOwn = true;
    }
    stats.SetDifficulty(IntEnum<gameDifficulty>(level));
    TandemNative.Log(s"outcomes: the host's difficulty \(level) (ours \(EnumInt(this.m_ownLevel)) until we leave)");
  }

  // ---- guest: our strikes ----

  public static func StrikeFlags(hitEvent: ref<gameHitEvent>, damage: Float, health: Float) -> Int32 {
    let data = hitEvent.attackData;
    let flags = 0;
    if data.HasFlag(hitFlag.WeakspotHit) {
      flags += 1;
    }
    if data.HasFlag(hitFlag.CriticalHit) {
      flags += 2;
    }
    if AttackData.IsMelee(data.GetAttackType()) {
      flags += 4;
    }
    if data.HasFlag(hitFlag.Nonlethal) {
      flags += 16;
    }
    // ApplyDamageSingle caps a finisher-grace or bleeding-perk hit at the
    // proxy's health - 1 (it is no killing blow then).
    if !data.HasFlag(hitFlag.WasKillingBlow) && health > 1.0 && damage > 0.0 && damage >= health - 1.001 {
      flags += 32;
    }
    if data.HasFlag(hitFlag.WasKillingBlow) {
      flags += 64;
    }
    if data.HasFlag(hitFlag.Headshot) {
      flags += 128;
    }
    if data.HasFlag(hitFlag.ForceDismember) {
      flags += 256;
    }
    return flags;
  }

  // Our hit on proxy `npc`, measured: kept until its pipeline ends.
  public func Measured(hitEvent: ref<gameHitEvent>, npc: Uint32, damage: Float, flags: Int32) -> Void {
    let strike = new TandemStrike();
    strike.npc = npc;
    strike.damage = damage;
    strike.flags = flags;
    strike.minHealth = hitEvent.attackData.GetMinimumHealthPercent();
    strike.attackType = EnumInt(hitEvent.attackData.GetAttackType());
    strike.zone = 255;
    strike.part = -1;
    strike.shape = -1;
    if ArraySize(hitEvent.hitRepresentationResult.hitShapes) > 0 {
      let data = hitEvent.hitRepresentationResult.hitShapes[0].userData as HitShapeUserDataBase;
      if IsDefined(data) {
        strike.zone = EnumInt(data.m_hitReactionZone);
        strike.part = EnumInt(data.m_dismembermentPart);
        strike.shape = EnumInt(data.m_hitShapeType);
      }
    }
    let definition = hitEvent.attackData.GetAttackDefinition();
    if IsDefined(definition) && IsDefined(definition.GetRecord()) {
      strike.attack = definition.GetRecord().GetID();
    }
    strike.position = hitEvent.hitPosition;
    strike.direction = hitEvent.hitDirection;
    strike.poiseBefore = -1.0;
    let i = ArrayFindFirst(this.m_hits, hitEvent);
    if i >= 0 {
      // Measured twice (an instant kill after its damage): one strike.
      strike.poiseBefore = this.m_strikes[i].poiseBefore;
      strike.flags = strike.flags | this.m_strikes[i].flags;
      this.m_strikes[i] = strike;
      return;
    }
    ArrayPush(this.m_hits, hitEvent);
    ArrayPush(this.m_strikes, strike);
  }

  // A hit on a proxy enters its pipeline: its poise now.
  public func Began(hitEvent: ref<gameHitEvent>) -> Float {
    return GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(hitEvent.target.GetEntityID()), gamedataStatPoolType.Poise, false);
  }

  // Its pipeline ended: the poise it took, and off it goes.
  public func Ended(hitEvent: ref<gameHitEvent>, poiseBefore: Float) -> Void {
    let i = ArrayFindFirst(this.m_hits, hitEvent);
    if i < 0 || this.m_strikes[i].player != 0u {
      return;
    }
    let after = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(hitEvent.target.GetEntityID()), gamedataStatPoolType.Poise, false);
    this.SendStrike(this.m_strikes[i], MaxF(poiseBefore - after, 0.0));
    ArrayErase(this.m_hits, i);
    ArrayErase(this.m_strikes, i);
  }

  private func SendStrike(strike: ref<TandemStrike>, poise: Float) -> Void {
    TandemFight.SendStrike(strike.npc, strike.damage, poise, strike.minHealth, strike.flags, strike.attackType, strike.zone, strike.part, strike.shape, strike.attack, strike.position, strike.direction);
    TandemNative.LogDebug(s"outcomes: strike on proxy \(strike.npc): \(strike.damage) health, \(poise) poise, flags \(strike.flags), zone \(strike.zone), part \(strike.part), attack \(TDBID.ToStringDEBUG(strike.attack))");
  }

  // ---- host: guests' strikes on our NPCs ----

  // Another player's strike on an NPC we run (not one of ours on a proxy).
  public func IsStrike(hitEvent: ref<gameHitEvent>) -> Bool {
    let i = ArrayFindFirst(this.m_hits, hitEvent);
    return i >= 0 && this.m_strikes[i].player != 0u;
  }

  public func Forget(hitEvent: ref<gameHitEvent>) -> Void {
    let i = ArrayFindFirst(this.m_hits, hitEvent);
    if i >= 0 && this.m_strikes[i].player != 0u {
      ArrayErase(this.m_hits, i);
      ArrayErase(this.m_strikes, i);
    }
  }

  private func TakeStrike(event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let body = GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(event.npc)) as ScriptedPuppet;
    if !IsDefined(body) || body.IsDead() {
      TandemNative.Log(s"outcomes: player \(event.player)'s strike on \(event.npc): no live NPC");
      return;
    }
    // Host: a guest's hit on an actor of our V's scene is refused while the
    // scene lasts (Restrictions.reds: a backstop to the guest's own holds).
    let story = TandemStorySystem.Get(this.m_game);
    if IsDefined(story) && IsDefined(story.Restrictions())
      && story.Restrictions().RefusesHit(body, event.npc, event.player, "a strike") {
      return;
    }
    // A fist fight's opponent takes hits from who is in the fight only
    // (Contests.reds).
    let contests = TandemContests.Of(this.m_game);
    if IsDefined(contests) && contests.Refuses(body, event.npc, event.player, TandemNative.You()) {
      contests.Dropped(event.npc, event.player, TandemNative.You());
      return;
    }
    npcs.NoteHitBy(event.npc, event.player);
    let strike = new TandemStrike();
    strike.npc = event.npc;
    strike.player = event.player;
    strike.damage = event.damage;
    strike.poise = event.poise;
    strike.minHealth = event.minHealth;
    strike.flags = event.flags;
    strike.attackType = event.attackType;
    strike.zone = event.zone;
    strike.part = event.part;
    strike.shape = event.shape;
    strike.attack = event.attack;
    strike.position = new Vector4(event.x, event.y, event.z, 1.0);
    strike.direction = new Vector4(event.dx, event.dy, event.dz, 0.0);
    strike.instigator = TandemNpcs.PuppetOf(this.m_game, event.player);
    if !IsDefined(strike.instigator) {
      // No puppet to hit from (not spawned yet): the pools only.
      this.ApplyPools(body, strike, null);
      return;
    }
    let hit = this.StrikeHit(body, strike);
    ArrayPush(this.m_hits, hit);
    ArrayPush(this.m_strikes, strike);
    GameInstance.GetDamageSystem(this.m_game).QueueHitEvent(hit, body);
    // A hostile NPC turns on whoever hit it (Reactions.reds).
    TandemReactions.OnGuestHit(body, strike.instigator);
  }

  // The guest's hit as one of its puppet's on our NPC: its attack, weapon
  // (the puppet holds the guest's), type, flags, the hit shape it landed on,
  // and its damage (for how strongly the NPC reacts).
  private func StrikeHit(body: ref<ScriptedPuppet>, strike: ref<TandemStrike>) -> ref<gameHitEvent> {
    let instigator = strike.instigator;
    let melee = (strike.flags & 4) != 0;
    let record = TweakDBInterface.GetAttackRecord(strike.attack);
    if !IsDefined(record) {
      record = TweakDBInterface.GetAttackRecord(melee ? t"Attacks.ForwardPush" : t"Attacks.REMOVE_BulletWithDamage");
    }
    let weapon = GameObject.GetActiveWeapon(instigator);
    let context: AttackInitContext;
    context.record = record;
    context.instigator = instigator;
    context.source = instigator;
    context.weapon = weapon;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = body;
    hit.hitPosition = strike.position;
    hit.hitDirection = strike.direction;
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    hit.attackData.SetInstigator(instigator);
    hit.attackData.SetSource(instigator);
    hit.attackData.SetWeapon(weapon);
    hit.attackData.SetAttackType(IntEnum<gamedataAttackType>(strike.attackType));
    hit.attackData.SetAttackPosition(instigator.GetWorldPosition());
    // The guest found it hostile: our attitude check must not drop it.
    hit.attackData.AddFlag(hitFlag.FriendlyFire, n"tandem_strike");
    hit.attackData.AddFlag(hitFlag.DontShowDamageFloater, n"tandem_strike");
    if (strike.flags & 1) != 0 {
      hit.attackData.AddFlag(hitFlag.WeakspotHit, n"tandem_strike");
    }
    if (strike.flags & 2) != 0 {
      hit.attackData.AddFlag(hitFlag.CriticalHit, n"tandem_strike");
    }
    if (strike.flags & 16) != 0 {
      hit.attackData.AddFlag(hitFlag.Nonlethal, n"tandem_strike");
    }
    if (strike.flags & 128) != 0 {
      hit.attackData.AddFlag(hitFlag.Headshot, n"tandem_strike");
    }
    if (strike.flags & 256) != 0 {
      hit.attackData.AddFlag(hitFlag.ForceDismember, n"tandem_strike");
    }
    hit.attackComputed = new gameAttackComputed();
    hit.attackComputed.SetAttackValue(strike.damage, gamedataDamageType.Physical);
    // The hit shape: the NPC's hit reaction needs one (GetBodyPart), and
    // reads the zone, the dismemberable part and the kind of shape from it.
    if strike.zone >= 0 && strike.zone < 255 {
      let data = new HitShapeUserDataBase();
      data.m_hitReactionZone = IntEnum<EHitReactionZone>(strike.zone);
      data.m_dismembermentPart = IntEnum<EAIDismembermentBodyPart>(strike.part);
      data.m_hitShapeType = IntEnum<EHitShapeType>(strike.shape);
      data.m_hitShapeTag = strike.zone == 0 ? n"Head" : n"Body";
      let landed: HitShapeResult;
      landed.hitPositionEnter = strike.position;
      landed.hitPositionExit = strike.position;
      let shape: HitShapeData;
      shape.userData = data;
      shape.result = landed;
      let shapes: HitRepresentationQueryResult;
      ArrayPush(shapes.hitShapes, shape);
      hit.hitRepresentationResult = shapes;
    }
    return hit;
  }

  // Host: where DamageSystem.Process would run for a strike's hit. What the
  // guest's game computed goes onto the pools; the damage events the game
  // sends after its own damage follow (damage history: the killer, the
  // non-lethal knock-out; hit reaction strength).
  public func ApplyStrike(hitEvent: ref<gameHitEvent>) -> Void {
    let i = ArrayFindFirst(this.m_hits, hitEvent);
    if i < 0 {
      return;
    }
    let body = hitEvent.target as ScriptedPuppet;
    if !IsDefined(body) || hitEvent.attackData.HasFlag(hitFlag.DealNoDamage) {
      let godMode = GameInstance.GetGodModeSystem(this.m_game);
      let alive = IsDefined(body) && !body.IsDead();
      let invulnerable = IsDefined(body) && godMode.HasGodMode(body.GetEntityID(), gameGodModeType.Invulnerable);
      TandemNative.LogDebug(s"outcomes: player \(this.m_strikes[i].player)'s strike on \(this.m_strikes[i].npc) deals no damage here (alive \(alive), invulnerable \(invulnerable), friendly fire ignored \(hitEvent.attackData.HasFlag(hitFlag.FriendlyFireIgnored)))");
      return;
    }
    let loss = this.ApplyPools(body, this.m_strikes[i], hitEvent.attackData.GetInstigator());
    hitEvent.attackData.AddFlag(hitFlag.SuccessfulAttack, n"tandem_strike");
    if loss > 0.0 {
      let received = new gameDamageReceivedEvent();
      received.totalDamageReceived = loss;
      received.hitEvent = hitEvent;
      body.QueueEvent(received);
    }
  }

  private func ApplyPools(body: ref<ScriptedPuppet>, strike: ref<TandemStrike>, instigator: wref<GameObject>) -> Float {
    let id = Cast<StatsObjectID>(body.GetEntityID());
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let health = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, false);
    let maxHealth = GameInstance.GetStatsSystem(this.m_game).GetStatValue(id, gamedataStatType.Health);
    let immortal = GameInstance.GetGodModeSystem(this.m_game).HasGodMode(body.GetEntityID(), gameGodModeType.Immortal);
    let loss = TandemFight.StrikeLoss(health, maxHealth, strike.damage, strike.flags, strike.minHealth, immortal);
    if (strike.flags & 8) != 0 && !immortal {
      // An instant kill (finisher, Kill flag), as the game does it.
      pools.RequestSettingStatPoolMinValue(id, gamedataStatPoolType.Health, instigator);
    } else if loss > 0.0 {
      pools.RequestChangingStatPoolValue(id, gamedataStatPoolType.Health, -loss, instigator, false, false);
    }
    if strike.poise > 0.0 && body.CanReceivePoiseDamage() {
      pools.RequestChangingStatPoolValue(id, gamedataStatPoolType.Poise, -strike.poise, instigator, false, false);
    }
    TandemNative.LogDebug(s"outcomes: \(strike.npc) struck by player \(strike.player): \(loss) of \(health) health, \(strike.poise) poise, flags \(strike.flags), zone \(strike.zone)");
    // A lethal strike kills, as the guest's game measured it: the health
    // pool alone stops at the defeat mechanic's 0.1 % (NPCDeathListener),
    // and the hit reaction that kills a defeated NPC in vanilla's own hit
    // did not for a strike (2026-11-19: one more hit was needed). A
    // non-lethal one leaves it to the defeat mechanic: knocked out.
    let lethal = !immortal && (strike.flags & 16) == 0 && ((strike.flags & 8) != 0 || (loss > 0.0 && loss >= health - 0.001));
    if lethal {
      if !body.IsDead() {
        body.Kill(instigator, false, false);
      }
      this.Dismember(body, strike, instigator);
    }
    return loss;
  }

  // The limb a killing strike cuts off, by the rules of the game's hit
  // reaction (HitReactionComponent.ProcessWoundsAndDismemberment,
  // GetDismembermentWoundType, DismembermentConditions): a forced
  // dismemberment, an explosion, or a blade's melee kill cuts the part the
  // strike landed on (not the torso, but by an explosion); a blade's wound
  // is clean, other melee a hole, the rest coarse. Through the wrapped
  // RequestDismemberment, so every proxy loses it too (NpcWound).
  private func Dismember(body: ref<ScriptedPuppet>, strike: ref<TandemStrike>, instigator: wref<GameObject>) -> Void {
    // The hit shape's part (a script enum, -1..5) as the dismemberment's
    // (native, other values), by name as HitShapeUserDataBase does.
    let shape = new HitShapeUserDataBase();
    shape.m_dismembermentPart = IntEnum<EAIDismembermentBodyPart>(strike.part);
    if strike.part < 0 || strike.part > 5 {
      return;
    }
    let part = HitShapeUserDataBase.GetDismembermentBodyPart(shape);
    let attackType = IntEnum<gamedataAttackType>(strike.attackType);
    let melee = (strike.flags & 4) != 0;
    let explosion = AttackData.IsDismembermentCause(attackType);
    let clean = false;
    let weapon = IsDefined(instigator) ? ScriptedPuppet.GetWeaponRight(instigator) : null;
    if IsDefined(weapon) && IsDefined(weapon.GetWeaponRecord()) {
      let kind = weapon.GetWeaponRecord().ItemType().Type();
      clean = Equals(kind, gamedataItemType.Cyb_NanoWires) || Equals(kind, gamedataItemType.Cyb_MantisBlades) || Equals(kind, gamedataItemType.Wea_Katana) || Equals(kind, gamedataItemType.Wea_ShortBlade) || Equals(kind, gamedataItemType.Wea_LongBlade) || Equals(kind, gamedataItemType.Wea_Axe) || Equals(kind, gamedataItemType.Wea_Chainsword) || Equals(kind, gamedataItemType.Wea_Machete) || Equals(kind, gamedataItemType.Wea_Sword);
    }
    let forced = (strike.flags & 256) != 0;
    if !forced && !explosion && !(melee && clean) {
      return;
    }
    if Equals(part, gameDismBodyPart.BODY) && !explosion {
      return; // the torso: only an explosion takes it apart
    }
    let wound = clean ? gameDismWoundType.CLEAN : melee ? gameDismWoundType.HOLE : gameDismWoundType.COARSE;
    DismembermentComponent.RequestDismemberment(body, part, wound, strike.position);
    TandemNative.Log(s"outcomes: \(strike.npc)'s killing strike cuts part \(TandemOutcomes.PartIndex(part)) (wound \(TandemOutcomes.WoundIndex(wound)), weapon \(IsDefined(weapon) ? TDBID.ToStringDEBUG(ItemID.GetTDBID(weapon.GetItemID())) : "none"))");
  }

  // ---- wounds: the host decides, every proxy shows ----

  // DismembermentComponent.RequestDismemberment (wrapped below): false to
  // leave it out.
  public func OnDismember(obj: ref<GameObject>, part: gameDismBodyPart, wound: gameDismWoundType, at: Vector4) -> Bool {
    let system = TandemCombat.System(this.m_game);
    if !IsDefined(system) || system.You() == 0u {
      return true;
    }
    let own = system.Npcs();
    if IsDefined(own) && own.OwnsEntity(obj.GetEntityID()) {
      let npc = system.SharedNpcOf(obj.GetEntityID());
      if npc != 0u {
        let by = 0u;
        let npcBody = obj as NPCPuppet;
        if IsDefined(npcBody) {
          let hitter = npcBody.GetLastHitInstigator();
          if IsDefined(hitter) {
            by = hitter.IsPlayer() ? system.You() : system.PlayerOfPuppet(hitter.GetEntityID());
          }
        }
        let index = TandemOutcomes.PartIndex(part);
        if index >= 0 {
          TandemFight.SendWound(npc, by, index, TandemOutcomes.WoundIndex(wound), at);
          TandemNative.Log(s"outcomes: \(npc) loses part \(index) (wound \(TandemOutcomes.WoundIndex(wound)), player \(by)'s hit)");
        } else {
          TandemNative.Log(s"outcomes: \(npc) loses parts \(EnumInt(part)) (not one limb: not sent)");
        }
      }
      return true;
    }
    // A proxy's limbs are its owner's to cut.
    return this.m_replaying || !IsDefined(own) || !own.IsProxyEntity(obj.GetEntityID());
  }

  private func Wound(event: TandemFightEvent, you: Uint32, npcs: ref<TandemNpcs>) -> Void {
    let proxy = GameInstance.GetDynamicEntitySystem().GetEntity(npcs.AnyProxy(event.npc)) as GameObject;
    if !IsDefined(proxy) {
      return;
    }
    let part = TandemOutcomes.PartOf(event.part);
    let wound = TandemOutcomes.WoundOf(event.shape);
    let at = new Vector4(event.x, event.y, event.z, 1.0);
    if event.player == you {
      // Our hit: the game's own request (its sound, our on-dismember perk).
      this.m_replaying = true;
      DismembermentComponent.RequestDismemberment(proxy, part, wound, at);
      this.m_replaying = false;
    } else {
      let cut = new DismembermentEvent();
      cut.m_bodyPart = part;
      cut.m_woundType = wound;
      cut.m_strength = 8.0;
      proxy.QueueEvent(cut);
    }
    TandemNative.Log(s"outcomes: proxy \(event.npc) loses part \(event.part) (wound \(event.shape), player \(event.player)'s hit)");
  }

  // ---- the Relic tree (docs/research/coop-perks.md §4) ----

  // Status effects the Relic's arm attacks put on their target without
  // naming an instigator: the Mantis Blades' leap dismemberment
  // (DamageSystem, final.reds 85964), the Gorilla Arms' and the Mantis
  // Blades' grand finale marks (NPCPuppet.OnApplyRelicMeleewareDamageOnNPCEvent,
  // 128094, 128101). On a guest they land on a proxy and reach the host's
  // NPC like any effect of our hit (Combat.reds). The marks act on a dead
  // or defeated NPC (NPCPuppet.OnStatusEffectApplied, 126466-126475: the
  // one-punch ragdoll, the grand finale's dismemberment), so the host puts
  // them on its NPC dead or not (Npcs.reds ApplyEffect).
  public static func IsRelicMark(effect: TweakDBID) -> Bool {
    return effect == t"BaseStatusEffect.MantisBladesRelicDismemberment" || effect == t"BaseStatusEffect.GorillaArmsOnePunchNPCMark" || effect == t"BaseStatusEffect.MantisBladesGrandFinaleNPCMark";
  }

  // The session NPC a proxy stands for, dead (our hit killed it ahead of
  // its owner, Npcs.reds PredictKill) or alive; 0 for one we run (any game
  // shows proxies now, regions.md: the host too).
  public static func ProxyEvenDead(game: GameInstance, proxy: ref<GameObject>) -> Uint32 {
    let system = TandemCombat.System(game);
    let npcs = IsDefined(system) && system.You() != 0u ? system.Npcs() : null;
    return IsDefined(npcs) && npcs.IsProxyEntity(proxy.GetEntityID()) ? npcs.NpcOf(proxy.GetEntityID()) : 0u;
  }

  // The NPC's owner (the host, or a guest for an NPC of its own,
  // regions.md): another V's one-punch mark landing on a dead or defeated
  // NPC we run, which sends the one-punch slow motion to "the player"
  // (126466-126472): our V, for a punch that is not ours. The puncher's own
  // game gave it to its V already (a local slow motion, Time.reds).
  public static func IsGuestsOnePunch(npc: ref<NPCPuppet>, evt: ref<ApplyStatusEffectEvent>) -> Bool {
    if !IsDefined(evt.staticData) || !evt.staticData.GameplayTagsContains(n"OnePunchedMark") || !TandemOwn.Entity(npc.GetGame(), npc.GetEntityID()) {
      return false;
    }
    if !npc.IsDead() && !ScriptedPuppet.IsDefeated(npc) && !npc.IsAboutToDieOrDefeated() {
      return false;
    }
    let system = TandemCombat.System(npc.GetGame());
    return IsDefined(system) && system.PlayerOfPuppet(evt.instigatorEntityID) != 0u;
  }

  public func DropOnePunchSlowMo() -> Void {
    let now = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    if now > this.m_slowMoDropUntil {
      this.m_slowMoDrops = 0;
    }
    this.m_slowMoDrops += 1;
    this.m_slowMoDropUntil = now + TDB.GetFloat(t"Items.StrongArms.slowMoDelay") + 1.0;
  }

  public func DropsOnePunchSlowMo() -> Bool {
    let now = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    if this.m_slowMoDrops <= 0 || now > this.m_slowMoDropUntil {
      this.m_slowMoDrops = 0;
      return false;
    }
    this.m_slowMoDrops -= 1;
    return true;
  }

  // The NPC's owner: an instant dismemberment (StatusEffectManagerComponent,
  // 321244: any effect tagged Dismember, as the grand finale's) on an NPC we
  // run whose V is another player: the effect named them, or their hit was
  // the NPC's last.
  public static func IsGuestsDismemberment(owner: ref<GameObject>, instigator: EntityID) -> Bool {
    let system = IsDefined(owner) && TandemOwn.Entity(owner.GetGame(), owner.GetEntityID()) ? TandemCombat.System(owner.GetGame()) : null;
    if !IsDefined(system) || system.You() == 0u {
      return false;
    }
    if EntityID.IsDefined(instigator) {
      return system.PlayerOfPuppet(instigator) != 0u;
    }
    let npc = owner as NPCPuppet;
    let hitter = IsDefined(npc) ? npc.GetLastHitInstigator() : null;
    return IsDefined(hitter) && system.PlayerOfPuppet(hitter.GetEntityID()) != 0u;
  }

  // ---- limbs on the wire ----
  // gameDismBodyPart and gameDismWoundType are native enums whose values
  // are not the scripts' 0..5 (LEFT_ARM is 14, 2026-11-19): NpcWound carries
  // the script order, mapped by name both ways.

  public static func PartIndex(part: gameDismBodyPart) -> Int32 {
    if Equals(part, gameDismBodyPart.LEFT_ARM) {
      return 0;
    }
    if Equals(part, gameDismBodyPart.RIGHT_ARM) {
      return 1;
    }
    if Equals(part, gameDismBodyPart.LEFT_LEG) {
      return 2;
    }
    if Equals(part, gameDismBodyPart.RIGHT_LEG) {
      return 3;
    }
    if Equals(part, gameDismBodyPart.HEAD) {
      return 4;
    }
    if Equals(part, gameDismBodyPart.BODY) {
      return 5;
    }
    return -1;
  }

  public static func PartOf(index: Int32) -> gameDismBodyPart {
    switch index {
      case 0:
        return gameDismBodyPart.LEFT_ARM;
      case 1:
        return gameDismBodyPart.RIGHT_ARM;
      case 2:
        return gameDismBodyPart.LEFT_LEG;
      case 3:
        return gameDismBodyPart.RIGHT_LEG;
      case 4:
        return gameDismBodyPart.HEAD;
      default:
        return gameDismBodyPart.BODY;
    }
  }

  public static func WoundIndex(wound: gameDismWoundType) -> Int32 {
    if Equals(wound, gameDismWoundType.CLEAN) {
      return 0;
    }
    if Equals(wound, gameDismWoundType.COARSE) {
      return 1;
    }
    return 2;
  }

  public static func WoundOf(index: Int32) -> gameDismWoundType {
    switch index {
      case 0:
        return gameDismWoundType.CLEAN;
      case 1:
        return gameDismWoundType.COARSE;
      default:
        return gameDismWoundType.HOLE;
    }
  }

  // ---- a V keeps its limbs (the wraps at the end) ----

  // Whether `obj` is a V (ours, or a remote player's puppet): it is never
  // cut or crippled. Logged as `why` for a puppet.
  public static func KeepsLimbs(obj: wref<GameObject>, why: String) -> Bool {
    if !IsDefined(obj) {
      return false;
    }
    if obj.IsPlayer() {
      // Solo is vanilla: our V's limbs are ours to keep only in a session.
      return TandemNative.IsStarted();
    }
    let player = TandemCombat.PuppetPlayer(obj.GetGame(), obj);
    if player == 0u {
      return false;
    }
    TandemNative.Log(s"outcomes: player \(player)'s puppet keeps its limbs (\(why))");
    return true;
  }

  // A crippled or dismembered status effect (HitReactionComponent's
  // BaseStatusEffect.Crippled*, Dismembered*): never on a V.
  public static func IsLimbEffect(record: wref<StatusEffect_Record>) -> Bool {
    if !IsDefined(record) {
      return false;
    }
    if IsDefined(record.StatusEffectType()) && Equals(record.StatusEffectType().Type(), gamedataStatusEffectType.Crippled) {
      return true;
    }
    let name = TDBID.ToStringDEBUG(record.GetID());
    return StrBeginsWith(name, "BaseStatusEffect.Crippled") || StrBeginsWith(name, "BaseStatusEffect.Dismembered");
  }

  // (test, tools/game.sh cmd puppet limbs) every way a limb could go, on
  // a remote player's puppet: each one must be refused.
  public static func TestLimbs(game: GameInstance, puppet: ref<GameObject>) -> Void {
    if !IsDefined(puppet) {
      TandemNative.Log("puppet limbs: no puppet");
      return;
    }
    let at = puppet.GetWorldPosition();
    DismembermentComponent.RequestDismemberment(puppet, gameDismBodyPart.LEFT_ARM, gameDismWoundType.CLEAN, at);
    let cut = new DismembermentEvent();
    cut.m_bodyPart = gameDismBodyPart.RIGHT_LEG;
    cut.m_woundType = gameDismWoundType.COARSE;
    cut.m_strength = 8.0;
    puppet.QueueEvent(cut);
    let blast = new DismembermentExplosionEvent();
    blast.m_epicentrum = at;
    blast.m_strength = 50.0;
    puppet.QueueEvent(blast);
    StatusEffectHelper.ApplyStatusEffect(puppet, t"BaseStatusEffect.CrippledLegLeft");
    TandemNative.Log("puppet limbs: dismemberment request, event, explosion and a crippled leg sent to the puppet");
  }

  // ---- a V's hits on another V: the attacker's game, once ----

  // Our V's hit (a shot, a blade, an explosion, a pressure wave: a grenade,
  // a barrel we set off) reaching another V's puppet: that V's game applies
  // it, while the session's rules allow friendly fire (TandemFight
  // .FriendlyFire, the host's tandem.ini). Off, it is dropped here as
  // vanilla drops a hit on a friend (CheckForQuickExit).
  public static func IsOurHit(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let instigator = hitEvent.attackData.GetInstigator();
    let player = GetPlayer(game);
    return IsDefined(instigator) && IsDefined(player) && instigator.GetEntityID() == player.GetEntityID() && !AttackData.IsDoT(hitEvent.attackData);
  }

  // The area ones (kept for the target-side stages, Combat.reds OnPuppet).
  public static func IsOurArea(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    return TandemOutcomes.IsOurHit(game, hitEvent) && AttackData.IsAreaOfEffect(hitEvent.attackData.GetAttackType());
  }

  // Our hit on another V's puppet goes to their game: friendly fire on.
  public static func HurtsPartner(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    return TandemOutcomes.IsOurHit(game, hitEvent) && TandemFight.FriendlyFire();
  }

  public static func SendAreaHit(game: GameInstance, victim: Uint32, hitEvent: ref<gameHitEvent>) -> Void {
    let values = hitEvent.attackComputed;
    let weapon = hitEvent.attackData.GetWeapon();
    let weaponId = TDBID.None();
    if IsDefined(weapon) {
      weaponId = ItemID.GetTDBID(weapon.GetItemID());
    }
    let attackId = TDBID.None();
    let definition = hitEvent.attackData.GetAttackDefinition();
    if IsDefined(definition) && IsDefined(definition.GetRecord()) {
      attackId = definition.GetRecord().GetID();
    }
    let physical = values.GetAttackValue(gamedataDamageType.Physical);
    let thermal = values.GetAttackValue(gamedataDamageType.Thermal);
    let electric = values.GetAttackValue(gamedataDamageType.Electric);
    let chemical = values.GetAttackValue(gamedataDamageType.Chemical);
    TandemFight.SendAreaHit(victim, physical, thermal, electric, chemical, EnumInt(hitEvent.attackData.GetAttackType()), TandemCombat.Flags(hitEvent), hitEvent.hitPosition, hitEvent.hitDirection, attackId, weaponId);
    TandemNative.Log(s"outcomes: our hit (type \(EnumInt(hitEvent.attackData.GetAttackType()))) on player \(victim)'s puppet: \(physical) physical, \(thermal) thermal, sent to their game (friendly fire on)");
  }

  // Another V's hit on our V (friendly fire on): run here as a hit from
  // their puppet, by our rules (our armour, perks, block, dodge, difficulty:
  // the target's side).
  private func TakeAreaHit(event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let player = GetPlayer(this.m_game);
    let attacker = TandemNpcs.PuppetOf(this.m_game, event.player);
    if !IsDefined(player) {
      return;
    }
    if !TandemFight.FriendlyFire() {
      TandemNative.Log(s"outcomes: player \(event.player)'s hit on our V dropped: friendly fire is off");
      return;
    }
    let record = TweakDBInterface.GetAttackRecord(event.attack);
    if !IsDefined(record) {
      record = TweakDBInterface.GetAttackRecord(t"Attacks.REMOVE_BulletWithDamage");
    }
    let source: ref<GameObject> = player;
    if IsDefined(attacker) {
      source = attacker;
    }
    let context: AttackInitContext;
    context.record = record;
    context.instigator = attacker;
    context.source = source;
    let hit = new gameHitEvent();
    hit.attackData = new AttackData();
    hit.target = player;
    hit.hitPosition = new Vector4(event.x, event.y, event.z, 1.0);
    hit.hitDirection = new Vector4(event.dx, event.dy, event.dz, 0.0);
    hit.attackData.SetAttackDefinition(IAttack.Create(context));
    hit.attackData.SetInstigator(attacker);
    hit.attackData.SetSource(source);
    hit.attackData.SetAttackType(IntEnum<gamedataAttackType>(event.attackType));
    hit.attackData.SetAttackPosition(hit.hitPosition);
    // From a friend: our friendly-fire check must not drop it.
    hit.attackData.AddFlag(hitFlag.FriendlyFire, n"tandem_area_hit");
    hit.attackData.AddFlag(hitFlag.CanDamageSelf, n"tandem_area_hit");
    if (event.flags & 2) != 0 {
      hit.attackData.AddFlag(hitFlag.CriticalHit, n"tandem_area_hit");
    }
    hit.attackComputed = new gameAttackComputed();
    hit.attackComputed.SetAttackValue(event.physical, gamedataDamageType.Physical);
    hit.attackComputed.SetAttackValue(event.thermal, gamedataDamageType.Thermal);
    hit.attackComputed.SetAttackValue(event.electric, gamedataDamageType.Electric);
    hit.attackComputed.SetAttackValue(event.chemical, gamedataDamageType.Chemical);
    npcs.Forward(hit, 0.0);
    GameInstance.GetDamageSystem(this.m_game).QueueHitEvent(hit, player);
    TandemNative.Log(s"outcomes: player \(event.player)'s area effect on our V: \(event.physical) physical, \(event.thermal) thermal, type \(event.attackType)");
  }

  // ---- a guest's defence against our NPC (host) ----

  // A guest blocked (level 1) or parried (2) our NPC's attack on its V, by
  // its game's rules: our NPC gets what vanilla's attacker gets then
  // (DamageManager.ProcessDefensiveState: the animation event, the AI event
  // and, for a block, the signal), so it recoils, staggers or changes its
  // next move as it would against our own V.
  private func TakeDefended(event: TandemFightEvent, npcs: ref<TandemNpcs>) -> Void {
    let body = GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(event.npc)) as ScriptedPuppet;
    if !IsDefined(body) || body.IsDead() {
      return;
    }
    let ai = new AIEvent();
    if event.level == 2 {
      AnimationControllerComponent.PushEvent(body, n"myAttackParried");
      ai.name = n"MyAttackParried";
    } else {
      AnimationControllerComponent.PushEvent(body, n"myAttackBlocked");
      ScriptedPuppet.SendActionSignal(body, n"BlockSignal", 0.3);
      ai.name = n"MyAttackBlocked";
    }
    body.QueueEvent(ai);
    TandemNative.Log(s"outcomes: player \(event.player) \(event.level == 2 ? "parried" : "blocked") \(event.npc)'s attack: it gets the recoil");
  }

  // Any game: a proxy's attack on our V (its owner's hit, PlayerHit) ended
  // blocked or parried here (ProcessPipeline, Combat.reds): the NPC's owner
  // hears of it (the host, or a guest for an NPC of its own, regions.md).
  public static func ReportDefence(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Void {
    if !IsDefined(hitEvent.attackData) {
      return;
    }
    let player = GetPlayer(game);
    let instigator = hitEvent.attackData.GetInstigator();
    if !IsDefined(player) || !IsDefined(instigator) || hitEvent.target != player {
      return;
    }
    let npc = TandemCombat.ProxyNpc(game, instigator);
    if npc == 0u {
      return;
    }
    let data = hitEvent.attackData;
    let how = 0;
    if data.HasFlag(hitFlag.WasDeflected) || data.HasFlag(hitFlag.WasBulletParried) || data.HasFlag(hitFlag.WasBulletDeflected) {
      how = 2;
    } else if data.WasBlocked() || data.HasFlag(hitFlag.WasBulletBlocked) {
      how = 1;
    }
    if how != 0 {
      TandemFight.SendDefended(npc, how);
      TandemNative.Log(s"outcomes: npc \(npc)'s attack \(how == 2 ? "parried" : "blocked") by our V, its owner told");
    }
  }

  // ---- one game computes each hit ----

  // A hit from a remote V's puppet (or, on the host, the copy of a car a
  // guest drives) that is not one of its strikes (host) nor another V's hit
  // forwarded to our V (AreaHit): an effect, an explosion, a collision, a
  // thrown object's copy (Throws.reds) that the V's own game computed
  // already and sends as strikes or AreaHit. Dropped, on every game.
  public static func IsRemoteComputed(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let instigator = hitEvent.attackData.GetInstigator();
    let system = TandemCombat.System(game);
    if !IsDefined(instigator) || !IsDefined(system) || system.You() == 0u {
      return false;
    }
    let id = instigator.GetEntityID();
    if system.PlayerOfPuppet(id) == 0u && (!TandemNative.IsHost() || system.PlayerOfCar(id) == 0u) {
      return false;
    }
    if system.IsForwardedHit(hitEvent) {
      return false;
    }
    // The game that runs the target replays the V's strikes on it: those
    // stand. An NPC's owner runs it (regions.md); the host runs the rest of
    // its world, but not a proxy of a guest's NPC.
    let target = IsDefined(hitEvent.target) ? hitEvent.target.GetEntityID() : EntityID();
    let runs = TandemOwn.Entity(game, target) || (TandemNative.IsHost() && !TandemOwn.IsProxy(game, target));
    let outcomes = system.Outcomes();
    return !runs || !IsDefined(outcomes) || !outcomes.IsStrike(hitEvent);
  }

  // Guest: a proxy's hit that the host did not send (a death explosion, an
  // effect running on the proxy, the copy of its grenade): the host's NPC is
  // the one that attacks, and its hits on our V come as PlayerHit (on other
  // NPCs they are the host's). Dropped; but for the shots the host asked our
  // game to fire from it at our V (Shots.reds), which are ours to decide.
  public static func IsProxyComputed(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let player = GetPlayer(game);
    if !IsDefined(player) || !IsDefined(hitEvent.target) {
      return false;
    }
    let system = TandemCombat.System(game);
    if !IsDefined(system) || system.You() == 0u || system.IsForwardedHit(hitEvent) {
      return false;
    }
    let shots = TandemShots.Of(game);
    if hitEvent.target.GetEntityID() == player.GetEntityID() && IsDefined(shots) && shots.IsSimulatedHit(hitEvent) {
      return false;
    }
    let npcs = system.Npcs();
    let instigator = hitEvent.attackData.GetInstigator();
    let source = hitEvent.attackData.GetSource();
    return IsDefined(npcs) && ((IsDefined(instigator) && npcs.IsProxyEntity(instigator.GetEntityID())) || (IsDefined(source) && npcs.IsProxyEntity(source.GetEntityID())));
  }
}

// The host's limbs and wounds for every proxy (TandemOutcomes.OnDismember).
@wrapMethod(DismembermentComponent)
public final static func RequestDismemberment(obj: ref<GameObject>, bodyPart: gameDismBodyPart, woundType: gameDismWoundType, opt hitPosition: Vector4, opt isCritical: Bool, opt debrisPath: String, opt debrisStrength: Float) {
  if TandemOutcomes.KeepsLimbs(obj, s"request, part \(TandemOutcomes.PartIndex(bodyPart))") {
    return;
  }
  let outcomes = IsDefined(obj) ? TandemOutcomes.Of(obj.GetGame()) : null;
  if IsDefined(outcomes) && !outcomes.OnDismember(obj, bodyPart, woundType, hitPosition) {
    return;
  }
  wrappedMethod(obj, bodyPart, woundType, hitPosition, isCritical, debrisPath, debrisStrength);
}

// ---- host: a strike's hit skips every stage that computes damage ----
// (final.reds 84290 ProcessPipeline: PreProcess, Process, ProcessHitReaction,
// PostProcess; the guest's game ran the attacker's and the target's sides.)

@wrapMethod(DamageSystem)
private final func ModifyHitData(hitEvent: ref<gameHitEvent>) {
  let outcomes = TandemOutcomes.Of(GetGameInstance());
  if IsDefined(outcomes) && outcomes.IsStrike(hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

@wrapMethod(DamageSystem)
private final func Process(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) {
  let outcomes = TandemOutcomes.Of(GetGameInstance());
  if IsDefined(outcomes) && outcomes.IsStrike(hitEvent) {
    outcomes.ApplyStrike(hitEvent);
    return;
  }
  wrappedMethod(hitEvent, cache);
}

// The attack's status effects are the guest's (NpcEffect), its returned
// damage and poise too.
@wrapMethod(DamageSystem)
private final func PostProcess(hitEvent: ref<gameHitEvent>) {
  let outcomes = TandemOutcomes.Of(GetGameInstance());
  if IsDefined(outcomes) && outcomes.IsStrike(hitEvent) {
    return;
  }
  wrappedMethod(hitEvent);
}

// Our V's hit on another V's puppet, friendly fire on: friendly or not, it
// goes on to DealDamages, where it is sent to that V's game. Off: vanilla
// drops it here, as a hit on any friend.
@wrapMethod(DamageSystem)
private final func CheckForQuickExit(hitEvent: ref<gameHitEvent>, cache: ref<CacheData>) -> Bool {
  let game = GetGameInstance();
  if IsDefined(hitEvent.attackData) && TandemCombat.PuppetPlayer(game, hitEvent.target) != 0u && TandemOutcomes.HurtsPartner(game, hitEvent) {
    hitEvent.attackData.AddFlag(hitFlag.FriendlyFire, n"tandem_friendly_fire");
  }
  return wrappedMethod(hitEvent, cache);
}

// ---- a V keeps its limbs ----
// Vanilla's V cannot lose a limb or be crippled: PlayerPuppet has neither a
// HitReactionComponent nor a DismembermentComponent. A remote player's
// puppet is built on an NPC body, which has both, and no record field or
// stat turns them off (none in the 2.50 scripts). So every way in is shut
// for a V here: the hit reaction's wounds and dismemberment, the
// dismemberment requests and events (and their explosion and guts), and
// the crippled and dismembered status effects (Combat.reds).

@wrapMethod(HitReactionComponent)
protected final func ProcessWoundsAndDismemberment() {
  if TandemOutcomes.KeepsLimbs(this.GetOwner(), "hit reaction") {
    return;
  }
  wrappedMethod();
}

@wrapMethod(DismembermentComponent)
protected cb func OnDismemberment(evt: ref<DismembermentEvent>) -> Bool {
  if TandemOutcomes.KeepsLimbs(this.GetEntity() as GameObject, "event") {
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(DismembermentComponent)
protected cb func OnDismembermentExplosion(evt: ref<DismembermentExplosionEvent>) -> Bool {
  if TandemOutcomes.KeepsLimbs(this.GetEntity() as GameObject, "explosion") {
    return true;
  }
  return wrappedMethod(evt);
}

@wrapMethod(DismembermentComponent)
protected cb func OnDismembermentDebris(evt: ref<DismembermentDebrisEvent>) -> Bool {
  if TandemOutcomes.KeepsLimbs(this.GetEntity() as GameObject, "debris") {
    return true;
  }
  return wrappedMethod(evt);
}

// ---- the Relic tree on the host (coop-perks.md §4) ----

// A guest's one-punch (Combat.reds marks it): its slow motion was theirs.
@wrapMethod(PlayerPuppet)
protected cb func OnSetSlowMoForOnePunchAttackEvent(evt: ref<SetSlowMoForOnePunchAttackEvent>) -> Bool {
  TandemNative.HookRan(n"Outcomes.PlayerPuppet.OnSetSlowMoForOnePunchAttackEvent");
  let outcomes = TandemOutcomes.Of(this.GetGame());
  if IsDefined(outcomes) && outcomes.DropsOnePunchSlowMo() {
    TandemNative.Log("outcomes: a guest's one-punch slow motion kept off our V");
    return true;
  }
  return wrappedMethod(evt);
}

// An instant dismemberment (the Relic's Mantis Blades): never on a V; on
// the NPC's owner, for another V's kill, the blood it puts on "the
// player's" screen (the local player, 321266) is not ours.
@wrapMethod(StatusEffectManagerComponent)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  let owner = this.GetOwner();
  let dismember = IsDefined(evt.staticData) && evt.staticData.GameplayTagsContains(n"Dismember");
  if dismember && TandemOutcomes.KeepsLimbs(owner, s"instant dismemberment \(TDBID.ToStringDEBUG(evt.staticData.GetID()))") {
    return true;
  }
  let guests = dismember && TandemOutcomes.IsGuestsDismemberment(owner, evt.instigatorEntityID);
  let result = wrappedMethod(evt);
  if guests {
    let player = GetPlayer(owner.GetGame());
    if IsDefined(player) {
      GameObjectEffectHelper.StopEffectEvent(player, n"blood_onscreen");
    }
    TandemNative.Log(s"outcomes: a guest's instant dismemberment (\(TDBID.ToStringDEBUG(evt.staticData.GetID()))): no blood on our screen");
  }
  return result;
}
