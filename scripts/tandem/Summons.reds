// A player's summoned car between players (docs/architecture/garage.md,
// row 1a). The game that summoned it owns it: its car drives in on its own
// (the garage's own autodrive), stops by its V and stands there, and every
// other game shows it as that player's car copy from the moment it comes,
// the copy its owner's Seat later seats the puppet in (one body: no copy
// spawned on top, none deleted).
//
// - Ours: the summoned car (VehicleSummonData.SummonedVehicleEntityID) is
//   said (OwnCar: its record, where it stands) when it is another car,
//   when it starts to move with our V on foot (kOwnCarMoving: our
//   VehicleState stream is it while it moves, the stream a car we drive
//   uses, so no new rate), and when it stops (where it stands now). Never
//   while our V sits in a car (then the stream is that car's), nor while a
//   car we bailed from rolls on (Wheels.reds says that one), nor while a
//   partner drives it (lent: the driver's game says where it is).
// - Theirs: each partner's car out gets its copy: the copy of that car we
//   have already (the one its owner drove and left, or this one), else one
//   spawned where its owner says, within kRange of our V. It is that
//   player's parked copy (TandemSystem.ParkCopy): their Seat takes it as
//   their car (Reseat), a partner may drive it by its owner's word, the
//   paint goes on it (Paint.reds). While its owner's stream is it, the copy
//   follows that stream (TandemVehicles.Follow, the kinematic move); when it
//   stops, it is put where its owner says it stands, then left to rest.
// - A car summoned again elsewhere is the same car (the game moves it): the
//   same copy follows, a jump over 8 m a teleport (MoveBody). Another car
//   of theirs: a copy of that one; the old copy stays parked where it was
//   (copies are never deleted, row 1b).
//
// Runs on the game thread (TandemSystem.OnTick). tools/game.sh cmd garage
// cars: what we say and what we show.

public class TandemSummoned {
  public let player: Uint32;
  public let record: TweakDBID;
  public let body: EntityID;
  // The OwnCar change last acted on (RemoteOwnCarChanges).
  public let changes: Int32;
  // Its owner's stream has reached this car (it moves where the stream says).
  public let following: Bool;
  // Put where its owner says it stands until then.
  public let placeUntil: Float;
  // Where its owner's stream was drawn when it last said (W 0: none).
  public let mark: Vector4;
  // A copy could not be spawned: said once.
  public let unseen: Bool;
}

public class TandemSummons {
  private let m_game: GameInstance;
  private let m_you: Uint32;
  private let m_now: Float;
  // Ours as last said.
  private let m_said: Bool;
  private let m_saidCar: EntityID;
  private let m_saidRecord: TweakDBID;
  private let m_saidMoving: Bool;
  private let m_saidAt: Vector4;
  private let m_lastCheck: Float;
  private let m_lastSent: Float;
  private let m_stillSince: Float;
  // Theirs.
  private let m_cars: array<ref<TandemSummoned>>;
  private let m_spawned: Int32;
  private let m_adopted: Int32;

  // A copy is spawned for a partner's car this near our V (as traffic,
  // traffic.md "Budget": shared within 120 m, dropped beyond 150 m).
  public static func Range() -> Float {
    return 150.0;
  }

  // Our car moves (m/s) / is at rest.
  public static func MoveSpeed() -> Float {
    return 0.5;
  }

  public static func RestSpeed() -> Float {
    return 0.3;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_saidAt = new Vector4(0.0, 0.0, 0.0, 0.0);
  }

  // The car our garage summoned, when our game has it (null: none, or
  // streamed out).
  public static func OurCar(game: GameInstance) -> ref<VehicleObject> {
    let board = GameInstance.GetBlackboardSystem(game).Get(GetAllBlackboardDefs().VehicleSummonData);
    if !IsDefined(board) {
      return null;
    }
    let id = board.GetEntityID(GetAllBlackboardDefs().VehicleSummonData.SummonedVehicleEntityID);
    if !EntityID.IsDefined(id) {
      return null;
    }
    return GameInstance.FindEntityByID(game, id) as VehicleObject;
  }

