// Street races in co-op (docs/architecture/activities.md, "Races"). A race
// is its starter's: the starter's game runs the race quest, as in single
// player. The others may join it on its grid, before its start, as new
// racers in their own game; they race the starter's checkpoints.
//
// - The starter's game: its race HUD's modes (hudCarRaceController's
//   ForwardVehicleRaceUIEvent, queued by the wrap below) say the race is on
//   the grid, started, ended; the quest fact the HUD counts checkpoints
//   with (sq024_current_race_checkpoint_fact_add, every race's) says when
//   our V passed one: where our V is then is that checkpoint, for everyone.
//   The whole route, when its race quest's data has it (the objectives' map
//   pins, ReadRoute), is said on the grid (Route): every racer then counts
//   its own checkpoints and finishes on its own.
//   Its NPC racers are the cars the racing system has registered
//   (RacingSystem.IsAIVehicleRegistered); they are shared like any traffic
//   (Traffic.reds, a wider range while joiners race). It orders everyone,
//   players and NPC racers, by checkpoints passed and when (host clock
//   times: what a laggy report says counts at the time it says), and pays.
// - A joiner's game: its own car (tandem.ini race_car = own: its garage's
//   active car) or a copy of the starter's (copy, or no car of its own) on
//   the grid, our V in it, its engine held until the start; each checkpoint
//   of the route (else each one the starter's game names) is passed when
//   our V's track goes through it (within 25 m, at the time it did, also
//   for one named after we passed it).
// - Everyone: the standings in a small list on the HUD (the vanilla race
//   HUD has one number for a place: it stays the starter's, among its NPCs).
// - Rewards: what the starter's quest pays its V during the race or 30 s
//   after it goes to each joiner that finished ahead of every NPC racer,
//   paid once by its own game through the co-op ledger (Story.reds PayRace,
//   keyed by the race quest). And whatever the starter's result, a joiner
//   that finished is paid by its own game the race's own rewards from its
//   own TweakDB (PayOwn: The Beast in Me's finish reward, and its win
//   reward when ahead of every NPC racer), through the same ledger. A
//   joiner's own copy of that quest is not touched.

public class TandemRacer {
  // A player, or 0x80000000 | an NPC racer's number (proto::kRacerNpc).
  public let who: Uint32;
  // The starter's game: an NPC racer's car.
  public let body: EntityID;
  public let checkpoint: Int32;
  // Our engine time of its last checkpoint, or of its finish.
  public let at: Float;
  public let finished: Bool;
  public let out: Bool;
  public let place: Int32;
  public let heardAt: Float;
  public let metres: Float;
  // Its track (positions and our engine times) and how far it was read.
  public let trail: array<Vector4>;
  public let times: array<Float>;
  public let scanned: Int32;
  public let from: Int32;
  public let known: Int32;
}

public class TandemRaces {
  private let m_game: GameInstance;
  private let m_you: Uint32;
  private let m_now: Float;
  // ---- ours (we started it) ----
  // 0 none, else proto kRace* (1 grid, 2 running, 3 over, 4 abandoned).
  private let m_phase: Int32;
  private let m_race: Uint32;
  private let m_count: Int32;
  private let m_start: Vector4;
  private let m_heading: Float;
  private let m_car: TweakDBID;
  private let m_quest: Uint32;
  private let m_countdown: Bool;
  private let m_points: array<Vector4>;
  // The whole route from the race quest's data (none: not known), the
  // race's rewards (proto kRaceRewards*), when it was last looked for.
  private let m_route: array<Vector4>;
  private let m_rewardsOf: Int32;
  private let m_routeAt: Float;
  private let m_racers: array<ref<TandemRacer>>;
  private let m_joiners: array<Uint32>;
  private let m_npcs: Int32;
  private let m_fact: Int32;
  private let m_goAt: Float;
  private let m_endedAt: Float;
  private let m_lastInfo: Float;
  private let m_lastBoard: Float;
  private let m_board: String;
  private let m_rewards: array<TweakDBID>;
  private let m_paid: array<String>;
  private let m_seenRunning: Float;
  private let m_wasRacing: Bool;
  // ---- theirs (a partner's race we may join, joined, or watch) ----
  private let m_starter: Uint32;
  private let m_theirRace: Uint32;
  private let m_theirPhase: Int32;
  private let m_theirCount: Int32;
  private let m_theirStart: Vector4;
  private let m_theirHeading: Float;
  private let m_theirCar: TweakDBID;
  private let m_theirQuest: Uint32;
  private let m_theirPoints: array<Vector4>;
  private let m_theirRoute: array<Vector4>;
  private let m_theirRewards: Int32;
  // Our own rewards (PayOwn): when the standings first said we finished.
  private let m_ownFinishAt: Float;
  private let m_paidOwn: Bool;
  private let m_theirCountdown: Bool;
  private let m_heardAt: Float;
  private let m_theirGoAt: Float;
  // 0 not joined, 1 asked, 2 seated (placing), 3 in our car, 4 out
  private let m_joined: Int32;
  private let m_slot: Int32;
  private let m_placing: Int32;
  private let m_placingAt: Float;
  private let m_ourCar: EntityID;
  private let m_carOutSince: Float;
  private let m_carDropAt: Float;
  private let m_me: ref<TandemRacer>;
  private let m_lastProgress: Float;
  private let m_mark: NewMappinID;
  private let m_markFor: Int32;
  private let m_finishedSaid: Bool;
  // The standings as last said (theirs) or made (ours).
  private let m_standings: array<ref<TandemRacer>>;
  private let m_standingsAt: Float;
  // The offer on the phone key.
  private let m_offered: Bool;
  private let m_listening: Bool;
  private let m_pressed: Bool;
  private let m_held: Bool;
  // Heard while reading a head and its parts (TakeRaces).
  private let m_reading: TandemRaceEvent;
  private let m_readPoints: array<Vector4>;
  private let m_readEntries: array<TandemRaceEvent>;
  private let m_hud: ref<inkVerticalPanel>;
  private let m_lines: array<ref<inkText>>;

  public static func Radius() -> Float {
    return 25.0;
  }

  // How near the start line a partner is offered the race.
  public static func OfferRange() -> Float {
    return 400.0;
  }

  public static func NpcBit() -> Uint32 {
    return 2147483648u;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_mark.value = 0ul;
    this.m_markFor = -1;
  }

  public static func Of(game: GameInstance) -> ref<TandemRaces> {
    let contests = TandemContests.Of(game);
    return IsDefined(contests) ? contests.Races() : null;
  }

  // Our V races in a partner's race now (its Activity says a race).
  public func Racing() -> Bool {
    return this.m_joined == 3 && this.m_theirPhase == 2;
  }

  // Our race's NPC racers are shared further while partners race in it
  // (Traffic.reds).
  public func SharesRacers() -> Bool {
    return (this.m_phase == 1 || this.m_phase == 2) && ArraySize(this.m_joiners) > 0;
  }

  public static func Racer(game: GameInstance, vehicle: ref<VehicleObject>) -> Bool {
    let races = TandemRaces.Of(game);
    return IsDefined(races) && IsDefined(vehicle) && races.SharesRacers() && GameInstance.GetRacingSystem(game).IsAIVehicleRegistered(vehicle);
  }

