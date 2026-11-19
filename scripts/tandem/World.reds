// The shared world (docs/architecture/world.md, milestone 4): the host's game time
// and weather, followed by every guest. Weather by name through Codeware's
// WeatherSystem (engine-knowledge.md / coop-quests-world.md §8).

public class TandemWorld {
  private let m_game: GameInstance;
  private let m_lastCheck: Float;
  private let m_lastSent: Float;
  private let m_sentTime: Int32;
  private let m_sentWeather: CName;
  private let m_sentAt: Float;
  private let m_applied: Int32;
  // Guest: how far the host's clock moved ours in all (our own would say
  // the game time minus this), and whether the host's weather is on (the
  // game's own weather cycle is off while it is).
  private let m_timeOffset: Int32;
  private let m_weatherSet: Bool;
  // Guest: when we last set the host's weather, and how often our weather
  // left it on its own since (another mod changing it here: WeatherSwitcher).
  private let m_weatherSetAt: Float;
  private let m_overrides: Int32;
  private let m_overrideSince: Float;
  private let m_overrideSaid: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func Tick(now: Float) -> Void {
    if now - this.m_lastCheck < 2.0 {
      return;
    }
    this.m_lastCheck = now;
    if TandemNative.IsHost() {
      this.Share(now);
    } else {
      this.Follow(now);
    }
  }

  // The recorder, solo (System.reds RecordTick): what a host would share.
  public func RecordTick(now: Float) -> Void {
    if now - this.m_lastCheck < 2.0 {
      return;
    }
    this.m_lastCheck = now;
    this.Share(now);
  }

  public static func GameSeconds(game: GameInstance) -> Int32 {
    return GameTime.GetSeconds(GameInstance.GetTimeSystem(game).GetGameTime());
  }

  public static func Weather(game: GameInstance) -> CName {
    let state = GameInstance.GetWeatherSystem(game).GetWeatherState();
    return IsDefined(state) ? state.name : n"";
  }

  // Host: on a weather change or a time jump at once, else every 10 s.
  private func Share(now: Float) -> Void {
    let time = TandemWorld.GameSeconds(this.m_game);
    let weather = TandemWorld.Weather(this.m_game);
    // Game time runs ~8 times faster than real time: a jump is a clock
    // that moved much more (or less) than that since the last sample.
    let expected = this.m_sentTime + Cast<Int32>((now - this.m_sentAt) * 8.0);
    let jumped = AbsF(Cast<Float>(time - expected)) > 300.0;
    let changed = NotEquals(weather, this.m_sentWeather);
    if this.m_lastSent > 0.0 && !changed && !jumped && now - this.m_lastSent < 10.0 {
      return;
    }
    TandemNative.SendWorld(time, weather, GameInstance.GetTimeSystem(this.m_game).IsPausedState());
    if this.m_lastSent == 0.0 || changed || jumped {
      TandemNative.Log(s"world: shared time \(time) s, weather \(NameToString(weather))");
    }
    this.m_lastSent = now;
    this.m_sentTime = time;
    this.m_sentAt = now;
    this.m_sentWeather = weather;
  }

  // Guest: the host's clock when ours is more than a minute off (a set every
  // time would restart lighting blends), the host's weather blended in. The
  // weather is the host's alone: when ours leaves it with no word from the
  // host (another mod set ours: WeatherSwitcher, a console command), it is
  // set back at once, above the usual priority, and the player is told once.
  private func Follow(now: Float) -> Void {
    let changes = TandemNative.HostWorldChanges();
    if changes == 0 {
      return;
    }
    let fresh = changes != this.m_applied;
    this.m_applied = changes;
    if fresh {
      let host = TandemNative.HostGameTime();
      let ours = TandemWorld.GameSeconds(this.m_game);
      // Our bed moved our clock and the others are asked (Activities.reds):
      // it stays until they answer.
      let activities = TandemActivities.Of(this.m_game);
      let held = IsDefined(activities) && activities.HoldsClock();
      if host >= 0 && Abs(host - ours) > 60 && !held {
        GameInstance.GetTimeSystem(this.m_game).SetGameTimeBySeconds(host);
        this.m_timeOffset += host - ours;
        TandemNative.Log(s"world: time set to the host's (\(host) s, was \(ours) s)");
      }
    }
    let weather = TandemNative.HostWeather();
    if !IsNameValid(weather) || Equals(weather, TandemWorld.Weather(this.m_game)) {
      return;
    }
    // Ours left the host's long after we set it, and the host said nothing.
    let overridden = !fresh && this.m_weatherSet && now - this.m_weatherSetAt > 15.0;
    if !fresh && !overridden {
      return; // still blending into the host's
    }
    let ok = GameInstance.GetWeatherSystem(this.m_game).SetWeather(weather, overridden ? 2.0 : 10.0, 100u);
    this.m_weatherSet = this.m_weatherSet || ok;
    this.m_weatherSetAt = now;
    if overridden {
      this.Overridden(now);
    }
    TandemNative.Log(s"world: weather to the host's \(NameToString(weather)) (\(ok))\(overridden ? ": ours was changed here" : "")");
  }

  // Our weather left the host's three times in a minute: a mod of ours
  // does it. Said once a session.
  private func Overridden(now: Float) -> Void {
    if now - this.m_overrideSince > 60.0 {
      this.m_overrideSince = now;
      this.m_overrides = 0;
    }
    this.m_overrides += 1;
    if this.m_overrides >= 3 && !this.m_overrideSaid {
      this.m_overrideSaid = true;
      TandemText.Notice(this.m_game, "World-WeatherIsHost");
    }
  }

  // Guest: the session is over. Our weather is the game's to cycle again at
  // once (SetWeather turned the cycle off: a save made now would keep it
  // off for good). Our clock goes back to what it would say had the host
  // never set it, only when we left by choice (ours): a host that is lost
  // may be back in seconds, and a clock that jumps twice is worse than one
  // that is ahead meanwhile.
  public func End(ours: Bool) -> Void {
    if this.m_weatherSet {
      let ok = GameInstance.GetWeatherSystem(this.m_game).ResetWeather(false, 10.0);
      this.m_weatherSet = false;
      TandemNative.Log(s"world: our weather is the game's again (\(ok))");
    }
    if ours && Abs(this.m_timeOffset) > 60 {
      let own = TandemWorld.GameSeconds(this.m_game) - this.m_timeOffset;
      if own > 0 {
        GameInstance.GetTimeSystem(this.m_game).SetGameTimeBySeconds(own);
        TandemNative.Log(s"world: our own clock back (\(own) s; the host's was \(this.m_timeOffset) s ahead)");
      }
      this.m_timeOffset = 0;
    }
    this.m_applied = 0;
    this.m_overrides = 0;
    this.m_overrideSaid = false;
  }

  // The host's weather is still on (no save meanwhile: Saves.reds).
  public func Borrowed() -> Bool {
    return this.m_weatherSet;
  }
}
