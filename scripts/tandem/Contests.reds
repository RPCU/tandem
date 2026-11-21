// Contests (docs/architecture/activities.md, "Contests"): a fist fight, a
// street race, a shooting competition. Decided 2026-11-19: a contest is its
// starter's, the others watch, and their hits on a duel's opponent are
// dropped.
//
// - Ours is read from the game's own data on our V, four times a second
//   (TandemActivities.SampleOurs): the fist fight restriction the quest puts
//   on V and on its opponents (GameplayRestriction.FistFight, the tag
//   FistFight), a shooting competition's (the tag ShootingRangeCompetition),
//   the racing system's word (RacingSystem.IsRaceInProgress). No list of
//   quests. It goes to the others as our Activity (kind 8, `entry` the sort,
//   `device` the session's number of a lone opponent).
// - An opponent is an NPC that wears the fist fight restriction. Its owner
//   says so for the games that show its proxy (NpcStatus: the proxy wears it
//   too), so every game knows a duel's opponents by the game's own mark.
// - A hit on an opponent counts only from a V that is in a duel itself (ours
//   wears the restriction; another player's Activity says so). Anyone else's
//   is dropped where every hit enters (DamageSystem.ProcessPipeline) and
//   where an owner takes a guest's strike (TandemOutcomes.TakeStrike).

public class TandemContest {
  public let player: Uint32;
  public let sort: Int32;
  public let opponent: Uint32;
}

public class TandemContests {
  private let m_game: GameInstance;
  // Ours, as last sampled: proto::ContestSort (0 none here), the lone
  // opponent's session number, and whether it was logged.
  private let m_sort: Int32;
  private let m_opponent: Uint32;
  // The others' (from their Activity).
  private let m_others: array<ref<TandemContest>>;
  // The NPCs we run that we said wear the fist fight restriction.
  private let m_marked: array<Uint32>;
  private let m_lastScan: Float;
  private let m_lastNotice: Float;
  private let m_dropped: Int32;
  private let m_test: Int32;
  // Street races: joining a partner's, its standings (Races.reds).
  private let m_races: ref<TandemRaces>;

  public static func DuelSort() -> Int32 {
    return 1;
  }

  public static func RaceSort() -> Int32 {
    return 2;
  }

  public static func RangeSort() -> Int32 {
    return 3;
  }

  // The fist fight's mark, on V and on its opponents (final.reds:21118,
  // 128701, 185843).
  public static func FightEffect() -> TweakDBID {
    return t"GameplayRestriction.FistFight";
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_races = new TandemRaces();
    this.m_races.Start(game);
  }

  public func Races() -> ref<TandemRaces> {
    return this.m_races;
  }

  public static func Of(game: GameInstance) -> ref<TandemContests> {
    let activities = TandemActivities.Of(game);
    return IsDefined(activities) ? activities.Contests() : null;
  }

  // ---- ours ----

  // In a fist fight by the game's own mark; not by one a host's story
  // moment lent us (Restrictions.reds no longer shares it, and never did
  // to a V that started its own).
  public static func InFight(object: wref<GameObject>) -> Bool {
    if !IsDefined(object) {
      return false;
    }
    return StatusEffectSystem.ObjectHasStatusEffect(object, TandemContests.FightEffect()) || StatusEffectSystem.ObjectHasStatusEffectWithTag(object, n"FistFight");
  }

  // What our V is in now (0: no contest).
  public func Sample(player: ref<PlayerPuppet>) -> Int32 {
    let sort = 0;
    if this.m_test != 0 {
      sort = this.m_test;
    } else if TandemContests.InFight(player) && !TandemStory.RestrictOurs(TandemContests.FightEffect()) {
      sort = TandemContests.DuelSort();
    } else if GameInstance.GetRacingSystem(this.m_game).IsRaceInProgress() || (IsDefined(this.m_races) && this.m_races.Racing()) {
      sort = TandemContests.RaceSort();
    } else if StatusEffectSystem.ObjectHasStatusEffectWithTag(player, n"ShootingRangeCompetition") {
      sort = TandemContests.RangeSort();
    }
    let opponent = 0u;
    if sort == TandemContests.DuelSort() {
      opponent = this.LoneOpponent(player);
    }
    if sort != this.m_sort {
      TandemNative.Log(s"contests: our V is in \(TandemContests.SortName(sort)) (was \(TandemContests.SortName(this.m_sort)); opponent npc \(opponent))");
    }
    this.m_sort = sort;
    this.m_opponent = opponent;
    return sort;
  }

  public func Opponent() -> Uint32 {
    return this.m_opponent;
  }

