// Slowed time in a shared world (docs/architecture/time.md, open problems:
// slowed and stopped time; research in docs/research/coop-time.md).
//
// Single player slows the whole world for one V (TimeSystem.SetTimeDilation,
// a global factor). In co-op the world is shared, so no V's ability may slow
// it as a whole. The rules, per mechanism:
//
// - Sandevistan (reason "sandevistan", SandevistanEvents.OnEnter,
//   final.reds:524119) and Kerenzikov ("kereznikov", KerenzikovEvents.OnEnter,
//   final.reds:527831): a time bubble. The NPCs within 150 m of that V (all
//   that any game shows) are
//   slowed, for every player, by the factor the implant asks for (its stat
//   TimeDilationSandevistanTimeScale / TimeDilationKerenzikovTimeScale): the
//   host slows its real NPCs with the per-entity dilation vanilla gives a
//   single NPC (TimeDilatable.SetIndividualTimeDilation, final.reds:14727;
//   vanilla slows one NPC to 0.05 for 2.5 s that way, final.reds:525204).
//   Guests show it through the proxies' motion only: entities that tandem's
//   own movement controller drives (proxies, puppets) get no per-entity
//   dilation (see Apply).
//   Rules of the bubbles (several Vs, overlaps, ends): core's
//   tandem/timebubble.hpp. Everything outside the bubbles keeps normal
//   speed, and so does every V: two Vs in a Sandevistan at once each see the
//   NPCs slowed and the other V at normal speed.
//   * Its owner: a guest keeps its single-player slow motion on its own
//     screen (its world is not the shared one); what the network drives
//     there moves at its true shared speed (their animations follow the
//     guest's own factor: not exempted, see Apply). The host's world is the shared one: by default (tandem.ini
//     time_host = bubble) its global factor is taken back within the frame
//     (kept "active" at normal speed, so the state machine carries on) and
//     the bubble alone slows what matters, the enemies around it; with
//     time_host = full it keeps the whole slow motion and exempts the NPCs
//     it shares (experimental: its projectiles, physics and clock still
//     slow).
//   * The others: that V's puppet keeps normal speed among slowed enemies,
//     with the implant's effect on it (PuppetFx, effect name to confirm).
//     A V that another V's bubble slows gets the game's own soft slow-motion
//     cue while it lasts (CueOn: the Focus perk's screen effect and sounds)
//     and, once a session, a notice naming whose it is.
// - Focus ("focusedStatePerkDilation", final.reds:542858), Deadeye's reload
//   ("coolReloadPerkDilation", final.reds:547583) and every effector that
//   slows time for our V (SetTimeDilationEffector, final.reds:307231: Reflex
//   Tuner, Synaptic Accelerator, second chance; reasons learned from the
//   wrap at the end): a bubble with their own factor, like the implants.
// - A bubble slows the other Vs in it too, each on its own game (SlowSelf).
// - Nothing of the game's own is changed (docs/research/coop-perks.md
//   §5.1a): its checks see a dilation active for as long as single player.
// - The host too keeps the game's own global dilation by default
//   (time_host = full): what its V's abilities read is single player's.
// - Scanner and quickhack panel ("focusMode", TimeDilationFocusModeEvents,
//   final.reds:529159): a bubble too, the standard one (the user's decision,
//   2026-11-19; it was switched off while another V was in the session
//   before). Its factor is the game's own (one TweakDB profile,
//   timeSystem.focusModeTimeDilation: no stat, perk or implant changes it,
//   docs/research/engine/time.md §13.40), read as the scanner sets it, and
//   it lasts while the game keeps it (an enemy or a sensor in view). Melee hit stop ("meleeHit", final.reds:45789) and
//   deflect/one-punch ("deflect", final.reds:39679): local only, no bubble. A guest keeps them on its
//   screen; the host drops their world part (time_host = bubble). Others see the V as it is: standing, scanning.
// - Any other global slow motion (a mod's: its reason cannot be read, the
//   time system's natives cannot be wrapped) is a bubble too, at the
//   world's factor, when none of the reasons above explains it and no local
//   one is on (Unexplained; core's TimeBubbles::UnexplainedSlow): told
//   once. The host cannot take it back (no reason to hold): its world stays
//   slowed, the NPCs it shares are exempted as with time_host = full.
// - A ride's own skip (the NCART, a cab: "customFFTimeDilation" at 10 while
//   the skip is held, PlayerPuppet.OnAction SceneFastForward under a
//   CustomFastForward effect, final.reds:40298): kept, on the host too. It
//   compresses a ride its owner runs alone (the others never share that
//   train: Transit.reds), and the clock moves on by what the ride takes
//   either way (World.reds brings the guests along). What the network
//   drives here is exempted from it, as from a slow motion.
// - Menus, popups and screens (docs/architecture/time.md, "Menus"): a
//   guest's slow or stop its own world, as in single player. The host's
//   world is the shared one: while another player is in the session
//   (HostKeepsWorld) none of ours slows it. Those that go through a
//   TimeDilationHelper profile (the weapon and vehicle wheels, the phone,
//   the hub menu, the radio, list popups) never start theirs, as vanilla's
//   own multiplayer branch has it (the wrap at the end); those set by name
//   (the map, a shop's, the stash's or the ripperdoc's screen, a tutorial
//   popup, the breach's screen: MenuReasons) are unset as soon as they are
//   set (DropMenus); a shard's popup pauses nothing (its wrap at the end).
// - Pause, time skips: not here (Pause.reds, Activities.reds).
//
// Runs from TandemSystem.Tick, every frame once the world plays: the script
// tick is one frame behind a dilation at most. (It ran from the plugin's
// per-frame call first, outside the script tick: the game crashed there
// loading a save and froze right after "playing", 2026-11-18.)

public class TandemTimeApplied {
  public let id: EntityID;
  // The factor set on it, with the local global factor ignored (-1: given
  // back this frame).
  public let factor: Float;
  // Still a target this frame.
  public let seen: Bool;
}

