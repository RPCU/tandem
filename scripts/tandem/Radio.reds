// The radio (docs/architecture/vehicles.md, "Radio: each V's own on foot, the
// driver's in a car"). On foot each V hears only their own pocket radio:
// nothing is sent, and no remote player's radio plays here (a puppet is an
// NPC, with no pocket radio; a car copy's radio is kept off). In a car the
// driver owns the radio: the driver's game sends it (VehicleRadio) when it
// changes and when a passenger gets in; a passenger's game sets its copy of
// the car to it and keeps its own pocket radio quiet until it gets out.
//
// Vanilla (final.reds, 2.50): the car's radio is VehicleObject's
// IsRadioReceiverActive / GetCurrentRadioIndex (an ERadioStationList index) /
// GetRadioReceiverTrackName, set through a VehicleRadioEvent on the car
// (VehicleComponent.OnVehicleRadioEvent, as the radio popup does) or the
// ToggleRadioReceiver / SetRadioReceiverStation natives. The pocket radio
// (PlayerPuppet.GetPocketRadio) goes quiet under a restriction and comes
// back with its station when the restriction is lifted. No playback position
// is exposed: the station is synced, and the driver's song is asked for on
// ours (AudioSystem.RequestSongOnRadioStation) when it differs.

public class TandemRadio {
  private let m_game: GameInstance;
  private let m_lastPoll: Float;
  // Driver: our car's radio as last sent, when its station last changed,
  // and how many remote players rode with us.
  private let m_sent: Bool;
  private let m_sentOn: Bool;
  private let m_sentStation: Int32;
  private let m_sentTrack: CName;
  private let m_stationSince: Float;
  private let m_riders: Int32;
  // Passenger: the car copy we ride in, its driver, the driver's radio as of
  // RemoteRadioChanges m_applied, retries left, the song asked for.
  private let m_rideCar: EntityID;
  private let m_rideDriver: Uint32;
  private let m_applied: Int32;
  private let m_appliedAt: Float;
  private let m_retries: Int32;
  private let m_requestedTrack: CName;
  private let m_requestedAt: Float;
  private let m_pocketBlocked: Bool;
  // Copies whose radio we turned off, for the log (once each).
  private let m_silenced: array<EntityID>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_applied = -1;
  }

  // The world goes away: our pocket radio back.
  public func Stop() -> Void {
    if EntityID.IsDefined(this.m_rideCar) {
      this.Unride();
    }
    this.m_sent = false;
  }

  // From TandemSystem.Tick; looks four times a second.
  public func Tick(now: Float, system: ref<TandemSystem>) -> Void {
    if now - this.m_lastPoll < 0.25 {
      return;
    }
    this.m_lastPoll = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let car: wref<VehicleObject>;
    let inCar = VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car);
    let owner = inCar ? system.CarOwnerOf(car.GetEntityID()) : 0u;
    // Our own car, at the wheel: its radio is ours to send.
    if inCar && owner == 0u && VehicleComponent.IsDriver(this.m_game, player) {
      this.Drive(car, now, system.Riders());
    } else {
      this.m_sent = false;
      this.m_riders = 0;
    }
    // Another player's car (our copy of it): theirs to play.
    if inCar && owner != 0u {
      this.Ride(car, owner, now);
    } else if EntityID.IsDefined(this.m_rideCar) {
      this.Unride();
    }
    this.SilenceCopies(system.CarCopies());
  }

  // ---- the driver ----

  private func Drive(car: wref<VehicleObject>, now: Float, riders: Int32) -> Void {
    let none = n"None";
    let on = car.IsRadioReceiverActive();
    let station = on ? Cast<Int32>(car.GetCurrentRadioIndex()) : -1;
    let track = on ? car.GetRadioReceiverTrackName() : none;
    let stationChanged = !this.m_sent || NotEquals(on, this.m_sentOn) || station != this.m_sentStation;
    let trackChanged = NotEquals(track, this.m_sentTrack);
    let gotIn = riders > this.m_riders;
    this.m_riders = riders;
    if !stationChanged && !trackChanged && !gotIn {
      return;
    }
    // A new song on a station we have been on for a while: it begins now.
    // (Right after tuning in, the name read may still be the last station's.)
    let started = trackChanged && !stationChanged && IsNameValid(track) && now - this.m_stationSince > 2.0;
    if stationChanged {
      this.m_stationSince = now;
    }
    TandemNative.SendVehicleRadio(on, station, track, started);
    this.m_sent = true;
    this.m_sentOn = on;
    this.m_sentStation = station;
    this.m_sentTrack = track;
    TandemNative.Log(s"radio: our car's radio sent: \(TandemRadio.Describe(on, station))\(gotIn ? ", a passenger got in" : "")\(started ? ", a new song" : "")");
  }

  // "station 4 (radio_station_09_downtempo)", or "off".
  public static func Describe(on: Bool, station: Int32) -> String {
    if !on {
      return "off";
    }
    let name = NameToString(RadioStationDataProvider.GetStationNameByIndex(station));
    return s"station \(station) (\(name))";
  }

  // ---- a passenger ----

  private func Ride(car: wref<VehicleObject>, driver: Uint32, now: Float) -> Void {
    if car.GetEntityID() != this.m_rideCar || driver != this.m_rideDriver {
      let none = n"None";
      this.m_rideCar = car.GetEntityID();
      this.m_rideDriver = driver;
      this.m_applied = -1;
      this.m_requestedTrack = none;
      this.BlockPocket(true);
      TandemNative.Log(s"radio: riding in player \(driver)'s car: our pocket radio quiet");
    }
    let changes = TandemNative.RemoteRadioChanges(driver);
    // Nothing from the driver yet: the car's radio stays off.
    let wantOn = changes > 0 && TandemNative.RemoteRadioOn(driver);
    let wantStation = wantOn ? TandemNative.RemoteRadioStation(driver) : -1;
    let isOn = car.IsRadioReceiverActive();
    let isStation = isOn ? Cast<Int32>(car.GetCurrentRadioIndex()) : -1;
    let matches = wantOn ? (isOn && isStation == wantStation) : !isOn;
    if changes != this.m_applied {
      this.m_applied = changes;
      this.m_appliedAt = now;
      this.m_retries = 2;
      if !matches {
        this.SetRadio(car, wantOn, wantStation, false);
      }
      if wantOn {
        TandemNative.Log(s"radio: player \(driver)'s car set to the driver's \(TandemRadio.Describe(true, wantStation))");
      } else {
        TandemNative.Log(s"radio: player \(driver)'s car radio off, as the driver's");
      }
    } else if !matches && now - this.m_appliedAt > 1.0 {
      // The event did not take, or something turned it back: the natives
      // directly, twice at most (a station index read another way than we
      // think would otherwise restart the stream every second).
      this.m_appliedAt = now;
      let playing = TandemRadio.Describe(isOn, isStation);
      let wanted = TandemRadio.Describe(wantOn, wantStation);
      if this.m_retries > 0 {
        this.m_retries -= 1;
        this.SetRadio(car, wantOn, wantStation, true);
        TandemNative.Log(s"radio: player \(driver)'s car was \(playing), set again to \(wanted)");
      } else if this.m_retries == 0 {
        this.m_retries = -1;
        TandemNative.Log(s"radio: player \(driver)'s car stays \(playing) instead of \(wanted): left so");
      }
    }
    if wantOn && matches {
      this.FollowSong(car, driver, wantStation, now);
    }
  }

  // The driver's song on our copy of the station, when ours plays another.
  // Each game's stations run their own playlists; the game has no seek, so
  // a song asked for at its start on the driver's side begins here within
  // the network delay, and one asked for later is the same song, behind.
  private func FollowSong(car: wref<VehicleObject>, driver: Uint32, station: Int32, now: Float) -> Void {
    let track = TandemNative.RemoteRadioTrack(driver);
    if !IsNameValid(track) {
      return;
    }
    if Equals(track, this.m_requestedTrack) {
      // Once, two seconds after asking: did it take?
      if this.m_requestedAt > 0.0 && now - this.m_requestedAt > 2.0 {
        this.m_requestedAt = 0.0;
        let playing = car.GetRadioReceiverTrackName();
        if Equals(playing, track) {
          TandemNative.Log("radio: the car now plays the driver's song");
        } else {
          TandemNative.Log(s"radio: the car still plays \(NameToString(playing)), not the driver's \(NameToString(track))");
        }
      }
      return;
    }
    this.m_requestedTrack = track;
    this.m_requestedAt = 0.0;
    if Equals(car.GetRadioReceiverTrackName(), track) {
      return;
    }
    let name = RadioStationDataProvider.GetStationNameByIndex(station);
    let age = TandemNative.RemoteRadioTrackAge(driver);
    GameInstance.GetAudioSystem(this.m_game).RequestSongOnRadioStation(name, track);
    this.m_requestedAt = now;
    let when = age >= 0.0 ? s"it began \(age) s ago for them" : "its start unknown";
    TandemNative.Log(s"radio: asked for the driver's song \(NameToString(track)) on \(NameToString(name)) (\(when))");
  }

  private func Unride() -> Void {
    let none = EntityID.FromHash(0ul);
    TandemNative.Log(s"radio: out of player \(this.m_rideDriver)'s car: our pocket radio back");
    this.m_rideCar = none;
    this.m_rideDriver = 0u;
    this.m_applied = -1;
    this.BlockPocket(false);
  }

  // On, at a station (an ERadioStationList index), or off: the way the
  // radio popup does it (QuickSlotsManager.SendRadioEvent), or the natives.
  private func SetRadio(car: wref<VehicleObject>, on: Bool, station: Int32, direct: Bool) -> Void {
    if direct {
      car.ToggleRadioReceiver(on);
      if on {
        car.SetRadioReceiverStation(Cast<Uint32>(station));
      }
      return;
    }
    let event = new VehicleRadioEvent();
    event.toggle = on;
    event.setStation = on;
    event.station = on ? station : -1;
    car.QueueEvent(event);
  }

  // The pocket radio's own vehicle restriction (PocketRadioRestrictions):
  // it turns off, and once lifted (after vanilla's short unlock delay)
  // plays the station it had again, or stays off if it was.
  private func BlockPocket(block: Bool) -> Void {
    if Equals(block, this.m_pocketBlocked) {
      return;
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let pocket = player.GetPocketRadio();
    if IsDefined(pocket) {
      pocket.HandleRestriction(PocketRadioRestrictions.VehicleBlockPocketRadio, block);
    }
    this.m_pocketBlocked = block;
  }

  // ---- the others' cars ----

  // A car copy nobody of ours rides in stays silent: the driver's radio is
  // for those aboard.
  private func SilenceCopies(copies: array<EntityID>) -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    for id in copies {
      if id != this.m_rideCar {
        let car = entities.GetEntity(id) as VehicleObject;
        if IsDefined(car) && car.IsRadioReceiverActive() {
          car.ToggleRadioReceiver(false);
          if !ArrayContains(this.m_silenced, id) {
            ArrayPush(this.m_silenced, id);
            TandemNative.Log(s"radio: a car copy's radio turned off (\(EntityID.ToDebugStringDecimal(id)))");
          }
        }
      }
    }
  }
}
