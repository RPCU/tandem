// Fast travel together, and regroup (docs/architecture/travel.md, milestone 4).
//
// Players stay free to be anywhere: nobody is ever moved without saying yes.
//
// - When our V arrives from a fast travel (a data terminal, the map), the
//   others are offered to join it (Regroup, reason 0) with the point it
//   used. Our own fast travel is seen where vanilla performs it
//   (FastTravelSystem.PerformFastTravel, wrapped below), the arrival when
//   V has moved and the world has run for a moment after the loading screen.
// - An offer from another player shows the game's own way: an on-screen
//   message and an input hint on the phone key ("Join NAME"), the key that
//   answers a holocall. Pressing it joins them; the offer lapses after 15 s
//   (they are told we stay).
// - Joining goes the game's way: our own fast travel to the same point when
//   we know it (PerformFastTravelRequest: its loading screen, streaming,
//   camera); else the same steps to that point's marker without adding it
//   to our map; else, far away, our fast travel point nearest to them, then
//   a short hop next to them; close by, the hop alone (a teleport).
// - The regroup action: the world map's marker of a player (Markers.reds),
//   held like a fast travel point ("Join NAME"), brings us to them the same
//   way. Fast travel locks (combat, a vehicle, a quest) block it too.
// - The story's "offer to join" (a far guest when a scene starts) calls
//   TandemTravel.OfferRegroup(game, player, 2, title).
// - The arrival: a join chosen in the co-op menu (TandemNative.ChooseArrival)
//   was the player's yes. At its first welcome our V goes beside the host,
//   no prompt, behind the fast travel's loading screen whatever the distance
//   (no fast travel point needed: a new V has none), once our V is free to
//   go (not in a locked scene, a vehicle or down). Our own fast travel locks
//   do not hold it: they are our own story's, which waits in the session.

public class TandemTravelOffer {
  public let player: Uint32;
  public let offer: Uint32;
  public let reason: Int32;
  public let point: TweakDBID;
  public let marker: Uint64;
  public let position: Vector4;
  public let yaw: Float;
  public let label: String;
  public let shownAt: Float;
}

// Where a player last arrived by fast travel: joining them later goes to
// that point the same way while they are still near it.
public class TandemTravelArrival {
  public let player: Uint32;
  public let point: TweakDBID;
  public let marker: Uint64;
  public let position: Vector4;
}

public class TandemTravel {
  // How long an offer is shown; how near counts as together already; how
  // far a hop alone may go (no loading screen); how near to its player a
  // fast travel point must be to travel by it.
  public static func OfferSeconds() -> Float {
    return 15.0;
  }

  public static func TogetherMetres() -> Float {
    return 60.0;
  }

  public static func HopMetres() -> Float {
    return 150.0;
  }

  public static func PointMetres() -> Float {
    return 1500.0;
  }

  // How far our V may be taken in a moment, with nothing to explain it,
  // before the others are offered to join it (WatchJump).
  public static func JumpMetres() -> Float {
    return 50.0;
  }

  private let m_game: GameInstance;
  private let m_you: Uint32;
  // Real time (the offers' lapses) and the world's (our trip's waits: a
  // loading screen stops it), this tick (TandemClock).
  private let m_now: Float;
  private let m_world: Float;
  // Our V's place at the last tick, on the world's time (WatchJump); until
  // when (real time) a move of ours is expected; a jump nothing explained,
  // waiting for our V to stand (world time, 0: none); our last offer.
  private let m_lastAt: Vector4;
  private let m_lastAtWorld: Float;
  private let m_ownMoveUntil: Float;
  private let m_jumpedAt: Float;
  private let m_offeredAt: Float;
  // Our trip under way: since when (world time), from where, by which
  // point; for whom (0: our own fast travel, offered to the others on
  // arrival).
  private let m_trip: Bool;
  private let m_tripSince: Float;
  private let m_tripFrom: Vector4;
  private let m_tripPoint: TweakDBID;
  private let m_tripMarker: Uint64;
  private let m_landedAt: Float;
  private let m_joining: Uint32;
  private let m_joinAt: Vector4;
  private let m_joinYaw: Float;
  // The offer on screen, and the phone key while it is.
  private let m_offer: ref<TandemTravelOffer>;
  // A guest: when our V was last beside the host (TakenAlong), and the
  // host's offer we follow without a question once the curtain is black.
  private let m_besideHostAt: Float;
  private let m_along: ref<TandemTravelOffer>;
  private let m_listening: Bool;
  private let m_held: Bool;
  private let m_arrivals: array<ref<TandemTravelArrival>>;
  // Our latest arrival, for an offer made later (the story's).
  private let m_lastPoint: TweakDBID;
  private let m_lastMarker: Uint64;
  private let m_lastArrival: Vector4;
  // The last player who made us an offer or answered ours (test commands).
  private let m_partner: Uint32;
  // The fast travel's loading screen (blackboard FastTRavelSystem): our
  // listener, and whether it has ended since our trip began, and when.
  private let m_loadingListener: ref<CallbackHandle>;
  private let m_loadingEnded: Bool;
  private let m_loadingEndedAt: Float;
  // The arrival (ArriveBeside): the host to go to (0: none), since when
  // (real time; 0: not ticked yet), and whether the trip under way is it.
  private let m_arrival: Uint32;
  private let m_arrivalSince: Float;
  private let m_arriving: Bool;

  // How long the arrival waits for our V to be free to go, and how near
  // the host it must land not to be moved again after the loading screen.
  public static func ArrivalSeconds() -> Float {
    return 90.0;
  }

