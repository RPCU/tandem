// Vehicle combat (docs/architecture/vehicle-combat.md). The game that
// simulates a vehicle owns it, its damage included: our car is ours (the
// one we drive, and the last one we drove, parked), a player's car is a copy
// here, a shared world vehicle is its area owner's (Traffic.reds).
//
// - Damage: our game reads how our car stands (health, fire, wreck, flat
//   tyres, dents, parts off) and says so when it changes; every copy is put
//   the same way, blown up the game's way when it hears at once, made a
//   wreck when it hears later. A hit on a copy is sent to its owner, which
//   applies it to the real car (TandemWheels.SendHit).
// - In a vehicle: how we use weapons there (driver, passenger, the
//   vehicle's own guns, a gunner seat), the vehicle's own shots, the gunner
//   seat of someone else's vehicle (asked for, given by its owner), our aim.
// - Leaving the hard way: jumping out on the move, thrown out, knocked off a
//   bike, dead, blown out; a car left on the move rolls on, still ours.
// - Collisions: our car running into an NPC we do not own (the ram goes to
//   its owner), two players' cars touching (each runs its own car's side,
//   the push goes to the other; vehiclecombat::BumpLedger), and what a copy
//   of someone else's car does here: nothing to our world (its driver's game
//   decides).
// - Chases: the vehicles our game runs chasing a player (the game's own
//   InVehicleCombatDecorator drives after its NPCs' combat target).
// - The owner's turret for a remote gunner: a gunner seat of a vehicle we
//   run, held by another player, fires our real vehicle's guns at the
//   gunner's aim (VehicleObject.NPCShoot) so everyone sees and hears it;
//   what those shots hit here is the gunner's game's to decide, never ours
//   (CausedElsewhere). Turning it to the gunner's aim between shots is a
//   probe (`wheels turret PART`, research/engine/vehicles.md §13.70).
// - Passenger combat in a partner's car copy: vanilla's checks are the
//   record's (§13.70); a puppet the game reads as not friendly must not cost
//   us our weapons in that seat (PassengerEvents.OnEnter).

public class TandemWheelsCopy {
  // A vehicle another game owns, as we put its body.
  public let vehicle: Uint32;
  public let body: EntityID;
  public let tyres: Int32;
  public let wrecked: Bool;
  public let parts: array<CName>;
}

public class TandemWheelsRoller {
  // Another player's car rolling on with nobody at the wheel: its copy
  // follows its driver's game's states a little longer.
  public let player: Uint32;
  public let body: EntityID;
  public let since: Float;
}

public class TandemWheelsSync {
  private let m_game: GameInstance;
  private let m_you: Uint32;
  private let m_now: Float;
  // Our car: the one we drive or last drove, and how we last said it stands.
  private let m_own: wref<VehicleObject>;
  private let m_ownRef: Uint32;
  private let m_sentHealth: Float;
  private let m_sentFlags: Int32;
  private let m_sentTyres: Int32;
  private let m_sent: Bool;
  private let m_dirty: Bool;
  private let m_lastSample: Float;
  // Copies of the others' vehicles as we put them.
  private let m_copies: array<ref<TandemWheelsCopy>>;
  private let m_lastCheck: Float;
  // Our car rolling on after we left it on the move; others' cars rolling.
  private let m_rolling: wref<VehicleObject>;
  private let m_rollingSince: Float;
  private let m_lastRoll: Float;
  private let m_rollers: array<ref<TandemWheelsRoller>>;
  // Our weapons in a vehicle, as last said; when our car last fired its own.
  private let m_sentMode: Int32;
  private let m_sentWeapon: TweakDBID;
  private let m_sentModeVehicle: Uint32;
  private let m_mountedAt: Float;
  // The gunner seat we asked for (0: none) and when we last sent our aim.
  private let m_gunnerOf: Uint32;
  private let m_gunnerSlot: CName;
  private let m_lastAim: Float;
  // Bumps already told, by player: one per contact.
  private let m_bumpPlayers: array<Uint32>;
  private let m_bumpAt: array<Float>;
  // Area owner: the chases we said, by shared vehicle.
  private let m_chaseIds: array<Uint32>;
  private let m_chaseTargets: array<Uint32>;
  // Others: the chases after us (for the report).
  private let m_chasedBy: array<Uint32>;
  // Our vehicles' guns fired for a remote gunner, and when (their hits are
  // the gunner's game's: CausedElsewhere); the probe's look-at part and its
  // last request; shots played for gunners.
  private let m_gunnerFiredBody: array<EntityID>;
  private let m_gunnerFiredAt: array<Float>;
  private let m_turretPart: CName;
  private let m_turretLook: ref<LookAtAddEvent>;
  private let m_turretLookOn: wref<VehicleObject>;
  private let m_lastTurret: Float;
  private let m_turretShots: Int32;
  // Counts for tools/game.sh cmd wheels.
  private let m_hitsSent: Int32;
  private let m_hitsTaken: Int32;
  private let m_rams: Int32;
  private let m_pushes: Int32;
  private let m_dropped: Int32;

  // 2 to the power `n`, for the bit of tyre `n`.
  public static func Bit(n: Int32) -> Int32 {
    let bit = 1;
    let i = 0;
    while i < n {
      bit *= 2;
      i += 1;
    }
    return bit;
  }

