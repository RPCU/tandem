// The host's quest interiors in a guest's world, and the story's gates
// (docs/architecture/story.md, "Quest prefabs" and "Story gates";
// engine-knowledge §13.111, §13.112). Once a second, in a session, from
// Story.reds' tick (the game thread):
// - Quest prefabs: the host's game reads which prefabs its quests have
//   loaded (the quests system's prefab handler) and sends them when they
//   change; a guest grouped with the host loads the same ones in its own
//   world, the engine's way, and puts back what it loaded when it goes apart
//   or leaves (TandemStory.PrefabsTick, PrefabsLift; core/prefabs.hpp
//   Mirror). A game whose layout check failed loads nothing: a guest near
//   the host while the host's quests have some on is told it cannot see
//   them (the fallback).
// - Story gates: each game says where its own story lets its V go (the
//   Watson lockdown lifted: fact q101_done; Dogtown's gate open: fact
//   ep1_side_content) and which side of each its V is on (its district).
//   At the join both players are told in words what the host's story has
//   open and the guest's keeps closed; in play, a guest that stands at the
//   closed side while the host is past it is told how to follow (in the
//   host's car: the copy carries it across), never moved through.
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemStoryWorld {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  private let m_lastCheck: Float;
  // Quest prefabs: how many of the host's are on here (guest, last check),
  // how many the host's set has (as heard), whether the fallback was said
  // for this set, whether "unavailable" was logged.
  private let m_ours: Int32;
  private let m_hostPrefabs: Int32;
  private let m_missingTold: Bool;
  private let m_unavailableLogged: Bool;
  // Our game cannot load quest prefabs (the layout check failed), as of the
  // last check.
  private let m_unavailable: Bool;
  // Story gates: ours as last read (-1: not yet), the players whose join
  // notes were said this session.
  private let m_ourGates: Int32;
  private let m_noted: array<Uint32>;
  // Host: the guests whose gates we heard (noted once ours are read).
  private let m_heard: array<Uint32>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
    this.m_ourGates = -1;
  }

  // Once a second while in a session and the world plays.
  public func Tick(now: Float, isHost: Bool, grouped: Bool) -> Void {
    if now - this.m_lastCheck < 1.0 {
      return;
    }
    this.m_lastCheck = now;
    this.Prefabs(isHost, grouped);
    this.Gates(isHost, grouped);
  }

  // ---- quest prefabs ----

  private func Prefabs(isHost: Bool, grouped: Bool) -> Void {
    let count = TandemStory.PrefabsTick(isHost, grouped);
    this.m_unavailable = count < 0;
    if count < 0 {
      if !this.m_unavailableLogged {
        this.m_unavailableLogged = true;
        TandemNative.Log(s"prefabs: unavailable here (\(TandemStory.PrefabsReport()))");
      }
      // The fallback: the host's quest has an interior we cannot load and
      // we are near it (the NPC links' reach), once per set the host says.
      // Spectating on, we watch it instead (Spectate.reds, reason e) and
      // its own notice says so: one notice, not two.
      let distance = this.m_story.DistanceToHost();
      if !isHost && grouped && !TandemStory.SpectateOn() && this.m_hostPrefabs > 0 && !this.m_missingTold && distance >= 0.0 && distance < 150.0 {
        this.m_missingTold = true;
        this.m_story.Notice("Prefabs-Missing", this.m_story.NameOf(this.m_story.HostPlayer()));
        TandemNative.Log(s"prefabs: the host's quests have \(this.m_hostPrefabs) on, we cannot load them: told");
      }
      return;
    }
    if !isHost && count != this.m_ours {
      TandemNative.Log(s"prefabs: \(count) of the host's quest prefab(s) on in our world (\(grouped ? "grouped" : "apart"))");
      this.m_ours = count;
    }
  }

  // The host's set changed (Story.reds, event 45).
  public func OnHostPrefabs(count: Int32) -> Void {
    TandemNative.Log(s"prefabs: the host's quests have \(count) prefab(s) on");
    this.m_hostPrefabs = count;
    this.m_missingTold = false;
  }

  // The host's quests have interiors on that our game cannot load (its
  // layout check failed here): a guest watches them (Spectate.reds, (e)).
  public func CannotLoad() -> Bool {
    return this.m_unavailable && this.m_hostPrefabs > 0;
  }

  // ---- story gates ----

  // A gate our own story keeps closed lies between our V and the host's:
  // the Watson lockdown (one of us in Watson, the other not) or Dogtown's
  // gate (one of us inside). Spectating never carries our V across it, nor
  // joins the host there by a loading screen; a quest taking the host from
  // beside us is not followed either. The in-play notice says how: in the
  // host's car (Gates).
  public func GateBetween() -> Bool {
    if this.m_ourGates < 0 {
      return false;
    }
    let host = TandemStory.GatesOf(this.m_story.HostPlayer());
    if host < 0 {
      return false;
    }
    let behind = TandemStory.GatesBehind(host, this.m_ourGates);
    if (behind & 1) != 0 && (host & 4) != (this.m_ourGates & 4) {
      return true;
    }
    return (behind & 2) != 0 && (host & 8) != (this.m_ourGates & 8);
  }

  // Where our own story lets our V go, and where it is (StoryGates' bits).
  public func OurGates() -> Int32 {
    let flags = 0;
    let quests = GameInstance.GetQuestsSystem(this.m_game);
    // Playing for Time done: Act 2, the lockdown lifted (vanilla reads the
    // fact too, HubMenuUtility.CreateMenuData, final.reds:332681).
    if quests.GetFact(n"q101_done") >= 1 {
      flags += 1;
    }
    // Dogtown's gate (combat_zone_gate.questphase's pause condition).
    if quests.GetFact(n"ep1_side_content") >= 1 {
      flags += 2;
    }
    return flags + this.Where();
  }

  // 4 our V in Watson, 8 in Dogtown, 0 elsewhere: the district the
  // prevention system's district manager has (PreventionSystem.
  // GetCurrentDistrict, final.reds:138920; District.IsDogTown, :193970;
  // District_Record.ParentDistrict, :51044).
  private func Where() -> Int32 {
    let prevention = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PreventionSystem") as PreventionSystem;
    if !IsDefined(prevention) {
      return 0;
    }
    let district = prevention.GetCurrentDistrict();
    if !IsDefined(district) {
      return 0;
    }
    if district.IsDogTown() {
      return 8;
    }
    let record: wref<District_Record> = district.GetDistrictRecord();
    let depth = 0;
    while IsDefined(record) && depth < 4 {
      if Equals(record.Type(), gamedataDistrict.Watson) {
        return 4;
      }
      record = record.ParentDistrict();
      depth += 1;
    }
    return 0;
  }

  public static func Describe(flags: Int32) -> String {
    return s"Watson lockdown \((flags & 1) != 0 ? "lifted" : "on"), Dogtown \((flags & 2) != 0 ? "open" : "shut"), in Watson \((flags & 4) != 0), in Dogtown \((flags & 8) != 0)";
  }

  private func Gates(isHost: Bool, grouped: Bool) -> Void {
    let flags = this.OurGates();
    if flags != this.m_ourGates {
      this.m_ourGates = flags;
      TandemStory.SendGates(flags);
      TandemNative.Log(s"gates: ours now: \(TandemStoryWorld.Describe(flags))");
    }
    let host = this.m_story.HostPlayer();
    if isHost {
      for player in this.m_heard {
        this.Note(player);
      }
      return;
    }
    this.Note(host);
    if !grouped {
      return;
    }
    // In play: we stand at the closed side of a gate the host is past.
    let hostGates = TandemStory.GatesOf(host);
    if hostGates < 0 {
      return;
    }
    let met = TandemStory.GateMet(hostGates, flags, this.m_story.DistanceToHost());
    let name = this.m_story.NameOf(host);
    if met == 1 {
      this.m_story.Notice("Gates-WatsonMet", name);
      TandemNative.Log("gates: at the Watson lockdown our story keeps, the host past it: told to ride along");
    } else if met == 2 {
      this.m_story.Notice("Gates-DogtownMet", name);
      TandemNative.Log("gates: at Dogtown's gate our story keeps shut, the host inside: told to ride along");
    }
  }

  // A player's gates heard (Story.reds, event 46).
  public func OnGates(player: Uint32, flags: Int32) -> Void {
    TandemNative.Log(s"gates: \(this.m_story.NameOf(player)) says \(TandemStoryWorld.Describe(flags))");
    if !ArrayContains(this.m_heard, player) {
      ArrayPush(this.m_heard, player);
    }
    this.Note(player);
  }

  // The join notes between us and `player`, once a session: a guest hears
  // what the host's story has open and its own keeps closed, the host hears
  // the same of each guest. The lockdown first: a story still in it has not
  // reached Dogtown either.
  private func Note(player: Uint32) -> Void {
    if player == 0u || ArrayContains(this.m_noted, player) || this.m_ourGates < 0 {
      return;
    }
    let isHost = this.m_story.IsHostInSession();
    if !isHost && player != this.m_story.HostPlayer() {
      return;
    }
    let theirs = TandemStory.GatesOf(player);
    if theirs < 0 {
      return;
    }
    ArrayPush(this.m_noted, player);
    let behind = isHost ? TandemStory.GatesBehind(this.m_ourGates, theirs) : TandemStory.GatesBehind(theirs, this.m_ourGates);
    let name = this.m_story.NameOf(player);
    if (behind & 1) != 0 {
      this.m_story.Notice(isHost ? "Gates-WatsonBehindHost" : "Gates-WatsonBehind", name);
    } else if (behind & 2) != 0 {
      this.m_story.Notice(isHost ? "Gates-DogtownBehindHost" : "Gates-DogtownBehind", name);
    }
    let what = "the same gates open";
    if behind != 0 {
      what = "behind the host's story:";
      if (behind & 1) != 0 {
        what += " the Watson lockdown";
      }
      if (behind & 2) != 0 {
        what += " Dogtown";
      }
    }
    TandemNative.Log(s"gates: with \(name): \(what)");
  }

  // A player left: its notes may be said again if it comes back.
  public func OnPlayerGone(player: Uint32) -> Void {
    ArrayRemove(this.m_noted, player);
    ArrayRemove(this.m_heard, player);
  }

  // The session is over (a guest: before its journal goes back): what we
  // loaded of the host's world, off again; everything forgotten.
  public func Leave(isGuest: Bool) -> Void {
    if isGuest {
      let off = TandemStory.PrefabsLift();
      if off > 0 {
        TandemNative.Log(s"prefabs: \(off) of the host's quest prefab(s) off again (leave)");
      }
    }
    this.m_ours = 0;
    this.m_hostPrefabs = 0;
    this.m_missingTold = false;
    this.m_unavailable = false;
    this.m_ourGates = -1;
    ArrayClear(this.m_noted);
    ArrayClear(this.m_heard);
  }

  // `cmd story prefabs`, `cmd story gates`.
  public func Report(what: String) -> Void {
    if Equals(what, "prefabs") {
      TandemNative.Log(s"story: quest prefabs here: \(TandemStory.PrefabsReport())");
      return;
    }
    let host = this.m_story.HostPlayer();
    TandemNative.Log(s"story: gates ours \(TandemStoryWorld.Describe(this.OurGates())); the host's \(TandemStory.GatesOf(host)); the host \(this.m_story.DistanceToHost()) m away");
  }
}