  public static func ArrivalNearMetres() -> Float {
    return 30.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    let blackboard = this.TravelBoard();
    if IsDefined(blackboard) && !IsDefined(this.m_loadingListener) {
      this.m_loadingListener = blackboard.RegisterListenerBool(GetAllBlackboardDefs().FastTRavelSystem.FastTravelLoadingScreenFinished, this, n"OnLoadingScreenFinished");
    }
  }

  public func Stop() -> Void {
    this.Hide();
    this.m_trip = false;
    this.m_joining = 0u;
    this.m_arrival = 0u;
    this.m_arriving = false;
    ArrayClear(this.m_arrivals);
    let blackboard = this.TravelBoard();
    if IsDefined(blackboard) && IsDefined(this.m_loadingListener) {
      blackboard.UnregisterListenerBool(GetAllBlackboardDefs().FastTRavelSystem.FastTravelLoadingScreenFinished, this.m_loadingListener);
    }
    this.m_loadingListener = null;
  }

  private func TravelBoard() -> ref<IBlackboard> {
    return GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().FastTRavelSystem);
  }

  // The fast travel's loading screen is over (vanilla's FastTravelSystem
  // waits for the same before its autosave): the game is drawn again.
  protected cb func OnLoadingScreenFinished(value: Bool) -> Bool {
    TandemNative.HookRan(n"Travel.TandemTravel.OnLoadingScreenFinished");
    if value && this.m_trip {
      this.m_loadingEnded = true;
      this.m_loadingEndedAt = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
      TandemNative.Log("travel: the fast travel's loading screen is over");
    }
    return false;
  }

  // A regroup offer is on screen (the phone key is its: Comms.reds).
  public func ShowingOffer() -> Bool {
    return IsDefined(this.m_offer);
  }

  public static func Of(game: GameInstance) -> ref<TandemTravel> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Travel() : null;
  }

  // ---- the API (the story work, the map, commands) ----

  // Offer `player` (0: everyone) to come to us. reason: 0 fast travel, 1
  // asked, 2 story (label: what starts), 3 teleport. Answers the offer's id
  // (0: not in a session).
  public static func OfferRegroup(game: GameInstance, player: Uint32, reason: Int32, label: String) -> Uint32 {
    let travel = TandemTravel.Of(game);
    return IsDefined(travel) ? travel.Offer(player, reason, label) : 0u;
  }

  // An offer is on screen (the phone key is ours until it goes).
  public func Showing() -> Bool {
    return IsDefined(this.m_offer);
  }

  // Go to `player` now (the regroup action). False when it cannot be done.
  public static func RegroupTo(game: GameInstance, player: Uint32) -> Bool {
    let travel = TandemTravel.Of(game);
    return IsDefined(travel) && travel.JoinPlayer(player);
  }

  public func Offer(player: Uint32, reason: Int32, label: String) -> Uint32 {
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) || this.m_you == 0u {
      return 0u;
    }
    let at = local.GetWorldPosition();
    // Still by the point we arrived at: joiners can travel by it.
    let point = TDBID.None();
    let marker = 0ul;
    if TDBID.IsValid(this.m_lastPoint) && Vector4.Distance(at, this.m_lastArrival) < 50.0 {
      point = this.m_lastPoint;
      marker = this.m_lastMarker;
    }
    let offer = TandemNative.SendRegroup(player, reason, point, TandemNative.NodeRefOf(marker), at, local.GetWorldOrientation(), label);
    if player == 0u {
      this.m_offeredAt = this.m_now;
    }
    let whom = player == 0u ? "everyone" : this.NameOf(player);
    TandemNative.Log(s"travel: offered \(whom) to join us (offer \(offer), reason \(reason))");
    return offer;
  }

  public func JoinPlayer(player: Uint32) -> Bool {
    let at = TandemNative.RemotePosition(player);
    if at.W == 0.0 {
      TandemNative.Log(s"travel: player \(player) is nowhere yet");
      return false;
    }
    at.W = 1.0;
    // By the point they arrived at, if they are still near it.
    let point = TDBID.None();
    let marker = 0ul;
    for arrival in this.m_arrivals {
      if arrival.player == player && Vector4.Distance(arrival.position, at) < 100.0 {
        point = arrival.point;
        marker = arrival.marker;
      }
    }
    return this.Join(player, 0u, at, TandemNative.RemoteYaw(player), point, marker);
  }

  // ---- every tick (TandemSystem, once the world plays) ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_world = TandemClock.World(this.m_game);
    this.m_you = you;
    for item in TandemNative.TakeRegroups() {
      if item.kind == 1 {
        this.Offered(item);
      } else if item.kind == 2 {
        this.Answered(item);
      }
    }
    if IsDefined(this.m_offer) && now - this.m_offer.shownAt > TandemTravel.OfferSeconds() {
      // No answer to a lapsed offer: its maker lets it lapse on its side too,
      // and a maker that left and joined again in between made no such offer
      // in its new session (soak-host: "answers our regroup offer 7, and we
      // made 0", 2026-11-22).
      TandemNative.Log(s"travel: offer \(this.m_offer.offer) of \(this.NameOf(this.m_offer.player)) lapsed");
      this.Hide();
    }
    this.TryArrival();
    this.NoteBeside();
    this.FollowAlong();
    this.FollowTrip();
    this.WatchJump();
  }

  // ---- the arrival ----

  // Our first welcome in a join the co-op menu chose: beside `host` once
  // our V is free to go (TryArrival).
  public func ArriveBeside(host: Uint32) -> Void {
    this.m_arrival = host;
    this.m_arrivalSince = 0.0;
    TandemNative.Log(s"arrival: to \(this.NameOf(host)) once our V is free to go");
  }

  // Why our V cannot go yet ("": it can).
  private func ArrivalWait(local: ref<PlayerPuppet>, at: Vector4) -> String {
    if !IsDefined(local) {
      return "no V";
    }
    if at.W == 0.0 {
      return "the host is nowhere yet";
    }
    if VehicleComponent.IsMountedToVehicle(this.m_game, local) {
      return "in a vehicle";
    }
    // A scene that holds our V (our own story's opening, a new V's
    // lifepath start): it plays out first.
    if PlayerPuppet.GetSceneTier(local) >= 3 {
      return "in a scene";
    }
    let system = TandemSystem.Of(this.m_game);
    if IsDefined(system) && IsDefined(system.Revive()) && system.Revive().IsDown() {
      return "down";
    }
    return "";
  }

  private func TryArrival() -> Void {
    if this.m_arrival == 0u || this.m_trip {
      return;
    }
    // The session ended before we could go: nobody to go to.
    if this.m_you == 0u {
      this.m_arrival = 0u;
      TandemNative.Log("arrival: dropped, out of the session");
      return;
    }
    if this.m_arrivalSince <= 0.0 {
      this.m_arrivalSince = MaxF(this.m_now, 0.001);
    }
    let host = this.m_arrival;
    let name = this.NameOf(host);
    let local = GetPlayer(this.m_game);
    let at = TandemNative.RemotePosition(host);
    let wait = this.ArrivalWait(local, at);
    if StrLen(wait) > 0 {
      if this.m_now - this.m_arrivalSince > TandemTravel.ArrivalSeconds() {
        this.m_arrival = 0u;
        TandemNative.Log(s"arrival: given up (\(wait)): \(name)'s marker and Join \(name) bring us there");
        TandemText.Notice(this.m_game, "Session-JoinedOf", name);
      }
      return;
    }
    this.m_arrival = 0u;
    at.W = 1.0;
    let yaw = TandemNative.RemoteYaw(host);
    let distance = Vector4.Distance(local.GetWorldPosition(), at);
    if distance < TandemTravel.TogetherMetres() {
      TandemNative.Log(s"arrival: beside \(name) already (\(RoundF(distance)) m)");
      TandemText.Notice(this.m_game, "Session-JoinedOf", name);
      return;
    }
    let travel = this.FastTravel();
    if IsDefined(travel) && !travel.IsFastTravelEnabled() {
      TandemNative.Log("arrival: our own fast travel locks are on (our story's, held in the session): not for the arrival");
    }
    this.m_joinAt = at;
    this.m_joinYaw = yaw;
    this.m_arriving = true;
    this.BeginTrip(TDBID.None(), 0ul, host);
    this.TeleportHidden(local, this.Behind(at, yaw), yaw);
    TandemNative.Log(s"arrival: joining \(name) behind the fast travel's loading screen (\(RoundF(distance)) m)");
  }

  // The trip is over (FollowTrip): near enough, we stay where the loading
  // screen left us; the host walked on meanwhile, the last metres.
  private func Arrived(host: Uint32) -> Void {
    this.m_arriving = false;
    let local = GetPlayer(this.m_game);
    let at = TandemNative.RemotePosition(host);
    if IsDefined(local) && at.W != 0.0 && Vector4.Distance(local.GetWorldPosition(), at) > TandemTravel.ArrivalNearMetres() {
      this.Hop(host);
    } else {
      TandemNative.Log(s"travel: arrived next to \(this.NameOf(host)) (the arrival)");
    }
    TandemText.Notice(this.m_game, "Session-JoinedOf", this.NameOf(host));
  }

  // ---- taken along (a guest beside the host when a quest moves it) ----

  // Our V beside the host's (grouped, within the moment's 40 m): if a
  // quest's teleport takes the host now, we go along (TakenAlong).
  private func NoteBeside() -> Void {
    if this.m_you == 0u || TandemNative.IsHost() {
      return;
    }
    let story = TandemStorySystem.Get(this.m_game);
    if !IsDefined(story) || !story.Grouped() {
      return;
    }
    let distance = story.DistanceToHost();
    if distance >= 0.0 && distance < 40.0 {
      this.m_besideHostAt = this.m_now;
    }
  }

  // The host was taken somewhere else (reason 3) while we stood beside it:
  // shared play resumes there, so we go along at once, with no question
  // (the user's rule, 2026-11-21): black first (Curtain.reds), then the
  // game's own way there (Join: a fast travel's loading screen, else a hop
  // behind the black). Not when the game's fast travel locks hold us (the
  // offer then, as before), nor across a gate our own story keeps closed.
  private func TakenAlong(e: TandemRegroup, offer: ref<TandemTravelOffer>) -> Bool {
    if e.reason != 3 || this.m_now - this.m_besideHostAt > 8.0 {
      return false;
    }
    let story = TandemStorySystem.Get(this.m_game);
    if !IsDefined(story) || !story.Grouped() || e.player != story.HostPlayer() {
      return false;
    }
    let world = story.World();
    if IsDefined(world) && world.GateBetween() {
      TandemNative.Log("travel: the host was taken across a gate our story keeps closed: offered, not followed");
      return false;
    }
    let travel = this.FastTravel();
    if !IsDefined(travel) || !travel.IsFastTravelEnabled() {
      return false;
    }
    this.m_along = offer;
    let curtain = story.Curtain();
    if IsDefined(curtain) {
      curtain.Close(0.3);
    }
    TandemNative.Log(s"travel: the host was taken from beside us: going along (offer \(e.offer))");
    return true;
  }

  private func FollowAlong() -> Void {
    if !IsDefined(this.m_along) {
      return;
    }
    let story = TandemStorySystem.Get(this.m_game);
    let curtain = IsDefined(story) ? story.Curtain() : null;
    if IsDefined(curtain) && !curtain.IsClosed() {
      return;
    }
    let offer = this.m_along;
    this.m_along = null;
    // Where the host is now, if it moved on since.
    let at = TandemNative.RemotePosition(offer.player);
    let yaw = TandemNative.RemoteYaw(offer.player);
    if at.W == 0.0 || Vector4.Distance(at, offer.position) > 100.0 {
      at = offer.position;
      yaw = offer.yaw;
    }
    at.W = 1.0;
    let joined = this.Join(offer.player, offer.offer, at, yaw, offer.point, offer.marker);
    if IsDefined(curtain) {
      // A fast travel's loading screen takes over: lifted behind it.
      curtain.Open(0.4, joined ? 1.0 : 0.0);
    }
    if !joined {
      this.Show(offer);
    }
  }

  // A move of our V that is ours (a regroup's hop, a test's teleport, a
  // race's grid): not a jump to offer (WatchJump).
  public static func ExpectMove(game: GameInstance) -> Void {
    let travel = TandemTravel.Of(game);
    if IsDefined(travel) {
      travel.Expect();
    }
  }

  public func Expect() -> Void {
    this.m_ownMoveUntil = TandemClock.Real(this.m_game) + 3.0;
  }

  // Host: our V taken more than JumpMetres in a moment (faster than it can
  // run, drive or fall) with nothing to explain it: a quest's teleport or
  // fade outside a shared moment. Once it has stood a second of the world's
  // time there (a quest may move it twice), the others are offered to join
  // it (reason 3, "NAME was taken somewhere else"; shown only to the far
  // ones). Explained: a fast travel or a regroup of ours, a move we expect,
  // a vehicle or a ride, the host's moment (the guests beside it watch it,
  // Spectate.reds), an offer of ours still on their screens.
  private func WatchJump() -> Void {
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) {
      return;
    }
    let at = local.GetWorldPosition();
    let moved = this.m_lastAtWorld > 0.0 ? Vector4.Distance(at, this.m_lastAt) : 0.0;
    let elapsed = MaxF(this.m_world - this.m_lastAtWorld, 0.0);
    this.m_lastAt = at;
    this.m_lastAtWorld = this.m_world;
    if this.m_you == 0u || !TandemNative.IsHost() || this.JumpExplained(local) {
      this.m_jumpedAt = 0.0;
      return;
    }
    if moved > TandemTravel.JumpMetres() + 100.0 * elapsed {
      this.m_jumpedAt = MaxF(this.m_world, 0.001);
      TandemNative.Log(s"travel: our V was taken \(RoundF(moved)) m with no travel of ours");
      return;
    }
    if this.m_jumpedAt > 0.0 && this.m_world - this.m_jumpedAt >= 1.0 {
      this.m_jumpedAt = 0.0;
      let others = TandemNative.OtherPlayers();
      if ArraySize(others) > 0 {
        this.Offer(0u, 3, "");
      }
    }
  }

  private func JumpExplained(local: ref<PlayerPuppet>) -> Bool {
    if this.m_trip || this.m_now < this.m_ownMoveUntil || this.m_now - this.m_offeredAt < TandemTravel.OfferSeconds() {
      return true;
    }
    if VehicleComponent.IsMountedToVehicle(this.m_game, local) {
      return true;
    }
    let system = TandemSystem.Of(this.m_game);
    if IsDefined(system) && IsDefined(system.Transit()) && system.Transit().OurKind() != 0 {
      return true;
    }
    // Down, and a moment after: a respawn places our V (Revive.reds).
    if IsDefined(system) && IsDefined(system.Revive()) && system.Revive().IsDown() {
      this.m_ownMoveUntil = this.m_now + 3.0;
      return true;
    }
    let story = TandemStorySystem.Get(this.m_game);
    return IsDefined(story) && IsDefined(story.Spectate()) && story.Spectate().HostMoment();
  }

  // ---- our own fast travel ----

  // Vanilla is about to teleport our V to a fast travel point (the wrap).
  public func Departing(point: TweakDBID, marker: Uint64) -> Void {
    if this.m_trip {
      return; // our regroup's own travel: already followed
    }
    this.BeginTrip(point, marker, 0u);
  }

  private func BeginTrip(point: TweakDBID, marker: Uint64, joining: Uint32) -> Void {
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) {
      return;
    }
    this.m_trip = true;
    this.m_tripSince = this.m_world;
    this.m_tripFrom = local.GetWorldPosition();
    this.m_tripPoint = point;
    this.m_tripMarker = marker;
    this.m_landedAt = 0.0;
    this.m_loadingEnded = false;
    this.m_joining = joining;
    this.Hide();
    let whom = joining != 0u ? " (joining " + this.NameOf(joining) + ")" : "";
    TandemNative.Log(s"travel: fast travel to \(this.PointName(point)) under way\(whom)");
  }

  // The jump, then the end of the fast travel's loading screen and a moment
  // of the world running behind it: we are there. Nothing moves V before
  // that: a teleport (the hop) while the fast travel was still fading in
  // left the screen black for good in game (2026-11-19).
  private func FollowTrip() -> Void {
    if !this.m_trip {
      return;
    }
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) {
      return;
    }
    if this.m_landedAt == 0.0 {
      if Vector4.Distance(local.GetWorldPosition(), this.m_tripFrom) > 30.0 {
        this.m_landedAt = this.m_world;
      } else if this.m_world - this.m_tripSince > 60.0 {
        TandemNative.Log("travel: no arrival seen in 60 s, given up");
        this.m_trip = false;
        this.m_joining = 0u;
        this.m_arriving = false;
      }
      return;
    }
    if this.m_loadingEnded {
      let since = this.m_world - this.m_loadingEndedAt;
      if since < 1.5 || this.m_world - this.m_landedAt < 1.5 {
        return;
      }
      TandemNative.Log("travel: screen restored (the loading screen ended)");
    } else if this.m_world - this.m_landedAt < 30.0 {
      return; // the loading screen's end may not have reached us yet
    } else {
      TandemNative.Log("travel: still black? no end of the fast travel's loading screen in 30 s since the jump");
    }
    this.m_trip = false;
    let at = local.GetWorldPosition();
    if TDBID.IsValid(this.m_tripPoint) {
      this.m_lastPoint = this.m_tripPoint;
      this.m_lastMarker = this.m_tripMarker;
      this.m_lastArrival = at;
    }
    if this.m_joining != 0u {
      let player = this.m_joining;
      this.m_joining = 0u;
      if this.m_arriving {
        this.Arrived(player);
      } else {
        this.Hop(player);
      }
      return;
    }
    if this.m_you == 0u {
      return; // not in a session: nobody to offer it to
    }
    let offer = TandemNative.SendRegroup(0u, 0, this.m_tripPoint, TandemNative.NodeRefOf(this.m_tripMarker), at, local.GetWorldOrientation(), "");
    this.m_offeredAt = this.m_now;
    TandemNative.Log(s"travel: arrived at \(this.PointName(this.m_tripPoint)), offered the others to join (offer \(offer))");
  }

  // ---- offers ----

  private func Offered(e: TandemRegroup) -> Void {
    if e.player == this.m_you {
      return;
    }
    this.m_partner = e.player;
    let at = new Vector4(e.x, e.y, e.z, 1.0);
    if e.reason == 0 && TDBID.IsValid(e.point) {
      this.NoteArrival(e.player, e.point, e.marker, at);
    }
    let name = this.NameOf(e.player);
    TandemNative.Log(s"travel: offer \(e.offer) from \(name) (reason \(e.reason)) to join them at (\(at.X), \(at.Y), \(at.Z))");
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) {
      return;
    }
    if this.m_trip {
      TandemNative.Log(s"travel: offer \(e.offer) of \(name) not shown: on our way somewhere");
      return;
    }
    if Vector4.Distance(local.GetWorldPosition(), at) < TandemTravel.TogetherMetres() {
      TandemNative.Log(s"travel: offer \(e.offer) of \(name) not shown: together already");
      return;
    }
    let offer = new TandemTravelOffer();
    offer.player = e.player;
    offer.offer = e.offer;
    offer.reason = e.reason;
    offer.point = e.point;
    offer.marker = e.marker;
    offer.position = at;
    offer.yaw = e.yaw;
    offer.label = e.label;
    offer.shownAt = this.m_now;
    if this.TakenAlong(e, offer) {
      return;
    }
    this.Show(offer);
  }

  private func Answered(e: TandemRegroup) -> Void {
    this.m_partner = e.player;
    let name = this.NameOf(e.player);
    if e.accepted {
      TandemNative.Log(s"travel: \(name) is on the way (offer \(e.offer))");
      this.Notice("Travel-OnTheWay", 4.0, name);
    } else {
      TandemNative.Log(s"travel: \(name) stays where they are (offer \(e.offer))");
      this.Notice("Travel-Stays", 4.0, name);
    }
  }

  private func NoteArrival(player: Uint32, point: TweakDBID, marker: Uint64, at: Vector4) -> Void {
    for arrival in this.m_arrivals {
      if arrival.player == player {
        arrival.point = point;
        arrival.marker = marker;
        arrival.position = at;
        return;
      }
    }
    let arrival = new TandemTravelArrival();
    arrival.player = player;
    arrival.point = point;
    arrival.marker = marker;
    arrival.position = at;
    ArrayPush(this.m_arrivals, arrival);
  }

  // The game's own on-screen message, and an input hint on the phone key.
  private func Show(offer: ref<TandemTravelOffer>) -> Void {
    this.Hide();
    this.m_offer = offer;
    let name = this.NameOf(offer.player);
    let key: String;
    let b = "";
    switch offer.reason {
      case 0:
        key = TDBID.IsValid(offer.point) ? "Travel-FastTravelledTo" : "Travel-FastTravelled";
        b = TDBID.IsValid(offer.point) ? this.PointName(offer.point) : "";
        break;
      case 2:
        key = StrLen(offer.label) > 0 ? "Travel-Labelled" : "Travel-Scene";
        b = offer.label;
        break;
      case 3:
        key = "Travel-Taken";
        break;
      default:
        key = "Travel-Asks";
        break;
    }
    // A label came over the wire: a key of ours (each game its own
    // language), else a title shown as it came.
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, TandemTravel.OfferSeconds(), key, name, TandemText.OfWire(b));
    this.Hint(true, TandemText.F("Travel-Join", name));
    let local = GetPlayer(this.m_game);
    if IsDefined(local) && !this.m_listening {
      local.RegisterInputListener(this, n"PhoneInteract");
      this.m_listening = true;
      this.m_held = false;
    }
    TandemNative.Log(s"travel: showing offer \(offer.offer) of \(name): \(TandemText.FE(true, key, name, b))");
  }

  private func Hide() -> Void {
    if IsDefined(this.m_offer) {
      this.Hint(false, "");
    }
    this.m_offer = null;
    if this.m_listening {
      let local = GetPlayer(this.m_game);
      if IsDefined(local) {
        local.UnregisterInputListener(this, n"PhoneInteract");
      }
      this.m_listening = false;
    }
  }

  // The phone key, while an offer is shown: a press (released before the
  // phone's own hold) joins. A holocall ringing keeps the key for itself.
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Travel.TandemTravel.OnAction");
    if !IsDefined(this.m_offer) || !Equals(ListenerAction.GetName(action), n"PhoneInteract") {
      return false;
    }
    let kind = ListenerAction.GetType(action);
    if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.m_held = true; // the phone opens: not for us
    } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
      if this.m_held {
        this.m_held = false;
      } else if !this.CallRinging() {
        this.Accept();
      }
    }
    return false;
  }

  public func Accept() -> Bool {
    if !IsDefined(this.m_offer) {
      TandemNative.Log("travel: no offer to accept");
      return false;
    }
    let offer = this.m_offer;
    this.Hide();
    TandemNative.Log(s"travel: joining \(this.NameOf(offer.player)) (offer \(offer.offer))");
    // Where they are now, if they moved on since.
    let at = TandemNative.RemotePosition(offer.player);
    let yaw = TandemNative.RemoteYaw(offer.player);
    if at.W == 0.0 || Vector4.Distance(at, offer.position) > 100.0 {
      at = offer.position;
      yaw = offer.yaw;
    }
    at.W = 1.0;
    if !this.Join(offer.player, offer.offer, at, yaw, offer.point, offer.marker) {
      TandemNative.SendRegroupAnswer(offer.player, offer.offer, false);
      return false;
    }
    return true;
  }

  public func Decline() -> Void {
    if !IsDefined(this.m_offer) {
      return;
    }
    TandemNative.SendRegroupAnswer(this.m_offer.player, this.m_offer.offer, false);
    TandemNative.Log(s"travel: declined offer \(this.m_offer.offer) of \(this.NameOf(this.m_offer.player))");
    this.Hide();
  }

  // ---- getting there ----

  private func Join(player: Uint32, offer: Uint32, at: Vector4, yaw: Float, point: TweakDBID, marker: Uint64) -> Bool {
    let local = GetPlayer(this.m_game);
    let travel = this.FastTravel();
    if !IsDefined(local) || !IsDefined(travel) || this.m_trip {
      return false;
    }
    let name = this.NameOf(player);
    // The game's own locks (combat, a vehicle, a quest) hold for us too.
    if !travel.IsFastTravelEnabled() {
      this.Notice("Travel-Blocked", 4.0);
      TandemNative.Log(s"travel: cannot join \(name): fast travel is locked here");
      return false;
    }
    this.m_joinAt = at;
    this.m_joinYaw = yaw;
    TandemNative.SendRegroupAnswer(player, offer, true);
    let distance = Vector4.Distance(local.GetWorldPosition(), at);
    let ours = this.OwnPoint(point);
    if IsDefined(ours) && distance > TandemTravel.HopMetres() {
      this.BeginTrip(point, marker, player);
      this.RequestFastTravel(local, ours);
      TandemNative.Log(s"travel: joining \(name) by our fast travel to \(this.PointName(point))");
      return true;
    }
    if marker != 0ul && distance > TandemTravel.HopMetres() {
      this.BeginTrip(point, marker, player);
      this.TravelToMarker(local, point, marker);
      TandemNative.Log(s"travel: joining \(name) by \(this.PointName(point))'s marker (not on our map)");
      return true;
    }
    if distance > TandemTravel.HopMetres() {
      let nearest = this.NearestOwnPoint(at);
      if IsDefined(nearest) {
        this.BeginTrip(nearest.GetPointRecord(), TandemNative.NodeRefHash(nearest.GetMarkerRef()), player);
        this.RequestFastTravel(local, nearest);
        TandemNative.Log(s"travel: joining \(name) by our fast travel to \(this.PointName(nearest.GetPointRecord())), then a hop");
        return true;
      }
      TandemNative.Log(s"travel: joining \(name): no fast travel point of ours near them, teleporting");
    }
    this.Hop(player);
    return true;
  }

  // Vanilla's own fast travel to a point we know.
  private func RequestFastTravel(local: ref<GameObject>, point: ref<FastTravelPointData>) -> Void {
    let request = new PerformFastTravelRequest();
    request.pointData = point;
    request.player = local;
    this.FastTravel().QueueRequest(request);
    this.FastTravelLoadingScreen();
  }

  // What FastTravelSystem.PerformFastTravel does, to a point that is not on
  // our map (not added to it: it stays undiscovered for us), with no
  // autosave after it.
  private func TravelToMarker(local: ref<PlayerPuppet>, point: TweakDBID, marker: Uint64) -> Void {
    PlayerGameplayRestrictions.RemoveAllGameplayRestrictions(local);
    let blackboard = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().FastTRavelSystem);
    if IsDefined(blackboard) {
      blackboard.SetVariant(GetAllBlackboardDefs().FastTRavelSystem.DestinationPoint, ToVariant(point));
    }
    local.GetFPPCameraComponent().ResetPitch();
    TandemSync.WorldRebuilt(2);
    GameInstance.GetTeleportationFacility(this.m_game).TeleportToNode(local, TandemNative.NodeRefOf(marker));
    this.FastTravelLoadingScreen();
    GameInstance.GetUISystem(this.m_game).NotifyFastTravelStart();
  }

  // What the world map does with its fast travel request
  // (WorldMapMenuGameController.FastTravel): the next loading screen is the
  // fast travel one, which fades the game back in when it ends. Without it
  // the game ran on behind a black screen (2026-11-19, a travel from the
  // `travel` command: the screen stayed black after the loading screen).
  private func FastTravelLoadingScreen() -> Void {
    let next = new inkSetNextLoadingScreenEvent();
    next.SetNextLoadingScreenType(inkLoadingScreenType.FastTravel);
    GameInstance.GetUISystem(this.m_game).QueueEvent(next);
  }

  // The last metres: two behind them, facing the way they face.
  private func Hop(player: Uint32) -> Void {
    let local = GetPlayer(this.m_game);
    if !IsDefined(local) {
      return;
    }
    let at = TandemNative.RemotePosition(player);
    let yaw = TandemNative.RemoteYaw(player);
    if at.W == 0.0 || Vector4.Distance(at, this.m_joinAt) > 100.0 {
      at = this.m_joinAt;
      yaw = this.m_joinYaw;
    }
    let spot = this.Behind(at, yaw);
    let gap = Vector4.Distance(local.GetWorldPosition(), spot);
    if gap > 4.0 {
      this.Expect();
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(local, spot, TandemYawAngles(Rad2Deg(yaw)));
    }
    TandemNative.Log(s"travel: arrived next to \(this.NameOf(player)) (\(gap) m hop)");
  }

  // Two metres behind a player at `at` facing `yaw`. The game's forward is
  // +Y turned by yaw: (-sin, cos).
  private func Behind(at: Vector4, yaw: Float) -> Vector4 {
    return new Vector4(at.X + SinF(yaw) * 2.0, at.Y - CosF(yaw) * 2.0, at.Z, 1.0);
  }

  // A teleport hidden as a fast travel is (FastTravelSystem.PerformFastTravel,
  // TravelToMarker): the gameplay restrictions off, the camera level, the
  // fast travel's loading screen over the move while the world streams in
  // around it. For a spot with no fast travel point (the arrival).
  private func TeleportHidden(local: ref<PlayerPuppet>, spot: Vector4, yaw: Float) -> Void {
    PlayerGameplayRestrictions.RemoveAllGameplayRestrictions(local);
    local.GetFPPCameraComponent().ResetPitch();
    TandemSync.WorldRebuilt(2);
    this.Expect();
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(local, spot, TandemYawAngles(Rad2Deg(yaw)));
    this.FastTravelLoadingScreen();
    GameInstance.GetUISystem(this.m_game).NotifyFastTravelStart();
  }

  // ---- the fast travel system ----

  private func FastTravel() -> ref<FastTravelSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"FastTravelSystem") as FastTravelSystem;
  }

  private func OwnPoint(point: TweakDBID) -> ref<FastTravelPointData> {
    let travel = this.FastTravel();
    if !TDBID.IsValid(point) || !IsDefined(travel) {
      return null;
    }
    for known in travel.GetFastTravelPoints() {
      if IsDefined(known) && known.GetPointRecord() == point {
        return known;
      }
    }
    return null;
  }

  // Ours on the world map nearest to `at`, within PointMetres.
  private func NearestOwnPoint(at: Vector4) -> ref<FastTravelPointData> {
    let travel = this.FastTravel();
    if !IsDefined(travel) {
      return null;
    }
    let mappins = GameInstance.GetMappinSystem(this.m_game);
    let best: ref<FastTravelPointData>;
    let bestDistance = TandemTravel.PointMetres();
    // (redscript 0.5 has no continue)
    for known in travel.GetFastTravelPoints() {
      if IsDefined(known) && known.ShouldShowMappinOnWorldMap() {
        let mappin = mappins.GetMappin(known.mappinID);
        if IsDefined(mappin) {
          let distance = Vector4.Distance(mappin.GetWorldPosition(), at);
          if distance < bestDistance {
            best = known;
            bestDistance = distance;
          }
        }
      }
    }
    return best;
  }

  // Ours, more than 500 m away (the test command "travel").
  public func TravelSomewhere() -> Bool {
    let local = GetPlayer(this.m_game);
    let travel = this.FastTravel();
    if !IsDefined(local) || !IsDefined(travel) {
      return false;
    }
    let mappins = GameInstance.GetMappinSystem(this.m_game);
    for known in travel.GetFastTravelPoints() {
      if IsDefined(known) && known.ShouldShowMappinOnWorldMap() {
        let mappin = mappins.GetMappin(known.mappinID);
        if IsDefined(mappin) && Vector4.Distance(mappin.GetWorldPosition(), local.GetWorldPosition()) > 500.0 {
          TandemNative.Log(s"travel: fast travelling to \(this.PointName(known.GetPointRecord()))");
          this.RequestFastTravel(local, known);
          return true;
        }
      }
    }
    TandemNative.Log("travel: no fast travel point of ours 500 m away");
    return false;
  }

  // ---- helpers ----

  private func PointName(point: TweakDBID) -> String {
    if !TDBID.IsValid(point) {
      return "no point";
    }
    let record = TweakDBInterface.GetFastTravelPointRecord(point);
    return IsDefined(record) ? GetLocalizedText(record.DisplayName()) : TDBID.ToStringDEBUG(point);
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

  private func CallRinging() -> Bool {
    let blackboard = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice);
    if !IsDefined(blackboard) {
      return false;
    }
    let info = blackboard.GetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation);
    if !IsDefined(info) {
      return false;
    }
    let call = FromVariant<PhoneCallInformation>(info);
    return Equals(call.callPhase, questPhoneCallPhase.IncomingCall);
  }

  // The HUD's warning line (Text.reds).
  private func Notice(key: String, seconds: Float, opt a: String) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, seconds, key, a);
  }

  // The hint bar draws the player's own binding for PhoneInteract.
  private func Hint(show: Bool, label: String) -> Void {
    TandemText.Hint(this.m_game, n"TandemRegroup", n"PhoneInteract", show, label, inkInputHintHoldIndicationType.Press);
  }

  // tools/game.sh cmd: regroup [PLAYER] | regroup accept | regroup decline |
  // regroup offer | travel
  public func Command(command: String) -> Void {
    if Equals(command, "travel") {
      this.TravelSomewhere();
    } else if Equals(command, "regroup accept") {
      this.Accept();
    } else if Equals(command, "regroup decline") {
      this.Decline();
    } else if Equals(command, "regroup offer") {
      this.Offer(0u, 1, "");
    } else if StrBeginsWith(command, "regroup jump ") {
      // (test) regroup jump X Y Z: our V taken there as a quest's teleport
      // would (no travel of ours to explain it).
      let words = StrSplit(command, " ");
      let local = GetPlayer(this.m_game);
      if ArraySize(words) >= 5 && IsDefined(local) {
        let x = words[2];
        let y = words[3];
        let z = words[4];
        let to = new Vector4(StringToFloat(x), StringToFloat(y), StringToFloat(z), 1.0);
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(local, to, TandemYawAngles(0.0));
        TandemNative.Log(s"travel: (test) our V taken to (\(to.X), \(to.Y), \(to.Z))");
      }
    } else if StrBeginsWith(command, "regroup") {
      let player = this.PlayerNamed(StrLen(command) > 8 ? StrAfterFirst(command, " ") : "");
      if player == 0u {
        TandemNative.Log("travel: regroup with nobody: no such player here");
        return;
      }
      let going = this.JoinPlayer(player);
      TandemNative.Log(s"travel: regroup with \(this.NameOf(player)): \(going ? "on the way" : "not possible")");
    }
  }

  // A player by id or name; none given: the last one who made us an offer or
  // answered ours, else the first with a puppet here. Ids go round 2..8 as
  // players come and go, so tests name players rather than guess their id.
  // 0: nobody.
  private func PlayerNamed(text: String) -> Uint32 {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let players: array<Uint32>;
    if IsDefined(system) {
      players = system.PuppetPlayers();
    }
    if StrLen(text) == 0 {
      if this.m_partner != 0u {
        return this.m_partner;
      }
      return ArraySize(players) > 0 ? players[0] : 0u;
    }
    for player in players {
      if Equals(TandemNative.PlayerName(player), text) {
        return player;
      }
    }
    let id = StringToInt(text);
    return id > 0 ? Cast<Uint32>(id) : 0u;
  }
}