public class TandemTime {
  private let m_game: GameInstance;
  // Our abilities as last sent (proto::kTime* bits) and their world speed.
  private let m_sentAbilities: Int32;
  private let m_sentScale: Float;
  // The speed a taken-back global dilation is kept at: 1.0, or just under if
  // the time system reads a dilation of exactly 1 as none (checked once).
  private let m_neutral: Float;
  private let m_neutralChecked: Bool;
  // Reasons taken back during their current activation (logged once each).
  private let m_taken: array<CName>;
  private let m_unknownLogged: Float;
  // A mod's slow motion was shared as a bubble (said once a session).
  private let m_unexplainedSaid: Bool;
  // What the game asked for under each reason we follow, and the reasons
  // of the effector dilations on our V now.
  private let m_askedReasons: array<CName>;
  private let m_askedScales: array<Float>;
  private let m_effectors: array<CName>;
  // Our V slowed by another V's bubble: on, by how much, and how (the
  // player's own factor, or the per-entity dilation).
  private let m_selfSet: Bool;
  private let m_selfFactor: Float;
  private let m_selfEntity: Bool;
  private let m_selfChecked: Bool;
  // Measured for the partner's side (time report): how long our scanner's
  // bubbles ran, and how long our V was slowed by the others' bubbles (by a
  // scanner's alone, too).
  private let m_scanSince: Float;
  private let m_scanSeconds: Float;
  private let m_scans: Int32;
  private let m_slowedSince: Float;
  private let m_slowedByScan: Bool;
  private let m_slowedSeconds: Float;
  private let m_slowedScanSeconds: Float;
  private let m_lastSlowed: Float;
  // The cue while another V's bubble slows ours: on, whether our own Focus
  // held the screen effect when it began, and whether this session's
  // notice was shown.
  private let m_cueOn: Bool;
  private let m_cueKept: Bool;
  private let m_cueNoticed: Bool;
  // Host: NPCs answering a V's Sandevistan with their own right now (the
  // AI's sandevistanVersusSandevistan sub-action): out of every bubble.
  private let m_counters: array<EntityID>;
  private let m_bulletLogged: Bool;
  // NPC shots at a guest's V (Shots.reds).
  private let m_shots: ref<TandemShots>;
  // What we set on entities, and the bubble update's clock (real time).
  private let m_applied: array<ref<TandemTimeApplied>>;
  private let m_lastBubbles: Float;
  // Remote players' abilities as last seen (for the puppet's effect).
  private let m_seenChanges: Int32;
  private let m_fxPlayers: array<Uint32>;
  private let m_fx: CName;
  // Bubble sizes as last logged, by owner.
  private let m_bubbleOwners: array<Uint32>;
  private let m_bubbleSizes: array<Int32>;
  // (test) "time test SCALE SECONDS": a dilation set as Sandevistan's;
  // "time scan SCALE SECONDS": as the scanner's.
  private let m_testUntil: Float;
  private let m_testScale: Float;
  private let m_testReason: CName;
  private let m_measureFrom: Float;
  private let m_measureSim: Float;
  // Host: the menus' reasons unset this session (logged once each), and a
  // shop's screen that still slowed our world after that (logged once a
  // screen).
  private let m_menusDropped: array<CName>;
  private let m_shopLogged: Bool;
  // (test) "time menu NAME SECONDS": a menu's slow motion as the game sets
  // it, without opening the menu.
  private let m_menuUntil: Float;
  private let m_menuName: String;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_shots = new TandemShots();
    this.m_shots.Start(game);
    this.m_neutral = 1.0;
    this.m_sentScale = 1.0;
    this.m_fx = TandemTime.PuppetFx();
  }

  // The implant's effect played on a puppet while its player is in a
  // Sandevistan. The NPC Sandevistan's own effect is in TweakDB/entity data
  // we have not read: this is the one vanilla's Sandevistan boss carries
  // (AdamSmasher, final.reds:296271). Try others in game: "time fx NAME".
  public static func PuppetFx() -> CName {
    return n"sandevistan_medium_00";
  }

  public static func Of(game: GameInstance) -> ref<TandemTime> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.TimeSync() : null;
  }

  private func TandemSys() -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
  }

  private func RealNow() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }

  private func SimNow() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  // ---- the frame ----

  public func Update() -> Void {
    if !TandemNative.IsStarted() {
      return;
    }
    let player = GetPlayer(this.m_game);
    let system = this.TandemSys();
    // Never behind the loading screen (TandemSystem.Tick checks it too).
    if !IsDefined(player) || !IsDefined(system) || system.You() == 0u || !system.IsPlaying() {
      return;
    }
    let times = GameInstance.GetTimeSystem(this.m_game);
    let now = this.RealNow();
    this.EndTest(times, now);
    this.EndMenu(now);
    // The host's menus slow nothing while another player is in the session:
    // before anything reads the world's factor.
    if TandemTime.HostKeepsWorld() {
      this.DropMenus(times);
    }
    this.SendOurs(times, player);
    // Alone in the session, single player rules: nothing is shared.
    let others = system.PuppetPlayers();
    let together = ArraySize(others) > 0;
    if together && TandemNative.IsHost() && !TandemNative.IsTimeHostFull() {
      this.TakeBack(times);
    } else {
      ArrayClear(this.m_taken);
    }
    this.PuppetEffects(system);
    if now - this.m_lastBubbles >= 0.05 {
      this.m_lastBubbles = now;
      this.Bubbles(system, player);
    }
    // Our world runs slower than the network (the host with time_host =
    // full), or faster (a ride's own skip, customFFTimeDilation at 10:
    // Transit.reds): the NPCs we share are exempted. Only while a dilation is
    // active: what GetActiveTimeDilation answers with none is unknown (0
    // would exempt everything, every frame).
    let exempt = together && TandemTime.Dilated(times);
    this.Apply(system, exempt);
    this.Measure(now);
    this.m_shots.Tick();
  }

  // Our V's time abilities, to the others when they change.
  private func SendOurs(times: ref<TimeSystem>, player: ref<GameObject>) -> Void {
    let abilities = 0;
    let scale = 1.0;
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let id = Cast<StatsObjectID>(player.GetEntityID());
    // The factor is the one the game itself asked for (the implant's stat
    // with every modifier on it, a perk's or an effector's own value),
    // followed while it is on: a change mid-effect reaches the bubble.
    if times.IsTimeDilationActive(n"sandevistan") {
      abilities += 1;
      let asked = this.Asked(times, n"sandevistan", this.m_testUntil > 0.0 && Equals(this.m_testReason, n"sandevistan") ? this.m_testScale : 1.0);
      scale = MinF(scale, TandemTime.ScaleOf(stats.GetStatValue(id, gamedataStatType.TimeDilationSandevistanTimeScale), asked));
    } else {
      this.Forget(n"sandevistan");
    }
    if times.IsTimeDilationActive(n"kereznikov") {
      abilities += 2;
      let askedDodge = this.Asked(times, n"kereznikov", 1.0);
      scale = MinF(scale, TandemTime.ScaleOf(stats.GetStatValue(id, gamedataStatType.TimeDilationKerenzikovTimeScale), askedDodge));
    } else {
      this.Forget(n"kereznikov");
    }
    // Perks and implants that slow time: Focus, Deadeye's reload, and every
    // SetTimeDilationEffector on our V (Reflex Tuner, Synaptic Accelerator,
    // second chance, Berserk if its record slows time...).
    let slow = MinF(this.Slow(times, n"focusedStatePerkDilation"), this.Slow(times, n"coolReloadPerkDilation"));
    let kept: array<CName>;
    for reason in this.m_effectors {
      if times.IsTimeDilationActive(reason) {
        ArrayPush(kept, reason);
        slow = MinF(slow, this.Slow(times, reason));
      } else {
        this.Forget(reason);
      }
    }
    this.m_effectors = kept;
    if slow < 1.0 {
      abilities += 4;
      scale = MinF(scale, slow);
    }
    // The scanner's own slow motion: its bubble, at the factor the game set.
    let scan = this.Slow(times, n"focusMode");
    if times.IsTimeDilationActive(n"focusMode") {
      abilities += 8;
      scale = MinF(scale, scan);
    }
    this.CountScan((abilities & 8) != 0);
    // A slow motion none of those explains: another mod's (the time system
    // does not say whose reason it is), shared as a bubble like a perk's.
    let other = this.Unexplained(times);
    if other < 1.0 {
      if (abilities & 4) == 0 {
        abilities += 4;
      }
      scale = MinF(scale, other);
    }
    if abilities == this.m_sentAbilities && AbsF(scale - this.m_sentScale) < 0.01 {
      return;
    }
    this.m_sentAbilities = abilities;
    this.m_sentScale = scale;
    TandemNative.SendTimeEffect(abilities, scale);
    TandemNative.Log(s"time: our abilities \(abilities) (world speed \(scale))");
  }

  // The world speed of a slow motion no reason we know of explains (a
  // mod's: OverclockOverheat's optional one, any other), else 1. The ones
  // we follow explain theirs; the local ones (melee hit stop, deflect, a
  // menu's, a ride's skip, our V slowed by another V's bubble) leave it
  // alone (core's TimeBubbles::UnexplainedSlow decides).
  private func Unexplained(times: ref<TimeSystem>) -> Float {
    if !TandemTime.Dilated(times) {
      return 1.0;
    }
    let explained = 1.0;
    let followed = [n"sandevistan", n"kereznikov", n"focusedStatePerkDilation", n"coolReloadPerkDilation", n"focusMode"];
    for reason in this.m_effectors {
      ArrayPush(followed, reason);
    }
    for reason in followed {
      if times.IsTimeDilationActive(reason) {
        explained = MinF(explained, times.GetActiveTimeDilation(reason, true));
      }
    }
    let local = this.m_selfSet;
    let kept = [n"meleeHit", n"meleeHitStrong", n"deflect", n"customFFTimeDilation", n"tandemBubble", n"timeDilation", n"TimeSkip", n"NetworkBreach",
      TweakDBInterface.GetCName(t"timeSystem.radialMenu.reason", n""), TweakDBInterface.GetCName(t"timeSystem.hubMenu.reason", n""),
      TweakDBInterface.GetCName(t"timeSystem.vehicleRadioMenu.reason", n""), TweakDBInterface.GetCName(t"timeSystem.nanoWireBreach.reason", n""),
      TweakDBInterface.GetCName(t"timeSystem.settings.reason", n"")];
    for reason in TandemTime.MenuReasons() {
      ArrayPush(kept, reason);
    }
    for reason in kept {
      if IsNameValid(reason) && times.IsTimeDilationActive(reason) {
        local = true;
      }
    }
    // A shop's or the stash's screen slows the world (0.05) for a reason
    // scripts cannot name: ours alone, like any menu's.
    let vendors = TandemVendors.Of(this.m_game);
    if IsDefined(vendors) && vendors.ScreenUp() {
      local = true;
    }
    let factor = TandemNative.TimeUnexplainedSlow(times.GetActiveTimeDilation(), explained, local);
    if factor < 1.0 && !this.m_unexplainedSaid {
      this.m_unexplainedSaid = true;
      TandemNative.Log(s"time: a slow motion of no reason we know slows our world to \(factor): shared as a time bubble");
      TandemText.Notice(this.m_game, "Time-ModSlow");
    }
    return factor;
  }

  // ---- the host's menus ----

  // We host and another player is in the session: our world is the shared
  // one, and no menu, popup or screen of ours may slow or stop it (every NPC
  // and turret we run would crawl on the others' screens). Alone, or a
  // guest: single player's menus. Reads natives only (the wraps at the end
  // call it from wherever the game opens a menu).
  public static func HostKeepsWorld() -> Bool {
    if !TandemNative.IsStarted() || !TandemNative.IsHost() {
      return false;
    }
    let others = TandemNative.OtherPlayers();
    return ArraySize(others) > 0;
  }

  // The slow motion or stop the menus and screens set by name, without a
  // TimeDilationHelper profile (docs/research/coop-time.md §3.4): the map
  // (WorldMapMenuGameController, 0), a shop's, the stash's, the ripperdoc's
  // and the wardrobe's screens (VendorStash, 0, the player's factor too), a
  // tutorial popup (UI_TutorialPopup, 0) and the breach's screen (minigame,
  // 1e-5). Not here: the time-skip wheel (TimeSkip: Activities.reds asks
  // everyone) and the breach's dive (NetworkBreach: a listener waits for it
  // to end, coop-time.md §8 question 6).
  // With them, the profiles' reasons (the wrap at the end refuses those
  // menus; one opened before the session began is unset here).
  public static func MenuReasons() -> array<CName> {
    let reasons = [n"WorldMap", n"VendorStash", n"UI_TutorialPopup", n"minigame"];
    for profile in [t"timeSystem.radialMenu.reason", t"timeSystem.hubMenu.reason", t"timeSystem.vehicleRadioMenu.reason"] {
      let reason = TweakDBInterface.GetCName(profile, n"");
      if IsNameValid(reason) && !ArrayContains(reasons, reason) {
        ArrayPush(reasons, reason);
      }
    }
    return reasons;
  }

  // Host, another player in the session: each menu's reason unset as soon
  // as it is set (the script tick runs on real time: a frame of it at most).
  // The menu's own close unsets it again, which changes nothing.
  private func DropMenus(times: ref<TimeSystem>) -> Void {
    for reason in TandemTime.MenuReasons() {
      if times.IsTimeDilationActive(reason) {
        times.UnsetTimeDilation(reason, n"None");
        // Those set our V's own factor too (VendorStash, the profiles).
        if !Equals(reason, n"WorldMap") && !Equals(reason, n"UI_TutorialPopup") && !Equals(reason, n"minigame") {
          times.UnsetTimeDilationOnLocalPlayerZero(reason, n"None");
        }
        if !ArrayContains(this.m_menusDropped, reason) {
          ArrayPush(this.m_menusDropped, reason);
          TandemNative.Log(s"time: a menu (\(NameToString(reason))) would slow the shared world: unset, the host's menus slow nothing");
        }
      }
    }
    // A shop's or the stash's screen slowed our world to 0.05 under a reason
    // scripts could not name (seen 2026-11-21): what is left of it, once a
    // screen, with the names it might be under.
    let vendors = TandemVendors.Of(this.m_game);
    let up = IsDefined(vendors) && vendors.ScreenUp();
    if up && !this.m_shopLogged && TandemTime.Dilated(times) {
      this.m_shopLogged = true;
      let hub = TweakDBInterface.GetCName(t"timeSystem.hubMenu.reason", n"");
      TandemNative.Log(s"time: a shop's screen still slows the shared world to \(times.GetActiveTimeDilation()) (VendorStash \(times.IsTimeDilationActive(n"VendorStash")), hub menu \(NameToString(hub)) \(IsNameValid(hub) && times.IsTimeDilationActive(hub))): the NPCs we share are exempted");
    } else if !up {
      this.m_shopLogged = false;
    }
  }

  // Our world runs slowed now: a dilation is active and its factor is one.
  private static func Dilated(times: ref<TimeSystem>) -> Bool {
    if !times.IsTimeDilationActive() {
      return false;
    }
    let factor = times.GetActiveTimeDilation();
    return factor > 0.0 && (factor < 0.99 || factor > 1.01);
  }

  // An implant's stat, or the test's scale when there is none (no implant).
  private static func ScaleOf(stat: Float, fallback: Float) -> Float {
    return stat > 0.0 && stat < 1.0 ? stat : fallback;
  }

  // The factor the game asked for under a reason: read while it is still
  // there, remembered once our takeback has put the reason at normal speed.
  private func Asked(times: ref<TimeSystem>, reason: CName, fallback: Float) -> Float {
    let factor = times.GetActiveTimeDilation(reason, true);
    let index = ArrayFindFirst(this.m_askedReasons, reason);
    if factor > 0.0 && factor < 0.99 {
      if index < 0 {
        ArrayPush(this.m_askedReasons, reason);
        ArrayPush(this.m_askedScales, factor);
      } else {
        this.m_askedScales[index] = factor;
      }
      return factor;
    }
    return index < 0 ? fallback : this.m_askedScales[index];
  }

  private func Forget(reason: CName) -> Void {
    let index = ArrayFindFirst(this.m_askedReasons, reason);
    if index >= 0 {
      ArrayErase(this.m_askedReasons, index);
      ArrayErase(this.m_askedScales, index);
    }
  }

  // A slow-motion reason's factor while it is on (1: off).
  private func Slow(times: ref<TimeSystem>, reason: CName) -> Float {
    if !times.IsTimeDilationActive(reason) {
      this.Forget(reason);
      return 1.0;
    }
    return this.Asked(times, reason, 1.0);
  }

  // A SetTimeDilationEffector slowed time for our V (the wrap below): its
  // reason is followed like the perks' until it ends.
  public func EffectorOn(reason: CName, dilation: Float) -> Void {
    if !IsNameValid(reason) || Equals(reason, n"sandevistan") || Equals(reason, n"kereznikov") || Equals(reason, n"focusMode") {
      return;
    }
    if !ArrayContains(this.m_effectors, reason) {
      ArrayPush(this.m_effectors, reason);
      TandemNative.Log(s"time: effector dilation \(NameToString(reason)) at \(dilation)");
    }
  }

  // Host (time_host = bubble): the world part of our V's dilations taken
  // back. The reasons a state holds are kept active at normal speed (their
  // state ends on "no longer active", and unsets them itself); the ones that
  // expire on their own are unset.
  private func TakeBack(times: ref<TimeSystem>) -> Void {
    this.Hold(times, n"sandevistan", 999.0);
    this.Hold(times, n"kereznikov", 999.0);
    this.Hold(times, n"focusedStatePerkDilation", 12.0);
    this.Hold(times, n"coolReloadPerkDilation", 3.0);
    // The scanner's state sets its dilation again whenever none is active
    // (TimeDilationFocusModeEvents.OnUpdate): held, never unset.
    this.Hold(times, n"focusMode", 999.0);
    // Effector dilations end by their own unset (ActionOff) or duration;
    // held a little at a time so one that only expires still does.
    for reason in this.m_effectors {
      this.Hold(times, reason, 0.5);
    }
    this.Drop(times, n"meleeHit");
    this.Drop(times, n"deflect");
    let left = times.GetActiveTimeDilation();
    let now = this.RealNow();
    if TandemTime.Dilated(times) && now - this.m_unknownLogged > 5.0 {
      this.m_unknownLogged = now;
      TandemNative.Log(s"time: an unknown dilation slows the world to \(left) (a menu, or a reason to add)");
    }
  }

  private func Hold(times: ref<TimeSystem>, reason: CName, duration: Float) -> Void {
    if !times.IsTimeDilationActive(reason) {
      ArrayRemove(this.m_taken, reason);
      return;
    }
    let asked = times.GetActiveTimeDilation(reason, true);
    if asked >= 0.99 {
      return;
    }
    this.Asked(times, reason, 1.0);
    times.SetTimeDilation(reason, this.m_neutral, duration, n"None", n"None");
    if !this.m_neutralChecked {
      this.m_neutralChecked = true;
      if !times.IsTimeDilationActive(reason) {
        this.m_neutral = 0.999;
        times.SetTimeDilation(reason, this.m_neutral, duration, n"None", n"None");
        TandemNative.Log("time: a dilation of 1.0 reads as none: taken-back dilations are kept at 0.999");
      }
    }
    if !ArrayContains(this.m_taken, reason) {
      ArrayPush(this.m_taken, reason);
      TandemNative.Log(s"time: \(NameToString(reason)) would slow the shared world to \(asked): kept at \(this.m_neutral), still active \(times.IsTimeDilationActive(reason))");
    }
  }

  private func Drop(times: ref<TimeSystem>, reason: CName) -> Void {
    if times.IsTimeDilationActive(reason) {
      times.UnsetTimeDilation(reason, n"None");
      TandemNative.Log(s"time: \(NameToString(reason)) dropped (it would slow the shared world)");
    }
  }

  // ---- the bubbles ----

  private func Bubbles(system: ref<TandemSystem>, player: ref<GameObject>) -> Void {
    let owners: array<Uint32>;
    let scales: array<Float>;
    let at: array<Vector4>;
    // Every other bubble is a scanner's (for the partner's measure).
    let scansOnly = true;
    // Ours and the others' through the core's rule (TimeBubbleScale): the
    // abilities that make a bubble, the scanner among them, the same on the
    // host and on a guest. Ours is also our `own` below: a V scanning, or in
    // a Sandevistan, is slowed by another V's bubble only by the ratio.
    let own = TandemNative.TimeBubbleScale(this.m_sentAbilities, this.m_sentScale);
    if own < 1.0 {
      ArrayPush(owners, system.You());
      ArrayPush(scales, own);
      ArrayPush(at, player.GetWorldPosition());
    }
    for other in TandemNative.TimePlayers() {
      let abilities = TandemNative.TimeAbilities(other);
      let scale = TandemNative.TimeBubbleScale(abilities, TandemNative.TimeScale(other));
      let spot = TandemNative.RemotePosition(other);
      if scale < 1.0 && spot.W != 0.0 {
        if (abilities & 7) != 0 {
          scansOnly = false;
        }
        ArrayPush(owners, other);
        ArrayPush(scales, scale);
        ArrayPush(at, spot);
      }
    }
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let npcs = system.NpcsSync();
    if IsDefined(npcs) {
      let shared = npcs.TimeIds();
      let entities = npcs.TimeEntities();
      let i = 0;
      while i < ArraySize(shared) {
        let body = GameInstance.FindEntityByID(this.m_game, entities[i]) as GameObject;
        if IsDefined(body) {
          ArrayPush(ids, shared[i]);
          ArrayPush(positions, body.GetWorldPosition());
        }
        i += 1;
      }
      // ...and the proxies of the NPCs the others run.
      let shown = npcs.ProxyIds();
      let bodies = npcs.ProxyEntities();
      i = 0;
      while i < ArraySize(shown) {
        let proxy = npcs.ProxyBody(bodies[i]) as GameObject;
        if IsDefined(proxy) {
          ArrayPush(ids, shown[i]);
          ArrayPush(positions, proxy.GetWorldPosition());
        }
        i += 1;
      }
    }
    let changes = TandemNative.UpdateTimeBubbles(owners, scales, at, ids, positions);
    if ArraySize(changes) > 0 {
      this.ReportBubbles(owners);
    }
    let by = TandemNative.TimeSlowedBy(system.You(), own, player.GetWorldPosition(), owners, scales, at, this.m_selfSet);
    this.SlowSelf(player, TandemNative.TimePlayerFactor(system.You(), own, player.GetWorldPosition(), owners, scales, at, this.m_selfSet), scansOnly, by);
  }

  // Our V inside another V's bubble is slowed like the NPCs in it, unless
  // its own ability keeps up (tandem/timebubble.hpp). Our game does it to
  // our own V, with the player's own time factor (what Kerenzikov gives V,
  // TimeSystem.SetTimeDilationOnLocalPlayerZero, final.reds:53870), eased
  // in and out with Kerenzikov's curves. If the time system counts that as
  // a dilation being active (it would bar our V's own Sandevistan, whose
  // enter condition refuses under a foreign dilation, final.reds:523680),
  // the per-entity dilation on V is used instead (checked once).
  // Not slowed: a dead or downed V, and a V in a vehicle (a car has no time
  // of its own to slow with it).
  // With an ability of its own (`own`: the scanner too), the factor is the
  // ratio of the other's bubble to it (PlayerFactor), and it is the whole
  // slow: the scanner and the Sandevistan exempt our V from their global
  // dilation (SetIgnoreTimeDilationOnLocalPlayerZero(true), final.reds
  // 522203-522207), so nothing multiplies it. Our reason, "tandemBubble",
  // is never theirs: their state machines set and unset "focusMode" and
  // "sandevistan" by name and never touch it, and it never bars them (the
  // check below). A scanner profile that slows the player too
  // (applyTimeDilationToPlayer) would put a second player factor next to
  // ours: how the two combine is to read in game ("time report" logs what
  // our V's speed reads).
  private func SlowSelf(player: ref<GameObject>, factor: Float, scansOnly: Bool, by: Uint32) -> Void {
    if player.IsDead() || VehicleComponent.IsMountedToVehicle(this.m_game, player) {
      factor = 1.0;
    }
    if !this.m_selfSet && factor >= 0.999 || this.m_selfSet && AbsF(factor - this.m_selfFactor) < 0.01 {
      return;
    }
    let times = GameInstance.GetTimeSystem(this.m_game);
    if factor >= 0.999 {
      this.FreeSelf();
      TandemNative.Log(s"time: our V back to its own speed after \(this.m_lastSlowed) s");
      return;
    }
    if !this.m_selfSet {
      this.m_slowedSince = this.RealNow();
      this.m_slowedByScan = scansOnly;
      this.CueOn(by);
    } else if !scansOnly {
      this.m_slowedByScan = false;
    }
    if !this.m_selfEntity {
      times.SetTimeDilationOnLocalPlayerZero(n"tandemBubble", factor, 999.0, n"KereznikovDodgeEaseIn", n"KerenzikovEaseOut");
      if !this.m_selfChecked {
        this.m_selfChecked = true;
        if times.IsTimeDilationActive(n"tandemBubble") {
          times.UnsetTimeDilationOnLocalPlayerZero(n"tandemBubble", n"None");
          this.m_selfEntity = true;
          TandemNative.Log("time: the player's own factor counts as an active dilation: our V is slowed as an entity instead");
          TandemDegraded.Fallback("v slowed as entity", "the player's own factor counts as an active dilation: our V is slowed as an entity");
        }
      }
    }
    if this.m_selfEntity {
      let body = player as gamePuppet;
      if IsDefined(body) {
        body.SetIndividualTimeDilation(n"tandemBubble", factor, 999.0, n"KereznikovDodgeEaseIn", n"KerenzikovEaseOut", true, false);
      }
    }
    this.m_selfSet = true;
    this.m_selfFactor = factor;
    let whose = scansOnly ? ", a scanner's" : "";
    TandemNative.Log(s"time: our V slowed to \(factor) (in another V's bubble\(whose); own ability \(this.m_sentAbilities) at \(this.m_sentScale))");
  }

  // Our V's own speed back: out of the bubble, and always when the session
  // or the world ends. How long it was slowed.
  private func FreeSelf() -> Void {
    if !this.m_selfSet {
      return;
    }
    let seconds = this.RealNow() - this.m_slowedSince;
    this.m_lastSlowed = seconds;
    this.m_slowedSeconds += seconds;
    if this.m_slowedByScan {
      this.m_slowedScanSeconds += seconds;
    }
    this.m_selfSet = false;
    this.m_selfFactor = 1.0;
    this.CueOff();
    if this.m_selfEntity {
      let body = GetPlayer(this.m_game);
      if IsDefined(body) && body.HasIndividualTimeDilation(n"tandemBubble") {
        body.UnsetIndividualTimeDilation(n"KerenzikovEaseOut");
      }
    } else {
      GameInstance.GetTimeSystem(this.m_game).UnsetTimeDilationOnLocalPlayerZero(n"tandemBubble", n"KerenzikovEaseOut");
    }
  }

  // ---- the cue on our side while another V's bubble slows us ----

  // The game's own soft slow motion, as the Focus perk plays it on V when it
  // slows time a little (AimingStateEvents, final.reds:542843-542849; its
  // end, :44513-44514): its full-screen effect on our V and its enter
  // sound; the exit sound when we are free again. Subtle on purpose: being
  // slowed reads as "my partner's slow time", not as a fault. Only while the
  // factor is under 1 (TimePlayerFactor): our own ability matching the
  // bubble plays nothing. Our own Focus or Deadeye reload holding the same
  // effect keeps it: never started nor broken here then.
  public static func CueEffect() -> CName {
    return n"cool_perk_focused_state_fullscreen";
  }

  private func OwnFocus() -> Bool {
    let times = GameInstance.GetTimeSystem(this.m_game);
    return times.IsTimeDilationActive(n"focusedStatePerkDilation") || times.IsTimeDilationActive(n"coolReloadPerkDilation");
  }

  private func CueOn(by: Uint32) -> Void {
    let player = GetPlayer(this.m_game);
    if this.m_cueOn || !IsDefined(player) {
      return;
    }
    this.m_cueOn = true;
    this.m_cueKept = this.OwnFocus();
    if !this.m_cueKept {
      GameObjectEffectHelper.StartEffectEvent(player, TandemTime.CueEffect(), false);
    }
    GameObject.PlaySoundEvent(player, n"time_dilation_focused_enter");
    let abilities = by != 0u ? TandemNative.TimeAbilities(by) : 0;
    TandemNative.Log(s"time: slowed by player \(by)'s bubble (abilities \(abilities)): the game's slow-motion cue on (\(NameToString(TandemTime.CueEffect()))\(this.m_cueKept ? ", kept by our own Focus" : ""), time_dilation_focused_enter)");
    if !this.m_cueNoticed && by != 0u {
      this.m_cueNoticed = true;
      let key = "Time-SlowedOther";
      if (abilities & 7) == 0 && (abilities & 8) != 0 {
        key = "Time-SlowedScanner";
      } else if (abilities & 1) != 0 {
        key = "Time-SlowedSandevistan";
      }
      TandemText.Notice(this.m_game, key, TandemPartyHud.NameOf(by));
    }
  }

  private func CueOff() -> Void {
    if !this.m_cueOn {
      return;
    }
    this.m_cueOn = false;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if !this.m_cueKept && !this.OwnFocus() {
      GameObjectEffectHelper.BreakEffectLoopEvent(player, TandemTime.CueEffect());
    }
    GameObject.PlaySoundEvent(player, n"time_dilation_focused_exit");
    TandemNative.Log("time: the slow-motion cue off (time_dilation_focused_exit)");
  }

  // Our scanner's bubble starts or ends: how long each ran, in real time.
  private func CountScan(on: Bool) -> Void {
    let now = this.RealNow();
    if on && this.m_scanSince <= 0.0 {
      this.m_scanSince = now;
    } else if !on && this.m_scanSince > 0.0 {
      let seconds = now - this.m_scanSince;
      this.m_scanSince = 0.0;
      this.m_scanSeconds += seconds;
      this.m_scans += 1;
      TandemNative.Log(s"time: our scanner's bubble ran \(seconds) s");
    }
  }

  // One line per bubble whose size changed, and per bubble that ended.
  private func ReportBubbles(owners: array<Uint32>) -> Void {
    let i = 0;
    while i < ArraySize(this.m_bubbleOwners) {
      if !ArrayContains(owners, this.m_bubbleOwners[i]) {
        TandemNative.Log(s"time: bubble of player \(this.m_bubbleOwners[i]) ended");
      }
      i += 1;
    }
    let sizes: array<Int32>;
    for owner in owners {
      let size = TandemNative.TimeBubbleMembers(owner);
      let index = ArrayFindFirst(this.m_bubbleOwners, owner);
      if index < 0 || this.m_bubbleSizes[index] != size {
        TandemNative.Log(s"time: bubble of player \(owner): \(size) NPC(s)");
      }
      ArrayPush(sizes, size);
    }
    this.m_bubbleOwners = owners;
    this.m_bubbleSizes = sizes;
  }

  // Host: each NPC we share at its bubble's speed, ignoring our own global
  // factor; with `exempt`, the other shared NPCs at normal speed, ignoring
  // it too. Only the host's own NPCs, run by the game's AI as vanilla's
  // per-entity dilations are: proxies and puppets are moved by tandem's own
  // movement controller (plugin/src/engine/controller.cpp), and a dilation
  // on those is untried. Suspected in the freeze of 2026-11-18: on the
  // first frame after "playing" this set a dilation on every puppet and
  // proxy (`exempt` was likely true with no dilation at all).
  private func Apply(system: ref<TandemSystem>, exempt: Bool) -> Void {
    for applied in this.m_applied {
      applied.seen = false;
    }
    let npcs = system.NpcsSync();
    // The NPCs we run: their owner slows them inside a bubble.
    if IsDefined(npcs) {
      let shared = npcs.TimeIds();
      let entities = npcs.TimeEntities();
      let i = 0;
      while i < ArraySize(shared) {
        // An enemy answering with its own Sandevistan is not slowed by a
        // bubble while it does (Counter).
        let factor = ArrayContains(this.m_counters, entities[i]) ? 1.0 : TandemNative.TimeFactorOf(shared[i]);
        this.Want(entities[i], factor, exempt, shared[i]);
        i += 1;
      }
    }
    // No longer a target (gone, dead, no longer shared): given back, unless
    // Want gave it back already (factor -1).
    let kept: array<ref<TandemTimeApplied>>;
    for applied in this.m_applied {
      if applied.seen {
        ArrayPush(kept, applied);
      } else if applied.factor >= 0.0 {
        this.Release(applied.id, n"None");
      }
    }
    this.m_applied = kept;
  }

  private func Want(id: EntityID, factor: Float, exempt: Bool, npc: Uint32) -> Void {
    let wanted = factor < 0.999 || exempt;
    let applied = this.Find(id);
    if IsDefined(applied) {
      applied.seen = wanted;
      if wanted && AbsF(applied.factor - factor) > 0.001 {
        this.Set(id, factor, npc);
        applied.factor = factor;
      } else if !wanted && applied.factor >= 0.0 {
        // Out of its last bubble: eased back like the implant's own end.
        this.Release(id, applied.factor < 0.999 ? n"SandevistanEaseOut" : n"None");
        if npc != 0u && applied.factor < 0.999 {
          TandemNative.Log(s"time: npc \(npc) back to normal speed");
        }
        applied.factor = -1.0;
      }
      return;
    }
    if !wanted {
      return;
    }
    if this.Set(id, factor, npc) {
      let entry = new TandemTimeApplied();
      entry.id = id;
      entry.factor = factor;
      entry.seen = true;
      ArrayPush(this.m_applied, entry);
    }
  }

  private func Set(id: EntityID, factor: Float, npc: Uint32) -> Bool {
    let body = GameInstance.FindEntityByID(this.m_game, id) as gamePuppet;
    if !IsDefined(body) {
      return false;
    }
    // An entity has one dilation of its own: an NPC that set one itself (an
    // enemy Sandevistan answering ours, sandevistanVersusSandevistan; a hit
    // reaction) keeps it, as in single player, and joins when it is over.
    if body.HasIndividualTimeDilation() {
      if !body.HasIndividualTimeDilation(n"tandemTime") {
        return false;
      }
      body.UnsetIndividualTimeDilation(n"None");
    }
    body.SetIndividualTimeDilation(n"tandemTime", factor, 999.0, n"None", n"None", true, false);
    if npc != 0u && factor < 0.999 {
      TandemNative.Log(s"time: npc \(npc) at \(factor) (now \(body.GetTimeDilationValue()), ignoring our global factor \(body.IsIgnoringGlobalTimeDilation()))");
    }
    return true;
  }

  private func Release(id: EntityID, curve: CName) -> Void {
    let body = GameInstance.FindEntityByID(this.m_game, id) as gamePuppet;
    if IsDefined(body) && body.HasIndividualTimeDilation(n"tandemTime") {
      body.UnsetIndividualTimeDilation(curve);
    }
  }

  private func Find(id: EntityID) -> ref<TandemTimeApplied> {
    for applied in this.m_applied {
      if applied.id == id {
        return applied;
      }
    }
    return null;
  }

  // ---- enemies with a Sandevistan, and bullets ----

  // An NPC's AI starts or ends its answer to a Sandevistan (the wraps at the
  // end). In single player the NPC speeds itself up by what V's slows the
  // world (final.reds:427380-427400), so it moves at real speed for as long:
  // here it is left out of every bubble for as long. The host's own V's
  // case is the game's own; this gives a guest's bubble the same.
  public func Counter(npc: ref<GameObject>, on: Bool) -> Void {
    if !IsDefined(npc) {
      return;
    }
    let id = npc.GetEntityID();
    if on && !ArrayContains(this.m_counters, id) {
      ArrayPush(this.m_counters, id);
      TandemNative.Log(s"time: an NPC answers with its own sandevistan (\(ArraySize(this.m_counters)) now)");
    } else if !on {
      ArrayRemove(this.m_counters, id);
    }
  }

  // The speed we set on an entity (-1: none).
  public func AppliedFactor(id: EntityID) -> Float {
    let applied = this.Find(id);
    return IsDefined(applied) ? applied.factor : -1.0;
  }

  // A bullet or projectile leaves an NPC we slowed: it flies at that NPC's
  // speed, as bullets do under V's Sandevistan in single player (there the
  // global factor slows every projectile entity; here the bullet, an
  // ItemObject and so a TimeDilatable like its shooter, gets the bubble's
  // factor of its own).
  public func SlowBullet(bullet: ref<ItemObject>, shooter: wref<GameObject>) -> Void {
    if !IsDefined(bullet) || !IsDefined(shooter) {
      return;
    }
    // A guest: a proxy's shot at our V (Shots.reds) brings its own speed.
    if this.m_shots.SetUp(bullet, shooter) {
      return;
    }
    let factor = this.AppliedFactor(shooter.GetEntityID());
    if factor < 0.0 {
      return;
    }
    bullet.SetIndividualTimeDilation(n"tandemTime", factor, 30.0, n"None", n"None", true, false);
    if !this.m_bulletLogged && factor < 0.999 {
      this.m_bulletLogged = true;
      TandemNative.Log(s"time: a slowed NPC's bullet flies at \(factor) (\(NameToString(bullet.GetClassName())))");
    }
  }

  // ---- what the others see ----

  // A remote player in a Sandevistan: the implant's effect on their puppet.
  private func PuppetEffects(system: ref<TandemSystem>) -> Void {
    let changes = TandemNative.TimeChanges();
    if changes == this.m_seenChanges {
      return;
    }
    this.m_seenChanges = changes;
    let active: array<Uint32>;
    for other in TandemNative.TimePlayers() {
      let abilities = TandemNative.TimeAbilities(other);
      if (abilities & 1) != 0 {
        ArrayPush(active, other);
      }
      TandemNative.Log(s"time: player \(other) abilities \(abilities) (world speed \(TandemNative.TimeScale(other)))");
    }
    for other in active {
      if !ArrayContains(this.m_fxPlayers, other) {
        let puppet = system.PuppetOf(other);
        if IsDefined(puppet) {
          GameObjectEffectHelper.StartEffectEvent(puppet, this.m_fx, false);
        }
        TandemNative.Log(s"time: player \(other) in a sandevistan (effect \(NameToString(this.m_fx)) on its puppet: \(IsDefined(puppet)))");
      }
    }
    for other in this.m_fxPlayers {
      if !ArrayContains(active, other) {
        let puppet = system.PuppetOf(other);
        if IsDefined(puppet) {
          GameObjectEffectHelper.BreakEffectLoopEvent(puppet, this.m_fx);
        }
        TandemNative.Log(s"time: player \(other)'s sandevistan ended");
      }
    }
    this.m_fxPlayers = active;
  }

  // ---- the session ends, the world goes ----

  public func Shots() -> ref<TandemShots> {
    return this.m_shots;
  }

  public func End() -> Void {
    TandemNative.ClearTimeBubbles();
    this.m_shots.End();
    if this.m_selfSet {
      this.FreeSelf();
      TandemNative.Log("time: our V back to its own speed (the session ended)");
    }
    for applied in this.m_applied {
      this.Release(applied.id, n"None");
    }
    ArrayClear(this.m_applied);
    ArrayClear(this.m_fxPlayers);
    ArrayClear(this.m_bubbleOwners);
    ArrayClear(this.m_bubbleSizes);
    ArrayClear(this.m_taken);
    ArrayClear(this.m_menusDropped);
    this.m_shopLogged = false;
    if this.m_menuUntil > 0.0 {
      this.CloseMenu();
    }
    this.CountScan(false);
    if this.m_scans > 0 || this.m_slowedSeconds > 0.0 {
      TandemNative.Log(s"time: this session, our scanner's bubbles: \(this.m_scans) for \(this.m_scanSeconds) s; our V slowed by the others' bubbles for \(this.m_slowedSeconds) s, \(this.m_slowedScanSeconds) s of it by a scanner's alone");
    }
    this.m_cueNoticed = false;
    this.m_scans = 0;
    this.m_scanSeconds = 0.0;
    this.m_slowedSeconds = 0.0;
    this.m_slowedScanSeconds = 0.0;
    this.m_sentAbilities = 0;
    this.m_sentScale = 1.0;
  }

  // (test reset) the test dilation of "time test" or "time scan" off.
  public func ResetTest() -> Void {
    if this.m_testUntil > 0.0 {
      this.m_testUntil = 0.0;
      GameInstance.GetTimeSystem(this.m_game).UnsetTimeDilation(this.m_testReason, n"None");
      TandemNative.Log("time: test dilation off (test reset)");
    }
    if this.m_menuUntil > 0.0 {
      this.CloseMenu();
      TandemNative.Log("time: test menu off (test reset)");
    }
    // The slowed-down notice may be shown again (a scenario reads it; the
    // session keeps the flag).
    this.m_cueNoticed = false;
  }

  public func Stop() -> Void {
    this.End();
    if this.m_testUntil > 0.0 {
      GameInstance.GetTimeSystem(this.m_game).UnsetTimeDilation(this.m_testReason, n"None");
      this.m_testUntil = 0.0;
    }
  }

  // ---- tests (tools/game.sh cmd time ...) ----

  // "time test SCALE SECONDS": a global dilation as Sandevistan's (no state
  // machine behind it); on the host it must be taken back, and the world
  // speed (sim time over real time) is logged a second in.
  // "time scan SCALE SECONDS": the same under the scanner's reason
  // ("focusMode"), as its state sets it: its bubble without opening it.
  // "time fx NAME": the effect played on puppets in a Sandevistan, now.
  // "time report": what this game does with time now.
  public func Command(command: String) -> Void {
    let times = GameInstance.GetTimeSystem(this.m_game);
    if StrBeginsWith(command, "time test") || StrBeginsWith(command, "time scan") {
      let scanner = StrBeginsWith(command, "time scan");
      let rest = StrAfterFirst(command, scanner ? "scan " : "test ");
      let scale = StringToFloat(StrBeforeFirst(rest + " ", " "), 0.25);
      let seconds = StringToFloat(StrAfterFirst(rest, " "), 3.0);
      if this.m_testUntil > 0.0 {
        times.UnsetTimeDilation(this.m_testReason, n"None");
      }
      this.m_testReason = scanner ? n"focusMode" : n"sandevistan";
      this.m_testScale = scale;
      this.m_testUntil = this.RealNow() + seconds;
      this.m_measureFrom = this.RealNow();
      this.m_measureSim = this.SimNow();
      times.SetTimeDilation(this.m_testReason, scale, 999.0, n"None", n"None");
      TandemNative.Log(s"time: test dilation \(scale) for \(seconds) s (as \(NameToString(this.m_testReason)))");
    } else if StrBeginsWith(command, "time menu ") {
      let rest = StrAfterFirst(command, "menu ");
      this.OpenMenu(StrBeforeFirst(rest + " ", " "), StringToFloat(StrAfterFirst(rest, " "), 3.0));
    } else if StrBeginsWith(command, "time fx ") {
      let system = this.TandemSys();
      for other in this.m_fxPlayers {
        let old = IsDefined(system) ? system.PuppetOf(other) : null;
        if IsDefined(old) {
          GameObjectEffectHelper.BreakEffectLoopEvent(old, this.m_fx);
        }
      }
      this.m_fx = StringToName(StrAfterFirst(command, "fx "));
      if IsDefined(system) {
        for other in system.PuppetPlayers() {
          let puppet = system.PuppetOf(other);
          if IsDefined(puppet) {
            GameObjectEffectHelper.StartEffectEvent(puppet, this.m_fx, false);
          }
        }
      }
      TandemNative.Log(s"time: puppet effect \(NameToString(this.m_fx)) played on every puppet");
    } else if Equals(command, "time report") {
      TandemNative.Log(s"time: what the game's fire-rate formula reads (CalcCycleTimeDeltaFactor): sandevistan active \(times.IsTimeDilationActive(n"sandevistan")), factor \(times.GetActiveTimeDilation()); sandevistan charge \(GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(GetPlayer(this.m_game).GetEntityID()), gamedataStatPoolType.SandevistanCharge, true))%");
      TandemNative.Log(s"time: world speed here \(times.GetActiveTimeDilation()) (a dilation active: \(times.IsTimeDilationActive())), our abilities \(this.m_sentAbilities) (\(this.m_sentScale)), \(ArraySize(this.m_applied)) entities set, taken back \(ArraySize(this.m_taken)), neutral \(this.m_neutral), host full \(TandemNative.IsTimeHostFull())");
      let body = GetPlayer(this.m_game);
      if IsDefined(body) {
        TandemNative.Log(s"time: our V reads \(body.GetTimeDilationValue()) (slowed by a bubble \(this.m_selfSet) to \(this.m_selfFactor), as an entity \(this.m_selfEntity); scanner \(times.IsTimeDilationActive(n"focusMode")), sandevistan \(times.IsTimeDilationActive(n"sandevistan")))");
      }
      TandemNative.Log(s"time: our scanner's bubbles: \(this.m_scans) for \(this.m_scanSeconds) s; our V slowed by the others' bubbles for \(this.m_slowedSeconds) s, \(this.m_slowedScanSeconds) s of it by a scanner's alone (this session)");
      for applied in this.m_applied {
        let body = GameInstance.FindEntityByID(this.m_game, applied.id) as gamePuppet;
        if IsDefined(body) {
          TandemNative.Log(s"time: \(EntityID.ToDebugStringDecimal(applied.id)) at \(applied.factor) (reads \(body.GetTimeDilationValue()))");
        }
      }
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }

  private func EndTest(times: ref<TimeSystem>, now: Float) -> Void {
    if this.m_testUntil > 0.0 && now >= this.m_testUntil {
      this.m_testUntil = 0.0;
      times.UnsetTimeDilation(this.m_testReason, n"None");
      TandemNative.Log("time: test dilation over");
    }
  }

  // "time menu NAME SECONDS": a menu's slow motion set as the game sets it
  // when that menu opens, without opening it: wheel (the radialMenu
  // profile, as QuickSlotsManager), hub (hubMenu, as MenuHubGameController),
  // map (WorldMap 0, as WorldMapMenuGameController), stash (VendorStash 0 on
  // the world and our V, as a shop's screen), tutorial (UI_TutorialPopup 0)
  // or breach (minigame 1e-5). The world speed is logged a second in.
  private func OpenMenu(name: String, seconds: Float) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if this.m_menuUntil > 0.0 {
      this.CloseMenu();
    }
    let times = GameInstance.GetTimeSystem(this.m_game);
    let started = true;
    if Equals(name, "wheel") {
      started = TimeDilationHelper.SetTimeDilationWithProfile(player, "radialMenu", true, true);
    } else if Equals(name, "hub") {
      started = TimeDilationHelper.SetTimeDilationWithProfile(player, "hubMenu", true, true);
    } else if Equals(name, "map") {
      times.SetTimeDilation(n"WorldMap", 0.0);
    } else if Equals(name, "stash") {
      times.SetTimeDilation(n"VendorStash", 0.0);
      times.SetTimeDilationOnLocalPlayerZero(n"VendorStash", 0.0);
    } else if Equals(name, "tutorial") {
      times.SetTimeDilation(n"UI_TutorialPopup", 0.0);
    } else if Equals(name, "breach") {
      times.SetTimeDilation(n"minigame", 0.00001);
    } else {
      TandemNative.Log(s"time: no test menu \(name) (wheel, hub, map, stash, tutorial, breach)");
      return;
    }
    this.m_menuName = name;
    this.m_menuUntil = this.RealNow() + seconds;
    this.m_measureFrom = this.RealNow();
    this.m_measureSim = this.SimNow();
    TandemNative.Log(s"time: test menu \(name) for \(seconds) s (its slow motion \(started ? "set" : "refused"))");
  }

  private func EndMenu(now: Float) -> Void {
    if this.m_menuUntil > 0.0 && now >= this.m_menuUntil {
      this.CloseMenu();
      TandemNative.Log("time: test menu over");
    }
  }

  // As the menu's own close does.
  private func CloseMenu() -> Void {
    this.m_menuUntil = 0.0;
    let player = GetPlayer(this.m_game);
    let times = GameInstance.GetTimeSystem(this.m_game);
    if Equals(this.m_menuName, "wheel") && IsDefined(player) {
      TimeDilationHelper.SetTimeDilationWithProfile(player, "radialMenu", false, false);
    } else if Equals(this.m_menuName, "hub") && IsDefined(player) {
      TimeDilationHelper.SetTimeDilationWithProfile(player, "hubMenu", false, false);
    } else if Equals(this.m_menuName, "map") {
      times.UnsetTimeDilation(n"WorldMap");
    } else if Equals(this.m_menuName, "stash") {
      times.UnsetTimeDilation(n"VendorStash");
      times.UnsetTimeDilationOnLocalPlayerZero(n"VendorStash");
    } else if Equals(this.m_menuName, "tutorial") {
      times.UnsetTimeDilation(n"UI_TutorialPopup");
    } else if Equals(this.m_menuName, "breach") {
      times.UnsetTimeDilation(n"minigame");
    }
    this.m_menuName = "";
  }

  // A second into a test: how fast the world ran (sim time over real time).
  private func Measure(now: Float) -> Void {
    if this.m_measureFrom > 0.0 && now - this.m_measureFrom >= 1.0 {
      let speed = (this.SimNow() - this.m_measureSim) / (now - this.m_measureFrom);
      this.m_measureFrom = 0.0;
      TandemNative.Log(s"time: world speed \(speed) over the test's first second");
    }
  }
}

