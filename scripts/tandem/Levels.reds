// Levels (docs/architecture/progression.md). Each V keeps its own level; the
// enemies the host runs are scaled to the mean of the levels of the players
// in the session instead of the host's (decided 2026-11-18).
//
// Every game tells the host its V's level (PlayerLevel). The host's endpoint
// works out the mean (SessionLevel, to everyone). The host's game moves each
// NPC it shares from "scaled to the host" to "scaled to the session" with the
// game's own tools: the NPC's content assignment holds the curve that turns a
// player level into its level (the journal reads it the same way,
// final.reds:357928), and its PowerLevel takes one additive stat modifier for
// the difference (what the game's own ScaleNPCsToPlayerLevel does,
// final.reds:82742). Health, armour and damage follow the stat. The modifier
// is a plain AddModifier: never saved (the saved kind is AddSavedModifier).
// An NPC of a constant level, or with no content assignment, keeps its level.
//
// The host then says each NPC's level (NpcLevel) and every guest gives the
// proxy the same, so a hit is computed against the same health and armour on
// both games and an NPC's skill XP is worth the same to everyone. A guest
// that owns NPCs (regions.md) does the same for them: its own NPCs are
// scaled from its V's level to the session's, their levels said, and every
// game linked with it gives its proxies the same (the owner's word, wherever
// the owner is).

