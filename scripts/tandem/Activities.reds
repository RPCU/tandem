// What players do outside combat and quests (docs/architecture/activities.md):
// time skips, workspots, menus and minigames, photo mode, looks after a
// mirror. Owned by TandemSystem, ticked four times a second.
//
// - Ours: what our V is busy with goes to the others (TandemActivity.SetOurs,
//   Activity): photo mode, a menu or minigame scenario (the wraps below), a
//   workspot (WorkspotGameSystem.IsActorInWorkspot, its resource, entry and
//   idle animation). Its body is anchored where the activity began; when our
//   game stands still (paused, or no time dilation left) the others are told
//   so and our poses stop (net_thread.cpp).
// - Theirs: a busy player's puppet is held at its anchor (no sliding off a
//   seat when its poses stop), plays the same workspot on its own graph
//   (TandemSpot, NpcAnims.reds) so its sounds and props come with it, and
//   idles on its own graph while that game stands still.
// - Time skips are never one player's alone: the wheel (TimeskipGameController,
//   wrapped below) and a bed (an unasked jump of our clock while in a
//   workspot) ask everyone else; each other player sees "NAME wants to ..."
//   with a hint on the phone key (press to agree; it lapses after 20 s);
//   only when all agree does the clock move, in every game, each fast
//   forwarding its own V. The host's story moving its clock is said, not
//   asked. Each game whose clock moved starts a new epoch (the crowd is new).
// - Leaving a mirror, the ripperdoc or the wardrobe sends our look again
//   (TandemSystem.RefreshLook; an unchanged look is sent to nobody).

public class TandemBusyPuppet {
  public let player: Uint32;
  public let state: TandemBusyState;
  public let held: Bool;
  public let placed: Bool;
  // When it was put at its anchor (its workspot starts once it is there).
  public let placedAt: Float;
  public let spot: ref<TandemSpot>;
  // Still to release: the state ended, its workspot exit is playing.
  public let ending: Bool;
}

public class TandemSkipPrompt {
  public let asker: Uint32;
  public let request: Uint32;
  public let reason: Int32;
  public let fromSeconds: Int32;
  public let toSeconds: Int32;
  public let heardAt: Float;
  public let shown: Bool;
}

public class TandemActivities {
  // How long the others have to answer (activity::kSkipAnswerUs).
  public static func AnswerSeconds() -> Float {
    return 20.0;
  }

  // A clock that ran this much faster than real time in one tick jumped
  // (game time runs about 8 times real time).
  public static func JumpSeconds() -> Int32 {
    return 600;
  }

  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_you: Uint32;
  private let m_now: Float;
  private let m_last: Float;

  // ---- ours ----
  // The menu scenario we are in (kind as proto::ActivityKind; 0: none),
  // since when, and whether leaving it sends our look again.
  private let m_menu: Int32;
  private let m_menuSince: Float;
  private let m_menuLook: Bool;
  private let m_menuName: CName;
  // A menu scenario (its end is also seen as the UI leaving its menus).
  private let m_menuScenario: Bool;
  // What we last told: kind, flags, workspot, entry; the anchor taken when
  // it began.
  private let m_kind: Int32;
  private let m_flags: Int32;
  private let m_workspot: Uint64;
  private let m_entry: Int32;
  private let m_anchor: Vector4;
  private let m_facing: Quaternion;
  private let m_test: Int32;
  // Since when our V is in a workspot: told once it has settled into it
  // (its entry carries the body into the seat first).
  private let m_spotSince: Float;

  // ---- the clock ----
  private let m_clockReady: Bool;
  private let m_clock: Int32;
  private let m_clockAt: Float;
  // Our own move of the clock is not an unasked jump.
  private let m_expectJump: Bool;
  private let m_wasInSpot: Bool;

  // ---- time skips ----
  // Ours, open: why, the clocks, whether our clock moved already (a guest's
  // bed), the wheel that waits for the answer and how many hours it asked.
  private let m_asking: Bool;
  private let m_askReason: Int32;
  private let m_askFrom: Int32;
  private let m_askTo: Int32;
  private let m_askMoved: Bool;
  private let m_wheel: wref<TimeskipGameController>;
  private let m_wheelHours: Int32;
  // The wheel our yes is applying (its Apply goes through once).
  private let m_granting: wref<TimeskipGameController>;
  // Another player's, waiting for our answer.
  private let m_prompt: ref<TandemSkipPrompt>;
  private let m_listening: Bool;
  private let m_held: Bool;

