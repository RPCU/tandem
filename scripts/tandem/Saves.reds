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
// The steps are core/progress.hpp's (tested there); this file feeds them
// and does what they ask. Nothing here runs in a scenario with its default
// (stay): tests never write the player's saves.

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

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    // A world was loaded: what a session left in the last one is gone.
    TandemProgress.LeaveReset();
    this.m_locked = false;
  }

  public func Locked() -> Bool {
    return this.m_locked;
  }

  // The co-op menu's Leave (TandemSystem.CoopLeave), before the session ends.
  public func Chosen() -> Void {
    this.m_chosen = true;
  }

  // Four times a second, in a session or not (TandemSystem.OnTick).
  public func Tick() -> Void {
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
    if now - this.m_lastTick < 0.25 || !IsDefined(this.m_system) {
      return;
    }
    this.m_lastTick = now;
    let guest = this.m_system.You() != 0u && !TandemNative.IsHost();
    let step = TandemProgress.LeaveStep();
    if guest {
      this.m_saidLeft = false;
      if step != 1 {
        // A session (again): its end has not been chosen yet.
        this.m_chosen = false;
      }
    } else if step == 0 || step == 5 || step == 6 {
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
    let action = TandemProgress.LeaveTick(now, guest, this.m_chosen, left, blocked, this.m_asked ? this.m_saved : 0);
    this.m_locked = TandemProgress.LeaveLocked();
    switch action {
      case 1:
        TandemNative.Log(s"save: back from co-op: our own world is back, saving unlocked (leave = \(TandemSaves.ModeName()))");
        break;
      case 2:
        this.Save();
        break;
      case 3:
        this.m_asked = false;
        TandemNative.Log("save: back from co-op: saved");
        break;
      case 4:
        this.m_asked = false;
        TandemNative.Log("save: back from co-op: saved; loading that save");
        GameInstance.GetSystemRequestsHandler().LoadLastCheckpoint(true);
        break;
      case 5:
        this.m_asked = false;
        TandemNative.Log(s"save: back from co-op: the save \(this.m_saved < 0 ? "failed" : "was not confirmed"): nothing is loaded");
        TandemCoop.Notice(this.m_game, "Saves-Failed");
        break;
      case 6:
        TandemNative.Log("save: back from co-op: the game allowed no save for two minutes: left to the player");
        TandemCoop.Notice(this.m_game, "Saves-WhenYouCan");
        break;
      case 7:
        TandemNative.Log(s"save: stays locked: \(TandemProgress.LeftNames(left)) left of the session: load your last save");
        // The story says its own (Story.reds GuestLeave).
        if left != 1 {
          TandemCoop.Notice(this.m_game, "Saves-NotRestored");
        }
        break;
      default:
        break;
    }
  }

  // One quick save, the game's own (the pause menu's: final.reds:340447),
  // with its "game saved" notification; its outcome comes to OnSavingComplete.
  private func Save() -> Void {
    let handler = GameInstance.GetSystemRequestsHandler();
    if !this.m_listening {
      handler.RegisterToCallback(n"OnSavingComplete", this, n"OnSavingComplete");
      this.m_listening = true;
    }
    this.m_saved = 0;
    this.m_asked = true;
    TandemNative.Log("save: back from co-op: quick save asked");
    handler.QuickSave();
  }

  protected cb func OnSavingComplete(success: Bool, locks: array<gameSaveLock>) -> Bool {
    TandemNative.HookRan(n"Saves.TandemSaves.OnSavingComplete");
    if this.m_asked {
      this.m_saved = success ? 1 : -1;
      TandemNative.Log(s"save: the game says the save \(success ? "was written" : "failed")");
    }
  }

  // The world goes away (TandemSystem.OnPlayerDetach).
  public func Stop() -> Void {
    if this.m_listening {
      GameInstance.GetSystemRequestsHandler().UnregisterFromCallback(n"OnSavingComplete", this, n"OnSavingComplete");
      this.m_listening = false;
    }
    this.m_asked = false;
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
    return s"saves: leave = \(TandemSaves.ModeName()), step \(names[step]), our lock \(this.m_locked), the game's locks \(blocked) (\(ArraySize(locks))), left of a session: \(left == 0 ? "nothing" : TandemProgress.LeftNames(left))";
  }
}