  public func Tick(now: Float, you: Uint32, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    if you != this.m_you {
      // A new session, or its end: nothing said, nothing shown (the copies
      // stay parked, nobody's: TandemSystem.ForgetCarOwners).
      this.m_you = you;
      this.m_said = false;
      this.m_saidMoving = false;
      this.m_saidRecord = TDBID.None();
      this.m_saidCar = EntityID.FromHash(0ul);
      ArrayClear(this.m_cars);
    }
    if you == 0u || !IsDefined(system) {
      return;
    }
    this.SayOurs(system);
    this.ShowTheirs(system);
  }

  // ---- ours ----

  private func Say(car: ref<VehicleObject>, moving: Bool) -> Void {
    let none = TDBID.None();
    let record = IsDefined(car) ? car.GetRecordID() : none;
    let at = IsDefined(car) ? car.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 0.0);
    let facing = IsDefined(car) ? car.GetWorldOrientation() : new Quaternion(0.0, 0.0, 0.0, 1.0);
    TandemNative.SendOwnCar(record, moving ? 1 : 0, at, facing, TandemMods.VehicleHint(record));
    this.m_said = true;
    this.m_saidCar = IsDefined(car) ? car.GetEntityID() : EntityID.FromHash(0ul);
    this.m_saidRecord = record;
    this.m_saidMoving = moving;
    this.m_saidAt = at;
    let player = GetPlayer(this.m_game);
    let from = IsDefined(player) ? player.GetWorldPosition() : at;
    if IsDefined(car) {
      TandemNative.Log(s"summons: our \(TDBID.ToStringDEBUG(record)) is out, \(moving ? "driving in" : "parked") \(Vector4.Distance(at, from)) m from our V");
    } else {
      TandemNative.Log("summons: no car of ours out");
    }
  }

  private func SayOurs(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().VehicleSummonData);
    let id = IsDefined(board) ? board.GetEntityID(GetAllBlackboardDefs().VehicleSummonData.SummonedVehicleEntityID) : EntityID.FromHash(0ul);
    if !EntityID.IsDefined(id) {
      if !this.m_said || TDBID.IsValid(this.m_saidRecord) {
        this.Say(null, false);
      }
      return;
    }
    let car = GameInstance.FindEntityByID(this.m_game, id) as VehicleObject;
    if !IsDefined(car) {
      // Streamed out of our world (our V walked away): the last word stands.
      return;
    }
    system.NoteOwnCar(id);
    let mounted: wref<VehicleObject>;
    let seated = VehicleComponent.GetVehicle(this.m_game, player, mounted) && IsDefined(mounted);
    let wheels = system.Wheels();
    // Its stream is another's now: our seat's car, a car of ours rolling
    // on, a partner at its wheel (lent).
    let busy = seated || (IsDefined(wheels) && wheels.Rolls()) || system.PartnerAtWheel(id);
    let velocity = car.GetLinearVelocity();
    let speed = Vector4.Length(velocity);
    let at = car.GetWorldPosition();
    if id != this.m_saidCar || car.GetRecordID() != this.m_saidRecord {
      this.Say(car, !busy && speed > TandemSummons.MoveSpeed());
      this.m_stillSince = this.m_now;
      return;
    }
    if busy {
      if this.m_saidMoving {
        // (our V got in while it drove in: its Seat and stream take over)
        this.Say(car, false);
      }
      this.m_stillSince = this.m_now;
      return;
    }
    if speed > TandemSummons.RestSpeed() {
      this.m_stillSince = this.m_now;
    }
    if !this.m_saidMoving {
      if speed > TandemSummons.MoveSpeed() {
        this.Say(car, true);
      } else if this.m_now - this.m_lastCheck >= 0.5 {
        // At rest somewhere else than said (we drove it and got out, it was
        // pushed): where it stands now, for late joiners too.
        this.m_lastCheck = this.m_now;
        if this.m_now - this.m_stillSince >= 0.5 && Vector4.Distance(at, this.m_saidAt) > 1.0 {
          this.Say(car, false);
        }
      }
      return;
    }
    // Moving on its own: its transform for the copies, as a driven car's.
    if this.m_now - this.m_stillSince >= 1.0 {
      TandemNative.SendVehicle(at, car.GetWorldOrientation(), new Vector4(0.0, 0.0, 0.0, 0.0));
      this.Say(car, false);
      return;
    }
    if this.m_now - this.m_lastSent >= 0.0333 {
      this.m_lastSent = this.m_now;
      TandemNative.SendVehicle(at, car.GetWorldOrientation(), velocity);
    }
  }

  // ---- theirs ----

  private func Find(player: Uint32) -> ref<TandemSummoned> {
    for car in this.m_cars {
      if car.player == player {
        return car;
      }
    }
    return null;
  }

  private func Forget(player: Uint32) -> Void {
    let kept: array<ref<TandemSummoned>>;
    for car in this.m_cars {
      if car.player != player {
        ArrayPush(kept, car);
      }
    }
    this.m_cars = kept;
  }

  // The copy of a partner's summoned car here (none: no copy).
  public func CopyOf(player: Uint32) -> EntityID {
    let car = this.Find(player);
    return IsDefined(car) ? car.body : EntityID.FromHash(0ul);
  }

  public func RecordOf(player: Uint32) -> TweakDBID {
    let car = this.Find(player);
    return IsDefined(car) ? car.record : TDBID.None();
  }

  private func ShowTheirs(system: ref<TandemSystem>) -> Void {
    let game = this.m_game;
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let here = player.GetWorldPosition();
    let players = system.PuppetPlayers();
    // Gone from the session: no one's car now (its copy stays parked).
    let kept: array<ref<TandemSummoned>>;
    for car in this.m_cars {
      if ArrayContains(players, car.player) {
        ArrayPush(kept, car);
      }
    }
    this.m_cars = kept;
    for other in players {
      let record = TandemNative.RemoteOwnCarVehicle(other);
      let entry = this.Find(other);
      if !TDBID.IsValid(record) {
        if IsDefined(entry) {
          TandemNative.Log(s"summons: player \(other) has no car out: its copy stays parked");
          this.Forget(other);
        }
      } else {
        if IsDefined(entry) && entry.record != record {
          TandemNative.Log(s"summons: player \(other) summoned another car (\(TDBID.ToStringDEBUG(record))): the old copy stays parked");
          this.Forget(other);
          entry = null;
        }
        if !IsDefined(entry) {
          entry = new TandemSummoned();
          entry.player = other;
          entry.record = record;
          entry.body = EntityID.FromHash(0ul);
          entry.changes = -1;
          entry.mark = new Vector4(0.0, 0.0, 0.0, 0.0);
          ArrayPush(this.m_cars, entry);
        }
        this.Show(entry, system, here);
      }
    }
  }

  private func Show(entry: ref<TandemSummoned>, system: ref<TandemSystem>, here: Vector4) -> Void {
    let game = this.m_game;
    let other = entry.player;
    let changes = TandemNative.RemoteOwnCarChanges(other);
    let moving = (TandemNative.RemoteOwnCarFlags(other) % 2) == 1;
    let said = TandemNative.RemoteOwnCarPosition(other);
    said.W = 1.0;
    let facing = TandemNative.RemoteOwnCarOrientation(other);
    let stream = TandemNative.RemoteVehiclePosition(other);
    if changes != entry.changes {
      entry.changes = changes;
      entry.following = false;
      // Where the stream was drawn when its owner said: a stream that is
      // still another car's (its last car, at rest) does not move from there.
      entry.mark = stream;
      if !moving && EntityID.IsDefined(entry.body) {
        entry.placeUntil = this.m_now + 1.0;
        TandemNative.Log(s"summons: player \(other)'s \(TDBID.ToStringDEBUG(entry.record)) parked here, \(Vector4.Distance(said, here)) m from our V");
      }
    }
    // Its owner's stream is this car once it moves, or is drawn where the
    // car started.
    if moving && !entry.following && stream.W > 0.0
      && (Vector4.Distance(stream, said) < 25.0 || entry.mark.W == 0.0 || Vector4.Distance(stream, entry.mark) > 0.5) {
      entry.following = true;
      TandemNative.Log(s"summons: player \(other)'s \(TDBID.ToStringDEBUG(entry.record)) follows its owner's stream, driving in");
    }
    let at = said;
    if moving && entry.following {
      at = stream;
      at.W = 1.0;
    }
    let body = TandemTraffic.Body(game, entry.body);
    if !IsDefined(body) {
      entry.body = EntityID.FromHash(0ul);
      // The copy of that car we have: the one its owner drove (and left),
      // or drives now.
      let last = TandemTraffic.Body(game, system.LastCarOf(other));
      if IsDefined(last) && last.GetRecordID() == entry.record {
        entry.body = last.GetEntityID();
        this.m_adopted += 1;
        TandemNative.Log(s"summons: player \(other)'s \(TDBID.ToStringDEBUG(entry.record)): its copy is the one we have, \(Vector4.Distance(last.GetWorldPosition(), here)) m from our V");
        return;
      }
      if Vector4.Distance(at, here) > TandemSummons.Range() {
        return;
      }
      let id = TandemVehicles.SpawnCopy(entry.record, at, facing);
      if !EntityID.IsDefined(id) {
        if !entry.unseen {
          entry.unseen = true;
          TandemNative.Log(s"summons: player \(other)'s \(TDBID.ToStringDEBUG(entry.record)): no copy could be spawned");
          TandemDegraded.Fallback("summoned car unseen", s"player \(other)'s summoned \(TDBID.ToStringDEBUG(entry.record)): no copy could be spawned");
        }
        return;
      }
      entry.body = id;
      system.ParkCopy(id, other);
      this.m_spawned += 1;
      entry.placeUntil = this.m_now + 1.0;
      TandemNative.Log(s"summons: player \(other)'s \(TDBID.ToStringDEBUG(entry.record)): copy spawned, \(moving ? "driving in" : "parked"), \(Vector4.Distance(at, here)) m from our V");
      return;
    }
    // Its owner drives it, a partner (or our V) sits at its wheel, it rolls
    // on with nobody at the wheel: moved by that (Seat, lent, rollers).
    let wheels = system.Wheels();
    let me = GetPlayer(game);
    if system.CarOf(other) == entry.body || system.PartnerAtWheel(entry.body)
      || (IsDefined(me) && VehicleComponent.IsMountedToProvidedVehicle(game, me.GetEntityID(), body))
      || (IsDefined(wheels) && wheels.FollowsRoller(other)) {
      entry.placeUntil = 0.0;
      return;
    }
    if moving {
      if entry.following {
        TandemVehicles.Follow(game, body, other);
      } else {
        system.MoveBody(system.MoverOf(entry.body), body, said, facing);
      }
      return;
    }
    if this.m_now < entry.placeUntil {
      system.MoveBody(system.MoverOf(entry.body), body, said, facing);
    }
  }

  // tools/game.sh cmd garage cars
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let game = this.m_game;
    let player = GetPlayer(game);
    let here = IsDefined(player) ? player.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 0.0);
    let ours = TandemSummons.OurCar(game);
    let line = s"garage cars: ours \(IsDefined(ours) ? TDBID.ToStringDEBUG(ours.GetRecordID()) : "none")";
    if IsDefined(ours) {
      line += s" \(Vector4.Distance(ours.GetWorldPosition(), here)) m from our V, said \(this.m_saidMoving ? "driving in" : "parked")";
    }
    line += s"; copies spawned \(this.m_spawned), adopted \(this.m_adopted)";
    for car in this.m_cars {
      let body = TandemTraffic.Body(game, car.body);
      let place = IsDefined(body) ? s"\(Vector4.Distance(body.GetWorldPosition(), here)) m" : "no copy";
      line += s"; player \(car.player)'s \(TDBID.ToStringDEBUG(car.record)) \(place)\(car.following ? ", following" : "")";
    }
    TandemNative.Log(line);
  }
}