  public static func Of(game: GameInstance) -> ref<TandemWheelsSync> {
    let system = TandemSystem.Of(game);
    return IsDefined(system) ? system.Wheels() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func InSession() -> Bool {
    return this.m_you != 0u;
  }

  public func Tick(now: Float, you: Uint32, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    if you != this.m_you {
      // A new session: our car is named by our new player id, said again.
      this.m_you = you;
      this.m_sent = false;
      this.m_sentMode = 0;
    }
    if you == 0u {
      return;
    }
    this.TakeEvents(system);
    this.Pushes();
    if now - this.m_lastSample >= 0.25 || this.m_dirty {
      this.m_lastSample = now;
      this.m_dirty = false;
      this.SampleOwn(system);
      this.SampleCombat(system);
    }
    this.Roll();
    this.FollowRollers(system);
    this.Aim();
    this.Turret(system);
    if now - this.m_lastCheck >= 1.0 {
      this.m_lastCheck = now;
      this.Check(system);
    }
  }

  // The session is over: copies stay as they are (they are parked cars now).
  public func End() -> Void {
    ArrayClear(this.m_copies);
    ArrayClear(this.m_rollers);
    ArrayClear(this.m_bumpPlayers);
    ArrayClear(this.m_bumpAt);
    ArrayClear(this.m_chaseIds);
    ArrayClear(this.m_chaseTargets);
    ArrayClear(this.m_chasedBy);
    ArrayClear(this.m_gunnerFiredBody);
    ArrayClear(this.m_gunnerFiredAt);
    this.StopTurretLook();
    this.m_rolling = null;
    this.m_gunnerOf = 0u;
    this.m_sent = false;
    this.m_sentMode = 0;
    this.m_you = 0u;
  }

  // (test, tools/game.sh cmd wheels) what we know.
  public func Report() -> String {
    let own = IsDefined(this.m_own) ? TDBID.ToStringDEBUG(this.m_own.GetRecordID()) : "none";
    return s"wheels: our car \(own) (health \(this.m_sentHealth), flags \(this.m_sentFlags), tyres \(this.m_sentTyres)), \(ArraySize(this.m_copies)) damaged copies, \(this.m_hitsSent) hits sent, \(this.m_hitsTaken) taken, \(this.m_rams) rams, \(this.m_pushes) pushes, \(this.m_dropped) remote collisions dropped, gunner of \(this.m_gunnerOf), \(ArraySize(this.m_chasedBy)) chasing us, \(ArraySize(this.m_chaseIds)) chases said, weapons mode \(this.m_sentMode), \(this.m_turretShots) turret shots for a gunner, turret look \(NameToString(this.m_turretPart))";
  }

  // (test, tools/game.sh cmd wheels ...)
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    if Equals(command, "wheels") {
      TandemNative.Log(this.Report());
      return;
    }
    // (test, §13.70) "wheels draw": our V seated somewhere draws a gun the
    // way the game does from a seat (EmptyHandsEvents: the driver combat
    // weapon request), and says which restrictions it wears.
    if Equals(command, "wheels draw") {
      let me = GetPlayer(this.m_game);
      let ride: wref<VehicleObject>;
      if !IsDefined(me) || !VehicleComponent.GetVehicle(this.m_game, me, ride) || !IsDefined(ride) {
        TandemNative.Log("wheels: draw: our V is in no vehicle");
        return;
      }
      let slot = n"None";
      VehicleComponent.GetMountedSlotName(this.m_game, me, slot);
      let noWeapons = StatusEffectSystem.ObjectHasStatusEffect(me, t"GameplayRestriction.NoWeapons");
      let noCombat = StatusEffectSystem.ObjectHasStatusEffect(me, t"GameplayRestriction.NoCombat");
      let firearms = StatusEffectSystem.ObjectHasStatusEffect(me, t"GameplayRestriction.DriverCombatFirearms");
      let request = new EquipmentSystemWeaponManipulationRequest();
      request.owner = me;
      request.requestType = EquipmentManipulationAction.RequestLastUsedOrFirstAvailableDriverCombatRangedWeapon;
      GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"EquipmentSystem").QueueRequest(request);
      TandemNative.Log(s"wheels: draw in \(NameToString(slot)) of vehicle \(this.RefOf(ride, system)) (copy of player \(system.CarOwnerOf(ride.GetEntityID()))): no weapons \(noWeapons), no combat \(noCombat), seat firearms \(firearms)");
      return;
    }
    // (probe, §13.70) "wheels turret PART": turn the turret of our vehicle
    // a remote gunner holds to that gunner's aim with a look-at on PART of
    // its rig; "wheels turret off".
    if StrBeginsWith(command, "wheels turret") {
      let part = StrReplace(command, "wheels turret", "");
      part = StrReplace(part, " ", "");
      this.StopTurretLook();
      this.m_turretPart = Equals(part, "off") || Equals(part, "") ? n"None" : StringToName(part);
      TandemNative.Log(s"wheels: turret look-at part \(NameToString(this.m_turretPart))");
      return;
    }
    // The first remote player's car copy here.
    let player = 0u;
    let body: ref<VehicleObject>;
    for other in system.PuppetPlayers() {
      let copy = TandemTraffic.Body(this.m_game, system.LastCarOf(other));
      if !IsDefined(body) && IsDefined(copy) {
        body = copy;
        player = other;
      }
    }
    if !IsDefined(body) {
      TandemNative.Log("wheels: no car copy here");
      return;
    }
    let named = TandemWheels.CarOf(player);
    if Equals(command, "wheels shoot") {
      // As our game would for a shot of ours that popped its tyre.
      TandemWheels.SendHit(named, 150.0, 4, 1, -1, body.GetWorldPosition(), body.GetWorldForward());
      this.m_hitsSent += 1;
      TandemNative.Log(s"wheels: our shot on player \(player)'s car sent to them");
    } else if Equals(command, "wheels gun") {
      let me = GetPlayer(this.m_game);
      let slot = TandemVehicles.FreeSlot(this.m_game, body, n"seat_back_left", me.GetEntityID());
      if !IsNameValid(slot) || VehicleComponent.IsDriverSlot(slot) {
        TandemNative.Log(s"wheels: no free seat in player \(player)'s car");
        return;
      }
      TandemVehicles.Seat(this.m_game, me, body, slot);
      this.m_gunnerOf = named;
      this.m_gunnerSlot = slot;
      TandemWheels.AskGunner(named, slot, false);
      TandemNative.Log(s"wheels: our V on the gun of player \(player)'s car (\(NameToString(slot))): asking its owner");
    } else if Equals(command, "wheels ungun") {
      TandemVehicles.Unseat(this.m_game, GetPlayer(this.m_game), body.GetEntityID(), this.m_gunnerSlot);
      TandemWheels.AskGunner(named, this.m_gunnerSlot, true);
      this.m_gunnerOf = 0u;
      TandemNative.Log("wheels: our V off the gun");
    }
  }

  // ---- naming vehicles ----

  // The name of a vehicle body across games (0: none we share): our car, a
  // copy of a player's car, a shared world vehicle.
  public func RefOf(body: ref<VehicleObject>, system: ref<TandemSystem>) -> Uint32 {
    if !IsDefined(body) || !IsDefined(system) {
      return 0u;
    }
    let id = body.GetEntityID();
    let owner = system.LastCarOwnerOf(id);
    if owner != 0u {
      return TandemWheels.CarOf(owner);
    }
    let traffic = system.Traffic();
    let shared = IsDefined(traffic) ? traffic.IdOfBody(id) : 0u;
    if shared != 0u {
      return TandemWheels.StreetOf(shared);
    }
    if IsDefined(this.m_own) && this.m_own.GetEntityID() == id {
      return this.m_ownRef;
    }
    return 0u;
  }

  // The body a vehicle name stands for here (null: none).
  public func BodyOf(vehicle: Uint32, system: ref<TandemSystem>) -> ref<VehicleObject> {
    if vehicle == 0u || !IsDefined(system) {
      return null;
    }
    if TandemWheels.IsStreet(vehicle) {
      let traffic = system.Traffic();
      return IsDefined(traffic) ? TandemTraffic.Body(this.m_game, traffic.BodyOf(TandemWheels.TrafficIdOf(vehicle))) : null;
    }
    let player = TandemWheels.OwnerOf(vehicle);
    if player == this.m_you {
      return this.m_own;
    }
    return TandemTraffic.Body(this.m_game, system.LastCarOf(player));
  }

  // ---- our car ----

  private func SampleOwn(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) && VehicleComponent.IsDriver(this.m_game, player) {
      let id = car.GetEntityID();
      let traffic = system.Traffic();
      // Ours to own: not a copy of someone's car, not a proxy of the street.
      if system.LastCarOwnerOf(id) == 0u && (!IsDefined(traffic) || traffic.IdOfBody(id) == 0u) && (!IsDefined(this.m_own) || this.m_own.GetEntityID() != id) {
        this.m_own = car;
        this.m_sent = false;
        this.m_sentTyres = 0;
        TandemNative.Log(s"wheels: our car is \(TDBID.ToStringDEBUG(car.GetRecordID())) now");
      }
    }
    if !IsDefined(this.m_own) {
      return;
    }
    this.m_ownRef = TandemWheels.CarOf(this.m_you);
    let car = this.m_own;
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let health = pools.GetStatPoolValue(Cast<StatsObjectID>(car.GetEntityID()), gamedataStatPoolType.Health, true) / 100.0;
    let flags = 0;
    let board = car.GetBlackboard();
    if car.IsDestroyed() {
      flags += 2;
      health = 0.0;
    } else if IsDefined(board) && board.GetInt(GetAllBlackboardDefs().Vehicle.DamageState) >= 3 {
      flags += 1;
    }
    let state = car.GetVehiclePS();
    if IsDefined(state) && state.GetIsSubmerged() {
      flags += 4;
    }
    let tyres = this.m_sentTyres;
    let wheeled = car as WheeledObject;
    if IsDefined(wheeled) {
      let flat = wheeled.GetFlatTireIndex();
      if flat >= 0 && flat < 8 {
        tyres = tyres | TandemWheelsSync.Bit(flat);
      }
    }
    if this.m_sent && AbsF(health - this.m_sentHealth) < 0.02 && flags == this.m_sentFlags && tyres == this.m_sentTyres && !this.m_dirty {
      return;
    }
    this.m_sent = true;
    this.m_sentHealth = health;
    this.m_sentFlags = flags;
    this.m_sentTyres = tyres;
    TandemWheels.SendDamage(this.m_ownRef, car.GetRecordID(), health, flags, tyres, 0, 0u);
    if (flags & 2) != 0 {
      TandemNative.Log("wheels: our car is a wreck");
    }
  }

  // The destruction grid of a car changed (VehicleComponent.OnGridDestruction).
  public func Dented(vehicle: ref<VehicleObject>, cells: array<Float>) -> Void {
    if this.m_you != 0u && IsDefined(this.m_own) && vehicle == this.m_own {
      TandemWheels.NoteGrid(TandemWheels.CarOf(this.m_you), cells);
      this.m_sent = false;
    }
  }

  // A part came off a car (VehicleComponent.OnVehicleOnPartDetached).
  public func PartOff(vehicle: ref<VehicleObject>, part: CName) -> Void {
    if this.m_you != 0u && IsDefined(this.m_own) && vehicle == this.m_own {
      TandemWheels.NotePart(TandemWheels.CarOf(this.m_you), part);
      this.m_sent = false;
      TandemNative.Log(s"wheels: our car lost its \(NameToString(part))");
    }
  }

  // ---- copies ----

  private func FindCopy(vehicle: Uint32) -> ref<TandemWheelsCopy> {
    for copy in this.m_copies {
      if copy.vehicle == vehicle {
        return copy;
      }
    }
    return null;
  }

  // A vehicle another game owns changed: its body here as the owner says.
  private func OnDamage(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    let body = this.BodyOf(event.vehicle, system);
    if !IsDefined(body) {
      return; // no body here yet: Check puts it right when there is
    }
    this.Apply(event.vehicle, body, event.a);
  }

  // `destruction`: 0 its health shows, 1 it blows up now, 2 a wreck put there.
  private func Apply(vehicle: Uint32, body: ref<VehicleObject>, destruction: Int32) -> Void {
    let damage = TandemWheels.DamageOf(vehicle);
    if !damage.known {
      return;
    }
    let copy = this.FindCopy(vehicle);
    if !IsDefined(copy) || copy.body != body.GetEntityID() {
      if !IsDefined(copy) {
        copy = new TandemWheelsCopy();
        ArrayPush(this.m_copies, copy);
      }
      copy.vehicle = vehicle;
      copy.body = body.GetEntityID();
      copy.tyres = 0;
      copy.wrecked = false;
      ArrayClear(copy.parts);
    }
    let street = TandemWheels.IsStreet(vehicle);
    // A shared world vehicle's health and wreck are TrafficStatus's (Traffic.reds).
    if !street {
      let pools = GameInstance.GetStatPoolsSystem(this.m_game);
      let id = Cast<StatsObjectID>(body.GetEntityID());
      if (damage.flags & 2) != 0 {
        if !copy.wrecked {
          copy.wrecked = true;
          if destruction == 1 {
            // The game's own end of a vehicle: blast, fire, sound, wreck.
            pools.RequestSettingStatPoolMinValue(id, gamedataStatPoolType.Health, null);
            TandemNative.Log(s"wheels: vehicle \(vehicle) blows up");
          } else {
            let state = body.GetVehiclePS();
            if IsDefined(state) {
              state.SetIsDestroyed(true);
            }
            let record = body.GetRecord();
            let look = IsDefined(record) ? record.DestroyedAppearance() : n"None";
            if IsNameValid(look) {
              body.ScheduleAppearanceChange(look);
            }
            TandemNative.Log(s"wheels: vehicle \(vehicle) is a wreck");
          }
        }
      } else {
        // Smoke, then fire: the game's damage stages show by themselves.
        pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, ClampF(damage.health, 0.02, 1.0) * 100.0, null, true);
      }
    }
    // Flat tyres, each once.
    let wheeled = body as WheeledObject;
    if IsDefined(wheeled) {
      let i = 0;
      while i < 8 {
        let bit = TandemWheelsSync.Bit(i);
        if (damage.tyres & bit) != 0 && (copy.tyres & bit) == 0 {
          wheeled.ToggleBrokenTire(Cast<Uint32>(i), true);
          copy.tyres = copy.tyres | bit;
        }
        i += 1;
      }
    }
    // Dents: the grid's first 15 cells are the body's layer, the last the
    // roof's (VehicleComponent.OnVehicleQuestVisualDestructionEvent).
    let points: [Float; 15];
    let j = 0;
    while j < 15 {
      points[j] = TandemWheels.GridCell(vehicle, j);
      j += 1;
    }
    body.SetDestructionGridPointValues(0u, points, false);
    let roof = TandemWheels.GridCell(vehicle, 15);
    j = 0;
    while j < 15 {
      points[j] = roof;
      j += 1;
    }
    body.SetDestructionGridPointValues(1u, points, false);
    // Parts that came off, each once.
    for part in TandemWheels.Parts(vehicle) {
      if !ArrayContains(copy.parts, part) {
        ArrayPush(copy.parts, part);
        body.DetachPart(part);
      }
    }
  }

  // Once a second: a copy whose body changed (spawned again, a car copy
  // made after we heard) gets its damage again, as a wreck if it is one.
  private func Check(system: ref<TandemSystem>) -> Void {
    for copy in this.m_copies {
      let body = this.BodyOf(copy.vehicle, system);
      if IsDefined(body) && body.GetEntityID() != copy.body {
        this.Apply(copy.vehicle, body, 2);
        TandemNative.Log(s"wheels: check: vehicle \(copy.vehicle) has a new body: its damage put on it");
      }
    }
  }

  // ---- hits ----

  // Our hit landed on a vehicle (VehicleObject.DamagePipelineFinalized): on
  // a copy of a player's car, or a shared vehicle's tyre, it is the owner's.
  public func OurHit(vehicle: ref<VehicleObject>, hit: ref<gameHitEvent>, system: ref<TandemSystem>) -> Void {
    if this.m_you == 0u || !IsDefined(vehicle) || !IsDefined(hit) {
      return;
    }
    let by = hit.attackData.GetInstigator();
    if !IsDefined(by) || !by.IsPlayer() {
      return;
    }
    let named = this.RefOf(vehicle, system);
    if named == 0u || TandemWheels.OwnerOf(named) == this.m_you {
      return;
    }
    let flags = 0;
    let kind = hit.attackData.GetAttackType();
    if Equals(kind, gamedataAttackType.Explosion) || Equals(kind, gamedataAttackType.PressureWave) {
      flags += 1;
    } else if AttackData.IsMelee(kind) {
      flags += 2;
    }
    // A tyre our shot popped on the copy: the owner's to pop.
    let tyre = -1;
    let wheeled = vehicle as WheeledObject;
    if IsDefined(wheeled) {
      let flat = wheeled.GetFlatTireIndex();
      let known = TandemWheels.DamageOf(named);
      if flat >= 0 && flat < 8 && (known.tyres & TandemWheelsSync.Bit(flat)) == 0 {
        tyre = flat;
        flags += 4;
      }
    }
    let damage = hit.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
    if TandemWheels.IsStreet(named) {
      // Its damage goes as TrafficHit (Traffic.reds); only the tyre here.
      if tyre < 0 {
        return;
      }
      damage = 0.0;
    } else if damage <= 0.0 && tyre < 0 {
      return;
    }
    TandemWheels.SendHit(named, damage, flags, tyre, -1, hit.hitPosition, hit.hitDirection);
    this.m_hitsSent += 1;
  }

  // The owner: another player's game hit one of our vehicles.
  private func OnHit(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    let body = this.BodyOf(event.vehicle, system);
    if !IsDefined(body) || TandemWheels.OwnerOf(event.vehicle) != this.m_you {
      return;
    }
    if !TandemWheels.IsStreet(event.vehicle) && body.IsDestroyed() {
      return;
    }
    let by = system.PuppetOf(event.player);
    if event.amount > 0.0 && !TandemWheels.IsStreet(event.vehicle) {
      GameInstance.GetStatPoolsSystem(this.m_game).RequestChangingStatPoolValue(Cast<StatsObjectID>(body.GetEntityID()), gamedataStatPoolType.Health, -event.amount, by, false, false);
    }
    let wheeled = body as WheeledObject;
    if (event.a & 4) != 0 && event.b >= 0 && IsDefined(wheeled) {
      wheeled.ToggleBrokenTire(Cast<Uint32>(event.b), true);
      if TandemWheels.IsStreet(event.vehicle) {
        let known = TandemWheels.DamageOf(event.vehicle);
        TandemWheels.SendDamage(event.vehicle, body.GetRecordID(), 1.0, 0, known.tyres | TandemWheelsSync.Bit(event.b), 0, 0u);
      }
    }
    this.m_hitsTaken += 1;
    this.m_dirty = true;
    TandemNative.Log(s"wheels: player \(event.player) hit vehicle \(event.vehicle) for \(event.amount) (flags \(event.a), tyre \(event.b))");
  }

  // A hit whose cause is another game's: a copy of a player's car running
  // into something here, or a remote player's puppet firing from a vehicle
  // (a copy's guns). That player's game decides it (its car, its V); here
  // it does nothing to our world. Not the hits our game makes in a remote
  // player's name on purpose: a guest's strike the host applies from its
  // puppet (Outcomes.reds), a forwarded hit. And our own V is the
  // exception: what hits our V is our game's, but for a partner's car (its
  // copy) running into it while the session's rules forbid friendly fire:
  // its collision hurts nobody then (the knock, OnVehicleHit, stays).
  public static func CausedElsewhere(game: GameInstance, hit: ref<gameHitEvent>) -> Bool {
    if !IsDefined(hit) || !IsDefined(hit.target) {
      return false;
    }
    let system = TandemSystem.Of(game);
    if !IsDefined(system) || system.You() == 0u || system.IsForwardedHit(hit) {
      return false;
    }
    if hit.target.IsPlayer() {
      let car = hit.attackData.GetSource() as VehicleObject;
      let partner = IsDefined(car) ? system.LastCarOwnerOf(car.GetEntityID()) : 0u;
      if partner != 0u && !TandemFight.FriendlyFire() {
        TandemNative.LogDebug(s"wheels: player \(partner)'s car ran into our V: no damage (friendly fire off)");
        return true;
      }
      return false;
    }
    let outcomes = TandemOutcomes.Of(game);
    if IsDefined(outcomes) && outcomes.IsStrike(hit) {
      return false;
    }
    let source = hit.attackData.GetSource();
    if IsDefined(source) && IsDefined(source as VehicleObject) && system.LastCarOwnerOf(source.GetEntityID()) != 0u {
      return true;
    }
    // Our vehicle's guns, fired for a remote gunner: its game decides.
    let wheels = TandemWheelsSync.Of(game);
    let weapon = hit.attackData.GetWeapon();
    // (A shot carries its weapon; a collision of that vehicle does not.)
    if IsDefined(wheels) && IsDefined(weapon) && (wheels.GunnerFired(source) || wheels.GunnerFired(hit.attackData.GetInstigator()) || wheels.GunnerFired(weapon.GetOwner())) {
      return true;
    }
    let by = hit.attackData.GetInstigator();
    return IsDefined(by) && system.PlayerOfPuppet(by.GetEntityID()) != 0u && TandemCombat.PuppetPlayer(game, hit.target) == 0u && VehicleComponent.IsMountedToVehicle(game, by);
  }

  public func Dropped() -> Void {
    this.m_dropped += 1;
  }

  // ---- collisions ----

  // A vehicle ran into `target` (GameObject.OnVehicleHit). False: drop it
  // (a copy's: its driver's game decides).
  public func VehicleHit(target: ref<GameObject>, evt: ref<gameVehicleHitEvent>, system: ref<TandemSystem>) -> Bool {
    if this.m_you == 0u || !IsDefined(target) || !IsDefined(evt) {
      return true;
    }
    let source = evt.attackData.GetSource() as VehicleObject;
    if !IsDefined(source) {
      return true;
    }
    if system.LastCarOwnerOf(source.GetEntityID()) != 0u && !target.IsPlayer() {
      this.m_dropped += 1;
      return false;
    }
    // Our car ran into one of the area owner's NPCs (its proxy here).
    let npc = system.NpcOfProxy(target.GetEntityID());
    if npc != 0u && IsDefined(this.m_own) && source == this.m_own {
      let relative = evt.vehicleVelocity - evt.preyVelocity;
      let puppet = target as ScriptedPuppet;
      let crowd = IsDefined(puppet) && (puppet.IsCrowd() || puppet.IsCivilian());
      let flags = TandemWheels.RamFlags(Vector4.Length(relative), crowd);
      TandemWheels.SendRam(npc, evt.vehicleVelocity, target.GetWorldPosition(), flags);
      this.m_rams += 1;
      TandemNative.Log(s"wheels: our car ran into npc \(npc) at \(Vector4.Length(relative)) m/s (flags \(flags))");
    }
    return true;
  }

  // The NPC's owner (the session only hands it to the game that runs the
  // NPC): a player's car ran into our NPC on their screen.
  private func OnRam(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    let npc = GameInstance.FindEntityByID(this.m_game, system.NpcEntity(event.npc)) as ScriptedPuppet;
    if !IsDefined(npc) || npc.IsDead() {
      return;
    }
    if (event.a & 1) != 0 {
      let velocity = new Vector4(event.dx, event.dy, event.dz, 0.0);
      let at = new Vector4(event.x, event.y, event.z, 1.0);
      npc.QueueEvent(CreateForceRagdollEvent(n"tandem_ram"));
      // An NPC's mass, roughly, times the car's velocity.
      npc.QueueEvent(CreateRagdollApplyImpulseEvent(at, velocity * 70.0, 0.5));
    }
    TandemNative.Log(s"wheels: player \(event.player)'s car ran into npc \(event.npc) (flags \(event.a))");
  }

  // Our car and another vehicle bumped (VehicleObject.OnVehicleBumpEvent).
  public func Bumped(vehicle: ref<VehicleObject>, other: ref<VehicleObject>, impact: Float, system: ref<TandemSystem>) -> Void {
    if this.m_you == 0u || !IsDefined(other) || !IsDefined(this.m_own) || impact <= 0.5 {
      return;
    }
    let mine = this.m_own;
    if mine != vehicle && mine != other {
      return;
    }
    let theirs = mine == vehicle ? other : vehicle;
    let player = system.LastCarOwnerOf(theirs.GetEntityID());
    if player == 0u {
      return; // a shared world vehicle's is Traffic.reds's
    }
    TandemWheels.NoteContact(player);
    let i = ArrayFindFirst(this.m_bumpPlayers, player);
    if i >= 0 && this.m_now - this.m_bumpAt[i] < 0.4 {
      return;
    }
    if i < 0 {
      ArrayPush(this.m_bumpPlayers, player);
      ArrayPush(this.m_bumpAt, this.m_now);
    } else {
      this.m_bumpAt[i] = this.m_now;
    }
    let way = theirs.GetWorldPosition() - mine.GetWorldPosition();
    way.Z = 0.0;
    way = Vector4.Normalize(way) * impact;
    TandemWheels.SendBump(player, way, theirs.GetWorldPosition());
    TandemNative.Log(s"wheels: our car pushed player \(player)'s car at \(impact) m/s");
  }

  // Pushes the others' cars gave ours that our game did not feel itself.
  private func Pushes() -> Void {
    for push in TandemWheels.TakePushes() {
      if IsDefined(this.m_own) && !this.m_own.IsDestroyed() {
        let car = this.m_own;
        let impulse = new PhysicalImpulseEvent();
        impulse.radius = 1.0;
        let at = car.GetWorldPosition();
        impulse.worldPosition.X = at.X;
        impulse.worldPosition.Y = at.Y;
        impulse.worldPosition.Z = at.Z + 0.5;
        let mass = car.GetTotalMass();
        impulse.worldImpulse.X = push.dx * mass;
        impulse.worldImpulse.Y = push.dy * mass;
        impulse.worldImpulse.Z = push.dz * mass;
        car.PhysicsWakeUp();
        car.QueueEvent(impulse);
        this.m_pushes += 1;
        TandemNative.Log(s"wheels: player \(push.player)'s car pushed ours");
      }
    }
  }

  // ---- leaving the hard way ----

  // The game ran one of the hard exits for our V (Speed-, Collision-,
  // Death- or another ImmediateExitWithForce state).
  public func OurExit(vehicle: ref<VehicleObject>, speed: Bool, collision: Bool, death: Bool, forced: Bool) -> Void {
    let system = TandemSystem.Of(this.m_game);
    let player = GetPlayer(this.m_game);
    if this.m_you == 0u || !IsDefined(vehicle) || !IsDefined(system) || !IsDefined(player) {
      return;
    }
    let named = this.RefOf(vehicle, system);
    if named == 0u {
      return;
    }
    let bike = IsDefined(vehicle as BikeObject);
    let kind = TandemWheels.ExitKindOf(speed, collision, death, forced, bike, vehicle.IsDestroyed());
    let driver = VehicleComponent.IsDriver(this.m_game, player);
    let velocity = vehicle.GetLinearVelocity();
    // Only our own car rolls on as ours; a copy's is its driver's game's.
    let rolls = named == TandemWheels.CarOf(this.m_you) && TandemWheels.Rolls(kind, Vector4.Length(velocity), driver);
    let flags = (rolls ? 1 : 0) + (driver ? 2 : 0);
    TandemWheels.SendExit(named, kind, flags, velocity);
    if rolls {
      this.m_rolling = vehicle;
      this.m_rollingSince = this.m_now;
    }
    TandemNative.Log(s"wheels: our V left vehicle \(named) the hard way (kind \(kind), \(Vector4.Length(velocity)) m/s\(rolls ? ", it rolls on" : ""))");
  }

  // Our car rolls on after we left it: we keep saying where it is.
  private func Roll() -> Void {
    if !IsDefined(this.m_rolling) {
      return;
    }
    let player = GetPlayer(this.m_game);
    let car = this.m_rolling;
    let mounted: wref<VehicleObject>;
    let speed = Vector4.Length(car.GetLinearVelocity());
    if !IsDefined(player) || VehicleComponent.GetVehicle(this.m_game, player, mounted) || this.m_now - this.m_rollingSince > TandemWheels.RollSeconds() || speed < TandemWheels.RestSpeed() {
      TandemNative.SendVehicle(car.GetWorldPosition(), car.GetWorldOrientation(), new Vector4(0.0, 0.0, 0.0, 0.0));
      this.m_rolling = null;
      TandemNative.Log("wheels: our car rolled to a stop");
      return;
    }
    if this.m_now - this.m_lastRoll >= 0.0333 {
      this.m_lastRoll = this.m_now;
      TandemNative.SendVehicle(car.GetWorldPosition(), car.GetWorldOrientation(), car.GetLinearVelocity());
    }
  }

  private func OnExit(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    if (event.b & 1) == 0 {
      return;
    }
    let body = system.LastCarOf(event.player);
    if !EntityID.IsDefined(body) {
      return;
    }
    let roller = new TandemWheelsRoller();
    roller.player = event.player;
    roller.body = body;
    roller.since = this.m_now;
    ArrayPush(this.m_rollers, roller);
    TandemNative.Log(s"wheels: player \(event.player)'s car rolls on with nobody at the wheel");
  }

  private func FollowRollers(system: ref<TandemSystem>) -> Void {
    if ArraySize(this.m_rollers) == 0 {
      return;
    }
    let kept: array<ref<TandemWheelsRoller>>;
    for roller in this.m_rollers {
      let body = TandemTraffic.Body(this.m_game, roller.body);
      // Until it stops, or its driver drives another car (its copy follows that).
      if IsDefined(body) && this.m_now - roller.since < TandemWheels.RollSeconds() && system.CarOf(roller.player) != roller.body {
        TandemVehicles.Follow(this.m_game, body, roller.player);
        ArrayPush(kept, roller);
      }
    }
    this.m_rollers = kept;
  }

  // ---- weapons in a vehicle ----

  private func SampleCombat(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let mode = 0;
    let named = 0u;
    let weaponId = TDBID.None();
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
      named = this.RefOf(car, system);
      let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
      let state = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vehicle) : 0;
      // gamePSMVehicle: Combat 2 (a passenger, or a gunner seat), Turret 5, DriverCombat 6.
      if state == 6 {
        mode = this.m_now - this.m_mountedAt < 3.0 ? 3 : 1;
      } else if state == 5 || (state == 2 && this.m_gunnerOf != 0u) {
        mode = 4;
      } else if state == 2 {
        mode = 2;
      }
      let weapon = GameObject.GetActiveWeapon(player);
      if IsDefined(weapon) {
        weaponId = ItemID.GetTDBID(weapon.GetItemID());
      }
    }
    if mode == this.m_sentMode && named == this.m_sentModeVehicle && weaponId == this.m_sentWeapon {
      return;
    }
    this.m_sentMode = mode;
    this.m_sentModeVehicle = named;
    this.m_sentWeapon = weaponId;
    TandemWheels.SendCombat(mode, named, weaponId);
    TandemNative.Log(s"wheels: our weapons in vehicle \(named): mode \(mode) (\(TDBID.ToStringDEBUG(weaponId)))");
  }

  // A vehicle's own weapon fired (VehicleComponent.OnWeaponShootEvent): ours
  // to say when we fire it, from our car or from a gunner seat we hold.
  public func Fired(vehicle: ref<VehicleObject>, weapon: TweakDBID, slot: TweakDBID) -> Void {
    let system = TandemSystem.Of(this.m_game);
    let player = GetPlayer(this.m_game);
    if this.m_you == 0u || !IsDefined(vehicle) || !IsDefined(system) || !IsDefined(player) {
      return;
    }
    if !VehicleComponent.IsMountedToProvidedVehicle(this.m_game, player.GetEntityID(), vehicle) {
      return; // someone else's shot (a copy's, played here)
    }
    let named = this.RefOf(vehicle, system);
    if named == 0u {
      return;
    }
    if named == TandemWheels.CarOf(this.m_you) {
      this.m_mountedAt = this.m_now;
    } else if TandemWheels.GunnerOf(named) != this.m_you {
      return;
    }
    let camera = GameInstance.GetCameraSystem(this.m_game);
    let target = vehicle.GetWorldPosition() + camera.GetActiveCameraForward() * 50.0;
    TandemWheels.SendFire(named, weapon, slot, target);
  }

  private func OnFire(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    let body = this.BodyOf(event.vehicle, system);
    if !IsDefined(body) || (event.x == 0.0 && event.y == 0.0 && event.z == 0.0) {
      return;
    }
    let target = new Vector4(event.x, event.y, event.z, 1.0);
    if TandemWheels.OwnerOf(event.vehicle) == this.m_you {
      // Our own vehicle, fired by the remote gunner who holds its seat: our
      // real guns fire where that gunner aimed, so we and everyone else see
      // and hear the vehicle fire. What they hit is the gunner's game's to
      // decide: dropped here (GunnerFired, CausedElsewhere).
      if TandemWheels.GunnerOf(event.vehicle) != event.player || event.player == this.m_you {
        return;
      }
      this.NoteGunnerFired(body);
      TandemWheelsSync.FireAt(body, target);
      this.m_turretShots += 1;
      if this.m_turretShots == 1 || this.m_turretShots % 10 == 0 {
        TandemNative.Log(s"wheels: our vehicle \(event.vehicle) fires for its gunner, player \(event.player) (\(this.m_turretShots) shot(s), armed \(body.IsArmedVehicle()))");
      }
      return;
    }
    // A copy's shot: cosmetic here (CausedElsewhere drops what it hits).
    TandemWheelsSync.FireAt(body, target);
  }

  // A vehicle's own guns at a point, the way vanilla's armed cars fire at
  // their target (AISubActionShootFromCar: NPC combat on, then NPCShoot,
  // final.reds:451963). Without the first, NPCShoot may do nothing.
  public static func FireAt(body: ref<VehicleObject>, target: Vector4) -> Void {
    if !body.IsNPCShooting() {
      body.EnableNPCCombat(true);
    }
    body.NPCShoot(target, 1u);
  }

  private func NoteGunnerFired(body: ref<VehicleObject>) -> Void {
    let id = body.GetEntityID();
    let i = ArrayFindFirst(this.m_gunnerFiredBody, id);
    if i < 0 {
      ArrayPush(this.m_gunnerFiredBody, id);
      ArrayPush(this.m_gunnerFiredAt, this.m_now);
    } else {
      this.m_gunnerFiredAt[i] = this.m_now;
    }
  }

  // Our vehicle's guns fired for a remote gunner within the last 3 s: what
  // they hit is that gunner's game's (it fired the real shot on its copy).
  public func GunnerFired(source: ref<GameObject>) -> Bool {
    if !IsDefined(source) {
      return false;
    }
    let i = ArrayFindFirst(this.m_gunnerFiredBody, source.GetEntityID());
    return i >= 0 && this.m_now - this.m_gunnerFiredAt[i] < 3.0;
  }

  // Ten times a second: a vehicle we run whose gunner seat a remote player
  // holds. With the probe's part set, its turret is turned to the gunner's
  // aim by a look-at on that part of its rig (the way a security turret
  // turns, final.reds:152184); no part known yet that does it.
  private func Turret(system: ref<TandemSystem>) -> Void {
    if !IsNameValid(this.m_turretPart) || this.m_now - this.m_lastTurret < 0.1 {
      return;
    }
    this.m_lastTurret = this.m_now;
    let own = this.m_own;
    if !IsDefined(own) {
      return;
    }
    let named = TandemWheels.CarOf(this.m_you);
    let gunner = TandemWheels.GunnerOf(named);
    let aim = TandemWheels.AimOf(named);
    if gunner == 0u || gunner == this.m_you || aim.W == 0.0 {
      this.StopTurretLook();
      return;
    }
    let point = TandemWheelsSync.AimPoint(own, aim.X, aim.Y);
    this.StopTurretLook();
    let look = new LookAtAddEvent();
    look.bodyPart = this.m_turretPart;
    look.SetStaticTarget(point);
    look.SetStyle(animLookAtStyle.Normal);
    look.SetLimits(animLookAtLimitDegreesType.None, animLookAtLimitDegreesType.None, animLookAtLimitDistanceType.None, animLookAtLimitDegreesType.None);
    look.request.transitionSpeed = 30.0;
    own.QueueEvent(look);
    this.m_turretLook = look;
    this.m_turretLookOn = own;
  }

  private func StopTurretLook() -> Void {
    if IsDefined(this.m_turretLook) && IsDefined(this.m_turretLookOn) {
      LookAtRemoveEvent.QueueRemoveLookatEvent(this.m_turretLookOn, this.m_turretLook);
    }
    this.m_turretLook = null;
    this.m_turretLookOn = null;
  }

  // A gunner's aim (yaw and pitch from the vehicle's forward, radians, left
  // and up positive: GunnerAim) as a point 50 m out in the world.
  public static func AimPoint(body: ref<VehicleObject>, yaw: Float, pitch: Float) -> Vector4 {
    let forward = body.GetWorldForward();
    let right = body.GetWorldRight();
    let up = body.GetWorldUp();
    let flat = forward * CosF(yaw) - right * SinF(yaw);
    let way = flat * CosF(pitch) + up * SinF(pitch);
    let point = body.GetWorldPosition() + way * 50.0;
    point.W = 1.0;
    return point;
  }

  // ---- the gunner seat ----

  // Our V took (or left) the gunner seat of a vehicle (GunnerEvents).
  public func Gunner(vehicle: ref<VehicleObject>, on: Bool) -> Void {
    let system = TandemSystem.Of(this.m_game);
    let player = GetPlayer(this.m_game);
    if this.m_you == 0u || !IsDefined(vehicle) || !IsDefined(system) || !IsDefined(player) {
      return;
    }
    let named = this.RefOf(vehicle, system);
    if named == 0u || TandemWheels.OwnerOf(named) == this.m_you {
      return; // our own vehicle: our seat is ours
    }
    let slot = n"None";
    VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
    if on {
      this.m_gunnerOf = named;
      this.m_gunnerSlot = slot;
      TandemWheels.AskGunner(named, slot, false);
      TandemNative.Log(s"wheels: our V takes the gun of vehicle \(named) (\(NameToString(slot))): asking its owner");
    } else if this.m_gunnerOf == named {
      this.m_gunnerOf = 0u;
      TandemWheels.AskGunner(named, slot, true);
      TandemNative.Log(s"wheels: our V leaves the gun of vehicle \(named)");
    }
  }

  private func OnGunner(event: TandemWheelsEvent, system: ref<TandemSystem>) -> Void {
    if event.vehicle == this.m_gunnerOf && event.player != this.m_you && event.player != 0u {
      // Someone else had it first: our V is out of that seat again.
      let player = GetPlayer(this.m_game);
      let body = this.BodyOf(event.vehicle, system);
      if IsDefined(player) && IsDefined(body) {
        TandemVehicles.Unseat(this.m_game, player, body.GetEntityID(), this.m_gunnerSlot);
      }
      this.m_gunnerOf = 0u;
      TandemNative.Log(s"wheels: player \(event.player) is on the gun of vehicle \(event.vehicle): our V is out of it");
      return;
    }
    TandemNative.Log(s"wheels: vehicle \(event.vehicle): gunner \(event.player)");
  }

  // Twenty times a second while we hold a gunner seat: where we aim,
  // relative to the vehicle.
  private func Aim() -> Void {
    if this.m_gunnerOf == 0u || this.m_now - this.m_lastAim < 0.05 || TandemWheels.GunnerOf(this.m_gunnerOf) != this.m_you {
      return;
    }
    this.m_lastAim = this.m_now;
    let system = TandemSystem.Of(this.m_game);
    let body = this.BodyOf(this.m_gunnerOf, system);
    if !IsDefined(body) {
      return;
    }
    let look = GameInstance.GetCameraSystem(this.m_game).GetActiveCameraForward();
    let forward = body.GetWorldForward();
    let up = body.GetWorldUp();
    let yaw = Vector4.GetAngleDegAroundAxis(look, forward, up) * Pi() / 180.0;
    let pitch = AsinF(ClampF(Vector4.Dot(look, up), -1.0, 1.0));
    TandemWheels.SendAim(this.m_gunnerOf, yaw, pitch, this.m_now - this.m_mountedAt < 0.2);
  }

  // ---- chases ----

  // The vehicle's owner: one of our NPCs drives a vehicle we run after `target`
  // (InVehicleCombatDecorator.ChaseNewTarget), or stopped (null).
  public func Chases(driver: ref<GameObject>, target: ref<GameObject>) -> Void {
    let system = TandemSystem.Of(this.m_game);
    if this.m_you == 0u || !IsDefined(driver) || !IsDefined(system) {
      return;
    }
    let vehicle: wref<VehicleObject>;
    if !VehicleComponent.GetVehicle(this.m_game, driver, vehicle) || !IsDefined(vehicle) {
      return;
    }
    let traffic = system.Traffic();
    let id = IsDefined(traffic) ? traffic.IdOfBody(vehicle.GetEntityID()) : 0u;
    if id == 0u || !traffic.Mine(id) {
      return; // not shared (nobody else sees it), or another game's to say
    }
    let player = 0u;
    if IsDefined(target) {
      player = target.IsPlayer() ? this.m_you : system.PlayerOfPuppet(target.GetEntityID());
    }
    let i = ArrayFindFirst(this.m_chaseIds, id);
    if i >= 0 && this.m_chaseTargets[i] == player {
      return;
    }
    if i < 0 {
      ArrayPush(this.m_chaseIds, id);
      ArrayPush(this.m_chaseTargets, player);
    } else {
      this.m_chaseTargets[i] = player;
    }
    TandemWheels.SendChase(id, player, 3);
    TandemNative.Log(s"wheels: vehicle \(id) chases player \(player)");
  }

  private func OnChase(event: TandemWheelsEvent) -> Void {
    if event.player == this.m_you && !ArrayContains(this.m_chasedBy, event.vehicle) {
      ArrayPush(this.m_chasedBy, event.vehicle);
      TandemNative.Log(s"wheels: vehicle \(event.vehicle) chases our V");
    } else if event.player != this.m_you && ArrayContains(this.m_chasedBy, event.vehicle) {
      ArrayRemove(this.m_chasedBy, event.vehicle);
      TandemNative.Log(s"wheels: vehicle \(event.vehicle) no longer chases our V");
    }
  }

  // ---- events ----

  private func TakeEvents(system: ref<TandemSystem>) -> Void {
    for event in TandemWheels.TakeEvents() {
      switch event.kind {
        case 1:
          this.OnDamage(event, system);
          break;
        case 2:
          this.OnHit(event, system);
          break;
        case 3:
          this.OnExit(event, system);
          break;
        case 5:
          this.OnFire(event, system);
          break;
        case 6:
          this.OnGunner(event, system);
          break;
        case 7:
          this.OnRam(event, system);
          break;
        case 8:
          this.OnChase(event);
          break;
        default:
          break;
      }
    }
  }
}