// ---- where vanilla fast travels: our own trip ----

@wrapMethod(FastTravelSystem)
private final func PerformFastTravel(player: ref<GameObject>, nodeData: ref<FastTravelPointData>) -> Void {
  let known = IsDefined(nodeData) && this.HasFastTravelPoint(nodeData);
  if known && IsDefined(player) && player.IsPlayer() {
    let travel = TandemTravel.Of(this.GetGameInstance());
    if IsDefined(travel) {
      travel.Departing(nodeData.GetPointRecord(), TandemNative.NodeRefHash(nodeData.GetMarkerRef()));
    }
    // Our world is rebuilt around our V: the session's epoch moves on
    // (docs/architecture/consistency.md, Consistency). Solo: nothing to tell.
    if TandemNative.IsStarted() {
      TandemSync.WorldRebuilt(2);
    }
  }
  wrappedMethod(player, nodeData);
}

// ---- the regroup action: a player's marker on the world map ----

// Held like a fast travel point, it brings us to them.
@wrapMethod(WorldMapMenuGameController)
private final func TryFastTravel() -> Void {
  if this.HasSelectedMappin() {
    let mappin = this.selectedMappin.GetMappin();
    if IsDefined(mappin) && TandemMarkers.IsOurs(mappin.GetVariant()) {
      let player = TandemMarkers.PlayerOf(mappin);
      if player != 0u && TandemTravel.RegroupTo(this.GetOwner().GetGame(), player) {
        this.m_menuEventDispatcher.SpawnEvent(n"OnBack");
      }
      return;
    }
  }
  wrappedMethod();
}

// Its input hint: "Join NAME", where fast travel is possible.
@wrapMethod(WorldMapMenuGameController)
private final func RefreshInputHints() -> Void {
  wrappedMethod();
  let name = "";
  if this.HasSelectedMappin() {
    name = TandemMarkers.NameOf(this.selectedMappin.GetMappin());
  }
  let ours = NotEquals(name, "") && this.GetFastTravelSystem().IsFastTravelEnabled();
  // Solo: no marker of ours, no hint to show or take back.
  if !ours && !TandemNative.IsStarted() {
    return;
  }
  let data: InputHintData;
  data.action = n"world_map_menu_fast_travel";
  data.source = n"TandemRegroup";
  data.localizedLabel = ours ? TandemText.F("Travel-Join", name) : "";

  data.queuePriority = 0;
  data.sortingPriority = 0;
  data.holdIndicationType = inkInputHintHoldIndicationType.Hold;
  let evt = new UpdateInputHintMultipleEvent();
  evt.targetHintContainer = n"WorldMapInputHints";
  evt.AddInputHint(data, ours);
  this.QueueEvent(evt);
}
