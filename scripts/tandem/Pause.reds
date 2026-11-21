// The pause menu pauses everyone; nothing else does (docs/architecture/time.md,
// open problems). Our pause menu opening and closing goes to the others;
// while any other player is in theirs, our world is paused the way the pause
// menu pauses it (PauseMenuBackgroundGameController: the system requests
// handler's PauseGame / UnpauseGame), without opening our menu.
//
// Our own delayed tick (TandemSystem.Tick) stands still while the world is
// paused, so the plugin calls TandemPause.Frame every frame instead
// (plugin/src/main.cpp, the running game state).

public class TandemPause {
  private let m_game: GameInstance;
  // The world has loaded (TandemSystem: "playing"): a pause before that
  // would hold up the load.
  private let m_ready: Bool;
  // Our pause menu is open.
  private let m_menuOpen: Bool;
  // We asked for a pause for the others, not given back yet.
  private let m_holding: Bool;
  // The plugin's per-frame call has reached us (logged once).
  private let m_framesSeen: Bool;
  // The other players in their pause menu, as of PausedChanges() m_seen.
  private let m_paused: array<Uint32>;
  private let m_seen: Int32;
  private let m_handler: wref<inkISystemRequestsHandler>;
  // The physics watch (research/engine/time.md §13.100): while the world is
  // paused, writes to physics bodies queue up undrained, and past the queue's
  // room the engine overwrites its own heap. The backlog is logged as it
  // grows; near the limit the pause is lifted for a frame or a few so the
  // physics update drains it. Frames paused, the backlog's peak, the last
  // 4096-entry step logged, frames left lifted (0: not lifting), lifts.
  private let m_pausedFrames: Int32;
  private let m_backlogPeak: Int32;
  private let m_backlogLogged: Int32;
  private let m_lifting: Int32;
  private let m_lifts: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func SetReady() -> Void {
    this.m_ready = true;
  }

  // The world goes away (another save, the main menu): give our pause back.
  public func Stop() -> Void {
    this.Release();
    this.m_ready = false;
    ArrayClear(this.m_paused);
    this.m_lifting = 0;
    this.m_pausedFrames = 0;
    this.m_backlogPeak = 0;
    this.m_backlogLogged = 0;
    this.m_lifts = 0;
  }