// ---- the game's hooks ----

// Our hit on a copy of a player's car, or a shared vehicle's tyre.
@wrapMethod(VehicleObject)
protected func DamagePipelineFinalized(evt: ref<gameHitEvent>) -> Void {
  TandemNative.HookRan(n"Wheels.VehicleObject.DamagePipelineFinalized");
  wrappedMethod(evt);
  let wheels = TandemWheelsSync.Of(this.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.OurHit(this, evt, TandemSystem.Of(this.GetGame()));
  }
}

// Our car and another player's (its copy here) touched.
@wrapMethod(VehicleObject)
protected cb func OnVehicleBumpEvent(evt: ref<VehicleBumpEvent>) -> Bool {
  TandemNative.HookRan(n"Wheels.VehicleObject.OnVehicleBumpEvent");
  let result = wrappedMethod(evt);
  let wheels = TandemWheelsSync.Of(this.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.Bumped(this, evt.hitVehicle, evt.impactVelocityChange, TandemSystem.Of(this.GetGame()));
  }
  return result;
}

// A vehicle ran into something: a copy's collision is its driver's game's;
// our car running into an NPC we do not own tells its owner.
@wrapMethod(GameObject)
protected cb func OnVehicleHit(evt: ref<gameVehicleHitEvent>) -> Bool {
  TandemNative.HookRan(n"Wheels.GameObject.OnVehicleHit");
  let wheels = TandemWheelsSync.Of(this.GetGame());
  if IsDefined(wheels) && wheels.InSession() && !wheels.VehicleHit(this, evt, TandemSystem.Of(this.GetGame())) {
    return false;
  }
  return wrappedMethod(evt);
}