  // ---- theirs ----
  private let m_changes: Uint32;
  private let m_puppets: array<ref<TandemBusyPuppet>>;

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
  }

  public func Stop() -> Void {
    this.HidePrompt();
    this.ReleaseAll();
    this.m_asking = false;
    this.m_wheel = null;
  }

  public static func Of(game: GameInstance) -> ref<TandemActivities> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Activities() : null;
  }

  // Four times a second (TandemSystem.Tick); `you` 0: not in a session.
  public func Tick(now: Float, you: Uint32) -> Void {
    if now - this.m_last < 0.25 && now >= this.m_last {
      return;
    }
    this.m_last = now;
    this.m_now = now;
    if you == 0u && this.m_you != 0u {
      // The session ended: nobody is busy for us, nothing asked is pending.
      this.HidePrompt();
      this.ReleaseAll();
      this.m_asking = false;
      this.m_wheel = null;
    }
    this.m_you = you;
    this.SampleOurs();
    this.WatchClock();
    if you == 0u {
      return;
    }
    for event in TandemActivity.TakeSkips() {
      this.OnSkip(event);
    }
    this.TickPrompt();
    this.FollowOthers();
  }

  // ---- ours ----

  // A menu scenario began (0 kind: one that ended).
  public func Menu(kind: Int32, name: CName, look: Bool) -> Void {
    if kind == 0 {
      if this.m_menu != 0 {
        TandemNative.Log(s"activities: out of \(NameToString(this.m_menuName))");
        if this.m_menuLook && IsDefined(this.m_system) {
          this.m_system.RefreshLook();
        }
      }
      this.m_menu = 0;
      this.m_menuLook = false;
      this.m_menuName = n"None";
      return;
    }
    // From one menu to another (the hub to the map): the latest counts, a
    // look to send is kept until all are closed.
    this.m_menu = kind;
    this.m_menuScenario = kind != 3;
    this.m_menuLook = this.m_menuLook || look;
    this.m_menuName = name;
    this.m_menuSince = this.m_now;
    TandemNative.Log(s"activities: in \(NameToString(name))");
  }

  private func SampleOurs() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    // A menu whose end we missed (a scenario that leaves without its base's
    // OnLeaveScenario): the UI says no menu is open any more.
    if this.m_menu != 0 && this.m_menuScenario && this.m_now - this.m_menuSince > 1.0 && !this.InMenu() {
      this.Menu(0, n"None", false);
    }
    let kind = 0;
    let flags = 0;
    let workspot = 0ul;
    let entry = 0;
    let anim = n"None";
    let spots = GameInstance.GetWorkspotSystem(this.m_game);
    let inSpot = spots.IsActorInWorkspot(player);
    if inSpot && !this.m_wasInSpot {
      this.m_spotSince = this.m_now;
    }
    let settled = inSpot && this.m_now - this.m_spotSince >= 1.0;
    if this.m_test != 0 {
      kind = this.m_test;
      flags = 1;
    } else if GameInstance.GetPhotoModeSystem(this.m_game).IsPhotoModeActive() {
      kind = 7;
      flags = 1;
    } else if this.m_menu != 0 {
      kind = this.m_menu;
      flags = 1;
    } else if settled {
      kind = 1;
      flags = 1;
    }
    if settled && (kind == 1 || kind == 4 || kind == 5) {
      let instance = spots.GetWorkspotInstance(player.GetEntityID());
      if IsDefined(instance) && IsDefined(instance.resource) {
        let path = instance.resource.path;
        let reference = path.resource;
        workspot = ResourceAsyncRef.GetHash(reference);
        entry = Cast<Int32>(instance.entryId.id);
        anim = IsNameValid(instance.idleAnim) ? instance.idleAnim : instance.animName;
      }
    }
    if kind != 0 && this.StandsStill() {
      flags += 4;
    }
    this.m_wasInSpot = inSpot;
    // Out of photo mode: our game stood still, the session's state moved on
    // without it (a new epoch: the host checks a guest at once).
    if this.m_kind == 7 && kind != 7 {
      TandemSync.WorldRebuilt(4);
    }
    if kind == this.m_kind && flags == this.m_flags && workspot == this.m_workspot && entry == this.m_entry {
      return;
    }
    // Held where it began: a seat's place, not wherever the animation
    // carries the root.
    if this.m_kind == 0 || kind == 0 || workspot != this.m_workspot {
      this.m_anchor = player.GetWorldPosition();
      this.m_facing = player.GetWorldOrientation();
    }
    TandemActivity.SetOurs(kind, flags, 0ul, workspot, entry, anim, this.m_anchor, this.m_facing);
    TandemNative.Log(s"activities: our V is busy \(TandemActivities.KindName(kind)) (flags \(flags), workspot \(workspot), entry \(entry))");
    this.m_kind = kind;
    this.m_flags = flags;
    this.m_workspot = workspot;
    this.m_entry = entry;
  }

  private func InMenu() -> Bool {
    let ui = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_System);
    return IsDefined(ui) && ui.GetBool(GetAllBlackboardDefs().UI_System.IsInMenu);
  }

  // Our world stands still: paused, or slowed to nothing (the time skip
  // wheel, a menu's time dilation).
  private func StandsStill() -> Bool {
    let time = GameInstance.GetTimeSystem(this.m_game);
    return time.IsPausedState() || (time.IsTimeDilationActive() && time.GetActiveTimeDilation() < 0.05);
  }

  // ---- the clock ----

  private func WatchClock() -> Void {
    let seconds = TandemWorld.GameSeconds(this.m_game);
    let real = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    if !this.m_clockReady {
      this.m_clockReady = true;
      this.m_clock = seconds;
      this.m_clockAt = real;
      return;
    }
    let expected = this.m_clock + Cast<Int32>(MaxF(real - this.m_clockAt, 0.0) * 8.0);
    let ahead = seconds - expected;
    let from = this.m_clock;
    let wasInSpot = this.m_wasInSpot;
    this.m_clock = seconds;
    this.m_clockAt = real;
    if this.m_expectJump {
      this.m_expectJump = false;
      return;
    }
    if ahead > TandemActivities.JumpSeconds() && this.m_you != 0u {
      this.UnaskedJump(from, seconds, wasInSpot);
    }
  }

  // Our clock jumped without our asking: a bed (we were in a workspot) or a
  // quest. A guest whose clock went to the host's was only corrected.
  private func UnaskedJump(from: Int32, to: Int32, inSpot: Bool) -> Void {
    let host = TandemNative.IsHost();
    if !host && Abs(to - TandemNative.HostGameTime()) < 120 {
      return;
    }
    TandemNative.Log(s"activities: our clock jumped \(from) -> \(to) s (\(inSpot ? "in a workspot: a bed" : "not in a workspot: the story"))");
    if !inSpot {
      if host {
        // Our story moved the clock: said to everyone, never asked.
        TandemActivity.AskSkip(2, from, to);
        TandemSync.WorldRebuilt(3);
      }
      // A guest's own story: the host's clock comes back with its World.
      return;
    }
    if this.m_asking {
      return;
    }
    if host {
      // The shared clock is not ours to move: back until the others agree.
      this.m_expectJump = true;
      GameInstance.GetTimeSystem(this.m_game).SetGameTimeBySeconds(from);
      this.m_clock = from;
      this.Ask(1, from, to, false);
    } else {
      // Our clock stays ahead while the others think (TandemWorld.Follow
      // holds back); declined, the host's comes back.
      this.Ask(1, from, to, true);
    }
  }

  // The guest's clock follows the host's unless our own sleep waits for an
  // answer (World.reds).
  public func HoldsClock() -> Bool {
    return this.m_asking && this.m_askMoved;
  }

  // Our clock by `delta` seconds, the wheel's way when it is whole hours;
  // our V fast forwarded as the wheel does; a new epoch (the crowd is new).
  private func MoveClock(delta: Int32, fastForward: Bool) -> Void {
    let time = GameInstance.GetTimeSystem(this.m_game);
    let now = time.GetGameTime();
    this.m_expectJump = true;
    if delta % 3600 == 0 {
      time.SetGameTimeByHMS(GameTime.Hours(now) + delta / 3600, GameTime.Minutes(now), GameTime.Seconds(now), n"ui_menu_timeskip");
    } else {
      time.SetGameTimeBySeconds(GameTime.GetSeconds(now) + delta);
    }
    let player = GetPlayer(this.m_game);
    if fastForward && IsDefined(player) {
      GameTimeUtils.FastForwardPlayerState(player);
    }
    TandemSync.WorldRebuilt(3);
    TandemNative.Log(s"activities: our clock moved by \(delta) s");
  }

  // ---- time skips: ours ----

  // The wheel's Apply (wrapped below): ask instead. True: asked (or still
  // waiting), the wheel waits for the answer.
  public func AskWheel(wheel: ref<TimeskipGameController>, hours: Int32) -> Bool {
    if this.m_you == 0u || hours <= 0 {
      return false;
    }
    if this.m_asking {
      this.Notice("Waiting for the others to agree...");
      return true;
    }
    let from = TandemWorld.GameSeconds(this.m_game);
    this.m_wheel = wheel;
    this.m_wheelHours = hours;
    this.Ask(0, from, from + hours * 3600, false);
    return true;
  }

  // The wheel's Apply goes through: the others said yes.
  public func Granted(wheel: ref<TimeskipGameController>) -> Bool {
    return IsDefined(this.m_granting) && this.m_granting == wheel;
  }

  // The wheel closed or cancelled while we wait: our request goes.
  public func WheelClosed(wheel: ref<TimeskipGameController>) -> Void {
    if this.m_asking && IsDefined(this.m_wheel) && this.m_wheel == wheel {
      TandemActivity.WithdrawSkip();
      TandemNative.Log("activities: our time skip withdrawn (the wheel closed)");
      this.m_asking = false;
    }
    if IsDefined(this.m_wheel) && this.m_wheel == wheel {
      this.m_wheel = null;
    }
  }

  private func Ask(reason: Int32, from: Int32, to: Int32, moved: Bool) -> Void {
    this.m_asking = true;
    this.m_askReason = reason;
    this.m_askFrom = from;
    this.m_askTo = to;
    this.m_askMoved = moved;
    TandemActivity.AskSkip(reason, from, to);
    TandemNative.Log(s"activities: asked to skip \(from) -> \(to) s (reason \(reason))");
    this.Notice(s"Waiting for the others to agree to \(reason == 1 ? "sleep" : "wait") until \(TandemActivities.Clock(to))...");
  }

  private func OnSkip(e: TandemSkipEvent) -> Void {
    switch e.kind {
      case 1:
        this.Asked(e);
        break;
      case 2:
        if e.accepted {
          this.Notice(s"\(this.NameOf(e.by)) agrees");
        }
        break;
      case 3:
        this.Ended(e);
        break;
      default:
        break;
    }
  }

  private func Ended(e: TandemSkipEvent) -> Void {
    if IsDefined(this.m_prompt) && this.m_prompt.asker == e.asker && this.m_prompt.request == e.request {
      this.HidePrompt();
    }
    let ours = e.asker == this.m_you;
    let delta = e.toSeconds - e.fromSeconds;
    TandemNative.Log(s"activities: time skip \(e.request) of player \(e.asker) ended: \(TandemActivities.OutcomeName(e.outcome)) (by \(e.by), why \(e.why)), \(delta) s");
    if e.outcome == 0 {
      if ours {
        this.m_asking = false;
        if IsDefined(this.m_wheel) {
          // The wheel moves the clock and fast forwards our V itself.
          let wheel = this.m_wheel;
          this.m_wheel = null;
          this.m_expectJump = true;
          this.m_granting = wheel;
          wheel.TandemGranted(this.m_wheelHours);
          this.m_granting = null;
          TandemSync.WorldRebuilt(3);
        } else if this.m_askMoved {
          TandemSync.WorldRebuilt(3); // our bed moved it already
        } else if e.reason != 2 {
          this.MoveClock(delta, true);
        }
      } else {
        this.MoveClock(delta, e.reason != 2);
        this.Notice(s"\(this.NameOf(e.asker)) \(e.reason == 2 ? "moved on to" : (e.reason == 1 ? "slept until" : "waited until")) \(TandemActivities.Clock(TandemWorld.GameSeconds(this.m_game)))");
      }
      return;
    }
    if !ours {
      return;
    }
    this.m_asking = false;
    let why = this.WhyNot(e);
    this.Notice(why);
    if IsDefined(this.m_wheel) {
      let wheel = this.m_wheel;
      this.m_wheel = null;
      wheel.TandemRefused();
    }
    // A guest's bed: the host's clock comes back with its World.
  }

  private func WhyNot(e: TandemSkipEvent) -> String {
    let name = this.NameOf(e.by);
    switch e.outcome {
      case 1:
        return e.why == 2 ? s"\(name) can't skip time right now" : s"\(name) declined";
      case 2:
        return s"\(name) didn't answer";
      case 3:
        return "Time skip cancelled";
      default:
        break;
    }
    switch e.why {
      case 1:
        return s"\(name) is already asking to skip time";
      case 3:
        return s"\(name) is down";
      case 4:
        return s"\(name) is bending time";
      case 5:
        return s"\(name) is somewhere else";
      case 6:
        return s"\(name) paused the game";
      default:
        return "Time can't be skipped now";
    }
  }

  // ---- time skips: theirs ----

  private func Asked(e: TandemSkipEvent) -> Void {
    let player = GetPlayer(this.m_game);
    // The game's own rules for a skip, on our V (not in combat, no quest
    // forbidding it...).
    if IsDefined(player) && !GameTimeUtils.CanPlayerTimeSkip(player) {
      TandemActivity.AnswerSkip(e.asker, e.request, false, 2);
      TandemNative.Log(s"activities: time skip \(e.request) of player \(e.asker) declined: our V cannot skip now");
      this.Notice(s"\(this.NameOf(e.asker)) wants to skip time: you can't right now");
      return;
    }
    this.HidePrompt();
    let prompt = new TandemSkipPrompt();
    prompt.asker = e.asker;
    prompt.request = e.request;
    prompt.reason = e.reason;
    prompt.fromSeconds = e.fromSeconds;
    prompt.toSeconds = e.toSeconds;
    prompt.heardAt = this.m_now;
    this.m_prompt = prompt;
    TandemNative.Log(s"activities: player \(e.asker) asks to skip \(e.fromSeconds) -> \(e.toSeconds) s (request \(e.request))");
  }

  // Shown once the phone key is free (a regroup offer keeps it until it
  // goes); gone when it lapses.
  private func TickPrompt() -> Void {
    if !IsDefined(this.m_prompt) {
      return;
    }
    if this.m_now - this.m_prompt.heardAt > TandemActivities.AnswerSeconds() {
      this.HidePrompt();
      return;
    }
    if this.m_prompt.shown {
      return;
    }
    let travel = TandemTravel.Of(this.m_game);
    if IsDefined(travel) && travel.Showing() {
      return;
    }
    let name = this.NameOf(this.m_prompt.asker);
    let until = TandemActivities.Clock(TandemWorld.GameSeconds(this.m_game) + this.m_prompt.toSeconds - this.m_prompt.fromSeconds);
    let what = this.m_prompt.reason == 1 ? "sleep" : "wait";
    let left = TandemActivities.AnswerSeconds() - (this.m_now - this.m_prompt.heardAt);
    this.m_prompt.shown = true;
    this.Notice(s"\(name) wants to \(what) until \(until)", left);
    this.Hint(true, s"Agree to \(what) until \(until)");
    let local = GetPlayer(this.m_game);
    if IsDefined(local) && !this.m_listening {
      local.RegisterInputListener(this, n"PhoneInteract");
      this.m_listening = true;
      this.m_held = false;
    }
  }

  private func HidePrompt() -> Void {
    if IsDefined(this.m_prompt) && this.m_prompt.shown {
      this.Hint(false, "");
    }
    this.m_prompt = null;
    if this.m_listening {
      let local = GetPlayer(this.m_game);
      if IsDefined(local) {
        local.UnregisterInputListener(this, n"PhoneInteract");
      }
      this.m_listening = false;
    }
  }

  // The phone key while a request is shown: a press agrees (as a regroup
  // offer's, Travel.reds); a hold is the phone's own.
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if !IsDefined(this.m_prompt) || !Equals(ListenerAction.GetName(action), n"PhoneInteract") {
      return false;
    }
    let kind = ListenerAction.GetType(action);
    if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.m_held = true;
    } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
      if this.m_held {
        this.m_held = false;
      } else {
        this.Answer(true);
      }
    }
    return false;
  }

  public func Answer(yes: Bool) -> Bool {
    if !IsDefined(this.m_prompt) {
      TandemNative.Log("activities: no time skip to answer");
      return false;
    }
    TandemActivity.AnswerSkip(this.m_prompt.asker, this.m_prompt.request, yes, 0);
    TandemNative.Log(s"activities: time skip \(this.m_prompt.request) of player \(this.m_prompt.asker) \(yes ? "accepted" : "declined")");
    if yes {
      this.Notice(s"Waiting for \(this.NameOf(this.m_prompt.asker))...");
    }
    this.HidePrompt();
    return true;
  }

  // ---- theirs: the puppets ----

  private func FollowOthers() -> Void {
    let changes = TandemActivity.Changes();
    if changes != this.m_changes {
      this.m_changes = changes;
      let states = TandemActivity.Others();
      // Ended: let go once its workspot exit has played.
      for busy in this.m_puppets {
        let still = false;
        for state in states {
          if state.player == busy.player {
            still = true;
          }
        }
        if !still && !busy.ending {
          busy.ending = true;
          this.Said(busy.player, busy.state.kind, 0);
          busy.state.kind = 0;
          busy.state.flags = 0;
          busy.state.workspot = 0ul;
        }
      }
      for state in states {
        if state.player != this.m_you {
          let busy = this.Find(state.player);
          if !IsDefined(busy) {
            busy = new TandemBusyPuppet();
            busy.player = state.player;
            busy.spot = new TandemSpot();
            ArrayPush(this.m_puppets, busy);
          }
          let before = busy.state;
          if !busy.ending {
            this.Said(state.player, before.kind, state.kind);
          }
          if state.x != before.x || state.y != before.y || state.z != before.z || busy.ending {
            busy.placed = false;
          }
          busy.state = state;
          busy.ending = false;
        }
      }
    }
    let kept: array<ref<TandemBusyPuppet>>;
    for busy in this.m_puppets {
      if this.Place(busy) {
        ArrayPush(kept, busy);
      }
    }
    this.m_puppets = kept;
  }

  // One busy puppet, each tick. False: done with it.
  private func Place(busy: ref<TandemBusyPuppet>) -> Bool {
    let body = this.PuppetBody(busy.player);
    if !IsDefined(body) {
      // No puppet (yet, or any more): nothing held.
      if busy.held {
        TandemNative.HoldPuppet(busy.player, false);
        busy.held = false;
      }
      busy.placed = false;
      return !busy.ending;
    }
    let anchored = (busy.state.flags & 1) != 0;
    if anchored && !busy.placed {
      if !busy.held {
        TandemNative.HoldPuppet(busy.player, true);
        busy.held = true;
      }
      let at = new Vector4(busy.state.x, busy.state.y, busy.state.z, 1.0);
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, TandemYawAngles(Rad2Deg(busy.state.yaw)));
      busy.placed = true;
      busy.placedAt = this.m_now;
    }
    // The teleport lands within a frame or two: the workspot is placed
    // where the body then stands.
    if anchored && this.m_now - busy.placedAt < 0.5 && !busy.ending {
      return true;
    }
    let want = busy.state.kind == 1 || busy.state.kind == 4 || busy.state.kind == 5 ? busy.state.workspot : 0ul;
    busy.spot.Step(this.m_game, body, 0u, want, busy.state.entry, this.m_now, s"activities: puppet \(busy.player)");
    if busy.ending && !busy.spot.Playing() && busy.spot.state == 0 {
      if busy.held {
        TandemNative.HoldPuppet(busy.player, false);
        busy.held = false;
      }
      return false;
    }
    return true;
  }

  private func ReleaseAll() -> Void {
    for busy in this.m_puppets {
      let body = this.PuppetBody(busy.player);
      busy.spot.Stop(this.m_game, body);
      if busy.held {
        TandemNative.HoldPuppet(busy.player, false);
      }
    }
    ArrayClear(this.m_puppets);
    this.m_changes = TandemActivity.Changes();
  }

  private func PuppetBody(player: Uint32) -> ref<ScriptedPuppet> {
    if !IsDefined(this.m_system) {
      return null;
    }
    return this.m_system.PuppetOf(player) as ScriptedPuppet;
  }

  private func Find(player: Uint32) -> ref<TandemBusyPuppet> {
    for busy in this.m_puppets {
      if busy.player == player {
        return busy;
      }
    }
    return null;
  }

  // What the others are told the game's way: photo mode and back.
  private func Said(player: Uint32, before: Int32, now: Int32) -> Void {
    if before == now {
      return;
    }
    TandemNative.Log(s"activities: player \(player) is busy \(TandemActivities.KindName(now)) (was \(TandemActivities.KindName(before)))");
    if now == 7 {
      this.Notice(s"\(this.NameOf(player)) is in photo mode");
    } else if before == 7 {
      this.Notice(s"\(this.NameOf(player)) is back");
    }
  }

  // ---- commands (tools/game.sh cmd) ----
  // skip ask HOURS | skip accept | skip decline | busy KIND|off | busy report
  public func Command(command: String) -> Void {
    if StrBeginsWith(command, "skip ask") {
      let hours = StringToInt(StrAfterFirst(StrAfterFirst(command, " "), " "));
      let from = TandemWorld.GameSeconds(this.m_game);
      if this.m_you == 0u || hours <= 0 {
        TandemNative.Log("activities: skip ask HOURS, in a session");
        return;
      }
      this.Ask(0, from, from + hours * 3600, false);
    } else if Equals(command, "skip accept") {
      this.Answer(true);
    } else if Equals(command, "skip decline") {
      this.Answer(false);
    } else if Equals(command, "busy report") {
      TandemNative.Log(s"activities: ours \(TandemActivities.KindName(this.m_kind)) (flags \(this.m_flags), workspot \(this.m_workspot)), \(ArraySize(this.m_puppets)) busy puppet(s), asking \(this.m_asking)");
      for busy in this.m_puppets {
        TandemNative.Log(s"  player \(busy.player): \(TandemActivities.KindName(busy.state.kind)), held \(busy.held), workspot \(busy.state.workspot) state \(busy.spot.state)");
      }
    } else if StrBeginsWith(command, "busy") {
      let what = StrAfterFirst(command, " ");
      this.m_test = Equals(what, "off") ? 0 : TandemActivities.KindNamed(what);
      TandemNative.Log(s"activities: (test) our V busy \(TandemActivities.KindName(this.m_test))");
    }
  }

  // ---- helpers ----

  public static func KindName(kind: Int32) -> String {
    switch kind {
      case 0:
        return "nothing";
      case 1:
        return "in a workspot";
      case 2:
        return "in bed";
      case 3:
        return "at a screen";
      case 4:
        return "in a minigame";
      case 5:
        return "in a shop";
      case 6:
        return "in a menu";
      case 7:
        return "in photo mode";
      case 8:
        return "in a contest";
      default:
        return "?";
    }
  }

  public static func KindNamed(name: String) -> Int32 {
    let i = 1;
    while i <= 8 {
      if StrContains(TandemActivities.KindName(i), name) {
        return i;
      }
      i += 1;
    }
    return 0;
  }

  public static func OutcomeName(outcome: Int32) -> String {
    switch outcome {
      case 0:
        return "applied";
      case 1:
        return "declined";
      case 2:
        return "lapsed";
      case 3:
        return "withdrawn";
      default:
        return "refused";
    }
  }

  // "14:05", the time of day of a game time.
  public static func Clock(seconds: Int32) -> String {
    let day = seconds % 86400;
    if day < 0 {
      day += 86400;
    }
    let hours = day / 3600;
    let minutes = (day % 3600) / 60;
    return s"\(hours):\(minutes < 10 ? "0" : "")\(minutes)";
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

  private func Notice(text: String, opt seconds: Float) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = seconds > 0.0 ? seconds : 4.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(this.m_game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }

  private func Hint(show: Bool, label: String) -> Void {
    let data: InputHintData;
    data.action = n"PhoneInteract";
    data.source = n"TandemTimeSkip";
    data.localizedLabel = label;
    data.queuePriority = 10;
    data.sortingPriority = 10;
    data.holdIndicationType = inkInputHintHoldIndicationType.Press;
    let evt = new UpdateInputHintEvent();
    evt.data = data;
    evt.show = show;
    evt.targetHintContainer = n"GameplayInputHelper";
    GameInstance.GetUISystem(this.m_game).QueueEvent(evt);
  }
}

