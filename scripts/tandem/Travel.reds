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

  private let m_game: GameInstance;
  private let m_you: Uint32;
  private let m_now: Float;
  // Our trip under way: since when, from where, by which point; for whom
  // (0: our own fast travel, offered to the others on arrival).
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
    if value && this.m_trip {
      this.m_loadingEnded = true;
      this.m_loadingEndedAt = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
      TandemNative.Log("travel: the fast travel's loading screen is over");
    }
    return false;
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
    this.m_you = you;
    for item in TandemNative.TakeRegroups() {
      if item.kind == 1 {
        this.Offered(item);
      } else if item.kind == 2 {
        this.Answered(item);
      }
    }
    if IsDefined(this.m_offer) && now - this.m_offer.shownAt > TandemTravel.OfferSeconds() {
      TandemNative.Log(s"travel: offer \(this.m_offer.offer) of \(this.NameOf(this.m_offer.player)) lapsed");
      this.Decline();
    }
    this.FollowTrip();
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
    this.m_tripSince = this.m_now;
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
        this.m_landedAt = this.m_now;
      } else if this.m_now - this.m_tripSince > 60.0 {
        TandemNative.Log("travel: no arrival seen in 60 s, given up");
        this.m_trip = false;
        this.m_joining = 0u;
      }
      return;
    }
    if this.m_loadingEnded {
      let since = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game)) - this.m_loadingEndedAt;
      if since < 1.5 || this.m_now - this.m_landedAt < 1.5 {
        return;
      }
      TandemNative.Log("travel: screen restored (the loading screen ended)");
    } else if this.m_now - this.m_landedAt < 30.0 {
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
      this.Hop(player);
      return;
    }
    if this.m_you == 0u {
      return; // not in a session: nobody to offer it to
    }
    let offer = TandemNative.SendRegroup(0u, 0, this.m_tripPoint, TandemNative.NodeRefOf(this.m_tripMarker), at, local.GetWorldOrientation(), "");
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
    this.Show(offer);
  }

  private func Answered(e: TandemRegroup) -> Void {
    this.m_partner = e.player;
    let name = this.NameOf(e.player);
    if e.accepted {
      TandemNative.Log(s"travel: \(name) is on the way (offer \(e.offer))");
      this.Notice(s"\(name) is on the way", 4.0);
    } else {
      TandemNative.Log(s"travel: \(name) stays where they are (offer \(e.offer))");
      this.Notice(s"\(name) stays where they are", 4.0);
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
    let text: String;
    switch offer.reason {
      case 0:
        text = TDBID.IsValid(offer.point) ? name + " fast travelled to " + this.PointName(offer.point) : name + " fast travelled";
        break;
      case 2:
        text = StrLen(offer.label) > 0 ? name + ": " + offer.label : name + " is starting a scene";
        break;
      case 3:
        text = s"\(name) was taken somewhere else";
        break;
      default:
        text = s"\(name) asks you to join them";
        break;
    }
    this.Notice(text, TandemTravel.OfferSeconds());
    this.Hint(true, s"Join \(name)");
    let local = GetPlayer(this.m_game);
    if IsDefined(local) && !this.m_listening {
      local.RegisterInputListener(this, n"PhoneInteract");
      this.m_listening = true;
      this.m_held = false;
    }
    TandemNative.Log(s"travel: showing offer \(offer.offer) of \(name): \(text)");
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
      this.Notice("You can't fast travel right now", 4.0);
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
    // The game's forward is +Y turned by yaw: (-sin, cos).
    let spot = new Vector4(at.X + SinF(yaw) * 2.0, at.Y - CosF(yaw) * 2.0, at.Z, 1.0);
    let gap = Vector4.Distance(local.GetWorldPosition(), spot);
    if gap > 4.0 {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(local, spot, TandemYawAngles(Rad2Deg(yaw)));
    }
    TandemNative.Log(s"travel: arrived next to \(this.NameOf(player)) (\(gap) m hop)");
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

  // As Pause.reds: the HUD's simple on-screen message.
  // TODO: localized strings (like the pause notice).
  private func Notice(text: String, seconds: Float) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = seconds;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(this.m_game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }

  private func Hint(show: Bool, label: String) -> Void {
    let data: InputHintData;
    data.action = n"PhoneInteract";
    data.source = n"TandemRegroup";
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
  data.localizedLabel = ours ? "Join " + name : "";
  data.queuePriority = 0;
  data.sortingPriority = 0;
  data.holdIndicationType = inkInputHintHoldIndicationType.Hold;
  let evt = new UpdateInputHintMultipleEvent();
  evt.targetHintContainer = n"WorldMapInputHints";
  evt.AddInputHint(data, ours);
  this.QueueEvent(evt);
}