// Nothing a copy of a player's car or a remote V's puppet causes here does
// anything to our world (CausedElsewhere).
@wrapMethod(DamageSystem)
private final func DealDamages(hitEvent: ref<gameHitEvent>) {
  let game = GetGameInstance();
  if TandemWheelsSync.CausedElsewhere(game, hitEvent) {
    let wheels = TandemWheelsSync.Of(game);
    if IsDefined(wheels) {
      wheels.Dropped();
    }
    return;
  }
  wrappedMethod(hitEvent);
}

// Our car's dents and the parts it loses, for its copies.
@wrapMethod(VehicleComponent)
protected cb func OnGridDestruction(evt: ref<VehicleGridDestructionEvent>) -> Bool {
  TandemNative.HookRan(n"Wheels.VehicleComponent.OnGridDestruction");
  let result = wrappedMethod(evt);
  let wheels = TandemWheelsSync.Of(this.GetVehicle().GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    let cells: array<Float>;
    let i = 0;
    while i < 16 {
      ArrayPush(cells, evt.state[i]);
      i += 1;
    }
    wheels.Dented(this.GetVehicle(), cells);
  }
  return result;
}

@wrapMethod(VehicleComponent)
protected cb func OnVehicleOnPartDetached(evt: ref<VehicleOnPartDetachedEvent>) -> Bool {
  TandemNative.HookRan(n"Wheels.VehicleComponent.OnVehicleOnPartDetached");
  let result = wrappedMethod(evt);
  let wheels = TandemWheelsSync.Of(this.GetVehicle().GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.PartOff(this.GetVehicle(), evt.partName);
  }
  return result;
}