  // The session's number of our duel's opponent when there is one alone
  // (0: none shared, or several: the twins).
  private func LoneOpponent(player: ref<PlayerPuppet>) -> Uint32 {
    let system = TandemCombat.System(this.m_game);
    if !IsDefined(system) {
      return 0u;
    }
    let found = 0u;
    for npc in player.GetNPCsAroundObject(30.0) {
      if IsDefined(npc) && !npc.IsDead() && TandemContests.InFight(npc) && system.PlayerOfPuppet(npc.GetEntityID()) == 0u {
        let id = system.SharedNpcOf(npc.GetEntityID());
        if id != 0u {
          if found != 0u {
            return 0u;
          }
          found = id;
        }
      }
    }
    return found;
  }

  // Once a second: the NPCs we run that wear the fist fight's mark, said
  // for the games that show their proxies (and unsaid when it comes off).
  public func Tick(now: Float, you: Uint32) -> Void {
    if IsDefined(this.m_races) {
      this.m_races.Tick(now, you);
    }
    if you == 0u {
      ArrayClear(this.m_marked);
      ArrayClear(this.m_others);
      return;
    }
    if now - this.m_lastScan < 1.0 && now >= this.m_lastScan {
      return;
    }
    this.m_lastScan = now;
    let player = GetPlayer(this.m_game);
    let system = TandemCombat.System(this.m_game);
    if !IsDefined(player) || !IsDefined(system) {
      return;
    }
    let wearing: array<Uint32>;
    for npc in player.GetNPCsAroundObject(60.0) {
      if IsDefined(npc) && TandemContests.InFight(npc) && TandemOwn.Entity(this.m_game, npc.GetEntityID()) {
        let id = system.SharedNpcOf(npc.GetEntityID());
        if id != 0u {
          ArrayPush(wearing, id);
          if !ArrayContains(this.m_marked, id) {
            ArrayPush(this.m_marked, id);
            TandemNative.SendNpcStatus(id, 0u, TandemContests.FightEffect(), true);
            TandemNative.Log(s"contests: npc \(id) is a fist fight's opponent: said for its proxies");
          }
        }
      }
    }
    let i = ArraySize(this.m_marked) - 1;
    while i >= 0 {
      let id = this.m_marked[i];
      if !ArrayContains(wearing, id) {
        ArrayErase(this.m_marked, i);
        // Still ours and here, the mark gone: the fight is over for it.
        if TandemOwn.Npc(this.m_game, id) {
          TandemNative.SendNpcStatus(id, 0u, TandemContests.FightEffect(), false);
          TandemNative.Log(s"contests: npc \(id) is no longer a fist fight's opponent");
        }
      }
      i -= 1;
    }
  }

  // ---- theirs ----

  // The others' activities changed (TandemActivities.FollowOthers): who is
  // in a contest now, said the game's way when one begins or ends.
  public func Others(states: array<TandemBusyState>, you: Uint32) -> Void {
    let now: array<ref<TandemContest>>;
    for state in states {
      if state.player != you && state.kind == 8 {
        let contest = new TandemContest();
        contest.player = state.player;
        contest.sort = state.entry;
        contest.opponent = Cast<Uint32>(state.device);
        ArrayPush(now, contest);
        let before = this.Find(state.player);
        if !IsDefined(before) || before.sort != contest.sort {
          TandemNative.Log(s"contests: player \(state.player) is in \(TandemContests.SortName(contest.sort)) (opponent npc \(contest.opponent)): we watch");
          // A race says itself (Races.reds: its offer, its standings).
          if contest.sort != TandemContests.RaceSort() {
            this.Notice(TandemContests.StartKey(contest.sort), state.player);
          }
        }
      }
    }
    for before in this.m_others {
      let still = false;
      for contest in now {
        if contest.player == before.player {
          still = true;
        }
      }
      if !still {
        TandemNative.Log(s"contests: player \(before.player) is no longer in \(TandemContests.SortName(before.sort))");
        if before.sort != TandemContests.RaceSort() {
          this.Notice("Contest-Over", before.player);
        }
      }
    }
    this.m_others = now;
  }

  private func Find(player: Uint32) -> ref<TandemContest> {
    for contest in this.m_others {
      if contest.player == player {
        return contest;
      }
    }
    return null;
  }

  // That player's V is in a duel (its game says so).
  public func InDuel(player: Uint32) -> Bool {
    let contest = this.Find(player);
    return IsDefined(contest) && contest.sort == TandemContests.DuelSort();
  }

  // The player whose duel `npc` (a session number; 0: unknown) is the
  // opponent of, when one names it (0: nobody does).
  private func NamedBy(npc: Uint32) -> Uint32 {
    if npc == 0u {
      return 0u;
    }
    for contest in this.m_others {
      if contest.sort == TandemContests.DuelSort() && contest.opponent == npc {
        return contest.player;
      }
    }
    return 0u;
  }

