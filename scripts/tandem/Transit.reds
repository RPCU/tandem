// Scripted transit and quest vehicles (docs/architecture/transit.md, "Scripted
// transit and quest vehicles"; engine-knowledge.md §13.27).
//
// Our V's ride, sent as proto::Transit when it changes:
// - Metro: V is mounted on an NCART train (ncartMetroObject, an AVObject
//   the ue_metro quest drives) with the MetroRide status effect tag. Each
//   game that rides runs its own train: no Seat, no VehicleState, no copy
//   (System.reds). The station facts (ue_metro_active_station,
//   ue_metro_next_station: ENcartStations numbers) say where it is.
// - Ride / Scene: V sits in a vehicle our game moves while V does not
//   drive it (a Delamain cab, a car Panam drives, an AV; IsDriver is false
//   for every AVObject): our game sends that vehicle's transform like a
//   driver's (System.SampleLocal), so the others see its copy with us in
//   our seat. Scene when a story scene runs it (the player state machine's
//   vehicle state Scene, tier 3+, or the VehicleScene tag); the host's
//   scene rides offer their free seats to partners near the copy.
// - Skipping (both): the ride's own fast-forward is held
//   (customFFTimeDilation, ten times faster: PlayerPuppet's SceneFastForward
//   input under a CustomFastForward effect).
//
// Another player's metro ride, here: their puppet sits in a train of our
// world when there is one where they are (the same train entity, streamed
// here too, or the one our V rides, near them), else it is hidden, with a
// notice, and placed along their ride every half second so their map
// marker follows. When they get off it comes back where they are: the exit
// station (a fast-forwarded ride is a jump, System.reds).
//
// A partner's scene ride with seats (the host's quest vehicle): our V, on
// foot within 20 m of its copy, up and out of combat, is asked the game's
// way (a notice and two input hints): "Ride with NAME" (the phone key, the
// one co-op offers use) or "Follow" (the reject key held). Nothing chosen
// in 6 s while still near: ride. Walking off (30 m) or getting into a car
// of our own is following.
// - Ride, with a free seat (a passenger or gunner seat nobody holds, never
//   the driver's): seated in it, as the scene would seat a companion.
// - No free seat (two seats: the host and Panam or Johnny; an NPC driving
//   the host): nobody is asked to ride, and nobody rides unseen. A notice
//   says the car is full and we follow (2026-11-27: the hidden carry of
//   before, a V mounted to the copy's trunk slot, is gone).
// - Out of the seat when the ride ends (the host's Transit leaves Scene),
//   its copy goes, or our V goes down. A game loading back up clears the
//   restrictions a save made while carried (before 2026-11-27) kept.
// - Follow (chosen, or no free seat): nothing is done to our V; the host's map marker sits on its car
//   (Markers.reds), and when the host's ride ends the host offers everyone
//   far from it to join (Travel.reds' regroup, "Join NAME").
// An extra seat on our copy only is not done: the host's game owns the car
// and its seats, the copy must look like it (an extra body would sit where
// the host's world has nobody, or in Panam's lap), and the scene could
// later seat someone where we are.