// A vehicle's own weapon fired.
@wrapMethod(VehicleComponent)
protected cb func OnWeaponShootEvent(evt: ref<VehicleMountedWeaponShootEvent>) -> Bool {
  TandemNative.HookRan(n"Wheels.VehicleComponent.OnWeaponShootEvent");
  let result = wrappedMethod(evt);
  let wheels = TandemWheelsSync.Of(this.GetVehicle().GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.Fired(this.GetVehicle(), evt.weaponID, evt.slotID);
  }
  return result;
}

// Our V jumps out of a car on the move.
@wrapMethod(SpeedExitingEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  let wheels = TandemWheelsSync.Of(scriptInterface.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.OurExit(scriptInterface.owner as VehicleObject, true, false, false, false);
  }
  wrappedMethod(stateContext, scriptInterface);
}

// Our V is thrown out by a crash, knocked off a bike, dies in the seat, or
// is blown out (CollisionExitingEvents and DeathExitingEvents run this too).
@wrapMethod(ImmediateExitWithForceEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  let wheels = TandemWheelsSync.Of(scriptInterface.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    let collision = IsDefined(this as CollisionExitingEvents);
    let death = IsDefined(this as DeathExitingEvents);
    wheels.OurExit(scriptInterface.owner as VehicleObject, false, collision, death, !collision && !death);
  }
  wrappedMethod(stateContext, scriptInterface);
}