// ---- the time skip wheel ----

// In a session the wheel asks the others first; their yes applies it (the
// wheel's own animation, clock change and fast forward), a no closes it.
@wrapMethod(TimeskipGameController)
private final func Apply() {
  let activities = TandemActivities.Of(this.m_gameInstance);
  if IsDefined(activities) && !activities.Granted(this) && this.m_inputEnabled && this.m_hoursToSkip > 0 {
    if activities.AskWheel(this, this.m_hoursToSkip) {
      return;
    }
  }
  wrappedMethod();
}

@addMethod(TimeskipGameController)
public func TandemGranted(hours: Int32) -> Void {
  this.m_hoursToSkip = hours;
  this.m_inputEnabled = true;
  this.Apply();
}

@addMethod(TimeskipGameController)
public func TandemRefused() -> Void {
  this.Cancel();
}

@wrapMethod(TimeskipGameController)
private final func Cancel() {
  let activities = TandemActivities.Of(this.m_gameInstance);
  if IsDefined(activities) {
    activities.WheelClosed(this);
  }
  wrappedMethod();
}

@wrapMethod(TimeskipGameController)
protected cb func OnUninitialize() -> Bool {
  let activities = TandemActivities.Of(this.m_gameInstance);
  if IsDefined(activities) {
    activities.WheelClosed(this);
  }
  return wrappedMethod();
}

