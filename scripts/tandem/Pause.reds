// The pause menu pauses everyone; nothing else does (docs/ARCHITECTURE.md,
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
        // TODO: a localized string, and a notice that shows while paused if
        // this one waits for the world (to check in game).
        this.Notice(s"\(name) paused the game");
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

  // The game's own simple on-screen message (as vanilla's SetWarningMessage).
  private func Notice(text: String) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 5.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(this.m_game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }
}

// Where vanilla pauses for the pause menu (final.reds,
// PauseMenuBackgroundGameController): our menu's state goes to the others.
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  TandemPause.MenuOpened(this.GetPlayerControlledObject());
  return result;
}

@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnUninitialize() -> Bool {
  let result = wrappedMethod();
  TandemPause.MenuClosed(this.GetPlayerControlledObject());
  return result;
}