// Our V a passenger in a partner's car copy (§13.70). Vanilla takes the
// weapons of a V that sits beside the driver of a car "occupied by
// someone not friendly" (stateMachineInitData.occupiedByNonFriendly,
// read from each occupant's attitude when the door was used): in a copy
// those occupants are the partner's puppet and the NPCs it carries, which
// tandem makes friends of our V (Attitudes.reds) but which the game may
// still read otherwise for a moment. A copy of a partner's car whose
// occupants are only players' puppets gives our V its weapons back; every
// other check of passenger combat is the record's, the same as alone.
@wrapMethod(PassengerEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if !this.noWeaponsRestrictionApplied {
    return;
  }
  let game = scriptInterface.GetGame();
  let system = TandemSystem.Of(game);
  let car = scriptInterface.owner as VehicleObject;
  if !IsDefined(system) || system.You() == 0u || !IsDefined(car) || system.CarOwnerOf(car.GetEntityID()) == 0u {
    return;
  }
  let passengers: array<wref<GameObject>>;
  VehicleComponent.GetAllPassengers(game, car.GetEntityID(), false, passengers);
  for passenger in passengers {
    if IsDefined(passenger) && !passenger.IsPlayer() && system.PlayerOfPuppet(passenger.GetEntityID()) == 0u {
      return; // someone else rides there: vanilla's call stands
    }
  }
  this.noWeaponsRestrictionApplied = false;
  StatusEffectHelper.RemoveStatusEffect(scriptInterface.executionOwner, t"GameplayRestriction.NoWeapons");
  TandemNative.Log(s"wheels: our V beside player \(system.CarOwnerOf(car.GetEntityID()))'s puppet: weapons kept (a friend's car)");
}

