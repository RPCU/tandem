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
// - Interactions (a workspot or a scene that holds our V: V's homes' shower,
//   bed, coffee, the cat...) are traced from start to end: the scene, the
//   clothes shown (a shower undresses V: our look goes again at once, and
//   again when the clothes come back), the clock, the apartment buffs our V
//   gained (status effects of type Housing; they stay ours).

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
  // The device whose screen we use (a computer's, a terminal's: its
  // EntityID hash), told with the rest.
  private let m_device: Uint64;
  // Our bed's jump was seen in this workspot: we lie asleep in it (kind 2).
  private let m_sleeping: Bool;

  // ---- the interaction we are in (a workspot, or a scene holding our V) ----
  private let m_inter: Bool;
  private let m_interSince: Float;
  private let m_interScene: Uint64;
  private let m_interSpot: Uint64;
  private let m_interTier: Int32;
  private let m_interClock: Int32;
  private let m_interClothes: Int32;
  private let m_interFewest: Int32;
  private let m_interBuffs: array<TweakDBID>;
  // When the last one ended: the look is watched a little longer (the
  // clothes come back after the scene lets go).
  private let m_interEnded: Float;
  private let m_worn: array<TweakDBID>;

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
  // Two beds at once: our sleep counts as our yes to this player's open
  // request (the host's arbiter joined it, activity::SkipArbiter::Joins).
  private let m_joined: Uint32;
  // Another player's, waiting for our answer.
  private let m_prompt: ref<TandemSkipPrompt>;
  private let m_listening: Bool;
  private let m_held: Bool;

  // ---- theirs ----
  private let m_changes: Uint32;
  private let m_puppets: array<ref<TandemBusyPuppet>>;
  // Contests: ours read from the game, the others' from their Activity
  // (Contests.reds).
  private let m_contests: ref<TandemContests>;

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    this.m_contests = new TandemContests();
    this.m_contests.Start(game);
  }

  public func Contests() -> ref<TandemContests> {
    return this.m_contests;
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
      this.m_joined = 0u;
      this.m_wheel = null;
    }
    this.m_you = you;
    if IsDefined(this.m_contests) {
      this.m_contests.Tick(now, you);
    }
    this.SampleOurs();
    this.WatchClock();
    this.TrackInteraction();
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
    let device = 0ul;
    let workspot = 0ul;
    let entry = 0;
    let anim = n"None";
    let spots = GameInstance.GetWorkspotSystem(this.m_game);
    let inSpot = spots.IsActorInWorkspot(player);
    if inSpot && !this.m_wasInSpot {
      this.m_spotSince = this.m_now;
    }
    if !inSpot {
      this.m_sleeping = false;
    }
    let settled = inSpot && this.m_now - this.m_spotSince >= 1.0;
    // A device's screen up close (a computer, a terminal, a smart window:
    // Device.SetZoomBlackboardValues), and which device.
    let psm = player.GetPlayerStateMachineBlackboard();
    let zoomed = IsDefined(psm) && psm.GetBool(GetAllBlackboardDefs().PlayerStateMachine.IsUIZoomDevice);
    if this.m_test != 0 {
      kind = this.m_test;
      flags = 1;
    } else if GameInstance.GetPhotoModeSystem(this.m_game).IsPhotoModeActive() {
      kind = 7;
      flags = 1;
    } else if IsDefined(this.m_contests) && this.m_contests.Sample(player) != 0 {
      // A fist fight, a race, a shooting competition (Contests.reds): our V
      // moves on, nothing is held; the others watch.
      kind = 8;
      entry = this.m_contests.Sample(player);
      device = Cast<Uint64>(this.m_contests.Opponent());
    } else if this.m_menu != 0 {
      kind = this.m_menu;
      flags = 1;
    } else if zoomed {
      kind = 3;
      flags = 1;
      device = Cast<Uint64>(EntityID.GetHash(psm.GetEntityID(GetAllBlackboardDefs().PlayerStateMachine.UIZoomDeviceID)));
    } else if settled && this.m_sleeping {
      // Asleep in a bed: lying, held where it lies.
      kind = 2;
      flags = 3;
    } else if settled {
      kind = 1;
      flags = 1;
    }
    if settled && (kind == 1 || kind == 2 || kind == 4 || kind == 5) {
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
    if kind == this.m_kind && flags == this.m_flags && workspot == this.m_workspot && entry == this.m_entry && device == this.m_device {
      return;
    }
    // Held where it began: a seat's place, not wherever the animation
    // carries the root (from sitting to sleeping in the same bed: the same).
    if this.m_kind == 0 || kind == 0 || workspot != this.m_workspot {
      this.m_anchor = player.GetWorldPosition();
      this.m_facing = player.GetWorldOrientation();
    }
    TandemActivity.SetOurs(kind, flags, device, workspot, entry, anim, this.m_anchor, this.m_facing);
    TandemNative.Log(s"activities: our V is busy \(TandemActivities.KindName(kind)) (flags \(flags), workspot \(workspot), entry \(entry), device \(device))");
    this.m_kind = kind;
    this.m_flags = flags;
    this.m_workspot = workspot;
    this.m_entry = entry;
    this.m_device = device;
  }

  // ---- the interaction we are in ----

  // A workspot, or a scene that holds our V (tier 2 and up): V's homes'
  // shower, bed, coffee machine, sink, couch, the cat, the guitar... One
  // line when it begins, one when it ends with what it did: how long, the
  // scene (an apartment's?), the clothes shown (fewest on the way: a shower
  // undresses V), the clock, the apartment buffs gained. While it lasts
  // and a little after, a change of the clothes shown sends our look at
  // once (not at the next 2 s check): the others see V undressed when our
  // game shows it so, and dressed again when it does.
  private func TrackInteraction() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || this.m_you == 0u {
      this.m_inter = false;
      return;
    }
    let tier = PlayerPuppet.GetSceneTier(player);
    let inSpot = GameInstance.GetWorkspotSystem(this.m_game).IsActorInWorkspot(player);
    let now = inSpot || tier >= 2;
    let scene = TandemActivity.OurScene();
    if now && !this.m_inter {
      this.m_inter = true;
      this.m_interSince = this.m_now;
      this.m_interScene = scene;
      this.m_interSpot = this.m_workspot;
      this.m_interTier = tier;
      this.m_interClock = TandemWorld.GameSeconds(this.m_game);
      this.m_interBuffs = TandemActivities.HousingBuffs(player);
      this.m_worn = TandemOutfit.Shown(player);
      this.m_interClothes = ArraySize(this.m_worn);
      this.m_interFewest = this.m_interClothes;
      TandemNative.Log(s"activities: interaction begins (scene \(scene), apartment \(TandemActivity.ApartmentScene(scene)), tier \(tier), in a workspot \(inSpot), clothes \(this.m_interClothes))");
    } else if now {
      if this.m_interScene == 0ul && scene != 0ul {
        this.m_interScene = scene;
        TandemNative.Log(s"activities: interaction's scene \(scene) (apartment \(TandemActivity.ApartmentScene(scene)))");
      }
      if this.m_interSpot == 0ul && this.m_workspot != 0ul {
        this.m_interSpot = this.m_workspot;
      }
      this.m_interTier = Max(this.m_interTier, tier);
    } else if this.m_inter {
      this.m_inter = false;
      this.m_interEnded = this.m_now;
      let gained = "";
      for buff in TandemActivities.HousingBuffs(player) {
        if !ArrayContains(this.m_interBuffs, buff) {
          gained += s" \(TDBID.ToStringDEBUG(buff))";
        }
      }
      let shown = TandemOutfit.Shown(player);
      let clothes = ArraySize(shown);
      let clock = TandemWorld.GameSeconds(this.m_game) - this.m_interClock;
      TandemNative.Log(s"activities: interaction over after \(this.m_now - this.m_interSince) s (scene \(this.m_interScene), apartment \(TandemActivity.ApartmentScene(this.m_interScene)), workspot \(this.m_interSpot), tier up to \(this.m_interTier), clothes \(this.m_interClothes) -> \(this.m_interFewest) -> \(clothes), clock +\(clock) s, buffs gained:\(StrLen(gained) > 0 ? gained : " none"))");
    }
    // The clothes shown, while it lasts and for 5 s after.
    if this.m_inter || this.m_now - this.m_interEnded < 5.0 {
      let worn = TandemOutfit.Shown(player);
      if !TandemOutfit.Same(worn, this.m_worn) {
        this.m_worn = worn;
        if this.m_inter {
          this.m_interFewest = Min(this.m_interFewest, ArraySize(worn));
        }
        TandemNative.Log(s"activities: clothes shown now \(ArraySize(worn)): our look goes again");
        if IsDefined(this.m_system) {
          this.m_system.RefreshLook();
        }
      }
    }
  }

  // The apartment buffs on `player` (status effects of type Housing: rested,
  // refreshed, energized), by record.
  public static func HousingBuffs(player: ref<PlayerPuppet>) -> array<TweakDBID> {
    let out: array<TweakDBID>;
    for effect in StatusEffectHelper.GetAppliedEffects(player) {
      if IsDefined(effect) && IsDefined(effect.GetRecord()) && IsDefined(effect.GetRecord().StatusEffectType())
        && Equals(effect.GetRecord().StatusEffectType().Type(), gamedataStatusEffectType.Housing) {
        ArrayPush(out, effect.GetRecord().GetID());
      }
    }
    return out;
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
    // A bed: a workspot, or one of V's homes' scenes (its bed is a scene's
    // section: the workspot system may not know it).
    let wasInSpot = this.m_wasInSpot || TandemActivity.ApartmentScene(TandemActivity.OurScene()) || (this.m_inter && TandemActivity.ApartmentScene(this.m_interScene));
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

  // Our clock jumped without our asking: a bed (we were in a workspot or an
  // apartment's scene) or a quest. A guest whose clock went to the host's was
  // only corrected.
  private func UnaskedJump(from: Int32, to: Int32, inSpot: Bool) -> Void {
    let host = TandemNative.IsHost();
    if !host && Abs(to - TandemNative.HostGameTime()) < 120 {
      return;
    }
    TandemNative.Log(s"activities: our clock jumped \(from) -> \(to) s (\(inSpot ? "in a workspot or an apartment's scene: a bed" : "not in a workspot: the story"))");
    if inSpot {
      // Asleep: the others see our V lying in its bed until it gets up.
      this.m_sleeping = true;
    }
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
      this.Notice("Activities-WaitingAll");
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
    this.Notice(reason == 1 ? "Activities-WaitingSleep" : "Activities-WaitingWait", TandemActivities.Clock(to));
  }

  private func OnSkip(e: TandemSkipEvent) -> Void {
    switch e.kind {
      case 1:
        this.Asked(e);
        break;
      case 2:
        if e.accepted {
          this.Notice("Activities-Agrees", this.NameOf(e.by));
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
          // Our own bed (the host's, put back while the others thought):
          // the hours pass the bed's way, which fast forwards nothing (its
          // Rested buff stays). The wheel's way fast forwards our V.
          this.MoveClock(delta, e.reason != 1);
        }
      } else {
        // We went to bed too (joined): asleep, not waiting; our own bed's
        // buff stays.
        let joined = this.m_joined == e.asker;
        this.m_joined = 0u;
        this.MoveClock(delta, e.reason != 2 && !joined);
        this.Notice(e.reason == 2 ? "Activities-MovedOn" : (e.reason == 1 ? "Activities-Slept" : "Activities-Waited"), this.NameOf(e.asker), TandemActivities.Clock(TandemWorld.GameSeconds(this.m_game)));
      }
      return;
    }
    if !ours {
      if this.m_joined == e.asker {
        // The request our sleep joined ended without the hours.
        this.m_joined = 0u;
        this.Notice(this.WhyNot(e), this.NameOf(e.by));
      }
      return;
    }
    this.m_asking = false;
    if e.outcome == 4 && e.why == 1 && e.reason == 1 {
      // Two beds at once: another player's request was open and we were to
      // answer it; our sleep is our yes to it (the host's arbiter counted
      // it), and its hours are ours when it is applied.
      this.m_joined = e.by;
      if IsDefined(this.m_prompt) && this.m_prompt.asker == e.by {
        this.HidePrompt();
      }
      TandemNative.Log(s"activities: our sleep joins player \(e.by)'s time skip");
      this.Notice("Activities-SleepWith", this.NameOf(e.by));
      return;
    }
    this.Notice(this.WhyNot(e), this.NameOf(e.by));
    if IsDefined(this.m_wheel) {
      let wheel = this.m_wheel;
      this.m_wheel = null;
      wheel.TandemRefused();
    }
    // A guest's bed: the host's clock comes back with its World.
  }

  // The key of why a skip of ours did not happen (its {1}: who).
  private func WhyNot(e: TandemSkipEvent) -> String {
    switch e.outcome {
      case 1:
        return e.why == 2 ? "Activities-CantNow" : "Activities-Declined";
      case 2:
        return "Activities-NoAnswer";
      case 3:
        return "Activities-Cancelled";
      default:
        break;
    }
    switch e.why {
      case 1:
        return "Activities-AlreadyAsking";
      case 3:
        return "Activities-IsDown";
      case 4:
        return "Activities-Bending";
      case 5:
        return "Activities-Elsewhere";
      case 6:
        return "Activities-Paused";
      default:
        return "Activities-CantSkip";
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
      this.Notice("Activities-CannotSkip", this.NameOf(e.asker));
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
    let sleep = this.m_prompt.reason == 1;
    let left = TandemActivities.AnswerSeconds() - (this.m_now - this.m_prompt.heardAt);
    this.m_prompt.shown = true;
    this.Notice(sleep ? "Activities-AsksSleep" : "Activities-AsksWait", name, until, left);
    this.Hint(true, TandemText.F(sleep ? "Activities-AgreeSleep" : "Activities-AgreeWait", until));
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
    TandemNative.HookRan(n"Activities.TandemActivities.OnAction");
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
      this.Notice("Activities-WaitingFor", this.NameOf(this.m_prompt.asker));
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
      if IsDefined(this.m_contests) {
        this.m_contests.Others(states, this.m_you);
      }
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
    let want = busy.state.kind == 1 || busy.state.kind == 2 || busy.state.kind == 4 || busy.state.kind == 5 ? busy.state.workspot : 0ul;
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
      this.Notice("Activities-Photo", this.NameOf(player));
    } else if before == 7 {
      this.Notice("Activities-Back", this.NameOf(player));
    }
  }

  // ---- commands (tools/game.sh cmd) ----
  // skip ask HOURS | skip accept | skip decline | busy KIND|off | busy report
  // | apartment report | contest duel|race|range|off|report|races|join|leave
  public func Command(command: String) -> Void {
    if StrBeginsWith(command, "contest") {
      if IsDefined(this.m_contests) {
        this.m_contests.Command(command);
      }
    } else if Equals(command, "apartment report") {
      this.ReportApartment();
    } else if StrBeginsWith(command, "skip ask") {
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

  // What V's home shows of our V now: the scene it plays (an apartment's?),
  // its tier and class, the workspot, the device screen, the clothes shown,
  // the apartment buffs, the game's nudity setting, a time skip joined.
  private func ReportApartment() -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      TandemNative.Log("apartment: no V");
      return;
    }
    let scene = TandemActivity.OurScene();
    let buffs = "";
    for buff in TandemActivities.HousingBuffs(player) {
      buffs += s" \(TDBID.ToStringDEBUG(buff))";
    }
    let clothes = "";
    for record in TandemOutfit.Shown(player) {
      clothes += s" \(TDBID.ToStringDEBUG(record))";
    }
    let nudity = GameInstance.GetCharacterCustomizationSystem(this.m_game).IsNudityAllowed();
    TandemNative.Log(s"apartment: scene \(scene) (apartment \(TandemActivity.ApartmentScene(scene)), class \(TandemStory.SceneClass())), tier \(PlayerPuppet.GetSceneTier(player)), ours \(TandemActivities.KindName(this.m_kind)) (workspot \(this.m_workspot), device \(this.m_device)), in an interaction \(this.m_inter), joined \(this.m_joined)");
    TandemNative.Log(s"apartment: buffs:\(StrLen(buffs) > 0 ? buffs : " none"); clothes shown:\(StrLen(clothes) > 0 ? clothes : " none"); nudity allowed \(nudity)");
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
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  // A notice by its key (Text.reds), for `seconds` (4 by default).
  private func Notice(key: String, opt a: String, opt b: String, opt seconds: Float) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, seconds > 0.0 ? seconds : 4.0, key, a, b);
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
  TandemNative.HookRan(n"Activities.TimeskipGameController.OnUninitialize");
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
  TandemNative.HookRan(n"Activities.MenuScenario_BaseMenu.OnLeaveScenario");
  let result = wrappedMethod(nextScenario);
  TandemMenus.Entered(0, n"None", false);
  return result;
}