// ---- menu scenarios: what our V is busy with ----
// Kinds as proto::ActivityKind: 4 a minigame, 5 a shop, 6 a menu.

public abstract class TandemMenus {
  public static func Entered(kind: Int32, name: CName, look: Bool) -> Void {
    let activities = TandemActivities.Of(GetGameInstance());
    if IsDefined(activities) {
      activities.Menu(kind, name, look);
    }
  }
}

@wrapMethod(MenuScenario_BaseMenu)
protected cb func OnLeaveScenario(nextScenario: CName) -> Bool {
  let result = wrappedMethod(nextScenario);
  TandemMenus.Entered(0, n"None", false);
  return result;
}

@wrapMethod(MenuScenario_NetworkBreach)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(4, n"network_breach", false);
  return result;
}

@wrapMethod(MenuScenario_ArcadeMinigame)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(4, n"arcade", false);
  return result;
}

@wrapMethod(MenuScenario_Vendor)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"vendor", false);
  return result;
}

@wrapMethod(MenuScenario_Vendor)
protected cb func OnSwitchToRipperDoc(opt userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(userData);
  TandemMenus.Entered(5, n"ripperdoc", true);
  return result;
}

@wrapMethod(MenuScenario_Wardrobe)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"wardrobe", true);
  return result;
}

@wrapMethod(MenuScenario_CharacterCustomizationMirror)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"mirror", true);
  return result;
}

@wrapMethod(MenuScenario_TimeSkip)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(6, n"time_skip", false);
  return result;
}

@wrapMethod(MenuScenario_HubMenu)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  let result = wrappedMethod(prevScenario, userData);
  // The inventory, cyberware and wardrobe tabs change what the others see.
  TandemMenus.Entered(6, n"hub_menu", true);
  return result;
}

// A shard read in the world: a screen (its popup stops our game).
@wrapMethod(ShardNotificationController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  TandemMenus.Entered(3, n"shard", false);
  return result;
}

@wrapMethod(ShardNotificationController)
protected cb func OnUninitialize() -> Bool {
  let result = wrappedMethod();
  TandemMenus.Entered(0, n"None", false);
  return result;
}