// Our V on a vehicle's gun.
@wrapMethod(GunnerEvents)
protected func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  let wheels = TandemWheelsSync.Of(scriptInterface.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.Gunner(scriptInterface.owner as VehicleObject, true);
  }
}

@wrapMethod(GunnerEvents)
public final func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  let wheels = TandemWheelsSync.Of(scriptInterface.GetGame());
  if IsDefined(wheels) && wheels.InSession() {
    wheels.Gunner(scriptInterface.owner as VehicleObject, false);
  }
  wrappedMethod(stateContext, scriptInterface);
}

// The vehicle's owner: a vehicle's NPCs drive after their combat target.
@wrapMethod(InVehicleCombatDecorator)
protected final func ChaseNewTarget(context: ScriptExecutionContext, newTarget: wref<GameObject>) -> Void {
  wrappedMethod(context, newTarget);
  let owner = ScriptExecutionContext.GetOwner(context);
  let wheels = IsDefined(owner) ? TandemWheelsSync.Of(owner.GetGame()) : null;
  if IsDefined(wheels) && wheels.InSession() && IsDefined(this.m_targetToChase) {
    wheels.Chases(owner, this.m_targetToChase);
  }
}

@wrapMethod(InVehicleCombatDecorator)
protected func Deactivate(context: ScriptExecutionContext) -> Void {
  let owner = ScriptExecutionContext.GetOwner(context);
  let wheels = IsDefined(owner) ? TandemWheelsSync.Of(owner.GetGame()) : null;
  let chasing = IsDefined(this.m_targetToChase);
  wrappedMethod(context);
  if IsDefined(wheels) && wheels.InSession() && chasing {
    wheels.Chases(owner, null);
  }
}