@wrapMethod(MenuScenario_NetworkBreach)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_NetworkBreach.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(4, n"network_breach", false);
  return result;
}

@wrapMethod(MenuScenario_ArcadeMinigame)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_ArcadeMinigame.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(4, n"arcade", false);
  return result;
}

@wrapMethod(MenuScenario_Vendor)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_Vendor.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  // A clothing store's or a ripperdoc's purchase may change what the
  // others see: our look again on the way out (unchanged: sent to nobody).
  TandemMenus.Entered(5, n"vendor", true);
  return result;
}

@wrapMethod(MenuScenario_Vendor)
protected cb func OnSwitchToRipperDoc(opt userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_Vendor.OnSwitchToRipperDoc");
  let result = wrappedMethod(userData);
  TandemMenus.Entered(5, n"ripperdoc", true);
  return result;
}

// V's stash (Stash.OnOpenStash fills StorageBlackboard; the storage screen
// opens through this scenario): a shop's screen, nothing worn changes.
@wrapMethod(MenuScenario_Storage)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_Storage.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"stash", false);
  return result;
}

@wrapMethod(MenuScenario_Wardrobe)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_Wardrobe.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"wardrobe", true);
  return result;
}

@wrapMethod(MenuScenario_CharacterCustomizationMirror)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_CharacterCustomizationMirror.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(5, n"mirror", true);
  return result;
}

@wrapMethod(MenuScenario_TimeSkip)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_TimeSkip.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  TandemMenus.Entered(6, n"time_skip", false);
  return result;
}

@wrapMethod(MenuScenario_HubMenu)
protected cb func OnEnterScenario(prevScenario: CName, userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Activities.MenuScenario_HubMenu.OnEnterScenario");
  let result = wrappedMethod(prevScenario, userData);
  // The inventory, cyberware and wardrobe tabs change what the others see.
  TandemMenus.Entered(6, n"hub_menu", true);
  return result;
}

// A shard read in the world: a screen (its popup stops our game).
@wrapMethod(ShardNotificationController)
protected cb func OnInitialize() -> Bool {
  TandemNative.HookRan(n"Activities.ShardNotificationController.OnInitialize");
  let result = wrappedMethod();
  TandemMenus.Entered(3, n"shard", false);
  return result;
}

@wrapMethod(ShardNotificationController)
protected cb func OnUninitialize() -> Bool {
  TandemNative.HookRan(n"Activities.ShardNotificationController.OnUninitialize");
  let result = wrappedMethod();
  TandemMenus.Entered(0, n"None", false);
  return result;
}
