// A guest's saves (docs/architecture/progression.md, "Saves").
//
// In a session a guest's world is the host's on top of its own: it never
// saves. What its V earns there (XP, street cred, money, items, the reward
// ledger) lives in the running game and reaches its own save with the first
// save made after the session. This file is the way from one to the other:
//
//   1. the session is over (we left, or the host is gone);
//   2. every part of tandem puts our own world back (journal, story, clock,
//      weather, difficulty, crowd; puppets and proxies deleted). Until
//      TandemSystem.LeftOver says nothing is left, saving stays locked: a
//      save must never hold co-op state. If something stays for 20 s the
//      lock stays for good and the player is told to load their last save;
//   3. saving unlocks. tandem.ini `leave` decides what follows:
//        stay    (default) nothing: we go on in place;
//        save    one quick save, as F5 makes one, once the game's own save
//                locks allow it (not in combat, not in a scene);
//        reload  that save, then the load of it, so the world is rebuilt
//                from our own save. Only when we chose to leave, and only
//                with the game's word that the save was written.
//
// Two more ways out want the save whatever `leave` says, and never a load:
//   - the pause menu's Exit (to the main menu or the desktop), held by its
//     wrap (Coop.reds) and taken here on the tick: the leave steps, the
//     save, then the exit. Out of a session that still owes its save
//     (`stay` wrote nothing), the save alone, then the exit;
//   - the checkpoint (tandem.ini `checkpoint`, minutes; off by default):
//     at a safe moment, the leave steps, the save, then the same session
//     joined again.
//
// The steps are core/progress.hpp's (tested there); this file feeds them
// and does what they ask. Nothing here writes a save in a scenario: `leave`
// stays `stay`, and the exits and checkpoints scenarios ask for are dry (the
// save counted as written, the exit only logged).