// Implants and perks that slow time through an effector (Reflex Tuner,
// Synaptic Accelerator, second chance...): their reasons are TweakDB data,
// learned here as the game sets them. The effector itself is untouched.
@wrapMethod(SetTimeDilationEffector)
protected func ActionOn(owner: ref<GameObject>) -> Void {
  wrappedMethod(owner);
  if IsDefined(owner) && owner.IsPlayer() && this.m_duration != 0.0 && this.m_dilation < 1.0 {
    let time = TandemTime.Of(owner.GetGame());
    if IsDefined(time) {
      time.EffectorOn(this.m_reason, this.m_dilation);
    }
  }
}

// An enemy's answer to a Sandevistan (AISubActionApplyTimeDilation with the
// reason sandevistanVersusSandevistan): seen here, not changed.
@wrapMethod(AISubActionApplyTimeDilation_Record_Implementation)
public final static func Activate(context: ScriptExecutionContext, record: wref<AISubActionApplyTimeDilation_Record>) -> Void {
  wrappedMethod(context, record);
  if Equals(record.Reason(), n"sandevistanVersusSandevistan") {
    let npc = ScriptExecutionContext.GetOwner(context);
    let time = IsDefined(npc) ? TandemTime.Of(npc.GetGame()) : null;
    if IsDefined(time) {
      time.Counter(npc, true);
    }
  }
}