  // Four times a second (TandemContests.Tick); `you` 0: not in a session.
  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    if you == 0u && this.m_you != 0u {
      this.EndOurs(false);
      this.EndTheirs();
    }
    this.m_you = you;
    this.Read(TandemActivity.TakeRaces());
    if you == 0u {
      this.DropCar();
      return;
    }
    this.TickOurs();
    this.TickTheirs();
    this.TickOffer();
    this.DropCar();
    this.Show();
  }

  // ---- what was said ----

  private func Read(events: array<TandemRaceEvent>) -> Void {
    let open = false;
    for event in events {
      if event.part == 0 {
        if open {
          this.Heard(this.m_reading, this.m_readPoints, this.m_readEntries);
          open = false;
        }
        if event.kind == 1 {
          this.m_reading = event;
          ArrayClear(this.m_readPoints);
          ArrayClear(this.m_readEntries);
          open = true;
        } else if event.kind == 2 {
          this.OnUi(event.arg, event.checkpoint, event.checkpoints);
        } else if event.kind == 3 {
          this.OnReward(event.record);
        }
      } else if open && event.part == 1 {
        ArrayPush(this.m_readPoints, new Vector4(event.x, event.y, event.z, 1.0));
      } else if open && event.part == 2 {
        ArrayPush(this.m_readEntries, event);
      }
    }
    if open {
      this.Heard(this.m_reading, this.m_readPoints, this.m_readEntries);
    }
  }

  private func Heard(head: TandemRaceEvent, points: array<Vector4>, entries: array<TandemRaceEvent>) -> Void {
    if head.player == this.m_you {
      return;
    }
    switch head.raceKind {
      case 1:
        this.HeardInfo(head, points);
        break;
      case 2:
        this.HeardJoin(head);
        break;
      case 3:
        this.HeardSeat(head);
        break;
      case 4:
        this.HeardProgress(head);
        break;
      case 5:
        this.HeardStandings(head, entries);
        break;
      case 6:
        this.HeardReward(head);
        break;
      case 7:
        this.HeardRoute(head, points);
        break;
      default:
        break;
    }
  }

  // ---- ours: the race we started ----

  // Our race HUD's mode (vehicleRaceUI: 0 the grid, 1 the countdown, 2 the
  // start, 3 the end, 4 off).
  private func OnUi(mode: Int32, maxPosition: Int32, maxCheckpoints: Int32) -> Void {
    if mode > 100 {
      this.OnKey(mode);
      return;
    }
    TandemNative.Log(s"races: our race HUD says \(mode) (max position \(maxPosition), checkpoints \(maxCheckpoints))");
    if mode == 0 {
      this.Begin(1, maxCheckpoints);
    } else if mode == 1 {
      // (Begin keeps a race already open.)
      this.Begin(1, 0);
      this.m_countdown = true;
      this.SayInfo();
    } else if mode == 2 {
      this.Begin(2, 0);
      this.Go();
    } else if mode == 3 {
      this.OursFinished();
    } else if mode == 4 {
      if this.m_phase == 1 || (this.m_phase == 2 && !this.OurRacer().finished) {
        this.EndOurs(true);
      } else if this.m_phase == 2 {
        this.OursFinished();
      }
    }
  }

  private func Begin(phase: Int32, count: Int32) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || this.m_you == 0u {
      return;
    }
    if this.m_phase == 1 || this.m_phase == 2 {
      if count > 0 && count != this.m_count {
        this.m_count = count;
        this.ReadRoute(true);
      }
      return;
    }
    this.m_race += 1u;
    this.m_phase = phase;
    this.m_count = count;
    this.m_countdown = false;
    this.m_npcs = 0;
    this.m_endedAt = 0.0;
    this.m_seenRunning = 0.0;
    this.m_goAt = phase == 2 ? this.m_now : 0.0;
    ArrayClear(this.m_points);
    ArrayClear(this.m_racers);
    ArrayClear(this.m_joiners);
    ArrayClear(this.m_rewards);
    ArrayClear(this.m_paid);
    ArrayClear(this.m_standings);
    this.m_board = "";
    this.m_fact = GameInstance.GetQuestsSystem(this.m_game).GetFact(n"sq024_current_race_checkpoint_fact_add");
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
      this.m_start = car.GetWorldPosition();
      this.m_heading = car.GetWorldYaw();
      this.m_car = car.GetRecordID();
    } else {
      this.m_start = player.GetWorldPosition();
      this.m_heading = player.GetWorldYaw();
      this.m_car = TDBID.None();
    }
    this.m_quest = this.TrackedQuest();
    this.m_rewardsOf = this.RewardsOf();
    ArrayClear(this.m_route);
    let us = new TandemRacer();
    us.who = this.m_you;
    ArrayPush(this.m_racers, us);
    TandemNative.Log(s"races: our race \(this.m_race) begins (\(phase == 1 ? "on its grid: partners may join" : "already running")), \(count) checkpoint(s), car \(TDBID.ToStringDEBUG(this.m_car)), quest \(this.m_quest), rewards \(this.m_rewardsOf)");
    this.SayInfo();
    this.ReadRoute(true);
  }

  private func Go() -> Void {
    if this.m_phase != 1 && this.m_phase != 2 {
      return;
    }
    this.m_phase = 2;
    this.m_goAt = this.m_now;
    let us = this.OurRacer();
    us.at = this.m_now;
    for racer in this.m_racers {
      racer.at = this.m_now;
      racer.heardAt = this.m_now;
    }
    TandemNative.Log(s"races: our race \(this.m_race) starts with \(ArraySize(this.m_joiners)) partner(s)");
    this.SayInfo();
  }

  // Our V crossed the line (or the race HUD ended): our finish; the race is
  // over once every partner in it finished or 60 s went by.
  private func OursFinished() -> Void {
    if this.m_phase != 2 {
      return;
    }
    let us = this.OurRacer();
    if !us.finished {
      us.finished = true;
      us.at = this.m_now;
      if this.m_count > 0 {
        us.checkpoint = this.m_count;
      }
      TandemNative.Log(s"races: we finished our race \(this.m_race) after \(this.m_now - this.m_goAt) s");
    }
    if this.m_endedAt == 0.0 {
      this.m_endedAt = this.m_now;
    }
  }

  private func OurRacer() -> ref<TandemRacer> {
    for racer in this.m_racers {
      if racer.who == this.m_you {
        return racer;
      }
    }
    let us = new TandemRacer();
    us.who = this.m_you;
    ArrayPush(this.m_racers, us);
    return us;
  }

  private func TickOurs() -> Void {
    let racing = GameInstance.GetRacingSystem(this.m_game).IsRaceInProgress();
    if racing {
      this.m_seenRunning = this.m_now;
      // A race whose HUD's grid we never heard (begun before the session).
      if !this.m_wasRacing && this.m_phase != 1 && this.m_phase != 2 {
        this.Begin(2, 0);
      }
    }
    this.m_wasRacing = racing;
    if this.m_phase != 1 && this.m_phase != 2 {
      return;
    }
    // The race is gone without its HUD's end: over (or left, unfinished).
    // (Only once the racing system said so during this race's run.)
    if !racing && this.m_phase == 2 && this.m_seenRunning > 0.0 && this.m_seenRunning >= this.m_goAt && this.m_now - this.m_seenRunning > 3.0 && this.m_endedAt == 0.0 {
      if this.OurRacer().finished || (this.m_count > 0 && ArraySize(this.m_points) >= this.m_count) {
        this.OursFinished();
      } else {
        this.EndOurs(true);
        return;
      }
    }
    // Our checkpoints: the HUD's own fact; where our V is, is that checkpoint.
    let fact = GameInstance.GetQuestsSystem(this.m_game).GetFact(n"sq024_current_race_checkpoint_fact_add");
    if fact > this.m_fact && this.m_phase == 2 {
      // One checkpoint per rise: where our V is now.
      let player = GetPlayer(this.m_game);
      let us = this.OurRacer();
      if ArraySize(this.m_points) < 64 && IsDefined(player) {
        ArrayPush(this.m_points, player.GetWorldPosition());
      }
      us.checkpoint = ArraySize(this.m_points);
      us.at = this.m_now;
      TandemNative.Log(s"races: we passed checkpoint \(us.checkpoint)/\(this.m_count) (fact \(fact))");
      this.CheckRoute();
      if this.m_count > 0 && us.checkpoint >= this.m_count {
        this.OursFinished();
      }
      this.SayInfo();
    }
    this.m_fact = fact;
    // The route, looked for again while on the grid (the quest may place
    // its pins a moment after the HUD's grid) and until our first checkpoint.
    if ArraySize(this.m_route) == 0 && ArraySize(this.m_points) == 0 && this.m_now - this.m_routeAt >= 2.0 {
      this.ReadRoute(false);
    }
    // Our NPC racers: the racing system's cars, followed through its
    // checkpoints.
    if this.m_phase == 2 {
      this.FollowNpcs();
    }
    // Partners that went quiet while racing are out.
    for racer in this.m_racers {
      if this.m_phase == 2 && racer.who != this.m_you && (racer.who & TandemRaces.NpcBit()) == 0u && !racer.out && !racer.finished && this.m_now - racer.heardAt > 15.0 {
        racer.out = true;
        ArrayRemove(this.m_joiners, racer.who);
        TandemNative.Log(s"races: player \(racer.who) went quiet: out of our race");
        this.Notice("Race-RacerLeft", racer.who);
      }
    }
    if this.m_now - this.m_lastInfo >= 2.0 {
      this.SayInfo();
    }
    this.SayStandings(false);
    // Over: everyone in it finished, or a minute after we did.
    if this.m_endedAt > 0.0 && (this.AllDone() || this.m_now - this.m_endedAt > 60.0) {
      this.m_phase = 3;
      this.SayStandings(true);
      this.SayInfo();
      this.PayJoiners();
      TandemNative.Log(s"races: our race \(this.m_race) is over");
    }
  }

  private func AllDone() -> Bool {
    for racer in this.m_racers {
      if (racer.who & TandemRaces.NpcBit()) == 0u && !racer.finished && !racer.out {
        return false;
      }
    }
    return true;
  }

  private func FollowNpcs() -> Void {
    let traffic = TandemTraffic.Of(this.m_game);
    if !IsDefined(traffic) {
      return;
    }
    let racing = GameInstance.GetRacingSystem(this.m_game);
    for vehicle in traffic.Live() {
      if IsDefined(vehicle) && !vehicle.IsPlayerVehicle() && racing.IsAIVehicleRegistered(vehicle) && !IsDefined(this.NpcOf(vehicle.GetEntityID())) && this.m_npcs < 12 {
        this.m_npcs += 1;
        let npc = new TandemRacer();
        npc.who = TandemRaces.NpcBit() | Cast<Uint32>(this.m_npcs);
        npc.body = vehicle.GetEntityID();
        npc.at = this.m_goAt;
        ArrayPush(this.m_racers, npc);
        TandemNative.Log(s"races: npc racer \(this.m_npcs) is \(EntityID.ToDebugString(npc.body))");
      }
    }
    for racer in this.m_racers {
      if EntityID.IsDefined(racer.body) && !racer.finished {
        let car = GameInstance.FindEntityByID(this.m_game, racer.body) as VehicleObject;
        if IsDefined(car) {
          this.Sample(racer, car.GetWorldPosition());
          this.Credit(racer, ArraySize(this.m_route) > 0 ? this.m_route : this.m_points, this.m_count);
        }
      }
    }
  }

  private func NpcOf(body: EntityID) -> ref<TandemRacer> {
    for racer in this.m_racers {
      if racer.body == body {
        return racer;
      }
    }
    return null;
  }

  private func FindRacer(who: Uint32) -> ref<TandemRacer> {
    for racer in this.m_racers {
      if racer.who == who {
        return racer;
      }
    }
    return null;
  }

  private func HeardJoin(head: TandemRaceEvent) -> Void {
    if head.starter != this.m_you || head.race != this.m_race {
      return;
    }
    let player = head.player;
    if head.arg == 0 {
      let racer = this.FindRacer(player);
      if IsDefined(racer) && !racer.out {
        racer.out = true;
        TandemNative.Log(s"races: player \(player) left our race");
        this.Notice("Race-RacerLeft", player);
      }
      ArrayRemove(this.m_joiners, player);
      return;
    }
    // On the grid only, up to the session's players; a late one watches.
    let slot = 0;
    if this.m_phase == 1 {
      if ArrayContains(this.m_joiners, player) {
        slot = ArrayFindFirst(this.m_joiners, player) + 1;
      } else if ArraySize(this.m_joiners) < 7 {
        ArrayPush(this.m_joiners, player);
        slot = ArraySize(this.m_joiners);
        let racer = new TandemRacer();
        racer.who = player;
        racer.heardAt = this.m_now;
        ArrayPush(this.m_racers, racer);
        this.Notice("Race-Joined", player);
      }
    }
    TandemNative.Log(s"races: player \(player) asks to join our race \(this.m_race): slot \(slot)");
    this.Say(3, player, slot, 0, 0.0, -1.0, TDBID.None());
    if slot > 0 {
      this.SayRoute();
    }
  }

  private func HeardProgress(head: TandemRaceEvent) -> Void {
    if head.starter == this.m_you && head.race == this.m_race {
      let racer = this.FindRacer(head.player);
      if IsDefined(racer) && !racer.out {
        racer.heardAt = this.m_now;
        racer.metres = head.metres;
        if head.checkpoint != racer.checkpoint || NotEquals(head.arg == 1, racer.finished) {
          racer.checkpoint = head.checkpoint;
          racer.finished = head.arg == 1;
          // When its game says it was, not when we heard it.
          racer.at = head.ago >= 0.0 ? this.m_now - head.ago : this.m_now;
        }
      }
    }
  }

  // Everyone's place: finished first by time, then by checkpoints and who
  // reached its last one first; those out last.
  private static func Ahead(a: ref<TandemRacer>, b: ref<TandemRacer>) -> Bool {
    if NotEquals(a.out, b.out) {
      return b.out;
    }
    if NotEquals(a.finished, b.finished) {
      return a.finished;
    }
    if a.finished {
      return a.at < b.at;
    }
    if a.checkpoint != b.checkpoint {
      return a.checkpoint > b.checkpoint;
    }
    if a.at != b.at {
      return a.at < b.at;
    }
    return a.who < b.who;
  }

  private func Ordered() -> array<ref<TandemRacer>> {
    let out: array<ref<TandemRacer>>;
    for racer in this.m_racers {
      let i = 0;
      let placed = false;
      while i < ArraySize(out) && !placed {
        if TandemRaces.Ahead(racer, out[i]) {
          ArrayInsert(out, i, racer);
          placed = true;
        }
        i += 1;
      }
      if !placed {
        ArrayPush(out, racer);
      }
    }
    let place = 1;
    for racer in out {
      racer.place = place;
      place += 1;
    }
    return out;
  }

  private func SayStandings(force: Bool) -> Void {
    if ArraySize(this.m_joiners) == 0 && !force {
      return;
    }
    let order = this.Ordered();
    let board = "";
    for racer in order {
      board += s"\(racer.who):\(racer.checkpoint):\(racer.finished):\(racer.out);";
    }
    if !force && Equals(board, this.m_board) && this.m_now - this.m_lastBoard < 3.0 {
      return;
    }
    if !force && this.m_now - this.m_lastBoard < 0.5 {
      return;
    }
    this.m_board = board;
    this.m_lastBoard = this.m_now;
    this.m_standings = order;
    this.m_standingsAt = this.m_now;
    let who: array<Uint32>;
    let passed: array<Int32>;
    let places: array<Int32>;
    let flags: array<Int32>;
    let agos: array<Float>;
    for racer in order {
      if ArraySize(who) < 16 {
        ArrayPush(who, racer.who);
        ArrayPush(passed, racer.checkpoint);
        ArrayPush(places, racer.place);
        ArrayPush(flags, (racer.finished ? 1 : 0) + (racer.out ? 2 : 0));
        ArrayPush(agos, racer.at > 0.0 ? MaxF(this.m_now - racer.at, 0.0) : -1.0);
      }
    }
    TandemActivity.StageRaceStandings(who, passed, places, flags, agos);
    TandemActivity.SendRace(5, this.m_you, 0u, this.m_race, 0, 0, this.m_count, 0.0, 0.0, -1.0, TDBID.None(), 0u, new Vector4(0.0, 0.0, 0.0, 1.0));
  }

  private func SayInfo() -> Void {
    if this.m_phase == 0 || this.m_you == 0u {
      return;
    }
    this.m_lastInfo = this.m_now;
    let ago = this.m_goAt > 0.0 ? MaxF(this.m_now - this.m_goAt, 0.0) : -1.0;
    TandemActivity.StageRacePoints(this.m_points);
    TandemActivity.SendRace(1, this.m_you, 0u, this.m_race, this.m_phase, this.m_countdown ? 1 : 0, this.m_count, 0.0, this.m_heading, ago, this.m_car, this.m_quest, this.m_start);
  }

  // A message of ours with no lists.
  private func Say(kind: Int32, to: Uint32, arg: Int32, checkpoint: Int32, metres: Float, ago: Float, record: TweakDBID, opt questHash: Uint32) -> Void {
    let starter = kind == 2 || kind == 4 ? this.m_starter : this.m_you;
    let race = kind == 2 || kind == 4 ? this.m_theirRace : this.m_race;
    let count = kind == 2 || kind == 4 ? this.m_theirCount : this.m_count;
    TandemActivity.SendRace(kind, starter, to, race, arg, checkpoint, count, metres, 0.0, ago, record, questHash, new Vector4(0.0, 0.0, 0.0, 1.0));
  }

  // Our race ended: abandoned (we left it, the session ended) or over.
  private func EndOurs(abandoned: Bool) -> Void {
    if this.m_phase != 1 && this.m_phase != 2 {
      return;
    }
    this.m_phase = abandoned ? 4 : 3;
    if this.m_you != 0u {
      this.SayInfo();
    }
    TandemNative.Log(s"races: our race \(this.m_race) \(abandoned ? "abandoned" : "over")");
    ArrayClear(this.m_joiners);
  }

  // A reward our V got: our race's while it runs and 30 s after.
  private func OnReward(reward: TweakDBID) -> Void {
    let fresh = this.m_phase == 2 || (this.m_phase == 3 && this.m_now - this.m_endedAt < 90.0);
    if !fresh || ArrayContains(this.m_rewards, reward) {
      return;
    }
    ArrayPush(this.m_rewards, reward);
    TandemNative.Log(s"races: our race paid \(TDBID.ToStringDEBUG(reward))");
    let which = this.RewardsFrom(reward);
    if which > 0 && which != this.m_rewardsOf {
      this.m_rewardsOf = which;
      this.SayRoute();
    }
    if this.m_phase == 3 {
      this.PayJoiners();
    }
  }

  // Each reward to each partner that finished ahead of every NPC racer, once.
  private func PayJoiners() -> Void {
    let order = this.Ordered();
    let firstNpc = 1000;
    for racer in order {
      if (racer.who & TandemRaces.NpcBit()) != 0u && racer.place < firstNpc {
        firstNpc = racer.place;
      }
    }
    for racer in order {
      if racer.who != this.m_you && (racer.who & TandemRaces.NpcBit()) == 0u && racer.finished && racer.place < firstNpc {
        for reward in this.m_rewards {
          let key = s"\(racer.who):\(TDBID.ToStringDEBUG(reward))";
          if !ArrayContains(this.m_paid, key) {
            ArrayPush(this.m_paid, key);
            TandemNative.Log(s"races: reward \(TDBID.ToStringDEBUG(reward)) to player \(racer.who) (place \(racer.place))");
            this.Say(6, racer.who, racer.place, 0, 0.0, -1.0, reward, this.LedgerKey());
          }
        }
      }
    }
  }

  // The race quest in the co-op ledger: its journal hash, else the race's
  // own key from its start line.
  private func LedgerKey() -> Uint32 {
    return this.m_quest != 0u ? this.m_quest : TandemRaces.PlaceKey(this.m_start);
  }

  // The tracked objective's quest (its journal hash).
  private func TrackedQuest() -> Uint32 {
    let journal = GameInstance.GetJournalManager(this.m_game);
    let entry = journal.GetTrackedEntry();
    let i = 0;
    while IsDefined(entry) && !IsDefined(entry as JournalQuest) && i < 4 {
      entry = journal.GetParentEntry(entry);
      i += 1;
    }
    return IsDefined(entry) ? Cast<Uint32>(journal.GetEntryHash(entry)) : 0u;
  }

  // ---- ours: the route from the race quest's data ----

  // The whole route: the map pins of the tracked objective, else of every
  // objective of its phase, in the journal's order (each pin where the
  // mappin system has it, as the quest menu reads them, final.reds:360688),
  // else the tracked objective's mappin positions. Kept only when it makes
  // the HUD's count: as many, one more by the start line (dropped), or a lap
  // repeated. Said when it changed, or when `say`.
  private func ReadRoute(say: Bool) -> Void {
    this.m_routeAt = this.m_now;
    let before = this.m_route;
    let route: array<Vector4>;
    let found = "no objective tracked";
    let journal = GameInstance.GetJournalManager(this.m_game);
    let tracked = journal.GetTrackedEntry();
    if this.m_count > 0 && IsDefined(tracked) {
      let pins = this.PinsOf(tracked);
      route = this.Fit(pins);
      found = s"the objective's pins \(ArraySize(pins))";
      if ArraySize(route) == 0 {
        let all: array<Vector4>;
        let phase = journal.GetParentEntry(tracked);
        if IsDefined(phase as JournalQuestPhase) {
          let objectives = this.ChildrenOf(phase);
          for objective in objectives {
            let theirs = this.PinsOf(objective);
            for pin in theirs {
              TandemRaces.AddPoint(all, pin);
            }
          }
        }
        route = this.Fit(all);
        found += s", the phase's pins \(ArraySize(all))";
      }
      if ArraySize(route) == 0 {
        let positions: array<Vector3>;
        let marked: array<Vector4>;
        if GameInstance.GetMappinSystem(this.m_game).GetQuestMappinPositionsByObjective(Cast<Uint32>(journal.GetEntryHash(tracked)), positions) {
          for at in positions {
            TandemRaces.AddPoint(marked, new Vector4(at.X, at.Y, at.Z, 1.0));
          }
        }
        route = this.Fit(marked);
        found += s", the objective's mappins \(ArraySize(marked))";
      }
    }
    let changed = ArraySize(route) != ArraySize(before);
    let i = 0;
    while !changed && i < ArraySize(route) {
      changed = Vector4.Distance(route[i], before[i]) > 1.0;
      i += 1;
    }
    this.m_route = route;
    if changed || say {
      TandemNative.Log(s"races: our race's route: \(ArraySize(route) > 0 ? s"\(ArraySize(route)) checkpoint(s)" : "not known") (\(this.m_count) on the HUD; \(found))");
      this.SayRoute();
    }
  }

  // The positions of an entry's map pins (every state), in journal order.
  private func PinsOf(entry: wref<JournalEntry>) -> array<Vector4> {
    let out: array<Vector4>;
    let journal = GameInstance.GetJournalManager(this.m_game);
    let mappins = GameInstance.GetMappinSystem(this.m_game);
    let children = this.ChildrenOf(entry);
    for child in children {
      if IsDefined(child as JournalQuestMapPinBase) {
        let at = new Vector3(0.0, 0.0, 0.0);
        if mappins.GetQuestMappinPosition(Cast<Uint32>(journal.GetEntryHash(child)), at) {
          TandemRaces.AddPoint(out, new Vector4(at.X, at.Y, at.Z, 1.0));
        }
      }
    }
    return out;
  }

  private func ChildrenOf(entry: wref<JournalEntry>) -> array<wref<JournalEntry>> {
    let children: array<wref<JournalEntry>>;
    let filter: JournalRequestStateFilter;
    filter.inactive = true;
    filter.active = true;
    filter.succeeded = true;
    filter.failed = true;
    if IsDefined(entry) {
      GameInstance.GetJournalManager(this.m_game).GetChildren(entry, filter, children);
    }
    return children;
  }

  // A point once (a pin within 5 m of another is the same checkpoint).
  private static func AddPoint(out points: array<Vector4>, at: Vector4) -> Void {
    if at.X == 0.0 && at.Y == 0.0 && at.Z == 0.0 {
      return;
    }
    for point in points {
      if Vector4.Distance(point, at) < 5.0 {
        return;
      }
    }
    ArrayPush(points, at);
  }

  // Pins as the route of our HUD's count, or none.
  private func Fit(pins: array<Vector4>) -> array<Vector4> {
    let out: array<Vector4>;
    let points = pins;
    let count = this.m_count;
    if ArraySize(points) == 0 || count <= 0 || count > 64 {
      return out;
    }
    if ArraySize(points) == count + 1 {
      let nearest = -1;
      let best = 50.0;
      let i = 0;
      while i < ArraySize(points) {
        let distance = Vector4.Distance(points[i], this.m_start);
        if distance < best {
          best = distance;
          nearest = i;
        }
        i += 1;
      }
      if nearest >= 0 {
        ArrayErase(points, nearest);
      }
    }
    let n = ArraySize(points);
    if n == 0 || Vector4.Distance(points[0], this.m_start) > 3000.0 {
      return out;
    }
    if n == count {
      return points;
    }
    // Laps: the same checkpoints again.
    if n >= 2 && n < count && count % n == 0 {
      let k = 0;
      while k < count {
        ArrayPush(out, points[k % n]);
        k += 1;
      }
    }
    return out;
  }

  // Our checkpoint against the route's: one far from it (a wrong order, pins
  // that were not the checkpoints) drops the route for everyone.
  private func CheckRoute() -> Void {
    let k = ArraySize(this.m_points);
    if k == 0 || ArraySize(this.m_route) < k {
      return;
    }
    let off = Vector4.Distance(this.m_points[k - 1], this.m_route[k - 1]);
    if off <= 80.0 {
      return;
    }
    TandemNative.Log(s"races: our checkpoint \(k) is \(off) m from the route's: the route is dropped");
    ArrayClear(this.m_route);
    for racer in this.m_racers {
      if EntityID.IsDefined(racer.body) {
        TandemRaces.ResetCredit(racer);
      }
    }
    this.SayRoute();
  }

  private func SayRoute() -> Void {
    if this.m_phase == 0 || this.m_phase == 4 || this.m_you == 0u {
      return;
    }
    TandemActivity.StageRacePoints(this.m_route);
    TandemActivity.SendRace(7, this.m_you, 0u, this.m_race, this.m_rewardsOf, 0, this.m_count, 0.0, this.m_heading, -1.0, TDBID.None(), this.m_quest, this.m_start);
  }

  // Which race of The Beast in Me (sq024) this is, from the tracked entry's
  // journal ids (proto kRaceRewards*; 0: another race, or not told).
  private func RewardsOf() -> Int32 {
    let journal = GameInstance.GetJournalManager(this.m_game);
    let entry = journal.GetTrackedEntry();
    let ids = "";
    let i = 0;
    while IsDefined(entry) && i < 6 {
      ids += StrLower(entry.GetId()) + "/";
      entry = journal.GetParentEntry(entry);
      i += 1;
    }
    let which = 0;
    if StrContains(ids, "sq024") {
      if StrContains(ids, "badlands") {
        which = 1;
      } else if StrContains(ids, "santo") || StrContains(ids, "domingo") {
        which = 4;
      } else if StrContains(ids, "big") {
        which = 2;
      } else if StrContains(ids, "city") {
        which = 3;
      }
    }
    TandemNative.Log(s"races: our race's journal \(ids): rewards \(which)");
    return which;
  }

  // The race's own reward records (QuestRewards, quest_rewards/side_quests.tweak).
  public static func RewardRecord(which: Int32, win: Bool) -> TweakDBID {
    let names = ["", "badlands", "big", "city", "santo_domingo"];
    if which < 1 || which >= ArraySize(names) {
      return TDBID.None();
    }
    return TDBID.Create(s"QuestRewards.sq024_\(win ? "win" : "finish")_\(names[which])_race");
  }

  // A reward our V got that names its race: what we tell the joiners.
  private func RewardsFrom(reward: TweakDBID) -> Int32 {
    let which = 1;
    while which <= 4 {
      if reward == TandemRaces.RewardRecord(which, true) || reward == TandemRaces.RewardRecord(which, false) {
        return which;
      }
      which += 1;
    }
    return 0;
  }

  // ---- theirs: a partner's race ----

  private func HeardInfo(head: TandemRaceEvent, points: array<Vector4>) -> Void {
    if head.starter != head.player {
      return;
    }
    // One race at a time: the first starter heard keeps us until its race
    // is over or goes quiet (TickTheirs), joined or not. Another starter's
    // meanwhile is not followed (two starters flapped the offer and its
    // notices).
    if this.m_starter != 0u && this.m_starter != head.starter {
      return;
    }
    let fresh = this.m_starter != head.starter || this.m_theirRace != head.race;
    if fresh {
      if head.arg >= 3 {
        return;
      }
      this.EndTheirs();
      this.m_starter = head.starter;
      this.m_theirRace = head.race;
      this.m_finishedSaid = false;
      TandemNative.Log(s"races: player \(head.starter)'s race \(head.race): phase \(head.arg), \(head.checkpoints) checkpoint(s)");
      if head.arg == 1 {
        this.Notice("Race-Offer", head.starter);
      } else {
        this.Notice("Contest-Race", head.starter);
      }
    }
    this.m_heardAt = this.m_now;
    let before = this.m_theirPhase;
    this.m_theirPhase = head.arg;
    this.m_theirCount = head.checkpoints;
    this.m_theirStart = new Vector4(head.x, head.y, head.z, 1.0);
    this.m_theirHeading = head.heading;
    this.m_theirCar = head.record;
    this.m_theirQuest = head.questHash;
    this.m_theirPoints = points;
    if head.checkpoint == 1 && !this.m_theirCountdown {
      this.m_theirCountdown = true;
      if this.m_joined == 3 {
        this.Notice("Race-Ready", head.starter);
      }
    }
    if head.arg == 2 && before != 2 {
      this.m_theirGoAt = head.ago >= 0.0 ? this.m_now - head.ago : this.m_now;
      if this.m_joined == 3 {
        this.Release(true);
        if IsDefined(this.m_me) {
          this.m_me.at = this.m_theirGoAt;
        }
        this.Notice("Race-Go", head.starter);
      } else if this.m_joined == 1 || this.m_joined == 2 {
        // Not on the grid in time: we watch.
        this.m_joined = 4;
        this.Notice("Race-TooLate", head.starter);
      }
    }
    if head.arg >= 3 {
      this.Notice(head.arg == 4 ? "Race-Abandoned" : "Race-Over", head.starter);
      this.EndTheirs();
    }
  }

  private func HeardSeat(head: TandemRaceEvent) -> Void {
    if head.to != this.m_you || head.starter != this.m_starter || head.race != this.m_theirRace || this.m_joined != 1 {
      return;
    }
    if head.arg <= 0 {
      this.m_joined = 4;
      this.Notice("Race-TooLate", this.m_starter);
      return;
    }
    this.m_slot = head.arg;
    this.m_joined = 2;
    this.m_placing = 0;
    this.m_placingAt = this.m_now;
    TandemNative.Log(s"races: seated in slot \(this.m_slot) of player \(this.m_starter)'s race");
  }

  private func HeardStandings(head: TandemRaceEvent, entries: array<TandemRaceEvent>) -> Void {
    if head.starter != this.m_starter || head.race != this.m_theirRace {
      return;
    }
    this.m_heardAt = this.m_now;
    let board: array<ref<TandemRacer>>;
    for entry in entries {
      let racer = new TandemRacer();
      racer.who = entry.who;
      racer.checkpoint = entry.checkpoint;
      racer.place = entry.place;
      racer.finished = (entry.flags & 1) != 0;
      racer.out = (entry.flags & 2) != 0;
      racer.at = entry.ago >= 0.0 ? this.m_now - entry.ago : 0.0;
      ArrayPush(board, racer);
      if racer.who == this.m_you && racer.finished && !this.m_finishedSaid {
        this.m_finishedSaid = true;
        this.NoticeText("Race-Finished", IntToString(racer.place));
      }
      if racer.who == this.m_you && racer.finished && this.m_ownFinishAt == 0.0 {
        this.m_ownFinishAt = this.m_now;
      }
    }
    this.m_standings = board;
    this.m_standingsAt = this.m_now;
  }

  private func HeardReward(head: TandemRaceEvent) -> Void {
    if head.to != this.m_you {
      return;
    }
    let story = TandemStorySystem.Get(this.m_game);
    if !IsDefined(story) {
      TandemNative.Log(s"races: reward \(TDBID.ToStringDEBUG(head.record)) of player \(head.starter)'s race not paid: no story system");
      return;
    }
    // The race quest's key in the ledger, as its starter's game says it.
    if head.questHash == 0u {
      return;
    }
    story.PayRace(head.record, head.questHash);
  }

  // The starter's route: every checkpoint in order (none: not known, we
  // follow those it names), and which race's rewards are ours to pay.
  private func HeardRoute(head: TandemRaceEvent, points: array<Vector4>) -> Void {
    if head.starter != this.m_starter || head.race != this.m_theirRace {
      return;
    }
    this.m_theirRewards = head.arg;
    let whole = ArraySize(points) > 0 && ArraySize(points) == head.checkpoints;
    let before = ArraySize(this.m_theirRoute);
    if whole {
      this.m_theirRoute = points;
    } else {
      ArrayClear(this.m_theirRoute);
    }
    if before != ArraySize(this.m_theirRoute) {
      TandemNative.Log(s"races: player \(this.m_starter)'s route: \(whole ? s"\(ArraySize(points)) checkpoint(s)" : "not known"), rewards \(head.arg)");
      // Our checkpoints counted again against the other list.
      if IsDefined(this.m_me) && !this.m_me.finished {
        TandemRaces.ResetCredit(this.m_me);
      }
      this.Unmark();
    }
  }

  // The checkpoints we race: the whole route, else those the starter named.
  private func TheirList() -> array<Vector4> {
    return ArraySize(this.m_theirRoute) > 0 ? this.m_theirRoute : this.m_theirPoints;
  }

  private static func ResetCredit(racer: ref<TandemRacer>) -> Void {
    racer.checkpoint = 0;
    racer.finished = false;
    racer.scanned = 1;
    racer.from = 1;
    racer.known = -1;
  }

  // Our own reward for the starter's race, whatever the starter's result,
  // once: the race's finish reward when we finished, its win reward when the
  // starter's standings put us ahead of every NPC racer (first, when it saw
  // none), from our own TweakDB, through the ledger (PayRace: not twice, not
  // when that quest was done alone; our quest state untouched).
  private func PayOwn() -> Void {
    if this.m_paidOwn || !IsDefined(this.m_me) || (this.m_joined != 3 && this.m_joined != 4) {
      return;
    }
    let mine: ref<TandemRacer>;
    let npcFirst = 1000;
    for racer in this.m_standings {
      if racer.who == this.m_you {
        mine = racer;
      } else if (racer.who & TandemRaces.NpcBit()) != 0u && racer.place < npcFirst {
        npcFirst = racer.place;
      }
    }
    let finished = this.m_me.finished || (IsDefined(mine) && mine.finished);
    if !finished {
      return;
    }
    this.m_paidOwn = true;
    if this.m_theirRewards <= 0 {
      TandemNative.Log(s"races: no rewards of our own known for player \(this.m_starter)'s race");
      return;
    }
    let won = IsDefined(mine) && mine.finished && (npcFirst < 1000 ? mine.place < npcFirst : mine.place == 1);
    let story = TandemStorySystem.Get(this.m_game);
    if !IsDefined(story) {
      TandemNative.Log("races: our own race rewards not paid: no story system");
      return;
    }
    let key = this.m_theirQuest != 0u ? this.m_theirQuest : TandemRaces.PlaceKey(this.m_theirStart);
    let records: array<TweakDBID>;
    ArrayPush(records, TandemRaces.RewardRecord(this.m_theirRewards, false));
    if won {
      ArrayPush(records, TandemRaces.RewardRecord(this.m_theirRewards, true));
    }
    TandemNative.Log(s"races: we finished player \(this.m_starter)'s race \(won ? "ahead of every NPC racer" : "behind an NPC racer"): our own rewards");
    for record in records {
      if IsDefined(TweakDBInterface.GetRewardBaseRecord(record)) {
        story.PayRace(record, key);
      } else {
        TandemNative.Log(s"races: reward \(TDBID.ToStringDEBUG(record)) is not in our TweakDB");
      }
    }
  }

  private static func PlaceKey(at: Vector4) -> Uint32 {
    let x = RoundF(at.X / 20.0);
    let y = RoundF(at.Y / 20.0);
    return Cast<Uint32>(x * 73856 + y * 19349663) | 1u;
  }

  private func TickTheirs() -> Void {
    if this.m_starter == 0u {
      return;
    }
    // The starter went quiet (gone, crashed): its race is over for us.
    if this.m_now - this.m_heardAt > 10.0 {
      TandemNative.Log(s"races: player \(this.m_starter)'s race went quiet");
      this.Notice("Race-Abandoned", this.m_starter);
      this.EndTheirs();
      return;
    }
    if this.m_joined == 2 {
      this.Place();
    }
    if this.m_joined == 3 {
      this.Drive();
    }
    if this.m_ownFinishAt > 0.0 && this.m_now - this.m_ownFinishAt >= 5.0 {
      this.PayOwn();
    }
  }

  // Seated: our V out of any car, at the grid; our race car in our slot
  // (RaceCar); our V in it, its engine held until the start.
  private func Place() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if this.m_now - this.m_placingAt > 20.0 {
      TandemNative.Log("races: could not get into the race car in time: we watch");
      TandemDegraded.Fallback("race watched", "could not get into the race car in time: we watch");
      this.Notice("Race-NoCar", this.m_starter);
      this.Leave();
      return;
    }
    let facility = GameInstance.GetMountingFacility(this.m_game);
    if this.m_placing == 0 {
      let record = this.RaceCar();
      if !TDBID.IsValid(record) {
        TandemNative.Log(s"races: the race's car \(TDBID.ToStringDEBUG(this.m_theirCar)) is no vehicle we can spawn");
        this.Notice("Race-NoCar", this.m_starter);
        this.Leave();
        return;
      }
      if facility.IsMountedToAnything(player.GetEntityID()) {
        let car: wref<VehicleObject>;
        let slot = n"None";
        if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
          VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
          TandemVehicles.Unseat(this.m_game, player, car.GetEntityID(), slot);
        }
        return;
      }
      let spot = this.SlotAt(this.m_slot);
      let side = this.Right();
      let stand = new Vector4(spot.X + side.X * 3.0, spot.Y + side.Y * 3.0, spot.Z + 0.5, 1.0);
      TandemTravel.ExpectMove(this.m_game);
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, stand, TandemYawAngles(this.m_theirHeading));
      let spec = new DynamicEntitySpec();
      spec.recordID = record;
      spec.position = new Vector4(spot.X, spot.Y, spot.Z + 0.3, 1.0);
      spec.orientation = EulerAngles.ToQuat(TandemYawAngles(this.m_theirHeading));
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      ArrayPush(spec.tags, n"Tandem.RaceCar");
      this.m_ourCar = TandemEntities.Create(spec);
      this.m_placing = 1;
      TandemNative.Log(s"races: our race car \(TDBID.ToStringDEBUG(record)) spawning in slot \(this.m_slot)");
      return;
    }
    let car = GameInstance.FindEntityByID(this.m_game, this.m_ourCar) as VehicleObject;
    if !IsDefined(car) {
      return;
    }
    if this.m_placing == 1 {
      this.Mount(player, car);
      this.m_placing = 2;
      return;
    }
    let mine: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, mine) && IsDefined(mine) && mine.GetEntityID() == this.m_ourCar {
      this.m_joined = 3;
      this.m_carOutSince = 0.0;
      this.Hold(car, this.m_theirPhase != 2);
      this.m_me = new TandemRacer();
      this.m_me.who = this.m_you;
      this.m_me.at = this.m_theirGoAt;
      this.m_lastProgress = 0.0;
      TandemNative.Log("races: in our race car on the grid");
      if this.m_theirPhase == 2 {
        this.Notice("Race-Go", this.m_starter);
      } else {
        this.Notice("Race-Grid", this.m_starter);
      }
    }
  }

  // tandem.ini race_car: own (default), our garage's active car, as a
  // spawned copy of its record; copy, or no car of our own: the starter's.
  // None: neither is a vehicle we can spawn.
  private func RaceCar() -> TweakDBID {
    let theirs = TDBID.IsValid(this.m_theirCar) && IsDefined(TweakDBInterface.GetVehicleRecord(this.m_theirCar)) ? this.m_theirCar : TDBID.None();
    if Equals(TandemNative.Setting("race_car"), "copy") {
      return theirs;
    }
    let active = GameInstance.GetVehicleSystem(this.m_game).GetActivePlayerVehicle(gamedataVehicleType.Car);
    let own = active.recordID;
    if TDBID.IsValid(own) && IsDefined(TweakDBInterface.GetVehicleRecord(own)) {
      return own;
    }
    TandemNative.Log("races: no car of our own (race_car own): a copy of the race's");
    return theirs;
  }

  // Where slot `slot` is: behind the start line, two abreast.
  private func SlotAt(slot: Int32) -> Vector4 {
    let forward = this.Forward();
    let side = this.Right();
    let back = 10.0 + 7.0 * Cast<Float>((slot - 1) / 2);
    let across = slot % 2 == 1 ? -2.5 : 2.5;
    return new Vector4(this.m_theirStart.X - forward.X * back + side.X * across, this.m_theirStart.Y - forward.Y * back + side.Y * across, this.m_theirStart.Z, 1.0);
  }

  private func Forward() -> Vector4 {
    return Quaternion.GetForward(EulerAngles.ToQuat(TandemYawAngles(this.m_theirHeading)));
  }

  private func Right() -> Vector4 {
    return Quaternion.GetRight(EulerAngles.ToQuat(TandemYawAngles(this.m_theirHeading)));
  }

  // Our V into the driver's seat, with the entry animation (as Pilot.reds).
  private func Mount(player: ref<PlayerPuppet>, car: ref<VehicleObject>) -> Void {
    let slot = n"seat_front_left";
    let info: MountingInfo;
    info.parentId = car.GetEntityID();
    info.childId = player.GetEntityID();
    info.slotId.id = slot;
    let request = new MountingRequest();
    request.lowLevelMountingInfo = info;
    request.preservePositionAfterMounting = false;
    request.mountData = new MountEventData();
    request.mountData.isInstant = false;
    request.mountData.slotName = slot;
    request.mountData.mountParentEntityId = car.GetEntityID();
    let options = new MountEventOptions();
    options.alive = true;
    options.occupiedByNonFriendly = false;
    request.mountData.mountEventOptions = options;
    GameInstance.GetMountingFacility(this.m_game).Mount(request);
  }

  // The race car's engine held on the grid (the quest's own engine event).
  private func Hold(car: ref<VehicleObject>, held: Bool) -> Void {
    if !IsDefined(car) {
      return;
    }
    let evt = new VehicleQuestToggleEngineEvent();
    evt.toggle = !held;
    evt.lockState = held ? VehicleQuestEngineLockState.Lock : VehicleQuestEngineLockState.Unlock;
    car.QueueEvent(evt);
  }

  private func Release(go: Bool) -> Void {
    let car = GameInstance.FindEntityByID(this.m_game, this.m_ourCar) as VehicleObject;
    if IsDefined(car) {
      this.Hold(car, !go);
    }
  }

  // Racing: our track through the starter's checkpoints; said when it
  // moves on and every 2 s.
  private func Drive() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || !IsDefined(this.m_me) {
      return;
    }
    let mine: wref<VehicleObject>;
    let inCar = VehicleComponent.GetVehicle(this.m_game, player, mine) && IsDefined(mine) && mine.GetEntityID() == this.m_ourCar;
    if !inCar && this.m_theirPhase == 2 && !this.m_me.finished {
      if this.m_carOutSince == 0.0 {
        this.m_carOutSince = this.m_now;
      } else if this.m_now - this.m_carOutSince > 10.0 {
        TandemNative.Log("races: out of our race car for 10 s: we left the race");
        this.Notice("Race-Left", this.m_starter);
        this.Leave();
        return;
      }
    } else {
      this.m_carOutSince = 0.0;
    }
    if this.m_theirPhase != 2 {
      return;
    }
    let before = this.m_me.checkpoint;
    let wasDone = this.m_me.finished;
    let list = this.TheirList();
    this.Sample(this.m_me, player.GetWorldPosition());
    this.Credit(this.m_me, list, this.m_theirCount);
    let metres = 0.0;
    if this.m_me.checkpoint < ArraySize(list) {
      metres = Vector4.Distance(player.GetWorldPosition(), list[this.m_me.checkpoint]);
    }
    this.Mark(list);
    if this.m_me.checkpoint != before || NotEquals(this.m_me.finished, wasDone) || this.m_now - this.m_lastProgress >= 2.0 {
      this.m_lastProgress = this.m_now;
      if this.m_me.checkpoint != before {
        TandemNative.Log(s"races: we passed checkpoint \(this.m_me.checkpoint)/\(this.m_theirCount)\(this.m_me.finished ? ": finished" : "")");
      }
      this.Say(4, 0u, this.m_me.finished ? 1 : 0, this.m_me.checkpoint, metres, MaxF(this.m_now - this.m_me.at, 0.0), TDBID.None());
    }
  }

  // The next checkpoint we know of, as a marker on our map and HUD.
  private func Mark(list: array<Vector4>) -> Void {
    let next = IsDefined(this.m_me) && !this.m_me.finished && this.m_me.checkpoint < ArraySize(list) ? this.m_me.checkpoint : -1;
    if next == this.m_markFor {
      return;
    }
    this.Unmark();
    this.m_markFor = next;
    let comms = TandemComms.Of(this.m_game);
    if next >= 0 && IsDefined(comms) {
      this.m_mark = comms.PlaceMarker(list[next], n"racing", TandemText.F("Race-Checkpoint", IntToString(next + 1)), this.m_starter);
    }
  }

  private func Unmark() -> Void {
    if this.m_mark.value != 0ul {
      GameInstance.GetMappinSystem(this.m_game).UnregisterMappin(this.m_mark);
    }
    this.m_mark.value = 0ul;
    this.m_markFor = -1;
  }

  // We leave the partner's race (or never got in): said, and we watch.
  private func Leave() -> Void {
    if this.m_joined != 0 && this.m_joined != 4 && this.m_starter != 0u {
      this.Say(2, 0u, 0, 0, 0.0, -1.0, TDBID.None());
    }
    this.m_joined = 4;
    this.Unmark();
    this.Release(true);
    this.m_carDropAt = this.m_now;
  }

  private func EndTheirs() -> Void {
    this.PayOwn();
    if this.m_joined == 1 || this.m_joined == 2 || this.m_joined == 3 {
      this.Release(true);
      this.m_carDropAt = this.m_now;
    }
    this.Unmark();
    this.HideOffer();
    this.m_starter = 0u;
    this.m_theirRace = 0u;
    this.m_theirPhase = 0;
    this.m_theirCountdown = false;
    this.m_joined = 0;
    this.m_me = null;
    ArrayClear(this.m_theirPoints);
    ArrayClear(this.m_theirRoute);
    this.m_theirRewards = 0;
    this.m_ownFinishAt = 0.0;
    this.m_paidOwn = false;
  }

  // Our race car goes once the race is over for us and our V is out of it.
  private func DropCar() -> Void {
    if !EntityID.IsDefined(this.m_ourCar) || this.m_joined == 2 || this.m_joined == 3 {
      return;
    }
    let player = GetPlayer(this.m_game);
    let mine: wref<VehicleObject>;
    if IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, mine) && IsDefined(mine) && mine.GetEntityID() == this.m_ourCar {
      this.m_carDropAt = this.m_now;
      return;
    }
    if this.m_now - this.m_carDropAt > 5.0 || this.m_now < this.m_carDropAt {
      TandemEntities.Delete(this.m_ourCar);
      TandemNative.Log("races: our race car is gone");
      this.m_ourCar = EntityID.FromHash(0ul);
    }
  }

  // ---- the offer ----

  private func Offerable() -> Bool {
    let player = GetPlayer(this.m_game);
    return this.m_starter != 0u && this.m_theirPhase == 1 && this.m_joined == 0 && IsDefined(player) && this.m_phase != 1 && this.m_phase != 2 && !GameInstance.GetRacingSystem(this.m_game).IsRaceInProgress() && Vector4.Distance(player.GetWorldPosition(), this.m_theirStart) <= TandemRaces.OfferRange();
  }

  private func TickOffer() -> Void {
    if !this.Offerable() {
      this.HideOffer();
      this.m_pressed = false;
      return;
    }
    if this.m_pressed {
      this.m_pressed = false;
      this.Join();
      return;
    }
    if this.m_offered {
      return;
    }
    let travel = TandemTravel.Of(this.m_game);
    if IsDefined(travel) && travel.Showing() {
      return;
    }
    this.m_offered = true;
    this.Hint(true, TandemText.F("Race-Join", this.NameOf(this.m_starter)));
    let local = GetPlayer(this.m_game);
    if IsDefined(local) && !this.m_listening {
      local.RegisterInputListener(this, n"PhoneInteract");
      this.m_listening = true;
      this.m_held = false;
    }
  }

  private func HideOffer() -> Void {
    if this.m_offered {
      this.Hint(false, "");
    }
    this.m_offered = false;
    if this.m_listening {
      let local = GetPlayer(this.m_game);
      if IsDefined(local) {
        local.UnregisterInputListener(this, n"PhoneInteract");
      }
      this.m_listening = false;
    }
  }

  // The phone key while the offer is shown: a press joins (a hold is the
  // phone's own). Only queued (as our HUD's modes, 101 held, 102 released);
  // acted on at the tick (OnKey).
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if !this.m_offered || !Equals(ListenerAction.GetName(action), n"PhoneInteract") {
      return false;
    }
    let kind = ListenerAction.GetType(action);
    if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      TandemActivity.QueueRaceUi(101, 0, 0);
    } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
      TandemActivity.QueueRaceUi(102, 0, 0);
    }
    return false;
  }

  private func OnKey(mode: Int32) -> Void {
    if mode == 101 {
      this.m_held = true;
    } else if this.m_held {
      this.m_held = false;
    } else if this.m_offered {
      this.m_pressed = true;
    }
  }

  public func Join() -> Bool {
    if this.m_starter == 0u || (this.m_joined != 0 && this.m_joined != 4) {
      TandemNative.Log("races: no partner's race to join");
      return false;
    }
    this.HideOffer();
    this.m_joined = 1;
    this.Say(2, 0u, 1, 0, 0.0, -1.0, TDBID.None());
    TandemNative.Log(s"races: asking to join player \(this.m_starter)'s race \(this.m_theirRace)");
    this.Notice("Race-Joining", this.m_starter);
    return true;
  }

  private func Hint(show: Bool, label: String) -> Void {
    let data: InputHintData;
    data.action = n"PhoneInteract";
    data.source = n"TandemRace";
    data.localizedLabel = label;
    data.queuePriority = 9;
    data.sortingPriority = 9;
    data.holdIndicationType = inkInputHintHoldIndicationType.Press;
    let evt = new UpdateInputHintEvent();
    evt.data = data;
    evt.show = show;
    evt.targetHintContainer = n"GameplayInputHelper";
    GameInstance.GetUISystem(this.m_game).QueueEvent(evt);
  }

  // ---- checkpoints, on a track ----

  private func Sample(racer: ref<TandemRacer>, at: Vector4) -> Void {
    let size = ArraySize(racer.trail);
    if size > 0 && Vector4.Distance(racer.trail[size - 1], at) < 1.0 {
      return;
    }
    // Ten minutes at four a second: older half dropped.
    if size >= 2400 {
      let trail: array<Vector4>;
      let times: array<Float>;
      let i = 1200;
      while i < size {
        ArrayPush(trail, racer.trail[i]);
        ArrayPush(times, racer.times[i]);
        i += 1;
      }
      racer.trail = trail;
      racer.times = times;
      racer.scanned = Max(racer.scanned - 1200, 1);
      racer.from = Max(racer.from - 1200, 1);
    }
    ArrayPush(racer.trail, at);
    ArrayPush(racer.times, this.m_now);
  }

  // The checkpoints its track went through, in order, at the time it did.
  private func Credit(racer: ref<TandemRacer>, points: array<Vector4>, count: Int32) -> Void {
    if racer.finished {
      return;
    }
    // New points named since: look again from its last crossing.
    if racer.known != ArraySize(points) {
      racer.known = ArraySize(points);
      racer.scanned = racer.from;
    }
    let size = ArraySize(racer.trail);
    let going = true;
    while going && racer.checkpoint < ArraySize(points) {
      let point = points[racer.checkpoint];
      let i = Max(racer.scanned, 1);
      let hit = -1.0;
      while hit < 0.0 && i < size {
        hit = TandemRaces.Through(racer.trail[i - 1], racer.trail[i], point, TandemRaces.Radius());
        if hit < 0.0 {
          i += 1;
        }
      }
      if hit >= 0.0 {
        racer.checkpoint += 1;
        racer.at = racer.times[i - 1] + (racer.times[i] - racer.times[i - 1]) * hit;
        racer.from = i;
        racer.scanned = i;
      } else {
        racer.scanned = Max(size - 1, 1);
        going = false;
      }
    }
    if count > 0 && racer.checkpoint >= count {
      racer.finished = true;
    }
  }

  // Where along the segment a to b it passes within `radius` of `point`
  // (0..1), or -1.
  public static func Through(a: Vector4, b: Vector4, point: Vector4, radius: Float) -> Float {
    let dx = b.X - a.X;
    let dy = b.Y - a.Y;
    let dz = b.Z - a.Z;
    let length = dx * dx + dy * dy + dz * dz;
    let t = 0.0;
    if length > 0.0001 {
      t = ClampF(((point.X - a.X) * dx + (point.Y - a.Y) * dy + (point.Z - a.Z) * dz) / length, 0.0, 1.0);
    }
    let x = a.X + dx * t - point.X;
    let y = a.Y + dy * t - point.Y;
    let z = a.Z + dz * t - point.Z;
    return SqrtF(x * x + y * y + z * z) <= radius ? t : -1.0;
  }

  // ---- the standings on the HUD ----

  private func Show() -> Void {
    // While a race goes on, and 20 s after its last word.
    let shown = ArraySize(this.m_standings) > 1 && this.m_now - this.m_standingsAt < 20.0 && this.m_now >= this.m_standingsAt;
    if !shown {
      if IsDefined(this.m_hud) {
        this.m_hud.SetVisible(false);
      }
      return;
    }
    if !this.Build() {
      return;
    }
    this.m_hud.SetVisible(true);
    let ours = this.m_phase >= 1 && this.m_phase <= 3 && this.m_standingsAt == this.m_lastBoard;
    let goAt = ours ? this.m_goAt : this.m_theirGoAt;
    let n = 0;
    for racer in this.m_standings {
      if n < ArraySize(this.m_lines) - 1 {
        let name = (racer.who & TandemRaces.NpcBit()) != 0u ? TandemText.F("Race-Npc", IntToString(Cast<Int32>(racer.who & 65535u))) : this.NameOf(racer.who);
        let state = "";
        if racer.out {
          state = TandemText.Get("Race-Out");
        } else if racer.finished && goAt > 0.0 {
          state = TandemRaces.Clock(racer.at - goAt);
        } else {
          state = s"\(racer.checkpoint)/\(ours ? this.m_count : this.m_theirCount)";
        }
        let line = this.m_lines[n + 1];
        line.SetText(s"\(racer.place)  \(name)  \(state)");
        line.SetVisible(true);
        n += 1;
      }
    }
    while n < ArraySize(this.m_lines) - 1 {
      this.m_lines[n + 1].SetVisible(false);
      n += 1;
    }
  }

  private static func Clock(seconds: Float) -> String {
    let total = MaxF(seconds, 0.0);
    let minutes = FloorF(total / 60.0);
    let rest = total - Cast<Float>(minutes) * 60.0;
    let tenths = Cast<Int32>(rest * 10.0);
    let whole = tenths / 10;
    return s"\(minutes):\(whole < 10 ? "0" : "")\(whole).\(tenths % 10)";
  }

  private func Build() -> Bool {
    if IsDefined(this.m_hud) {
      return true;
    }
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    let window = IsDefined(layer) ? layer.GetVirtualWindow() : null;
    if !IsDefined(window) {
      return false;
    }
    let root = new inkVerticalPanel();
    root.SetName(n"tandem_race");
    root.SetFitToContent(true);
    root.SetAnchor(inkEAnchor.TopRight);
    root.SetAnchorPoint(new Vector2(1.0, 0.0));
    // HUD units (3840 x 2160) in a window of pixels: drawn at the window's
    // scale, about its top right corner (PartyHud.reds, WindowScale).
    let scale = TandemPartyHud.WindowScale(this.m_game);
    root.SetRenderTransformPivot(new Vector2(1.0, 0.0));
    root.SetScale(new Vector2(scale, scale));
    root.SetMargin(new inkMargin(0.0, 760.0 * scale, 120.0 * scale, 0.0));
    root.SetInteractive(false);
    root.Reparent(window);
    this.m_hud = root;
    let i = 0;
    while i < 17 {
      let text = new inkText();
      text.SetName(StringToName(s"line\(i)"));
      text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
      text.SetFontStyle(i == 0 ? n"Medium" : n"Regular");
      text.SetFontSize(i == 0 ? 42 : 36);
      text.SetLetterCase(textLetterCase.UpperCase);
      text.SetStyle(r"base\\gameplay\\gui\\common\\main_colors.inkstyle");
      text.BindProperty(n"tintColor", n"MainColors.Red");
      text.SetFitToContent(true);
      text.SetInteractive(false);
      text.SetHAlign(inkEHorizontalAlign.Right);
      if i == 0 {
        text.SetText(TandemText.Get("Race-Standings"));
      } else {
        text.SetVisible(false);
      }
      text.Reparent(root);
      ArrayPush(this.m_lines, text);
      i += 1;
    }
    TandemNative.Log("races: standings widget built on the HUD layer");
    return true;
  }

  // ---- helpers ----

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  private func Notice(key: String, player: Uint32) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 4.0, key, this.NameOf(player));
  }

  private func NoticeText(key: String, a: String) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 4.0, key, a);
  }

  // tools/game.sh cmd contest races|join|leave
  public func Command(command: String) -> Void {
    let what = StrAfterFirst(command, " ");
    if Equals(what, "join") {
      this.Join();
    } else if Equals(what, "leave") {
      if this.m_joined != 0 && this.m_joined != 4 {
        this.Notice("Race-Left", this.m_starter);
        this.Leave();
      }
    }
    let racers = "";
    for racer in this.m_standings {
      racers += s" \(racer.place):\(racer.who) cp \(racer.checkpoint)\(racer.finished ? " finished" : "")\(racer.out ? " out" : "")";
    }
    TandemNative.Log(s"races: ours phase \(this.m_phase) race \(this.m_race) (\(ArraySize(this.m_points))/\(this.m_count) checkpoint(s) passed, route \(ArraySize(this.m_route)), rewards \(this.m_rewardsOf), \(ArraySize(this.m_joiners)) partner(s), \(this.m_npcs) npc racer(s), \(ArraySize(this.m_rewards)) reward(s)); theirs: player \(this.m_starter) race \(this.m_theirRace) phase \(this.m_theirPhase), joined \(this.m_joined) slot \(this.m_slot), \(ArraySize(this.m_theirPoints))/\(this.m_theirCount) checkpoint(s) named, route \(ArraySize(this.m_theirRoute)), rewards \(this.m_theirRewards)\(this.m_paidOwn ? " paid" : ""), ours \(IsDefined(this.m_me) ? this.m_me.checkpoint : -1); standings:\(StrLen(racers) > 0 ? racers : " none")");
  }
}

// Our race HUD's modes (final.reds:415728): the starter's race on its grid,
// counting down, started, over. Only queued (TandemRaces acts on its tick).
@wrapMethod(hudCarRaceController)
protected cb func OnForwardVehicleRaceUIEvent(evt: ref<ForwardVehicleRaceUIEvent>) -> Bool {
  if IsDefined(evt) {
    TandemActivity.QueueRaceUi(EnumInt(evt.mode), evt.maxPosition, evt.maxCheckpoints);
  }
  return wrappedMethod(evt);
}