  public static func Of(game: GameInstance) -> ref<TandemPause> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(system) {
      return null;
    }
    return system.PauseSync();
  }

  // Every frame, from the plugin, paused or not.
  // No return type: RedLib's CallStatic counts one as an extra argument and
  // refuses the call when the plugin passes only the game.
  public static func Frame(game: GameInstance) {
    // The plugin has no game instance to hand: an empty one comes in, which
    // finds no system. The game's own global is the right one.
    let pause = TandemPause.Of(GetGameInstance());
    if IsDefined(pause) {
      if !pause.m_framesSeen {
        pause.m_framesSeen = true;
        TandemNative.Log("pause: following the other players' pause menus every frame");
      }
      pause.Update();
    }
  }

  // The pause menu's background (it pauses the game) came up or went away.
  // The death menu uses the same background: a V's death pauses nobody else.
  public static func MenuOpened(owner: ref<GameObject>) -> Void {
    if !IsDefined(owner) || owner.IsDead() {
      return;
    }
    let pause = TandemPause.Of(owner.GetGame());
    if IsDefined(pause) {
      pause.OpenMenu();
    }
  }

  public static func MenuClosed(owner: ref<GameObject>) -> Void {
    if !IsDefined(owner) {
      return;
    }
    let pause = TandemPause.Of(owner.GetGame());
    if IsDefined(pause) {
      pause.CloseMenu();
    }
  }

  public func OpenMenu() -> Void {
    if this.m_menuOpen || !TandemNative.IsStarted() {
      return;
    }
    this.m_menuOpen = true;
    TandemNative.SendPaused(true);
    TandemNative.Log("pause: our pause menu opened, pausing everyone");
  }

  public func CloseMenu() -> Void {
    if !this.m_menuOpen {
      return;
    }
    this.m_menuOpen = false;
    TandemNative.SendPaused(false);
    TandemNative.Log("pause: our pause menu closed");
    // At once: if someone else is still paused, the world must not run for
    // a frame (the menu's close has just unpaused it).
    this.Update();
  }

  private func Handler() -> wref<inkISystemRequestsHandler> {
    if !IsDefined(this.m_handler) {
      this.m_handler = GameInstance.GetSystemRequestsHandler();
    }
    return this.m_handler;
  }

  public func Update() -> Void {
    let changes = TandemNative.PausedChanges();
    if changes != this.m_seen {
      this.m_seen = changes;
      this.Report(TandemNative.PausedPlayers());
    }
    if !this.m_ready {
      return;
    }
    let handler = this.Handler();
    if !IsDefined(handler) {
      return;
    }
    if this.m_lifting > 0 {
      this.Lifting(handler);
      return;
    }
    let wanted = ArraySize(this.m_paused) > 0;
    if wanted && !this.m_holding {
      handler.PauseGame();
      this.m_holding = true;
      TandemNative.Log(s"pause: world paused (\(ArraySize(this.m_paused)) player(s) in the pause menu)");
    } else if wanted && !handler.IsGamePaused() {
      // Our own pause menu closed and unpaused it: hold it again.
      handler.PauseGame();
    } else if !wanted && this.m_holding && !this.m_menuOpen {
      // With our menu open, its close gives the world back instead.
      this.Release();
      TandemNative.Log("pause: world resumed");
    }
    if handler.IsGamePaused() && (this.m_holding || this.m_menuOpen) {
      this.Watch(handler);
    } else if this.m_pausedFrames > 0 {
      this.WatchOver();
    }
  }

  // Every frame of a co-op pause (ours to hold, or our menu's in a session).
  private func Watch(handler: wref<inkISystemRequestsHandler>) -> Void {
    this.m_pausedFrames += 1;
    let backlog = TandemNative.PhysicsBacklog();
    if backlog < 0 {
      return; // the watch is off (the plugin logged why)
    }
    if backlog > this.m_backlogPeak {
      this.m_backlogPeak = backlog;
    }
    let limit = TandemNative.PhysicsBacklogLimit();
    let step = backlog / 4096;
    if step > this.m_backlogLogged {
      this.m_backlogLogged = step;
      TandemNative.Log(s"pause: physics backlog \(backlog) of \(limit) entries after \(this.m_pausedFrames) paused frames");
    }
    if backlog >= limit / 4 * 3 {
      handler.UnpauseGame();
      this.m_lifting = 10;
      this.m_lifts += 1;
      TandemNative.Log(s"pause: physics backlog \(backlog) of \(limit): the pause lifted for the physics to drain");
    }
  }

  // The world runs for the physics to drain its queue; paused again once it
  // has (or after 10 frames), if still wanted.
  private func Lifting(handler: wref<inkISystemRequestsHandler>) -> Void {
    this.m_lifting -= 1;
    let backlog = TandemNative.PhysicsBacklog();
    if backlog >= 1024 && this.m_lifting > 0 {
      return;
    }
    this.m_lifting = 0;
    let again = (this.m_holding && ArraySize(this.m_paused) > 0) || this.m_menuOpen;
    if again && !handler.IsGamePaused() {
      handler.PauseGame();
    }
    this.m_backlogLogged = backlog / 4096;
    let what = again ? "paused again" : "not paused any more";
    TandemNative.Log(s"pause: physics backlog \(backlog) after the lift, \(what)");
  }

  // A co-op pause is over: what the physics queue did during it.
  private func WatchOver() -> Void {
    TandemNative.Log(s"pause: over after \(this.m_pausedFrames) frames, physics backlog peaked at \(this.m_backlogPeak), \(this.m_lifts) lift(s)");
    this.m_pausedFrames = 0;
    this.m_backlogPeak = 0;
    this.m_backlogLogged = 0;
    this.m_lifts = 0;
  }

  private func Release() -> Void {
    if !this.m_holding {
      return;
    }
    this.m_holding = false;
    let handler = this.Handler();
    // Only if still paused: our menu's close may have unpaused it already.
    if IsDefined(handler) && handler.IsGamePaused() {
      handler.UnpauseGame();
    }
  }

  // Who paused or resumed since the last look, in the log; a notice on
  // screen for who paused.
  private func Report(now: array<Uint32>) -> Void {
    for player in now {
      if !ArrayContains(this.m_paused, player) {
        let name = this.NameOf(player);
        TandemNative.Log(s"pause: \(name) paused the game");
        // TODO: a notice that shows while paused if this one waits for the
        // world (to check in game).
        TandemText.Notice(this.m_game, "Pause-By", name);
      }
    }
    for player in this.m_paused {
      if !ArrayContains(now, player) {
        TandemNative.Log(s"pause: \(this.NameOf(player)) resumed");
      }
    }
    this.m_paused = now;
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

}

// Where vanilla pauses for the pause menu (final.reds,
// PauseMenuBackgroundGameController): our menu's state goes to the others.
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnInitialize() -> Bool {
  TandemNative.HookRan(n"Pause.PauseMenuBackgroundGameController.OnInitialize");
  let result = wrappedMethod();
  TandemPause.MenuOpened(this.GetPlayerControlledObject());
  return result;
}

@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnUninitialize() -> Bool {
  TandemNative.HookRan(n"Pause.PauseMenuBackgroundGameController.OnUninitialize");
  let result = wrappedMethod();
  TandemPause.MenuClosed(this.GetPlayerControlledObject());
  return result;
}