public class TandemSaves {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  // Our save lock, as the steps last had it (IsSavingLocked reads it).
  private let m_locked: Bool;
  // The session ended by the co-op menu's Leave.
  private let m_chosen: Bool;
  // The game's word about the save we asked for: 0 none yet, 1 written, -1 failed.
  private let m_saved: Int32;
  private let m_asked: Bool;
  private let m_listening: Bool;
  private let m_lastTick: Float;
  // What the session left, as last seen and said (said at least once per
  // way out).
  private let m_left: Int32;
  private let m_saidLeft: Bool;
  // The pause menu's Exit waiting for our save: 0 none, 1 to the main menu,
  // 2 to the desktop.
  private let m_exit: Int32;
  // A checkpoint's leave is under way: the session is joined again after
  // its save.
  private let m_checkpoint: Bool;
  // When this session began, or its last checkpoint ended.
  private let m_checkpointAt: Float;
  // Why no checkpoint can be made in this session, said once.
  private let m_checkpointSaid: Bool;
  // A test's exit or checkpoint (tools/game.sh cmd exit … dry): no save
  // written (counted as written), no exit made.
  private let m_dry: Bool;
  // Back from a session with `leave = stay`: the offer to save now (a
  // notice, and the game's own quick save key as a hint), and when it lapses.
  private let m_offering: Bool;
  private let m_offerUntil: Float;

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    // A world was loaded: what a session left in the last one is gone.
    TandemProgress.LeaveReset();
    this.m_locked = false;
    this.m_exit = 0;
    this.m_checkpoint = false;
    this.m_dry = false;
    this.m_offering = false;
    // Every save of this world, ours or the game's: its playthrough time
    // tells campaigns.txt which save it was (CampaignNatives.reds).
    if !this.m_listening {
      GameInstance.GetSystemRequestsHandler().RegisterToCallback(n"OnSavingComplete", this, n"OnSavingComplete");
      this.m_listening = true;
    }
  }

  public func Locked() -> Bool {
    return this.m_locked;
  }

  // The co-op menu's Leave (TandemSystem.CoopLeave), before the session ends.
  public func Chosen() -> Void {
    this.m_chosen = true;
  }

  // Our V's session gains are not on disk: a guest in a session, the leave
  // steps under way, or a session that ended and wrote nothing since. The
  // pause menu's Exit saves first then (Coop.reds).
  public func ExitOwes() -> Bool {
    if !IsDefined(this.m_system) {
      return false;
    }
    let step = TandemProgress.LeaveStep();
    let guest = this.m_system.You() != 0u && !TandemNative.IsHost();
    return guest || step == 2 || step == 3 || step == 4 || step == 6 || TandemProgress.LeaveOwed();
  }

  // The pause menu's Exit (taken on the tick) or `cmd exit`: the leave
  // steps and the save first, then the exit (1 the main menu, 2 the
  // desktop). The pause menu asked the player already when no save could
  // be made (TandemSystem.SaveBlocker).
  public func BeginExit(exit: Int32, dry: Bool) -> Void {
    if !IsDefined(this.m_system) || exit == 0 {
      return;
    }
    let name = TandemSaves.ExitName(exit);
    if !this.ExitOwes() {
      TandemNative.Log(s"exit: \(name): nothing of a session to save");
      this.m_dry = dry;
      this.Exit(exit);
      this.m_dry = false;
      return;
    }
    if TandemProgress.LeaveStep() == 6 {
      // Co-op state stayed: no save can be made until a load.
      TandemNative.Log(s"exit: \(name): no save can be made (co-op state stayed): not exiting");
      TandemCoop.Notice(this.m_game, "Exit-NotSaved");
      return;
    }
    this.m_exit = exit;
    this.m_dry = dry;
    this.m_checkpoint = false;
    TandemNative.Log(s"exit: \(name) held: our V is saved first\(dry ? " (dry)" : "")");
    if this.m_system.You() != 0u && !TandemNative.IsHost() {
      this.m_system.CoopLeaveSaying("Exit-Saving");
    } else {
      TandemCoop.Notice(this.m_game, "Exit-Saving");
    }
  }

  // A checkpoint now (the timer, or `cmd checkpoint`). False: none (not a
  // guest in a session, or not a session we can join again).
  public func BeginCheckpoint(dry: Bool) -> Bool {
    if !IsDefined(this.m_system) || this.m_exit != 0 || this.m_checkpoint {
      return false;
    }
    if this.m_system.You() == 0u || TandemNative.IsHost() {
      TandemNative.Log("checkpoint: not a guest in a session");
      return false;
    }
    if !TandemProgress.KeepRejoin() {
      if !this.m_checkpointSaid {
        this.m_checkpointSaid = true;
        TandemNative.Log("checkpoint: this session was not joined by a choice we can make again: none");
      }
      return false;
    }
    this.m_checkpoint = true;
    this.m_dry = dry;
    TandemNative.Log(s"checkpoint: leaving to save our V, then joining again\(dry ? " (dry)" : "")");
    this.m_system.CoopLeaveSaying("Checkpoint-Saving");
    return true;
  }

  // Four times a second, in a session or not (TandemSystem.OnTick).
  public func Tick() -> Void {
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    if now - this.m_lastTick < 0.25 || !IsDefined(this.m_system) {
      return;
    }
    this.m_lastTick = now;
    // The pause menu's Exit, held for the tick (Coop.reds).
    let asked = TandemProgress.TakeExit();
    if asked != 0 {
      this.BeginExit(asked, false);
    }
    this.CheckpointTick(now);
    let guest = this.m_system.You() != 0u && !TandemNative.IsHost();
    if this.m_offering && (guest || now > this.m_offerUntil) {
      this.EndOffer();
    }
    let step = TandemProgress.LeaveStep();
    if guest {
      this.m_saidLeft = false;
      if step != 1 {
        // A session (again): its end has not been chosen yet, its first
        // checkpoint is a whole period away, and what was under way of an
        // exit or a checkpoint is dropped.
        this.m_chosen = false;
        this.m_checkpointAt = now;
        this.m_checkpointSaid = false;
        if this.m_exit != 0 || this.m_checkpoint {
          TandemNative.Log("save: in a session again: the exit or checkpoint under way is dropped");
          this.m_exit = 0;
          this.m_checkpoint = false;
          this.m_dry = false;
        }
      }
    } else if (step == 0 || step == 5 || step == 6) && this.m_exit == 0 {
      // Nothing under way.
      return;
    }
    let left = 0;
    let blocked = false;
    if !guest {
      left = this.m_system.LeftOver();
      if left != this.m_left || !this.m_saidLeft {
        this.m_saidLeft = true;
        TandemNative.Log(s"save: left of the session: \(left == 0 ? "nothing" : TandemProgress.LeftNames(left))");
      }
      if step == 3 {
        // The game's own locks (combat, a scene, a menu): ours is off by now.
        let locks: array<gameSaveLock>;
        blocked = GameInstance.IsSavingLocked(this.m_game, locks);
      }
    }
    this.m_left = left;
    let wanted = this.m_exit != 0 || this.m_checkpoint;
    let action = TandemProgress.LeaveTick(now, guest, this.m_chosen, left, blocked, this.m_asked ? this.m_saved : 0, wanted);
    this.m_locked = TandemProgress.LeaveLocked();
    switch action {
      case 1:
        TandemNative.Log(s"save: back from co-op: our own world is back, saving unlocked (leave = \(TandemSaves.ModeName())\(wanted ? ", a save wanted" : ""))");
        if !wanted && TandemProgress.LeaveMode() == 0 {
          this.Offer(now);
        }
        break;
      case 2:
        this.Save();
        break;
      case 3:
        this.m_asked = false;
        TandemNative.Log("save: back from co-op: saved");
        this.AfterSave(true);
        break;
      case 4:
        this.m_asked = false;
        TandemNative.Log("save: back from co-op: saved; loading that save");
        GameInstance.GetSystemRequestsHandler().LoadLastCheckpoint(true);
        break;
      case 5:
        this.m_asked = false;
        TandemNative.Log(s"save: back from co-op: the save \(this.m_saved < 0 ? "failed" : "was not confirmed"): nothing is loaded");
        if !wanted {
          TandemCoop.Notice(this.m_game, "Saves-Failed");
        }
        this.AfterSave(false);
        break;
      case 6:
        TandemNative.Log(s"save: back from co-op: the game allowed no save for \(wanted ? "15 seconds" : "two minutes"): left to the player");
        if !wanted {
          TandemCoop.Notice(this.m_game, "Saves-WhenYouCan");
        }
        this.AfterSave(false);
        break;
      case 7:
        TandemNative.Log(s"save: stays locked: \(TandemProgress.LeftNames(left)) left of the session: load your last save");
        // The story says its own (Story.reds GuestLeave).
        if left != 1 {
          TandemCoop.Notice(this.m_game, "Saves-NotRestored");
        }
        this.AfterSave(false);
        break;
      default:
        break;
    }
  }

  // Every `checkpoint` minutes in a session, at a safe moment
  // (TandemSystem.CheckpointBlocker).
  private func CheckpointTick(now: Float) -> Void {
    let minutes = TandemProgress.CheckpointMinutes();
    if minutes <= 0 || this.m_exit != 0 || this.m_checkpoint || TandemProgress.LeaveStep() != 1 {
      return;
    }
    if this.m_system.You() == 0u || TandemNative.IsHost() {
      return;
    }
    if now - this.m_checkpointAt < Cast<Float>(minutes) * 60.0 {
      return;
    }
    if StrLen(this.m_system.CheckpointBlocker()) > 0 {
      return;
    }
    if !this.BeginCheckpoint(false) {
      // Not this session: the next period may be another one.
      this.m_checkpointAt = now;
    }
  }

  // The save an exit or a checkpoint waited for is written (or not).
  private func AfterSave(saved: Bool) -> Void {
    if this.m_exit != 0 {
      let exit = this.m_exit;
      this.m_exit = 0;
      if saved {
        this.Exit(exit);
      } else {
        TandemNative.Log(s"exit: \(TandemSaves.ExitName(exit)): our V could not be saved: not exiting (exit again to quit without it)");
        TandemCoop.Notice(this.m_game, "Exit-NotSaved");
      }
    }
    if this.m_checkpoint {
      this.m_checkpoint = false;
      this.m_checkpointAt = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
      TandemNative.Log(s"checkpoint: \(saved ? "saved" : "not saved"): joining the session again");
      if saved {
        TandemCoop.Notice(this.m_game, "Checkpoint-Saved");
      } else {
        TandemCoop.Notice(this.m_game, "Checkpoint-Failed");
      }
      if !this.m_system.CoopRejoin() {
        TandemNative.Log("checkpoint: the session could not be chosen again");
      }
    }
    this.m_dry = false;
  }

  // The exit the player asked for, now (1 the main menu, 2 the desktop):
  // the game's own, through the system requests handler (the pause menu
  // that asked is closed).
  private func Exit(exit: Int32) -> Void {
    if this.m_dry {
      TandemNative.Log(s"exit: dry: would go to \(TandemSaves.ExitName(exit)) now");
      return;
    }
    TandemNative.Log(s"exit: to \(TandemSaves.ExitName(exit))");
    this.m_system.EndForExit();
    let handler = GameInstance.GetSystemRequestsHandler();
    if exit == 2 {
      handler.ExitGame();
    } else {
      // A co-op choice made in game was for this game (as the pause menu's
      // own Exit to the main menu does, Coop.reds).
      TandemNative.ResetChoice();
      handler.GotoMainMenu();
    }
  }

  public static func ExitName(exit: Int32) -> String {
    return exit == 2 ? "the desktop" : "the main menu";
  }

  // One quick save, the game's own (the pause menu's: final.reds:340447),
  // with its "game saved" notification; its outcome comes to OnSavingComplete.
  private func Save() -> Void {
    this.m_saved = 0;
    this.m_asked = true;
    if this.m_dry {
      // A test: nothing of it reaches the player's saves.
      TandemNative.Log("save: back from co-op: dry: no save written, counted as written");
      this.m_saved = 1;
      return;
    }
    let handler = GameInstance.GetSystemRequestsHandler();
    if !this.m_listening {
      handler.RegisterToCallback(n"OnSavingComplete", this, n"OnSavingComplete");
      this.m_listening = true;
    }
    TandemNative.Log("save: back from co-op: quick save asked");
    handler.QuickSave();
  }

  protected cb func OnSavingComplete(success: Bool, locks: array<gameSaveLock>) -> Bool {
    TandemNative.HookRan(n"Saves.TandemSaves.OnSavingComplete");
    if this.m_asked {
      this.m_saved = success ? 1 : -1;
      TandemNative.Log(s"save: the game says the save \(success ? "was written" : "failed")");
    }
    if success {
      TandemCampaignNative.WorldSaved(EngineTime.ToFloat(GameInstance.GetPlaythroughTime(this.m_game)));
    }
  }

  // The choice on leaving (progression.md, Leaving), worded plainly: our V
  // kept what it earned; the game's own quick save keeps it for good, now or
  // whenever the player saves (the pause menu's Exit saves it anyway).
  private func Offer(now: Float) -> Void {
    let recent = TandemNative.RecentHosts();
    let host = ArraySize(recent) > 0 ? recent[0].name : "";
    if StrLen(host) > 0 {
      TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 12.0, "Saves-Offer", host);
    } else {
      TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 12.0, "Saves-OfferAlone");
    }
    TandemText.Hint(this.m_game, n"TandemSaveOffer", n"QuickSave", true, TandemText.Get("Saves-OfferHint"), inkInputHintHoldIndicationType.Press);
    this.m_offering = true;
    this.m_offerUntil = now + 30.0;
  }

  private func EndOffer() -> Void {
    TandemText.Hint(this.m_game, n"TandemSaveOffer", n"QuickSave", false, "", inkInputHintHoldIndicationType.Press);
    this.m_offering = false;
  }

  // The world goes away (TandemSystem.OnPlayerDetach).
  public func Stop() -> Void {
    if this.m_offering {
      this.EndOffer();
    }
    if this.m_listening {
      GameInstance.GetSystemRequestsHandler().UnregisterFromCallback(n"OnSavingComplete", this, n"OnSavingComplete");
      this.m_listening = false;
    }
    this.m_asked = false;
    this.m_exit = 0;
    this.m_checkpoint = false;
  }

  public static func ModeName() -> String {
    let mode = TandemProgress.LeaveMode();
    return mode == 2 ? "reload" : (mode == 1 ? "save" : "stay");
  }

  public func Report() -> String {
    let step = TandemProgress.LeaveStep();
    let names: array<String> = ["solo", "session", "cleaning", "waiting", "saving", "loading", "dirty"];
    let left = IsDefined(this.m_system) ? this.m_system.LeftOver() : 0;
    let locks: array<gameSaveLock>;
    let blocked = GameInstance.IsSavingLocked(this.m_game, locks);
    let exit = this.m_exit == 0 ? "none" : TandemSaves.ExitName(this.m_exit);
    return s"saves: leave = \(TandemSaves.ModeName()), step \(names[step]), our lock \(this.m_locked), the game's locks \(blocked) (\(ArraySize(locks))), left of a session: \(left == 0 ? "nothing" : TandemProgress.LeftNames(left)), owed \(TandemProgress.LeaveOwed()), exit \(exit), checkpoint every \(TandemProgress.CheckpointMinutes()) min\(this.m_checkpoint ? " (under way)" : "")";
  }
}