  // Whose fight to name in the notice: who names the NPC, else anyone in
  // a duel.
  private func Dueller(npc: Uint32) -> Uint32 {
    let by = this.NamedBy(npc);
    if by != 0u {
      return by;
    }
    for contest in this.m_others {
      if contest.sort == TandemContests.DuelSort() {
        return contest.player;
      }
    }
    return 0u;
  }

  // ---- the rule ----

  // `target` (session number `npc`, 0 when unknown) is a duel's opponent
  // and `by` (a player; ours: `you`) is not in a duel: its hit is dropped.
  public func Refuses(target: wref<GameObject>, npc: Uint32, by: Uint32, you: Uint32) -> Bool {
    if !IsDefined(target) || by == 0u {
      return false;
    }
    if !TandemContests.InFight(target) && this.NamedBy(npc) == 0u {
      return false;
    }
    if by == you {
      return !TandemContests.InFight(GetPlayer(this.m_game));
    }
    return !this.InDuel(by);
  }

  // Every hit, where it enters (Combat.reds): our V's, or a remote V's
  // puppet's, on a duel's opponent it has no part in.
  public static func Watched(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) || !IsDefined(hitEvent.target as NPCPuppet) {
      return false;
    }
    let system = TandemCombat.System(game);
    let contests = TandemContests.Of(game);
    let from = hitEvent.attackData.GetInstigator();
    if !IsDefined(system) || !IsDefined(contests) || !IsDefined(from) || system.You() == 0u {
      return false;
    }
    let by = from.IsPlayer() ? system.You() : system.PlayerOfPuppet(from.GetEntityID());
    if by == 0u {
      return false;
    }
    let id = hitEvent.target.GetEntityID();
    let npc = system.SharedNpcOf(id);
    if npc == 0u {
      npc = system.NpcOfProxy(id);
    }
    if !contests.Refuses(hitEvent.target, npc, by, system.You()) {
      return false;
    }
    contests.Dropped(npc, by, system.You());
    return true;
  }

  public func Dropped(npc: Uint32, by: Uint32, you: Uint32) -> Void {
    this.m_dropped += 1;
    TandemNative.LogDebug(s"contests: player \(by)'s hit on npc \(npc) dropped: a fist fight's opponent, and they are not in it");
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    if by == you && (now - this.m_lastNotice > 5.0 || now < this.m_lastNotice) {
      this.m_lastNotice = now;
      TandemNative.Log(s"contests: our hit on npc \(npc) dropped: player \(this.Dueller(npc))'s fist fight, we watch");
      this.Notice("Contest-WatchOnly", this.Dueller(npc));
    }
  }

  // ---- helpers ----

  public static func SortName(sort: Int32) -> String {
    switch sort {
      case 0:
        return "no contest";
      case 1:
        return "a fist fight";
      case 2:
        return "a race";
      case 3:
        return "a shooting competition";
      default:
        return "a contest";
    }
  }

  private static func StartKey(sort: Int32) -> String {
    switch sort {
      case 1:
        return "Contest-Duel";
      case 2:
        return "Contest-Race";
      case 3:
        return "Contest-Range";
      default:
        return "Contest-Other";
    }
  }

  private func Notice(key: String, player: Uint32) -> Void {
    let name = TandemNative.PlayerName(player);
    if StrLen(name) == 0 {
      name = TandemText.Unnamed(player);
    }
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 4.0, key, name);
  }

  // (test reset) the test contest off (`contest duel|race|range`).
  public func ClearTest() -> Void {
    if this.m_test != 0 {
      this.m_test = 0;
      TandemNative.Log("contests: (test) none");
    }
  }

  // tools/game.sh cmd contest duel|race|range|off|report|races|join|leave
  public func Command(command: String) -> Void {
    let what = StrAfterFirst(command, " ");
    if Equals(what, "races") || Equals(what, "join") || Equals(what, "leave") {
      if IsDefined(this.m_races) {
        this.m_races.Command(command);
      }
      return;
    }
    if Equals(what, "duel") {
      this.m_test = 1;
    } else if Equals(what, "race") {
      this.m_test = 2;
    } else if Equals(what, "range") {
      this.m_test = 3;
    } else if Equals(what, "off") {
      this.m_test = 0;
    }
    let others = "";
    for contest in this.m_others {
      others += s" player \(contest.player): \(TandemContests.SortName(contest.sort)) (npc \(contest.opponent))";
    }
    TandemNative.Log(s"contests: ours \(TandemContests.SortName(this.m_sort)) (opponent npc \(this.m_opponent), test \(this.m_test)); others:\(StrLen(others) > 0 ? others : " none"); opponents of ours said \(ArraySize(this.m_marked)); hits dropped \(this.m_dropped)");
  }
}
