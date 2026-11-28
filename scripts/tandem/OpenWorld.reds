// The host's open-world activities on a guest's map (docs/architecture/
// story.md, "Open-world activities"; engine facts §13.167). The NCPD
// scanner's activities (hustles, reported crimes, gang watches, outposts,
// hidden stashes: the pins the game's own MinimapPOIMappinController
// .IsNcpdScanner names, final.reds (2.50):434330) belong to the world whose
// game leads where the players stand: the host's while a guest is grouped
// with it, the guest's own apart or leading its group (regions.md §9). Every
// three seconds, in a session, from Story.reds' tick (the game thread):
// - Host: its done pins (their phase CompletedPhase) by their journal path
//   hash (the same in every game), sent when they change
//   (TandemStory.ActivitiesSend).
// - Guest, grouped: its own activity phases wait, as its gigs do
//   (TandemStory.ActivitiesGrouped: the host's game runs the activity and
//   its NPCs, shown as proxies); each of its pins shows the host's state,
//   done or open, by the engine's own phase setter (TandemStory.SetPoiPhase;
//   core/openworld.hpp Mirror says which), and an activity the host's world
//   finishes with us near the host is credited in our ledger once.
// - Guest, apart (or leaving): every pin we set goes back to its own phase,
//   and our own activities run (their held signals given back). Nothing of
//   the host's is in our save: the session never saves, and the leave puts
//   the pins back before saving unlocks.
// Names checked against the 2.50 scripts and Codeware (MappinSystem
// .GetAllMappins); final.reds lines in the comments.