public class TandemTransit {
  private let m_game: GameInstance;
  // Our ride as last sent.
  private let m_sent: Bool;
  private let m_sentKind: Int32;
  private let m_sentFlags: Int32;
  private let m_sentAt: Int32;
  private let m_sentNext: Int32;
  private let m_sentVehicle: EntityID;
  private let m_sentSeat: CName;
  private let m_lastSample: Float;
  private let m_lastInvites: Float;
  // A partner's scene ride we were given a seat in: whose, and where.
  private let m_seatedBy: Uint32;
  private let m_seatedCar: EntityID;
  private let m_seatedSlot: CName;
  // Rides we answered (player, their ride's change count): not asked again.
  private let m_answeredPlayers: array<Uint32>;
  private let m_answeredChanges: array<Int32>;
  // The question on screen: whose ride, since when, the input listened to.
  private let m_askedBy: Uint32;
  private let m_askedAt: Float;
  private let m_listening: Bool;
  private let m_held: Bool;
  private let m_now: Float;
  // Our last ride was the host's scene ride with seats: its end offers the
  // far ones to join us.
  private let m_sceneSent: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    // A save made while carried along (before 2026-11-27) may have kept
    // its restrictions.
    let player = GetPlayer(game);
    if IsDefined(player) {
      TandemTransit.Restrict(player, false);
    }
  }

  // The session is over (or the world goes).
  public func End() -> Void {
    this.Ask(0u);
    this.m_seatedBy = 0u;
    ArrayClear(this.m_answeredPlayers);
    ArrayClear(this.m_answeredChanges);
  }

  // ---- what our V rides ----

  // The NCART: the train class, or the status effect the metro ride puts on
  // V (pocket radio and HUD code check it the same way).
  public static func IsMetro(game: GameInstance, player: ref<GameObject>, vehicle: ref<VehicleObject>) -> Bool {
    return IsDefined(vehicle) && (IsDefined(vehicle as ncartMetroObject) || StatusEffectSystem.ObjectHasStatusEffectWithTag(player, n"MetroRide"));
  }

  // The vehicle our V sits in and our game moves for it while it does not
  // drive (Ride, Scene): not a copy of another player's car, not the metro.
  public static func Carried(game: GameInstance, system: ref<TandemSystem>) -> ref<VehicleObject> {
    let player = GetPlayer(game);
    let car: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(game, player, car) || !IsDefined(car) {
      return null;
    }
    if system.CarOwnerOf(car.GetEntityID()) != 0u || TandemTransit.IsMetro(game, player, car) || VehicleComponent.IsDriver(game, player) {
      return null;
    }
    return car;
  }

  // In a story scene's vehicle: the player state machine's vehicle state
  // (gamePSMVehicle.Scene = 7), a cinematic tier, or the scene's tag.
  private func InScene(player: ref<PlayerPuppet>) -> Bool {
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let vehicleState = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vehicle) : 0;
    return vehicleState == 7 || PlayerPuppet.GetSceneTier(player) >= 3 || StatusEffectSystem.ObjectHasStatusEffectWithTag(player, n"VehicleScene");
  }

  // Four times a second: our ride, sent when it changes (and once at the
  // start, so a game back from a crash says it rides nothing).
  private func SampleOurs(now: Float, system: ref<TandemSystem>) -> Void {
    if this.m_sent && now - this.m_lastSample < 0.25 {
      return;
    }
    this.m_lastSample = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let kind = 0;
    let flags = 0;
    let at = 0;
    let next = 0;
    let vehicleId = EntityID.FromHash(0ul);
    let seat = n"None";
    let carrier = 0u;
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
      if TandemTransit.IsMetro(this.m_game, player, car) {
        kind = 1;
        let quests = GameInstance.GetQuestsSystem(this.m_game);
        at = quests.GetFact(n"ue_metro_active_station");
        next = quests.GetFact(n"ue_metro_next_station");
      } else if IsDefined(TandemTransit.Carried(this.m_game, system)) {
        kind = this.InScene(player) ? 3 : 2;
        // The host's story scenes are the session's: partners get a seat.
        if kind == 3 && TandemNative.IsHost() {
          flags += 2;
        }
      }
      if kind != 0 {
        vehicleId = car.GetEntityID();
        VehicleComponent.GetMountedSlotName(this.m_game, player, seat);
        if GameInstance.GetTimeSystem(this.m_game).IsTimeDilationActive(n"customFFTimeDilation") {
          flags += 1;
        }
      }
    }
    if this.m_sent && kind == this.m_sentKind && flags == this.m_sentFlags && at == this.m_sentAt && next == this.m_sentNext
      && vehicleId == this.m_sentVehicle && Equals(seat, this.m_sentSeat) {
      return;
    }
    TandemNative.SendTransit(kind, flags, at, next, vehicleId, seat, carrier);
    // The host's scene ride is over: whoever followed from afar is offered
    // to join us (Travel.reds shows it only to the far ones).
    let scene = kind == 3 && (flags & 2) != 0;
    if this.m_sceneSent && !scene {
      // The label goes over the wire as a key; each game shows it in its language.
      TandemTravel.OfferRegroup(this.m_game, 0u, 2, "Transit-RideOver");

      TandemNative.Log("ride: our scene ride is over, the far ones are offered to join");
    }
    this.m_sceneSent = scene;
    if this.m_sent || kind != 0 {
      TandemNative.Log(s"our ride: kind \(kind), flags \(flags), station \(at), next \(next), seat \(NameToString(seat)), vehicle \(EntityID.ToDebugStringDecimal(vehicleId))");
    }
    this.m_sent = true;
    this.m_sentKind = kind;
    this.m_sentFlags = flags;
    this.m_sentAt = at;
    this.m_sentNext = next;
    this.m_sentVehicle = vehicleId;
    this.m_sentSeat = seat;
  }

  // The kind of ride our V is on (0 none), as last sent.
  public func OurKind() -> Int32 {
    return this.m_sentKind;
  }

  // ---- every frame ----

  public func Tick(now: Float, system: ref<TandemSystem>, puppets: array<ref<TandemPuppet>>) -> Void {
    this.m_now = now;
    this.SampleOurs(now, system);
    for puppet in puppets {
      this.Follow(now, system, puppet);
    }
    if now - this.m_lastInvites >= 0.5 {
      this.m_lastInvites = now;
      this.Invites(system, puppets);
    }
  }

  // ---- another player's metro ride ----

  private func Follow(now: Float, system: ref<TandemSystem>, puppet: ref<TandemPuppet>) -> Void {
    let changes = TandemNative.RemoteTransitChanges(puppet.player);
    let metro = TandemNative.RemoteTransitKind(puppet.player) == 1;
    if changes != puppet.transitChanges {
      puppet.transitChanges = changes;
      if metro {
        this.Station(puppet);
      }
    }
    if metro {
      this.Ride(now, system, puppet);
    } else if puppet.transitMode != 0 {
      this.GetOff(now, puppet);
    }
  }

  // On its train: in one of ours if there is one where they are, else
  // hidden and carried along.
  private func Ride(now: Float, system: ref<TandemSystem>, puppet: ref<TandemPuppet>) -> Void {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if !IsDefined(body) {
      return;
    }
    let train = this.TrainFor(puppet);
    if IsDefined(train) && (puppet.transitMode != 1 || train.GetEntityID() != puppet.transitTrain) {
      let slot = TandemVehicles.FreeSlot(this.m_game, train, TandemNative.RemoteTransitSeat(puppet.player), body.GetEntityID());
      if IsNameValid(slot) {
        this.OffTrain(puppet, body);
        TandemVisibility.Show(this.m_game, puppet.player, n"metro", 0.3);
        TandemVehicles.Seat(this.m_game, body, train, slot);
        puppet.transitMode = 1;
        puppet.transitTrain = train.GetEntityID();
        puppet.seatedIn = puppet.transitTrain;
        puppet.seatedSlot = slot;
        puppet.controlled = false;
        TandemNative.Log(s"player \(puppet.player): on the NCART with us, seat \(NameToString(slot)) of train \(EntityID.ToDebugStringDecimal(puppet.transitTrain))");
        return;
      }
    }
    if IsDefined(train) && puppet.transitMode == 1 {
      return; // seated, and the train is still where they are
    }
    // No train of ours there: out of sight, along their ride.
    if puppet.transitMode == 1 {
      this.OffTrain(puppet, body);
    }
    if puppet.transitMode != 2 {
      TandemVisibility.Hide(this.m_game, puppet.player, n"metro", 0.3);
      TandemNative.Log(s"player \(puppet.player): on the NCART away from us, hidden");
    }
    puppet.transitMode = 2;
    if now - puppet.transitPlacedAt >= 0.5 {
      puppet.transitPlacedAt = now;
      let at = TandemNative.RemotePosition(puppet.player);
      if at.W != 0.0 {
        at.W = 1.0;
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, TandemYawAngles(Rad2Deg(TandemNative.RemoteYaw(puppet.player))));
      }
    }
  }

  // A train of our world where the rider is: the entity they ride, when it
  // is streamed here too (a placed or quest train has the same id in every
  // game) and near them, or the train our own V rides, near them. Once
  // seated, kept while it stays within 300 m (two games' trains run on
  // their own clocks).
  private func TrainFor(puppet: ref<TandemPuppet>) -> ref<VehicleObject> {
    let spot = TandemNative.RemotePosition(puppet.player);
    if spot.W == 0.0 {
      return null;
    }
    let keep = puppet.transitMode == 1 ? 300.0 : 0.0;
    let theirs = GameInstance.FindEntityByID(this.m_game, TandemNative.RemoteTransitVehicle(puppet.player)) as ncartMetroObject;
    if IsDefined(theirs) && Vector4.Distance(theirs.GetWorldPosition(), spot) < MaxF(keep, 60.0) {
      return theirs;
    }
    let player = GetPlayer(this.m_game);
    let ours: wref<VehicleObject>;
    if IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, ours) && TandemTransit.IsMetro(this.m_game, player, ours)
      && Vector4.Distance(ours.GetWorldPosition(), spot) < MaxF(keep, 150.0) {
      return ours;
    }
    return null;
  }

  private func OffTrain(puppet: ref<TandemPuppet>, body: ref<GameObject>) -> Void {
    let none = EntityID.FromHash(0ul);
    if puppet.transitMode == 1 && EntityID.IsDefined(puppet.transitTrain) {
      if puppet.seatedIn == puppet.transitTrain {
        TandemVehicles.Unseat(this.m_game, body, puppet.transitTrain, puppet.seatedSlot);
        puppet.seatedIn = none;
      }
      // On TandemSystem's clock, which compares it: real time.
      puppet.reattachAt = TandemClock.Real(this.m_game) + 0.5;
    }
    puppet.transitTrain = none;
  }

  // Off the train: seen again where they are (the exit station).
  private func GetOff(now: Float, puppet: ref<TandemPuppet>) -> Void {
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as GameObject;
    if IsDefined(body) {
      this.OffTrain(puppet, body);
      let at = TandemNative.RemotePosition(puppet.player);
      if at.W != 0.0 {
        at.W = 1.0;
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, TandemYawAngles(Rad2Deg(TandemNative.RemoteYaw(puppet.player))));
      }
    }
    TandemVisibility.Show(this.m_game, puppet.player, n"metro", 0.3);
    let station = TandemTransit.StationName(puppet.transitAt);
    TandemTransit.Notice(this.m_game, StrLen(station) > 0 ? "Transit-GotOffAt" : "Transit-GotOff", puppet.name, station);
    TandemNative.Log(s"player \(puppet.player): off the NCART (station \(puppet.transitAt))");
    puppet.transitMode = 0;
    puppet.transitNoticed = false;
    puppet.transitAt = 0;
  }

  // The rider's stations changed: a notice when the ride begins or skips.
  private func Station(puppet: ref<TandemPuppet>) -> Void {
    let at = TandemNative.RemoteTransitAt(puppet.player);
    let next = TandemNative.RemoteTransitNext(puppet.player);
    if at > 0 {
      puppet.transitAt = at;
    }
    let skipping = (TandemNative.RemoteTransitFlags(puppet.player) & 1) != 0;
    if !puppet.transitNoticed || (skipping && !puppet.transitSkipNoticed) {
      let toward = TandemTransit.StationName(next);
      let key = StrLen(toward) > 0 ? "Transit-RidingToward" : "Transit-Riding";
      TandemTransit.Notice(this.m_game, skipping ? "Transit-Skipping" : key, puppet.name, toward);
      puppet.transitNoticed = true;
      puppet.transitSkipNoticed = skipping;
    }
    TandemNative.Log(s"player \(puppet.player): NCART station \(at), next \(next)\(skipping ? ", skipping" : "")");
  }

  // ---- a partner's scene ride: a seat for our V, or follow ----

  private func Invites(system: ref<TandemSystem>, puppets: array<ref<TandemPuppet>>) -> Void {
    let game = this.m_game;
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let down = TandemTransit.Down(game);
    // Seated by a ride that has ended (or moved on): out again.
    if this.m_seatedBy != 0u {
      let still = TandemNative.RemoteTransitKind(this.m_seatedBy) == 3 && system.CarOf(this.m_seatedBy) == this.m_seatedCar;
      if !still || !VehicleComponent.IsMountedToVehicle(game, player) || down {
        if VehicleComponent.IsMountedToVehicle(game, player) {
          TandemVehicles.Unseat(game, player, this.m_seatedCar, this.m_seatedSlot);
          TandemNative.Log(s"ride: player \(this.m_seatedBy)'s scene ride is over, out of its seat");
        }
        this.m_seatedBy = 0u;
      }
      return;
    }
    // The question on screen: lapsed (the ride ended), followed (we walked
    // off or got into a car of ours), or ridden (nothing chosen in time).
    if this.m_askedBy != 0u {
      let asker = this.m_askedBy;
      let askedCar = TandemTraffic.Body(this.m_game, system.CarOf(asker));
      let open = TandemNative.RemoteTransitKind(asker) == 3 && (TandemNative.RemoteTransitFlags(asker) & 2) != 0 && IsDefined(askedCar);
      if !open {
        TandemNative.Log(s"ride: player \(asker)'s ride ended before we chose");
        this.Ask(0u);
      } else if down || VehicleComponent.IsMountedToVehicle(game, player) || Vector4.Distance(askedCar.GetWorldPosition(), player.GetWorldPosition()) > 30.0 {
        this.FollowOnOwn(asker, down ? "down" : "on our own");
      } else if this.m_now - this.m_askedAt >= 6.0 {
        this.Board(system, asker);
      }
      return;
    }
    // Watching the host's moment (Spectate.reds): our V is parked, its ride
    // is watched through the host's camera.
    if VehicleComponent.IsMountedToVehicle(game, player) || player.IsInCombat() || down || TandemStory.SpectateWatching() {
      return;
    }
    for puppet in puppets {
      if TandemNative.RemoteTransitKind(puppet.player) == 3 && (TandemNative.RemoteTransitFlags(puppet.player) & 2) != 0 {
        let car = TandemTraffic.Body(this.m_game, system.CarOf(puppet.player));
        if IsDefined(car) && Vector4.Distance(car.GetWorldPosition(), player.GetWorldPosition()) <= 20.0 && !this.Answered(puppet.player) {
          // A seat or nothing: a full car asks nobody to ride.
          if IsNameValid(this.SeatIn(car)) {
            this.Ask(puppet.player);
          } else {
            this.Full(puppet.player, "no free seat");
          }
        }
      }
    }
  }

  // This ride of `player` was answered already.
  private func Answered(player: Uint32) -> Bool {
    let i = ArrayFindFirst(this.m_answeredPlayers, player);
    return i >= 0 && this.m_answeredChanges[i] == TandemNative.RemoteTransitChanges(player);
  }

  private func Answer(player: Uint32) -> Void {
    let changes = TandemNative.RemoteTransitChanges(player);
    let i = ArrayFindFirst(this.m_answeredPlayers, player);
    if i < 0 {
      ArrayPush(this.m_answeredPlayers, player);
      ArrayPush(this.m_answeredChanges, changes);
    } else {
      this.m_answeredChanges[i] = changes;
    }
  }

  // The question, the game's way: a notice and two input hints (0: none).
  private func Ask(player: Uint32) -> Void {
    let local = GetPlayer(this.m_game);
    if this.m_askedBy != 0u {
      this.Hint(n"PhoneInteract", false, "", inkInputHintHoldIndicationType.Press);
      this.Hint(n"PhoneReject", false, "", inkInputHintHoldIndicationType.Hold);
    }
    if this.m_listening && IsDefined(local) {
      local.UnregisterInputListener(this, n"PhoneInteract");
      local.UnregisterInputListener(this, n"PhoneReject");
      this.m_listening = false;
    }
    this.m_askedBy = player;
    if player == 0u {
      return;
    }
    this.m_askedAt = this.m_now;
    this.m_held = false;
    let name = this.NameOf(player);
    TandemTransit.Notice(this.m_game, "Transit-Leaving", name);
    this.Hint(n"PhoneInteract", true, TandemText.F("Transit-RideWith", name), inkInputHintHoldIndicationType.Press);
    this.Hint(n"PhoneReject", true, TandemText.Get("Transit-Follow"), inkInputHintHoldIndicationType.Hold);
    if IsDefined(local) {
      local.RegisterInputListener(this, n"PhoneInteract");
      local.RegisterInputListener(this, n"PhoneReject");
      this.m_listening = true;
    }
    TandemNative.Log(s"ride: asked to ride with player \(player) or follow");
  }

  // The phone key pressed: ride. The reject key held: follow. (The phone's
  // own hold on the same key opens the phone: not for us.)
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Transit.TandemTransit.OnAction");
    if this.m_askedBy == 0u {
      return false;
    }
    let name = ListenerAction.GetName(action);
    let kind = ListenerAction.GetType(action);
    if Equals(name, n"PhoneInteract") {
      if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
        this.m_held = true;
      } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
        if this.m_held {
          this.m_held = false;
        } else {
          this.Choose(true);
        }
      }
    } else if Equals(name, n"PhoneReject") && Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.Choose(false);
    }
    return false;
  }

  // (the input, or tools/game.sh cmd ride along | ride follow)
  public func Choose(ride: Bool) -> Void {
    let asker = this.m_askedBy;
    if asker == 0u {
      TandemNative.Log("ride: nothing to choose");
      return;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if ride && IsDefined(system) {
      this.Board(system, asker);
    } else {
      this.FollowOnOwn(asker, "chosen");
    }
  }

  private func FollowOnOwn(asker: Uint32, why: String) -> Void {
    this.Answer(asker);
    this.Ask(0u);
    let name = this.NameOf(asker);
    TandemTransit.Notice(this.m_game, "Transit-Following", name);
    TandemNative.Log(s"ride: following player \(asker) (\(why))");
  }

  // A free seat of the copy for our V (never the driver's; n"None": none).
  private func SeatIn(car: ref<VehicleObject>) -> CName {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || !IsDefined(car) {
      return n"None";
    }
    let slot = TandemVehicles.FreeSlot(this.m_game, car, n"seat_front_right", player.GetEntityID());
    return IsNameValid(slot) && !VehicleComponent.IsDriverSlot(slot) ? slot : n"None";
  }

  // No seat for us: we follow, told so (the regroup at the ride's end
  // brings us to where it went).
  private func Full(asker: Uint32, why: String) -> Void {
    this.Answer(asker);
    this.Ask(0u);
    TandemTransit.Notice(this.m_game, "Transit-Full", this.NameOf(asker));
    TandemNative.Log(s"ride: player \(asker)'s scene ride has no free seat: following (\(why))");
  }

  // Ride: a free seat of the copy (never the driver's), else follow.
  private func Board(system: ref<TandemSystem>, asker: Uint32) -> Void {
    this.Answer(asker);
    this.Ask(0u);
    let player = GetPlayer(this.m_game);
    let carId = system.CarOf(asker);
    let car = TandemTraffic.Body(this.m_game, carId);
    if !IsDefined(player) || !IsDefined(car) {
      return;
    }
    let name = this.NameOf(asker);
    let slot = this.SeatIn(car);
    if IsNameValid(slot) {
      TandemVehicles.Seat(this.m_game, player, car, slot);
      this.m_seatedBy = asker;
      this.m_seatedCar = carId;
      this.m_seatedSlot = slot;
      TandemTransit.Notice(this.m_game, "Transit-YouRide", name);
      TandemNative.Log(s"ride: seated in \(NameToString(slot)) of player \(asker)'s scene ride");
      return;
    }
    this.Full(asker, "its last seat taken before we chose");
  }

  // The slot that carries a V watching the host's view (Spectate.reds):
  // not a seat.
  public static func CarrySlot() -> CName {
    return n"trunk_body";
  }

  // The restrictions a V carried along wore (status effects, removed by
  // record): cleared when a game loads, for a save made while carried.
  public static func Restrict(player: ref<GameObject>, on: Bool) -> Void {
    if !IsDefined(player) {
      return;
    }
    let records = [
      t"GameplayRestriction.NoMovement", t"GameplayRestriction.NoJump", t"GameplayRestriction.NoWeapons",
      // NoWeapons only keeps a weapon from being drawn; NoCombat also puts
      // one already in hand away (ForceEmptyHands, final.reds:540418): a V
      // carried along never fights from a seat it does not have.
      t"GameplayRestriction.NoCombat",
      t"GameplayRestriction.NoWorldInteractions", t"GameplayRestriction.VehicleNoInteraction",
      t"GameplayRestriction.ForceCrouch", t"BaseStatusEffect.Invulnerable"
    ];
    for record in records {
      if on {
        StatusEffectHelper.ApplyStatusEffect(player, record);
      } else {
        StatusEffectHelper.RemoveStatusEffect(player, record);
      }
    }
  }

  // Our V is down (Revive.reds): never carried.
  private static func Down(game: GameInstance) -> Bool {
    let revive = TandemRevive.Of(game);
    return IsDefined(revive) && revive.IsDown();
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

  private func Hint(action: CName, show: Bool, label: String, hold: inkInputHintHoldIndicationType) -> Void {
    TandemText.Hint(this.m_game, n"TandemRide", action, show, label, hold);
  }

  // ---- helpers ----

  // An NCART station's name (ENcartStations number, as the metro's own
  // screens name them: NcartTrainInkControllerBase); "" when unknown.
  public static func StationName(station: Int32) -> String {
    let keys = [
      "LocKey#95277", "LocKey#95260", "LocKey#95261", "LocKey#95262", "LocKey#95259",
      "LocKey#95263", "LocKey#95264", "LocKey#95265", "LocKey#95266", "LocKey#95267",
      "LocKey#95268", "LocKey#95269", "LocKey#95270", "LocKey#95271", "LocKey#95272",
      "LocKey#95273", "LocKey#95274", "LocKey#95275", "LocKey#95276"
    ];
    if station < 1 || station > ArraySize(keys) {
      return "";
    }
    let key = keys[station - 1];
    return GetLocalizedText(key);
  }

  // The HUD's warning line (Text.reds).
  public static func Notice(game: GameInstance, key: String, opt a: String, opt b: String) -> Void {
    TandemText.Notice(game, key, a, b);
  }


  // (tools/game.sh cmd ride) what we ride and what the others ride, for the log.
  public func Report(system: ref<TandemSystem>, puppets: array<ref<TandemPuppet>>) -> Void {
    let player = GetPlayer(this.m_game);
    let car: wref<VehicleObject>;
    let mounted = IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car);
    let slot = n"None";
    if mounted {
      VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
    }
    let detail = "";
    if mounted {
      detail = s" in \(NameToString(car.GetClassName())) \(TDBID.ToStringDEBUG(car.GetRecordID())) seat \(NameToString(slot)), metro \(TandemTransit.IsMetro(this.m_game, player, car)), driver \(VehicleComponent.IsDriver(this.m_game, player)), scene animation \(car.IsPerformingSceneAnimation())";
    }
    TandemNative.Log(s"ride: ours kind \(this.m_sentKind) flags \(this.m_sentFlags) station \(this.m_sentAt) next \(this.m_sentNext); mounted \(mounted)\(detail)");
    if mounted {
      let seats: array<wref<VehicleSeat_Record>>;
      VehicleComponent.GetSeats(this.m_game, car, seats);
      let names = "";
      for seat in seats {
        names += s" \(NameToString(seat.SeatName()))\(VehicleComponent.IsSlotOccupied(this.m_game, car.GetEntityID(), seat.SeatName()) ? "(taken)" : "")";
      }
      TandemNative.Log(s"ride: its seats:\(names)");
    }
    for puppet in puppets {
      TandemNative.Log(s"ride: player \(puppet.player) kind \(TandemNative.RemoteTransitKind(puppet.player)) flags \(TandemNative.RemoteTransitFlags(puppet.player)) station \(TandemNative.RemoteTransitAt(puppet.player)) next \(TandemNative.RemoteTransitNext(puppet.player)) seat \(NameToString(TandemNative.RemoteTransitSeat(puppet.player))); here mode \(puppet.transitMode), hidden \(TandemVisibility.IsHidden(this.m_game, puppet.player)), seated \(EntityID.IsDefined(puppet.seatedIn))");
    }
    TandemNative.Log(s"ride: seated by player \(this.m_seatedBy), asked by player \(this.m_askedBy)");
  }
}