public class TandemLevels {
  private let m_game: GameInstance;
  // Our V's level as last told to the host (0: not yet), and when.
  private let m_sent: Int32;
  private let m_sentAt: Float;
  private let m_lastOwn: Float;
  // The session's level and how many players it is the mean of (0: not told).
  private let m_session: Int32;
  private let m_players: Int32;
  // Host: moved on whenever the answer may change (the session's level, our
  // own): each NPC is worked out again once (TandemNpcKnown.levelGen).
  private let m_gen: Int32;
  private let m_own: Float;
  private let m_max: Float;
  // Host: the modifiers on now, and what was decided for the NPCs seen.
  private let m_on: Int32;
  private let m_scaled: Int32;
  private let m_fixed: Int32;
  private let m_unassigned: Int32;
  // Each shown NPC's level as its owner said (kept for a proxy not there yet).
  private let m_npcs: array<Uint32>;
  private let m_npcLevels: array<Float>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_gen = 1;
    this.m_max = 0.0;
  }

  public static func LevelOf(game: GameInstance, id: EntityID) -> Float {
    return GameInstance.GetStatsSystem(game).GetStatValue(Cast<StatsObjectID>(id), gamedataStatType.PowerLevel);
  }

  // test reset: our V's level said and sent again.
  public func ResayForTests() -> Void {
    this.m_sent = 0;
  }

  // Our V's level, to the host: when it changes, at each session's start,
  // and every half minute (a repeat changes nothing there).
  public func Tick(now: Float) -> Void {
    if now - this.m_lastOwn < 1.0 {
      return;
    }
    this.m_lastOwn = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let own = TandemLevels.LevelOf(this.m_game, player.GetEntityID());
    let level = RoundF(own);
    if level < 1 {
      return;
    }
    if AbsF(own - this.m_own) >= 0.01 {
      this.m_own = own;
      this.m_gen += 1;
    }
    if level != this.m_sent || now - this.m_sentAt >= 30.0 {
      if level != this.m_sent {
        TandemNative.Log(s"levels: our V is level \(level)");
      }
      this.m_sent = level;
      this.m_sentAt = now;
      TandemProgress.SendOwnLevel(level);
    }
  }

  // The session's level (TandemFightEvent kind 10).
  public func OnSession(level: Int32, players: Int32) -> Void {
    if level == this.m_session && players == this.m_players {
      return;
    }
    this.m_session = level;
    this.m_players = players;
    this.m_gen += 1;
    TandemNative.Log(s"levels: the session's level is \(level) (\(players) players, ours \(this.m_sent))");
    if players > 1 && this.m_sent > 0 && level != this.m_sent {
      TandemCoop.Notice(this.m_game, "Levels-Scaled", s"\(level)");
    }
  }

  public func Session() -> Int32 {
    return this.m_session;
  }

  // ---- the owner (the host, or a guest for its own NPCs) ----

  // One of our NPCs, each round it is shared: scaled to the session's level,
  // and its level told to the games that show it when it is first known or
  // changes.
  public func Host(known: ref<TandemNpcKnown>, npc: ref<ScriptedPuppet>) -> Void {
    if known.levelGen == this.m_gen {
      return;
    }
    known.levelGen = this.m_gen;
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let id = Cast<StatsObjectID>(npc.GetEntityID());
    let level = stats.GetStatValue(id, gamedataStatType.PowerLevel);
    // Alone, or not told yet: as our game made it.
    let session: Float = this.m_players > 1 ? Cast<Float>(this.m_session) : 0.0;
    let atHost: Float = -1.0;
    let atSession: Float = -1.0;
    let fixed = true;
    let kind = 0;
    let record = TweakDBInterface.GetCharacterRecord(npc.GetRecordID());
    let assignment: wref<ContentAssignment_Record>;
    if IsDefined(record) {
      assignment = record.ContentAssignment();
    }
    if IsDefined(assignment) {
      let curve = assignment.PowerLevelMod() as CurveStatModifier_Record;
      if IsDefined(curve) {
        // The game's own scaling: the level this content gives a player of
        // a given level.
        fixed = false;
        kind = 1;
        if session > 0.0 {
          let data = GameInstance.GetStatsDataSystem(this.m_game);
          atHost = data.GetValueFromCurve(StringToName(curve.Id()), this.m_own, StringToName(curve.Column()));
          atSession = data.GetValueFromCurve(StringToName(curve.Id()), session, StringToName(curve.Column()));
        }
      } else {
        kind = 2;
      }
    }
    if !known.levelSeen {
      known.levelSeen = true;
      if kind == 1 {
        this.m_scaled += 1;
      } else if kind == 2 {
        this.m_fixed += 1;
      } else {
        this.m_unassigned += 1;
      }
    }
    if this.m_max <= 0.0 {
      this.m_max = TweakDBInterface.GetStatRecord(t"BaseStats.PowerLevel").Max();
    }
    let shift = TandemProgress.NpcLevelShift(level, known.levelShift, this.m_own, session, atHost, atSession, fixed, this.m_max);
    let own = level - known.levelShift;
    if AbsF(shift - known.levelShift) >= 0.01 {
      // Its health as a share of what it had: the pool follows the new
      // maximum.
      let pools = GameInstance.GetStatPoolsSystem(this.m_game);
      let health = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, true);
      this.Release(known, npc.GetEntityID());
      if shift != 0.0 {
        let modifier = RPGManager.CreateStatModifier(gamedataStatType.PowerLevel, gameStatModifierType.Additive, shift);
        if stats.AddModifier(id, modifier) {
          known.levelMod = modifier;
          known.levelShift = shift;
          this.m_on += 1;
        }
      }
      if health > 0.0 {
        pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, health, null, true);
      }
      TandemNative.Log(s"levels: npc \(known.npc) \(TDBID.ToStringDEBUG(npc.GetRecordID())) level \(own) -> \(own + known.levelShift) (session \(this.m_session), ours \(this.m_own), curve \(atHost) -> \(atSession))");
    }
    let now = own + known.levelShift;
    if AbsF(now - known.levelSent) >= 0.01 {
      known.levelSent = now;
      TandemProgress.SendNpcLevel(known.npc, now);
    }
  }

  // Our modifier off an NPC (it is no longer shared, or the session is over).
  public func Release(known: ref<TandemNpcKnown>, id: EntityID) -> Void {
    if IsDefined(known.levelMod) {
      GameInstance.GetStatsSystem(this.m_game).RemoveModifier(Cast<StatsObjectID>(id), known.levelMod);
      this.m_on -= 1;
    }
    known.levelMod = null;
    known.levelShift = 0.0;
  }

  // ---- our proxies ----

  // Its owner's level for one of its NPCs (TandemFightEvent kind 11).
  public func OnNpc(npc: Uint32, level: Float) -> Void {
    let i = 0;
    while i < ArraySize(this.m_npcs) {
      if this.m_npcs[i] == npc {
        this.m_npcLevels[i] = level;
        return;
      }
      i += 1;
    }
    ArrayPush(this.m_npcs, npc);
    ArrayPush(this.m_npcLevels, level);
  }

  public func HostLevelOf(npc: Uint32) -> Float {
    let i = 0;
    while i < ArraySize(this.m_npcs) {
      if this.m_npcs[i] == npc {
        return this.m_npcLevels[i];
      }
      i += 1;
    }
    return 0.0;
  }

  public func Forget(npc: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_npcs) {
      if this.m_npcs[i] == npc {
        ArrayErase(this.m_npcs, i);
        ArrayErase(this.m_npcLevels, i);
        return;
      }
      i += 1;
    }
  }

  // A proxy, once its body exists and whenever its owner's level for it
  // changes: the same power level as the owner's NPC.
  public func Proxy(proxy: ref<TandemNpcProxy>, body: ref<ScriptedPuppet>) -> Void {
    let wanted = this.HostLevelOf(proxy.npc);
    if wanted <= 0.0 || proxy.dead {
      return;
    }
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let id = Cast<StatsObjectID>(body.GetEntityID());
    let level = stats.GetStatValue(id, gamedataStatType.PowerLevel);
    let shift = TandemProgress.ProxyLevelShift(level, proxy.levelShift, wanted);
    if AbsF(shift - proxy.levelShift) < 0.01 {
      return;
    }
    if IsDefined(proxy.levelMod) {
      stats.RemoveModifier(id, proxy.levelMod);
    }
    proxy.levelMod = null;
    let own = level - proxy.levelShift;
    proxy.levelShift = 0.0;
    if shift != 0.0 {
      let modifier = RPGManager.CreateStatModifier(gamedataStatType.PowerLevel, gameStatModifierType.Additive, shift);
      if stats.AddModifier(id, modifier) {
        proxy.levelMod = modifier;
        proxy.levelShift = shift;
      }
    }
    TandemNative.Log(s"levels: proxy \(proxy.npc) level \(own) -> \(own + proxy.levelShift) (its owner's \(wanted))");
  }

  // ---- both ----

  // The session is over: nothing of it is told again, and the host's NPCs
  // are theirs again (TandemNpcs.End releases each one first).
  public func End() -> Void {
    this.m_sent = 0;
    this.m_session = 0;
    this.m_players = 0;
    this.m_gen += 1;
    ArrayClear(this.m_npcs);
    ArrayClear(this.m_npcLevels);
  }

  // Modifiers on our own NPCs now (0 once a session is over).
  public func On() -> Int32 {
    return this.m_on;
  }

  public func Report() -> String {
    return s"levels: ours \(this.m_sent), session \(this.m_session) (\(this.m_players) players), \(this.m_on) NPCs moved; seen \(this.m_scaled) scaled by a curve, \(this.m_fixed) of a constant level, \(this.m_unassigned) with no content assignment; \(ArraySize(this.m_npcs)) owners' levels known";
  }
}
