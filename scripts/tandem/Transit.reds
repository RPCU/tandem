// Scripted transit and quest vehicles (docs/ARCHITECTURE.md, "Scripted
// transit and quest vehicles"; engine-knowledge.md §13.18).
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
// A partner's scene ride with seats (the host's quest vehicle): our V,
// on foot within 20 m of its copy and out of combat, is seated in a free
// passenger or gunner seat of the copy, as the scene would seat a
// companion, and taken out when the ride ends. No free seat: it stays out
// (the story's own rules then: weapons lowered near the scene, the join
// offer from afar).

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
  // Rides we were offered no seat in (player, their ride's change count).
  private let m_noSeatPlayers: array<Uint32>;
  private let m_noSeatChanges: array<Int32>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
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
    let vehicleId: EntityID;
    let seat: CName;
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
    TandemNative.SendTransit(kind, flags, at, next, vehicleId, seat);
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
        if puppet.hidden {
          TandemTransit.Hide(body, false);
          puppet.hidden = false;
        }
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
    if !puppet.hidden {
      TandemTransit.Hide(body, true);
      puppet.hidden = true;
      TandemNative.Log(s"player \(puppet.player): on the NCART away from us, hidden");
    }
    puppet.transitMode = 2;
    if now - puppet.transitPlacedAt >= 0.5 {
      puppet.transitPlacedAt = now;
      let at = TandemNative.RemotePosition(puppet.player);
      if at.W != 0.0 {
        at.W = 1.0;
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, new EulerAngles(0.0, 0.0, Rad2Deg(TandemNative.RemoteYaw(puppet.player))));
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
    let none: EntityID;
    if puppet.transitMode == 1 && EntityID.IsDefined(puppet.transitTrain) {
      if puppet.seatedIn == puppet.transitTrain {
        TandemVehicles.Unseat(this.m_game, body, puppet.transitTrain, puppet.seatedSlot);
        puppet.seatedIn = none;
      }
      puppet.reattachAt = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game)) + 0.5;
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
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, at, new EulerAngles(0.0, 0.0, Rad2Deg(TandemNative.RemoteYaw(puppet.player))));
      }
      if puppet.hidden {
        TandemTransit.Hide(body, false);
        puppet.hidden = false;
      }
    }
    let station = TandemTransit.StationName(puppet.transitAt);
    TandemTransit.Notice(this.m_game, StrLen(station) > 0 ? s"\(puppet.name) got off the NCART at \(station)" : s"\(puppet.name) got off the NCART");
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
      let text = StrLen(toward) > 0 ? s"\(puppet.name) is riding the NCART toward \(toward)" : s"\(puppet.name) is riding the NCART";
      TandemTransit.Notice(this.m_game, skipping ? s"\(puppet.name) is skipping the NCART ride" : text);
      puppet.transitNoticed = true;
      puppet.transitSkipNoticed = skipping;
    }
    TandemNative.Log(s"player \(puppet.player): NCART station \(at), next \(next)\(skipping ? ", skipping" : "")");
  }

  // ---- a partner's scene ride: a seat for our V ----

  private func Invites(system: ref<TandemSystem>, puppets: array<ref<TandemPuppet>>) -> Void {
    let game = this.m_game;
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    // Seated by a ride that has ended (or moved on): out again.
    if this.m_seatedBy != 0u {
      let still = TandemNative.RemoteTransitKind(this.m_seatedBy) == 3 && system.CarOf(this.m_seatedBy) == this.m_seatedCar;
      if !still || !VehicleComponent.IsMountedToVehicle(game, player) {
        if VehicleComponent.IsMountedToVehicle(game, player) {
          TandemVehicles.Unseat(game, player, this.m_seatedCar, this.m_seatedSlot);
          TandemNative.Log(s"ride: player \(this.m_seatedBy)'s scene ride is over, out of its seat");
        }
        this.m_seatedBy = 0u;
      }
      return;
    }
    if VehicleComponent.IsMountedToVehicle(game, player) || player.IsInCombat() {
      return;
    }
    for puppet in puppets {
      if TandemNative.RemoteTransitKind(puppet.player) == 3 && (TandemNative.RemoteTransitFlags(puppet.player) & 2) != 0 {
        this.Offer(system, puppet, player);
      }
    }
  }

  private func Offer(system: ref<TandemSystem>, puppet: ref<TandemPuppet>, player: ref<PlayerPuppet>) -> Void {
    let carId = system.CarOf(puppet.player);
    let car = GameInstance.GetDynamicEntitySystem().GetEntity(carId) as VehicleObject;
    if !IsDefined(car) || Vector4.Distance(car.GetWorldPosition(), player.GetWorldPosition()) > 20.0 {
      return;
    }
    let changes = TandemNative.RemoteTransitChanges(puppet.player);
    let i = ArrayFindFirst(this.m_noSeatPlayers, puppet.player);
    if i >= 0 && this.m_noSeatChanges[i] == changes {
      return; // already told there is no seat for this ride
    }
    let slot = TandemVehicles.FreeSlot(this.m_game, car, n"seat_front_right", player.GetEntityID());
    if !IsNameValid(slot) || VehicleComponent.IsDriverSlot(slot) {
      if i < 0 {
        ArrayPush(this.m_noSeatPlayers, puppet.player);
        ArrayPush(this.m_noSeatChanges, changes);
      } else {
        this.m_noSeatChanges[i] = changes;
      }
      TandemTransit.Notice(this.m_game, s"No free seat in \(puppet.name)'s ride: stay close");
      TandemNative.Log(s"ride: no free seat in player \(puppet.player)'s scene ride");
      return;
    }
    TandemVehicles.Seat(this.m_game, player, car, slot);
    this.m_seatedBy = puppet.player;
    this.m_seatedCar = carId;
    this.m_seatedSlot = slot;
    TandemTransit.Notice(this.m_game, s"You ride with \(puppet.name)");
    TandemNative.Log(s"ride: seated in \(NameToString(slot)) of player \(puppet.player)'s scene ride");
  }

  // ---- helpers ----

  // A body out of sight and back, reversibly (IVisualComponent.TemporaryHide):
  // its meshes and clothes, and what it holds.
  public static func Hide(entity: ref<Entity>, hidden: Bool) -> Void {
    if !IsDefined(entity) {
      return;
    }
    for component in entity.GetComponents() {
      let visual = component as IVisualComponent;
      if IsDefined(visual) {
        visual.TemporaryHide(hidden);
      }
    }
    let body = entity as GameObject;
    if IsDefined(body) {
      let transactions = GameInstance.GetTransactionSystem(body.GetGame());
      for slot in [t"AttachmentSlots.WeaponRight", t"AttachmentSlots.WeaponLeft"] {
        let item = transactions.GetItemInSlot(body, slot);
        if IsDefined(item) {
          for component in item.GetComponents() {
            let visual = component as IVisualComponent;
            if IsDefined(visual) {
              visual.TemporaryHide(hidden);
            }
          }
        }
      }
    }
  }

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

  // The game's own simple on-screen message (as Pause.reds).
  public static func Notice(game: GameInstance, text: String) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 5.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }

  // (tools/game.sh cmd ride) what we ride and what the others ride, for the log.
  public func Report(system: ref<TandemSystem>, puppets: array<ref<TandemPuppet>>) -> Void {
    let player = GetPlayer(this.m_game);
    let car: wref<VehicleObject>;
    let mounted = IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car);
    let slot: CName;
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
      TandemNative.Log(s"ride: player \(puppet.player) kind \(TandemNative.RemoteTransitKind(puppet.player)) flags \(TandemNative.RemoteTransitFlags(puppet.player)) station \(TandemNative.RemoteTransitAt(puppet.player)) next \(TandemNative.RemoteTransitNext(puppet.player)) seat \(NameToString(TandemNative.RemoteTransitSeat(puppet.player))); here mode \(puppet.transitMode), hidden \(puppet.hidden), seated \(EntityID.IsDefined(puppet.seatedIn)), jumps \(TandemNative.RemoteJumps(puppet.player))");
    }
    TandemNative.Log(s"ride: seated by player \(this.m_seatedBy)");
  }
}