public class TandemOpenWorld {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  private let m_lastCheck: Float;
  private let m_lastScan: Float;
  // Our activity pins as of the last scan (every 30 s, or at once when
  // the host's set changed): parallel arrays.
  private let m_pins: array<ref<IMappin>>;
  private let m_markers: array<Uint32>;
  // Guest: how many of ours show the host's state (last check), the size
  // of the host's set as heard, our activities' last word to the freeze.
  private let m_shown: Int32;
  private let m_hostDone: Int32;
  private let m_waiting: Bool;
  private let m_toldWaiting: Bool;
  // Pins the engine would not set, said once each.
  private let m_refused: array<Uint32>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
    this.m_lastScan = -100.0;
  }

  // The NCPD scanner's pins, as the game's minimap tells them apart
  // (MinimapPOIMappinController.IsNcpdScanner, final.reds (2.50):434330).
  public static func IsNcpd(variant: gamedataMappinVariant) -> Bool {
    return Equals(variant, gamedataMappinVariant.MinorActivityVariant) || Equals(variant, gamedataMappinVariant.Zzz06_NCPDGigVariant) || Equals(variant, gamedataMappinVariant.GangWatchVariant) || Equals(variant, gamedataMappinVariant.OutpostVariant) || Equals(variant, gamedataMappinVariant.HiddenStashVariant);
  }

  // Every three seconds while in a session and the world plays.
  public func Tick(now: Float, isHost: Bool, grouped: Bool) -> Void {
    if now - this.m_lastCheck < 3.0 {
      return;
    }
    this.m_lastCheck = now;
    if now - this.m_lastScan >= 30.0 {
      this.Scan(now);
    }
    let phases = this.Phases();
    if isHost {
      TandemStory.ActivitiesSend(this.m_markers, phases);
      return;
    }
    // Whose world leads where we stand: the host's while grouped with it
    // (regions.md §9: the group's leader), ours apart or leading.
    let leader = TandemRegions.Leader();
    let waiting = grouped && leader != this.m_story.You();
    TandemStory.ActivitiesGrouped(waiting);
    if NotEquals(waiting, this.m_waiting) || !this.m_toldWaiting {
      this.m_waiting = waiting;
      this.m_toldWaiting = true;
      TandemNative.Log(s"openworld: our NCPD activities \(waiting ? "wait: the host's world leads here" : "run: ours lead here") (leader \(leader))");
    }
    // The pins show the host's world only while it is the one that leads
    // (a third player holding its region leads a world we have no word of:
    // ours stay ours).
    let mirrored = waiting && leader == this.m_story.HostPlayer();
    this.Credit(mirrored);
    let steps = TandemStory.ActivitiesSteps(mirrored, this.m_markers, phases);
    if ArraySize(steps) > 0 {
      let set = this.Apply(steps);
      TandemNative.Log(s"openworld: \(set) of \(ArraySize(steps)) pin(s) set \(mirrored ? "to the host's" : "back to our own (apart)")");
    }
  }

  // Every activity pin our map has, by its journal path hash.
  private func Scan(now: Float) -> Void {
    this.m_lastScan = now;
    ArrayClear(this.m_pins);
    ArrayClear(this.m_markers);
    let system = GameInstance.GetMappinSystem(this.m_game);
    if !IsDefined(system) {
      return;
    }
    let all = system.GetAllMappins();
    for mappin in all {
      let poi = mappin as PointOfInterestMappin;
      if IsDefined(poi) && TandemOpenWorld.IsNcpd(mappin.GetVariant()) {
        let marker = poi.GetJournalPathHash();
        if marker != 0u && !ArrayContains(this.m_markers, marker) {
          ArrayPush(this.m_pins, mappin);
          ArrayPush(this.m_markers, marker);
        }
      }
    }
  }

  // Each scanned pin's phase now (gamedataMappinPhase).
  private func Phases() -> array<Int32> {
    let out: array<Int32>; // assigned on every path
    for mappin in this.m_pins {
      ArrayPush(out, IsDefined(mappin) ? EnumInt(mappin.GetPhase()) : -1);
    }
    return out;
  }

  // Sets each step's pin the engine's way, read back: answers how many
  // took. One that does not is our own pin left as our game has it, said
  // once (the fallback).
  private func Apply(steps: array<TandemActivityStep>) -> Int32 {
    let set = 0;
    for step in steps {
      let marker = step.marker;
      let phase = step.phase;
      // Not on our map now: nothing shows it.
      let i = ArrayFindFirst(this.m_markers, marker);
      let mappin: ref<IMappin> = i >= 0 ? this.m_pins[i] : null;
      if IsDefined(mappin) {
        let took = TandemStory.SetPoiPhase(mappin, phase) && EnumInt(mappin.GetPhase()) == phase;
        if took {
          set += 1;
        } else if !ArrayContains(this.m_refused, marker) {
          ArrayPush(this.m_refused, marker);
          TandemDegraded.Fallback("activity not marked", s"pin \(marker) would not take phase \(phase)");
        }
      }
    }
    this.m_shown = set;
    return set;
  }

  // What the host's world finished since the last check: ours too when we
  // were grouped with it and near its V (the reward rule's 150 m), once.
  private func Credit(mirrored: Bool) -> Void {
    let added = TandemStory.ActivitiesAdded();
    if ArraySize(added) == 0 {
      return;
    }
    let distance = this.m_story.DistanceToHost();
    let near = mirrored && distance >= 0.0 && distance <= 150.0;
    for marker in added {
      if !near {
        TandemNative.Log(s"openworld: the host's world finished activity \(marker), we were not with it (\(distance) m)");
      } else {
        // Our own state of it: 3 done in our world, else 1 not started.
        let own = this.OwnDone(marker) ? 3 : 1;
        this.m_story.NoteActivity(marker, own);
      }
    }
    // The pins may have come or gone with it.
    this.m_lastScan = -100.0;
  }

  // Our own world has it done: the phase we kept of a pin we set, else the
  // pin's own (CompletedPhase 0).
  private func OwnDone(marker: Uint32) -> Bool {
    let kept = TandemStory.ActivityOwnPhase(marker);
    if kept >= 0 {
      return kept == 0;
    }
    let i = ArrayFindFirst(this.m_markers, marker);
    if i < 0 {
      return false;
    }
    let mappin = this.m_pins[i];
    return IsDefined(mappin) && EnumInt(mappin.GetPhase()) == 0;
  }

  // The host's set changed (Story.reds, event 47): looked at next check.
  public func OnHost(count: Int32) -> Void {
    if count != this.m_hostDone {
      TandemNative.Log(s"openworld: the host's world has \(count) NCPD activit\(count == 1 ? "y" : "ies") done");
    }
    this.m_hostDone = count;
    this.m_lastCheck = -100.0;
  }

  // The session is over (a guest: before its journal goes back): every pin
  // we set, back to its own phase; our activities run again.
  public func Leave(isGuest: Bool) -> Void {
    if isGuest {
      if ArraySize(this.m_pins) == 0 {
        this.Scan(0.0);
      }
      let steps = TandemStory.ActivitiesLift();
      if ArraySize(steps) > 0 {
        TandemNative.Log(s"openworld: \(this.Apply(steps)) of \(ArraySize(steps)) pin(s) back to our own (leave)");
      }
      TandemStory.ActivitiesGrouped(true);
    }
    this.m_shown = 0;
    this.m_hostDone = 0;
    this.m_toldWaiting = false;
    this.m_lastScan = -100.0;
    ArrayClear(this.m_pins);
    ArrayClear(this.m_markers);
    ArrayClear(this.m_refused);
  }

  // `cmd story activities` (counts); `cmd story activity open` and
  // `… done` (our nearest pin not done, or done: its marker, phase,
  // distance); `cmd story activity MARKER` (that pin, by its decimal hash).
  public func Report(what: String) -> Void {
    if ArraySize(this.m_pins) == 0 {
      this.Scan(0.0);
    }
    if Equals(what, "") {
      let done = 0;
      for mappin in this.m_pins {
        if IsDefined(mappin) && EnumInt(mappin.GetPhase()) == 0 {
          done += 1;
        }
      }
      TandemNative.Log(s"openworld: \(ArraySize(this.m_pins)) NCPD activity pins here, \(done) shown done; \(TandemStory.ActivitiesReport())");
      return;
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let at = player.GetWorldPosition();
    let wantOpen = Equals(what, "open");
    let wantDone = Equals(what, "done");
    let best = -1;
    let bestDistance = 1000000.0;
    let i = 0;
    while i < ArraySize(this.m_pins) {
      let mappin = this.m_pins[i];
      if IsDefined(mappin) {
        let phase = EnumInt(mappin.GetPhase());
        let fits = wantOpen ? phase != 0 : (wantDone ? phase == 0 : Equals(s"\(this.m_markers[i])", what));
        let where = mappin.GetWorldPosition();
        let distance = Vector4.Distance(at, where);
        if fits && distance < bestDistance {
          bestDistance = distance;
          best = i;
        }
      }
      i += 1;
    }
    if best < 0 {
      TandemNative.Log(s"openworld: no \(what) NCPD activity pin on our map");
      return;
    }
    let found = this.m_pins[best];
    TandemNative.Log(s"openworld: activity \(this.m_markers[best]) phase \(EnumInt(found.GetPhase())) at \(bestDistance) m (\(what))");
  }
}
