// A black screen of our own over a switch the player must not see happen
// (docs/architecture/scripted-moments.md, "Seamless"): into and out of
// watching the host's eyes, our V carried to or put back beside the host,
// a quest taking the host away from beside us (Travel.reds). A plain black
// rectangle on the HUD layer, built in script from the game's own widgets
// (the party HUD's way), its opacity stepped each frame while it moves, in
// real time (no slow motion stretches it). Never black for good: closed
// MaxClosed seconds, it lifts whatever its owner does (a travel left the
// screen black once, 2026-11-19, Travel.reds).
// Owned by TandemStorySystem (Curtain()); every switch asks it.

public class TandemCurtainFrame extends DelayCallback {
  public let curtain: wref<TandemCurtain>;

  public func Call() -> Void {
    if IsDefined(this.curtain) {
      this.curtain.Frame();
    }
  }
}

public class TandemCurtain {
  private let m_game: GameInstance;
  private let m_widget: ref<inkRectangle>;
  private let m_opacity: Float;
  private let m_target: Float;
  // Opacity per second, toward the target.
  private let m_speed: Float;
  // When the opening asked for starts (real time; 0: none waits), and how
  // fast it goes.
  private let m_openAt: Float;
  private let m_openSpeed: Float;
  private let m_closedSince: Float;
  private let m_last: Float;
  private let m_running: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public static func MaxClosed() -> Float {
    return 2.5;
  }

  // Darken over `seconds`. Without a HUD to draw on, nothing shows and
  // IsClosed answers true at once: the switch happens as before.
  public func Close(seconds: Float) -> Void {
    if !this.Build() {
      return;
    }
    this.m_target = 1.0;
    this.m_openAt = 0.0;
    this.m_speed = 1.0 / MaxF(seconds, 0.05);
    this.Run();
  }

  // Lift over `seconds`, once `hold` more seconds of black have passed.
  public func Open(seconds: Float, hold: Float) -> Void {
    if !IsDefined(this.m_widget) || (this.m_opacity <= 0.0 && this.m_target <= 0.0) {
      return;
    }
    this.m_openSpeed = 1.0 / MaxF(seconds, 0.05);
    this.m_openAt = MaxF(TandemClock.Real(this.m_game) + hold, 0.001);
    this.Run();
  }

  // Fully black now (or nothing to draw on): the switch may happen.
  public func IsClosed() -> Bool {
    return !IsDefined(this.m_widget) || this.m_opacity >= 0.99;
  }

  // Gone at once (the session's end, a switch called off).
  public func Clear() -> Void {
    this.m_target = 0.0;
    this.m_opacity = 0.0;
    this.m_openAt = 0.0;
    this.m_closedSince = 0.0;
    if IsDefined(this.m_widget) {
      this.m_widget.SetOpacity(0.0);
      this.m_widget.SetVisible(false);
    }
  }

  private func Build() -> Bool {
    if IsDefined(this.m_widget) {
      return true;
    }
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    let window = IsDefined(layer) ? layer.GetVirtualWindow() : null;
    if !IsDefined(window) {
      return false;
    }
    let black = new inkRectangle();
    black.SetName(n"tandem_curtain");
    black.SetAnchor(inkEAnchor.Fill);
    black.SetSize(new Vector2(3840.0, 2160.0));
    black.SetTintColor(new HDRColor(0.0, 0.0, 0.0, 1.0));
    black.SetOpacity(0.0);
    black.SetVisible(false);
    black.SetInteractive(false);
    // Last on the HUD's window: over every widget on it.
    black.Reparent(window);
    this.m_widget = black;
    TandemNative.Log("curtain: built on the HUD layer");
    return true;
  }

  private func Run() -> Void {
    if !this.m_running {
      this.m_running = true;
      this.m_last = TandemClock.Real(this.m_game);
      this.Schedule();
    }
  }

  private func Schedule() -> Void {
    let frame = new TandemCurtainFrame();
    frame.curtain = this;
    // 0 s: the next frame; real time (not slowed by time dilation).
    GameInstance.GetDelaySystem(this.m_game).DelayCallback(frame, 0.0, false);
  }

  public func Frame() -> Void {
    let now = TandemClock.Real(this.m_game);
    let step = ClampF(now - this.m_last, 0.0, 0.1);
    this.m_last = now;
    if this.m_openAt > 0.0 && now >= this.m_openAt {
      this.m_openAt = 0.0;
      this.m_target = 0.0;
      this.m_speed = this.m_openSpeed;
    }
    if this.m_opacity >= 0.99 && this.m_target >= 1.0 {
      if this.m_closedSince <= 0.0 {
        this.m_closedSince = MaxF(now, 0.001);
      } else if now - this.m_closedSince > TandemCurtain.MaxClosed() {
        TandemNative.Log("curtain: black too long, lifted");
        this.m_target = 0.0;
        this.m_speed = 3.0;
      }
    } else {
      this.m_closedSince = 0.0;
    }
    if this.m_opacity < this.m_target {
      this.m_opacity = MinF(this.m_target, this.m_opacity + this.m_speed * step);
    } else if this.m_opacity > this.m_target {
      this.m_opacity = MaxF(this.m_target, this.m_opacity - this.m_speed * step);
    }
    if IsDefined(this.m_widget) {
      this.m_widget.SetOpacity(this.m_opacity);
      this.m_widget.SetVisible(this.m_opacity > 0.0);
    }
    // Clear and nothing asked: stop. Black: go on, to lift it in time.
    if this.m_opacity <= 0.0 && this.m_target <= 0.0 && this.m_openAt <= 0.0 {
      this.m_running = false;
      return;
    }
    this.Schedule();
  }
}
