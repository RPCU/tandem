// The shared world (docs/ARCHITECTURE.md, milestone 4): the host's game time
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
      this.Follow();
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
  // time would restart lighting blends), the host's weather blended in.
  private func Follow() -> Void {
    let changes = TandemNative.HostWorldChanges();
    if changes == this.m_applied || changes == 0 {
      return;
    }
    this.m_applied = changes;
    let host = TandemNative.HostGameTime();
    let ours = TandemWorld.GameSeconds(this.m_game);
    if host >= 0 && Abs(host - ours) > 60 {
      GameInstance.GetTimeSystem(this.m_game).SetGameTimeBySeconds(host);
      TandemNative.Log(s"world: time set to the host's (\(host) s, was \(ours) s)");
    }
    let weather = TandemNative.HostWeather();
    if IsNameValid(weather) && NotEquals(weather, TandemWorld.Weather(this.m_game)) {
      let ok = GameInstance.GetWeatherSystem(this.m_game).SetWeather(weather, 10.0);
      TandemNative.Log(s"world: weather to the host's \(NameToString(weather)) (\(ok))");
    }
  }
}