@wrapMethod(AISubActionApplyTimeDilation_Record_Implementation)
public final static func Deactivate(context: ScriptExecutionContext, record: wref<AISubActionApplyTimeDilation_Record>, const duration: Float, interrupted: Bool) -> Void {
  wrappedMethod(context, record, duration, interrupted);
  if Equals(record.Reason(), n"sandevistanVersusSandevistan") {
    let npc = ScriptExecutionContext.GetOwner(context);
    let time = IsDefined(npc) ? TandemTime.Of(npc.GetGame()) : null;
    if IsDefined(time) {
      time.Counter(npc, false);
    }
  }
}

// Under V's Sandevistan the game turns NPC shots into projectiles a V can
// see and dodge, when a dilation is active on that game
// (final.reds:173366-173373). An NPC slowed by a guest's bubble shoots on
// the host, where none is: the same choice is made for it, from the same
// records.
@wrapMethod(AIWeapon)
public final static func SetAttackBasedOnTimeDilation(owner: wref<GameObject>, weapon: wref<WeaponObject>, opt overrideRangedAttack: TweakDBID) -> Void {
  wrappedMethod(owner, weapon, overrideRangedAttack);
  if !IsDefined(owner) || !IsDefined(weapon) || !owner.IsNPC() {
    return;
  }
  let game = owner.GetGame();
  let time = TandemTime.Of(game);
  if !IsDefined(time) || GameInstance.GetTimeSystem(game).IsTimeDilationActive() {
    return;
  }
  let factor = time.AppliedFactor(owner.GetEntityID());
  // A guest: the speed of a shot its proxy fires at our V (Shots.reds).
  if factor < 0.0 && IsDefined(time.Shots()) {
    factor = time.Shots().FiredFactor(owner.GetEntityID());
  }
  if factor < 0.0 || factor >= 0.999 || StatusEffectSystem.ObjectHasStatusEffect(owner, t"BaseStatusEffect.SuicideWithWeapon") {
    return;
  }
  let attack: wref<RangedAttack_Record>;
  if TDBID.IsValid(overrideRangedAttack) {
    attack = TweakDBInterface.GetRangedAttackRecord(overrideRangedAttack);
  } else if IsDefined(weapon.GetCurrentRangedAttack()) {
    attack = weapon.GetCurrentRangedAttack().DefaultFire();
  }
  if IsDefined(attack) && IsDefined(attack.NPCTimeDilated()) {
    weapon.SetAttack(attack.NPCTimeDilated().GetID());
  }
}

@wrapMethod(BaseBullet)
protected cb func OnProjectileInitialize(eventData: ref<gameprojectileSetUpEvent>) -> Bool {
  TandemNative.HookRan(n"Time.BaseBullet.OnProjectileInitialize");
  let result = wrappedMethod(eventData);
  let time = TandemTime.Of(this.GetGame());
  if IsDefined(time) {
    time.SlowBullet(this, eventData.owner);
  }
  return result;
}

@wrapMethod(BaseProjectile)
protected cb func OnProjectileInitialize(eventData: ref<gameprojectileSetUpEvent>) -> Bool {
  TandemNative.HookRan(n"Time.BaseProjectile.OnProjectileInitialize");
  let result = wrappedMethod(eventData);
  let time = TandemTime.Of(this.GetGame());
  if IsDefined(time) {
    time.SlowBullet(this, eventData.owner);
  }
  return result;
}

// The menus' and popups' slow motion through a profile (every vanilla
// caller is one: the weapon and vehicle wheels, the phone and its message
// popup, the hub menu, the radio, list popups, docs/research/coop-time.md
// §2.8-2.9): refused on the host while another player is in the session,
// exactly as vanilla's own multiplayer branch refuses it (IsMultiplayer(),
// false in tandem's sessions), the close too: in multiplayer the close does
// nothing either, and ours would restore our V's exemption from the global
// factor (RestorePreviousIgnoreTimeDilationOnLocalPlayerZero) to what an
// enable we refused never saved: our V slowed by its own Sandevistan after
// the weapon wheel. A guest's, and the host's alone: unchanged.
// job thread safe: it reads natives (HostKeepsWorld) and logs; it changes no
// script state.
@wrapMethod(TimeDilationHelper)
public final static func SetTimeDilationWithProfile(requester: wref<GameObject>, const profileName: script_ref<String>, enable: Bool, allowMultipleTimeDilationSimultaneously: Bool) -> Bool {
  if TandemTime.HostKeepsWorld() {
    if enable {
      TandemNative.Log(s"time: the \(Deref(profileName)) menu would slow the shared world: refused, the host's menus slow nothing");
    }
    return false;
  }
  return wrappedMethod(requester, profileName, enable, allowMultipleTimeDilationSimultaneously);
}

// A shard's popup pauses the game (the system requests handler's
// PauseGame). On the host while another player is in the session it pauses
// nothing: the popup stays up over a running world (only the pause menu
// stops everyone: Pause.reds, which holds its pause again at once).
// job thread safe: it changes no script state; the handler's unpause is the
// one this controller's own close calls.
@wrapMethod(ShardNotificationController)
private final func PauseGameState(enable: Bool) {
  wrappedMethod(enable);
  let paused = TandemNative.PausedPlayers();
  if enable && TandemTime.HostKeepsWorld() && ArraySize(paused) == 0 {
    this.GetSystemRequestsHandler().UnpauseGame();
    TandemNative.Log("time: a shard's popup would pause the shared world: unpaused, the host's menus pause nothing");
  }
}
