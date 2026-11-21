// World vehicles: traffic, parked cars, police cars (docs/architecture/traffic.md).
// Each is run by one game, as an NPC is (docs/architecture/regions.md): the
// area owner that announced it (TandemArea, the regions' area owner), until
// the host hands it over. Our game announces each vehicle of our world near
// a player while it owns our area (TandemStreet.SendSpawn), and says where
// the ones it runs are ten times a second and how each stands (health,
// wreck, siren). The vehicles another game runs are shown as proxies: a copy
// of the same record moved to where its owner says, or our own instance
// when it is a placed vehicle (the same EntityID in every game).
//
// Hand-over (event 6 give, 7 take, 8 owner changed): both games already
// have a body for the vehicle. The giver keeps its body as the proxy
// (stopped in its lane, moved by the new owner's states); the receiver's
// proxy becomes the real one, woken up with the giver's velocity and sent
// back into traffic. Its driver and passengers are handed over with it
// (Npcs.reds keeps them seated). Nothing is spawned or deleted.
//
// A vehicle a player gets into becomes that player's car under the usual
// rule (Vehicles.reds): the first claim wins (TandemStreet.Claim, decided by
// the game that runs it), and every game hands the body it has (the real
// car at the owner's, the proxy elsewhere) to that player's seat as its car
// copy.

// Who runs the world around a player: its NPCs, its traffic, its police.
// The area owner of per-player regions (docs/architecture/regions.md, "The
// area owner"; Endpoint::AreaOwner). Traffic and police are written against
// this alone.
public abstract class TandemArea {
  // Our game runs the world around our V.
  public static func Owns() -> Bool {
    return TandemRegions.OwnsArea();
  }

  // Our game runs the world around that player's V (its puppet here).
  public static func OwnsAround(player: Uint32) -> Bool {
    let you = TandemNative.You();
    return you != 0u && TandemRegions.AreaOwner(player) == you;
  }
}

public class TandemCar {
  public let id: Uint32;
  // Ours: the real vehicle. Others': the proxy (a copy we spawned, our own
  // instance of a placed vehicle, or a vehicle of ours we gave away).
  public let body: EntityID;
  public let record: TweakDBID;
  public let appearance: CName;
  // proto::kTraffic*: 1 parked, 2 police, 4 destroyed, 8 burning, 16 siren,
  // 32 lights, 64 panic, 128 abandoned.
  public let flags: Int32;
  public let health: Float;
  // Our game runs it (we announced it, or it was handed to us).
  public let mine: Bool;
  // Ours: listed this round.
  public let seen: Bool;
  // A proxy whose body is a vehicle of our world (our own placed instance,
  // or one we ran and gave away): never spawned, never deleted.
  public let own: Bool;
  // A crowd car of ours we gave away: held out of its lane while it is a
  // proxy (TandemHold), back into traffic when it is ours again.
  public let held: Bool;
  // Proxies: its body exists and shows how the owner says it stands.
  public let ready: Bool;
  public let wrecked: Bool;
  public let siren: Bool;
  // Our V is getting in: we no longer move it (the claim is on its way).
  public let claimed: Bool;
  // Someone has sat in it here: its body is never deleted (engine-knowledge §13.6).
  public let seated: Bool;
  // Proxies: where we last put it.
  public let x: Float;
  public let y: Float;
  public let z: Float;
  // Proxies, the consistency checks: since when its body is missing (0: not so).
  public let goneSince: Float;
}

// A body a player took (TrafficGone taken): that player's car copy here,
// for System.reds to seat its puppet in instead of spawning another.
public class TandemAdopted {
  public let player: Uint32;
  public let body: EntityID;
  public let at: Float;
}

// A proxy no longer shown that cannot be deleted: kept to be the next proxy
// of the same record.
public class TandemRetired {
  public let body: EntityID;
  public let record: TweakDBID;
}

public class TandemTraffic {
  private let m_game: GameInstance;
  // Every vehicle attached in our world (VehicleObject.OnGameAttached).
  private let m_live: array<wref<VehicleObject>>;
  // The vehicles we run and the proxies we show, in one list (`mine`).
  private let m_cars: array<ref<TandemCar>>;
  private let m_next: Uint32;
  private let m_last: Float;
  private let m_lastCheck: Float;
  private let m_now: Float;
  private let m_you: Uint32;
  // Vehicles that are a player's car now (ours too): never shared again.
  private let m_taken: array<EntityID>;
  private let m_adopted: array<ref<TandemAdopted>>;
  private let m_retired: array<ref<TandemRetired>>;
  // Proxies, the checks: vehicles shared that we do not show, since when.
  private let m_missingIds: array<Uint32>;
  private let m_missingSince: array<Float>;
  // Rams already told, by body: one hit per contact, not one per frame.
  private let m_ramBodies: array<EntityID>;
  private let m_ramAt: array<Float>;
  // Hand-overs so far (the report).
  private let m_given: Int32;
  private let m_taken2: Int32;
  // The overlap (regions.md §6): our crowd cars inside a higher-ranked
  // linked player's region are held back (not announced while in there);
  // how many now, and the ones asked to despawn (overlap_despawn).
  private let m_heldBack: Int32;
  private let m_despawnAsked: array<EntityID>;
  private let m_despawn: Bool;
  private let m_despawnRead: Bool;

  public static func ShareRange() -> Float {
    return 120.0;
  }

  public static func DropRange() -> Float {
    return 150.0;
  }

  public static func MaxShared() -> Int32 {
    return 48;
  }

  // The NPC racers of a street race of ours that partners race in
  // (Races.reds): shared as far as a race spreads.
  public static func RacerRange() -> Float {
    return 1500.0;
  }

  // The vehicles of our world now.
  public func Live() -> array<wref<VehicleObject>> {
    return this.m_live;
  }

  public static func Of(game: GameInstance) -> ref<TandemTraffic> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Traffic() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_next = 1u;
  }

  // ---- every game: the vehicles of our world ----

  // Every tick, session or not (TandemSystem.OnTick): the vehicles that came
  // and went since, as their wraps queued them (job threads while the world
  // streams in: VehicleObject.OnGameAttached, OnDetach).
  public func Drain() -> Void {
    let streamed = TandemStreaming.TakeStreamed(TandemStreamKind.Vehicle());
    for event in streamed {
      if event.attached {
        // Gone again since: its detach is further down the queue.
        let vehicle = GameInstance.FindEntityByID(this.m_game, event.entity) as VehicleObject;
        if IsDefined(vehicle) && !ArrayContains(this.m_live, vehicle) {
          ArrayPush(this.m_live, vehicle);
        }
      } else {
        this.Detached(event.entity);
      }
    }
  }

  private func Detached(entity: EntityID) -> Void {
    let i = ArraySize(this.m_live) - 1;
    while i >= 0 {
      let vehicle = this.m_live[i];
      if !IsDefined(vehicle) || vehicle.GetEntityID() == entity {
        ArrayErase(this.m_live, i);
      }
      i -= 1;
    }
  }

  public func Tick(now: Float, you: Uint32, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    this.m_you = you;
    if you == 0u {
      return;
    }
    this.TakeEvents(system);
    // Ours: announced while we own our area, said for as long as we run them.
    if now - this.m_last >= 0.1 {
      this.m_last = now;
      this.Share(system);
    }
    // Theirs: the proxies follow.
    this.Drive();
    if now - this.m_lastCheck >= 1.0 {
      this.m_lastCheck = now;
      this.Check();
    }
    // Bodies nobody asked for after a while are parked cars like any other.
    let kept: array<ref<TandemAdopted>>;
    for adopted in this.m_adopted {
      if now - adopted.at < 15.0 {
        ArrayPush(kept, adopted);
      }
    }
    this.m_adopted = kept;
  }

  // The session is over: the others' vehicles go, ours are our own again.
  public func End() -> Void {
    for car in this.m_cars {
      if !car.mine {
        this.Retire(car);
      }
    }
    ArrayClear(this.m_cars);
    ArrayClear(this.m_taken);
    ArrayClear(this.m_adopted);
    ArrayClear(this.m_missingIds);
    ArrayClear(this.m_missingSince);
    ArrayClear(this.m_ramBodies);
    ArrayClear(this.m_ramAt);
    ArrayClear(this.m_despawnAsked);
    this.m_heldBack = 0;
    this.m_next = 1u;
  }

  public func Count() -> Int32 {
    return ArraySize(this.m_cars);
  }

  // (test, tools/game.sh cmd street count) what we share or show.
  public func Report() -> String {
    let parked = 0;
    let wrecks = 0;
    let own = 0;
    let mine = 0;
    for car in this.m_cars {
      if (car.flags & 1) != 0 {
        parked += 1;
      }
      if (car.flags & 4) != 0 {
        wrecks += 1;
      }
      if car.own {
        own += 1;
      }
      if car.mine {
        mine += 1;
      }
    }
    let area = TandemArea.Owns() ? "ours" : s"player \(TandemRegions.AreaOwner(this.m_you))'s";
    return s"count: \(ArraySize(this.m_cars)) (\(mine) run by us, \(ArraySize(this.m_cars) - mine) shown as proxies; \(parked) parked, \(wrecks) wrecks, \(own) bodies of our own world), area \(area), \(ArraySize(TandemStreet.Shared())) proxies in the session's record, \(ArraySize(this.m_live)) vehicles in our world, \(ArraySize(this.m_taken)) taken by players, \(ArraySize(this.m_retired)) proxies kept for reuse, \(this.m_given) given, \(this.m_taken2) taken over";
  }

  // The shared vehicle a body is (0: none), whoever runs it.
  public func IdOfBody(body: EntityID) -> Uint32 {
    for car in this.m_cars {
      if car.body == body {
        return car.id;
      }
    }
    return 0u;
  }

  // The shared vehicle a body is the proxy of (0: none, or one we run).
  public func ProxyOfBody(body: EntityID) -> Uint32 {
    for car in this.m_cars {
      if car.body == body && !car.mine {
        return car.id;
      }
    }
    return 0u;
  }

  // Our game runs it.
  public func Mine(id: Uint32) -> Bool {
    let car = this.Find(id);
    return IsDefined(car) && car.mine;
  }

  public func BodyOf(id: Uint32) -> EntityID {
    for car in this.m_cars {
      if car.id == id {
        return car.body;
      }
    }
    return EntityID.FromHash(0ul);
  }

  // A proxy (NPC or puppet) was seated in it here.
  public func MarkSeated(id: Uint32) -> Void {
    let car = this.Find(id);
    if IsDefined(car) {
      car.seated = true;
    }
  }

  // The body `player` took from the street, once: its car copy from now on.
  public func TakeAdopted(player: Uint32) -> EntityID {
    let i = 0;
    while i < ArraySize(this.m_adopted) {
      if this.m_adopted[i].player == player {
        let body = this.m_adopted[i].body;
        ArrayErase(this.m_adopted, i);
        return body;
      }
      i += 1;
    }
    return EntityID.FromHash(0ul);
  }

  // A vehicle's body, whoever made it: a copy of ours, or the world's.
  public static func Body(game: GameInstance, id: EntityID) -> ref<VehicleObject> {
    if !EntityID.IsDefined(id) {
      return null;
    }
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(id) as VehicleObject;
    if !IsDefined(body) {
      body = GameInstance.FindEntityByID(game, id) as VehicleObject;
    }
    return body;
  }

  private func Find(id: Uint32) -> ref<TandemCar> {
    for car in this.m_cars {
      if car.id == id {
        return car;
      }
    }
    return null;
  }

  private func FindBody(body: EntityID) -> ref<TandemCar> {
    for car in this.m_cars {
      if car.body == body {
        return car;
      }
    }
    return null;
  }

  private func Forget(id: Uint32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_cars) {
      if this.m_cars[i].id == id {
        ArrayErase(this.m_cars, i);
        return;
      }
      i += 1;
    }
  }

  // ---- our V gets into a vehicle ----

  // From VehicleObject.OnMountingEvent, for our own V: a shared vehicle
  // becomes ours if we are first (the answer is a Gone event, taken).
  public func PlayerMounts(vehicle: ref<VehicleObject>, slot: CName) -> Void {
    if this.m_you == 0u || !IsDefined(vehicle) {
      return;
    }
    let body = vehicle.GetEntityID();
    let car = this.FindBody(body);
    if IsDefined(car) && !car.claimed {
      car.claimed = true;
      TandemStreet.Claim(car.id, slot);
      TandemNative.Log(s"street: our V gets into vehicle \(car.id) (\(NameToString(slot))): claiming it");
    }
    // Ours from now on, shared or not: never announced as a world vehicle.
    if !ArrayContains(this.m_taken, body) {
      ArrayPush(this.m_taken, body);
    }
  }

  // ---- events from the session ----

  private func TakeEvents(system: ref<TandemSystem>) -> Void {
    for event in TandemStreet.TakeEvents() {
      if event.kind == 1 {
        this.OnSpawn(event);
      } else if event.kind == 2 {
        this.OnGone(event, system);
      } else if event.kind == 3 {
        this.OnStatus(event);
      } else if event.kind == 4 {
        this.OnHit(event, system);
      } else if event.kind == 5 {
        TandemPolice.OnCrime(this.m_game, event);
      } else if event.kind == 6 {
        this.OnGive(event);
      } else if event.kind == 7 {
        this.OnTake(event);
      } else if event.kind == 8 {
        this.OnOwner(event);
      }
    }
  }

  // A vehicle another game runs, shown from now on.
  private func OnSpawn(event: TandemStreetEvent) -> Void {
    if IsDefined(this.Find(event.id)) {
      return;
    }
    let car = new TandemCar();
    car.id = event.id;
    car.record = event.record;
    car.appearance = event.appearance;
    car.flags = event.flags;
    car.health = 1.0;
    car.x = event.x;
    car.y = event.y;
    car.z = event.z;
    let at = new Vector4(event.x, event.y, event.z, 1.0);
    let turn = new Quaternion(event.i, event.j, event.k, event.r);
    // A placed vehicle is the same entity in every game: ours is its body.
    let placed = EntityID.IsDefined(event.entity) ? GameInstance.FindEntityByID(this.m_game, event.entity) as VehicleObject : null;
    if IsDefined(placed) && !GameInstance.GetDynamicEntitySystem().IsManaged(event.entity) {
      car.body = event.entity;
      car.own = true;
      TandemNative.Log(s"street: vehicle \(car.id) is our own placed \(TDBID.ToStringDEBUG(car.record))");
    } else {
      car.body = this.Reuse(car.record, at, turn);
      if !EntityID.IsDefined(car.body) {
        car.body = TandemTraffic.SpawnProxy(car.record, car.appearance, at, turn);
      }
      TandemNative.Log(s"street: vehicle \(car.id) \(TDBID.ToStringDEBUG(car.record)) of player \(TandemStreet.RunBy(car.id)) (flags \(car.flags)) spawning at (\(event.x), \(event.y), \(event.z))");
    }
    ArrayPush(this.m_cars, car);
  }

  private static func SpawnProxy(record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion) -> EntityID {
    let spec = new DynamicEntitySpec();
    spec.recordID = record;
    if IsNameValid(appearance) {
      spec.appearanceName = appearance;
    }
    spec.position = position;
    spec.orientation = orientation;
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Traffic");
    return GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
  }

  // A retired proxy of the same record, put where the new one is.
  private func Reuse(record: TweakDBID, position: Vector4, orientation: Quaternion) -> EntityID {
    let i = 0;
    while i < ArraySize(this.m_retired) {
      if this.m_retired[i].record == record {
        let body = this.m_retired[i].body;
        ArrayErase(this.m_retired, i);
        let entity = GameInstance.GetDynamicEntitySystem().GetEntity(body) as GameObject;
        if IsDefined(entity) {
          GameInstance.GetTeleportationFacility(this.m_game).Teleport(entity, position, Quaternion.ToEulerAngles(orientation));
          return body;
        }
      } else {
        i += 1;
      }
    }
    return EntityID.FromHash(0ul);
  }

  // No longer shared: out of range, removed, or a player's car now.
  private func OnGone(event: TandemStreetEvent, system: ref<TandemSystem>) -> Void {
    let car = this.Find(event.id);
    if !IsDefined(car) {
      return;
    }
    this.Forget(event.id);
    if event.reason != 1 {
      // A proxy goes; a vehicle we ran is our world's again.
      if !car.mine {
        this.Retire(car);
      }
      return;
    }
    // Taken: it is that player's car from now on.
    if !ArrayContains(this.m_taken, car.body) {
      ArrayPush(this.m_taken, car.body);
    }
    if event.player == this.m_you {
      TandemNative.Log(s"street: vehicle \(car.id) is ours now");
      return;
    }
    TandemNative.Log(s"street: vehicle \(car.id) is player \(event.player)'s car now");
    let body = TandemTraffic.Body(this.m_game, car.body);
    // We were getting in too and lost: out again.
    let player = GetPlayer(this.m_game);
    if car.claimed && IsDefined(player) && IsDefined(body) && VehicleComponent.IsMountedToProvidedVehicle(this.m_game, player.GetEntityID(), body) {
      let slot = n"None";
      VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
      TandemVehicles.Unseat(this.m_game, player, car.body, slot);
      TandemNative.Log(s"street: player \(event.player) had vehicle \(car.id) first: our V is out of it");
    }
    if car.mine && IsDefined(body) {
      this.Evict(body);
    }
    if car.held && IsDefined(body) {
      body.TandemHold(false);
    }
    let adopted = new TandemAdopted();
    adopted.player = event.player;
    adopted.body = car.body;
    adopted.at = this.m_now;
    ArrayPush(this.m_adopted, adopted);
  }

  // Owner: whoever sat in a vehicle a player took is out of it, beside its
  // door, with the game's own reaction to a hijack.
  private func Evict(body: ref<VehicleObject>) -> Void {
    let passengers: array<wref<GameObject>>;
    VehicleComponent.GetAllPassengers(this.m_game, body.GetEntityID(), false, passengers);
    let side = 0.0;
    for passenger in passengers {
      let npc = passenger as ScriptedPuppet;
      if IsDefined(npc) && !npc.IsPlayer() && !GameInstance.GetDynamicEntitySystem().IsManaged(npc.GetEntityID()) {
        let slot = n"None";
        VehicleComponent.GetMountedSlotName(this.m_game, npc, slot);
        TandemVehicles.Unseat(this.m_game, npc, body.GetEntityID(), slot);
        side += 1.0;
        let out = body.GetWorldPosition() - body.GetWorldRight() * (1.6 + 0.6 * side);
        GameInstance.GetTeleportationFacility(this.m_game).Teleport(npc, out, Quaternion.ToEulerAngles(body.GetWorldOrientation()));
        let hijack = new VehicleHijackEvent();
        hijack.driverAllowedToGetAggressive = Equals(slot, n"seat_front_left");
        npc.QueueEvent(hijack);
        TandemNative.Log(s"street: \(TDBID.ToStringDEBUG(npc.GetRecordID())) put out of \(NameToString(slot))");
      }
    }
    body.QueueEvent(new StealVehicleEvent());
  }

  // A proxy no longer shown. Deleted when nobody ever sat in it, else kept
  // for the next vehicle of its record (a body that has had an occupant
  // must not be deleted, §13.6); a body of our own world stays (and a crowd
  // car we held drives on).
  private func Retire(car: ref<TandemCar>) -> Void {
    if car.held {
      let body = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(body) {
        body.TandemHold(false);
      }
      car.held = false;
    }
    if car.own || !EntityID.IsDefined(car.body) || !GameInstance.GetDynamicEntitySystem().IsManaged(car.body) {
      return;
    }
    if car.seated || car.claimed || GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(car.body) {
      if !car.claimed {
        let retired = new TandemRetired();
        retired.body = car.body;
        retired.record = car.record;
        ArrayPush(this.m_retired, retired);
      }
      return;
    }
    GameInstance.GetDynamicEntitySystem().DeleteEntity(car.body);
  }

  private func OnStatus(event: TandemStreetEvent) -> Void {
    let car = this.Find(event.id);
    if !IsDefined(car) || car.mine {
      return;
    }
    car.health = event.amount;
    // The lasting flags follow; what a vehicle is (police) stays.
    car.flags = (car.flags & 2) | (event.flags & 253);
    if car.ready {
      let body = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(body) {
        this.Show(car, body, false);
      }
    }
  }

  // Proxies: the body as the owner says it stands. `joining`: it stood so
  // before we saw it (a wreck is put there, not blown up again).
  private func Show(car: ref<TandemCar>, body: ref<VehicleObject>, joining: Bool) -> Void {
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let id = Cast<StatsObjectID>(body.GetEntityID());
    let destroyed = (car.flags & 4) != 0;
    if destroyed && !car.wrecked {
      car.wrecked = true;
      if joining {
        let state = body.GetVehiclePS();
        if IsDefined(state) {
          state.SetIsDestroyed(true);
        }
        let record = body.GetRecord();
        let look = IsDefined(record) ? record.DestroyedAppearance() : n"None";
        if IsNameValid(look) {
          body.ScheduleAppearanceChange(look);
        }
        TandemNative.Log(s"street: vehicle \(car.id) is a wreck");
      } else {
        // The game's own end of a vehicle: explosion, fire, sound, wreck.
        pools.RequestSettingStatPoolMinValue(id, gamedataStatPoolType.Health, null);
        TandemNative.Log(s"street: vehicle \(car.id) blows up");
      }
    } else if !destroyed {
      // Its health shows by itself (smoke, then fire: the game's damage stages).
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, ClampF(car.health, 0.02, 1.0) * 100.0, null, true);
    }
    let siren = (car.flags & 16) != 0;
    if NotEquals(siren, car.siren) {
      car.siren = siren;
      body.ToggleSiren(siren);
    }
  }

  // ---- hand-overs (traffic.md, "Who owns an area") ----

  // The host asks us to give one of ours to `to` (our V is leaving it, theirs
  // is near). Its state is read and our body stays as its proxy: nothing
  // pops. Not while a player is getting in, nor while our V is elsewhere.
  private func OnGive(event: TandemStreetEvent) -> Void {
    let car = this.Find(event.id);
    let body = IsDefined(car) ? TandemTraffic.Body(this.m_game, car.body) : null;
    let player = GetPlayer(this.m_game);
    let boarded = IsDefined(body) && IsDefined(player) && VehicleComponent.IsMountedToProvidedVehicle(this.m_game, player.GetEntityID(), body);
    if !IsDefined(car) || !car.mine || !IsDefined(body) || car.claimed || boarded || ArrayContains(this.m_taken, car.body) || TandemElsewhere.AwayFromBody(this.m_game) {
      TandemStreet.Answer(event.id, false);
      TandemNative.Log(s"street: handover of vehicle \(event.id) to player \(event.player) refused");
      return;
    }
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let health = pools.GetStatPoolValue(Cast<StatsObjectID>(car.body), gamedataStatPoolType.Health, true) / 100.0;
    let flags = this.FlagsOf(body);
    let at = body.GetWorldPosition();
    TandemStreet.Give(car.id, event.player, at, body.GetWorldOrientation(), body.GetLinearVelocity(), health, flags);
    // Our body is its proxy from now on.
    this.BecomeProxy(car, body);
    this.m_given += 1;
    TandemNative.Log(s"street: handover out: vehicle \(car.id) to player \(event.player), health \(health), flags \(flags)");
  }

  // A vehicle we ran is another game's now: our body stays as its proxy.
  // A car of our crowd is held out of its lane (the traffic system would
  // drive it against the new owner's states).
  private func BecomeProxy(car: ref<TandemCar>, body: ref<VehicleObject>) -> Void {
    car.mine = false;
    car.ready = true;
    car.wrecked = body.IsDestroyed();
    car.health = 1.0;
    let at = body.GetWorldPosition();
    car.x = at.X;
    car.y = at.Y;
    car.z = at.Z;
    if !GameInstance.GetDynamicEntitySystem().IsManaged(car.body) {
      car.own = true;
      if body.IsCrowdVehicle() {
        body.TandemHold(true);
        car.held = true;
      }
    }
  }

  // A vehicle we showed is ours to run from now on (handed over, or
  // inherited from an owner that is gone): the giver's state goes on our
  // body, which drives on. Same body: nothing pops.
  private func OnTake(event: TandemStreetEvent) -> Void {
    let car = this.Find(event.id);
    let body = IsDefined(car) ? TandemTraffic.Body(this.m_game, car.body) : null;
    if !IsDefined(car) || !IsDefined(body) || TandemElsewhere.AwayFromBody(this.m_game) {
      // Nothing here to run: it goes for everyone rather than stay with an
      // owner that does not run it.
      if IsDefined(car) {
        this.Forget(car.id);
        this.Retire(car);
      }
      TandemStreet.Answer(event.id, false);
      TandemNative.Log(s"street: handover in: vehicle \(event.id) from player \(event.player) refused, no body here");
      return;
    }
    car.mine = true;
    car.seen = true;
    car.claimed = false;
    car.flags = event.flags;
    car.health = event.amount;
    let inherited = (event.reason & 16) != 0;
    if !inherited {
      // Where the giver left it (our proxy was drawn a little in the past).
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, new Vector4(event.x, event.y, event.z, 1.0), Quaternion.ToEulerAngles(new Quaternion(event.i, event.j, event.k, event.r)));
    }
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    if (event.flags & 4) == 0 {
      pools.RequestSettingStatPoolValue(Cast<StatsObjectID>(car.body), gamedataStatPoolType.Health, ClampF(event.amount, 0.02, 1.0) * 100.0, null, true);
    }
    if car.held {
      // One of our own crowd's, given and back: into its lane again.
      body.TandemHold(false);
      car.held = false;
    } else if (event.flags & 5) == 0 {
      // A car on the move with a driver: it keeps the giver's speed and
      // joins our traffic (as a cab does when it goes free, final.reds:217147).
      body.TandemDriveOn(new Vector4(event.dx, event.dy, event.dz, 0.0));
    }
    car.own = false;
    TandemStreet.Answer(event.id, true);
    this.m_taken2 += 1;
    TandemNative.Log(s"street: handover in: vehicle \(car.id) from player \(event.player)\(inherited ? " (inherited)" : ""), health \(event.amount), \(Vector4.Length(new Vector4(event.dx, event.dy, event.dz, 0.0))) m/s");
  }

  // Whoever runs a vehicle changed. Ours without our giving it (our world
  // was rebuilt, an heir near it took it): our body is its proxy now.
  private func OnOwner(event: TandemStreetEvent) -> Void {
    let car = this.Find(event.id);
    if !IsDefined(car) || !car.mine || event.player == this.m_you {
      return;
    }
    let body = TandemTraffic.Body(this.m_game, car.body);
    if IsDefined(body) {
      this.BecomeProxy(car, body);
    } else {
      car.mine = false;
      car.ready = false;
    }
    TandemNative.Log(s"street: vehicle \(car.id) is no longer ours (now player \(event.player)'s)");
  }

  // ---- proxies follow ----

  private func Drive() -> Void {
    let teleports = GameInstance.GetTeleportationFacility(this.m_game);
    for car in this.m_cars {
      if !car.mine && !car.claimed {
        let body = TandemTraffic.Body(this.m_game, car.body);
        if IsDefined(body) {
          if !car.ready {
            car.ready = true;
            this.Show(car, body, true);
          }
          let at = TandemStreet.Position(car.id);
          // Only when it moved: a parked car is left to rest.
          if at.W != 0.0 && (AbsF(at.X - car.x) > 0.01 || AbsF(at.Y - car.y) > 0.01 || AbsF(at.Z - car.z) > 0.01) {
            car.x = at.X;
            car.y = at.Y;
            car.z = at.Z;
            at.W = 1.0;
            teleports.Teleport(body, at, Quaternion.ToEulerAngles(TandemStreet.Orientation(car.id)));
          }
        }
      }
    }
  }

  // Once a second: our proxies against the session's record
  // (docs/ARCHITECTURE.md, Consistency). A vehicle shared with no proxy is
  // spawned; a proxy its owner no longer shares goes; a proxy whose body is
  // gone (streamed out, our fast travel) is spawned again.
  private func Check() -> Void {
    let shared = TandemStreet.Shared();
    let ids: array<Uint32>;
    for entry in shared {
      ArrayPush(ids, entry.id);
      let car = this.Find(entry.id);
      if !IsDefined(car) {
        let i = ArrayFindFirst(this.m_missingIds, entry.id);
        if i < 0 {
          ArrayPush(this.m_missingIds, entry.id);
          ArrayPush(this.m_missingSince, this.m_now);
        } else if this.m_now - this.m_missingSince[i] > 2.0 {
          let event: TandemStreetEvent;
          event.kind = 1;
          event.id = entry.id;
          event.record = entry.record;
          event.appearance = entry.appearance;
          event.entity = entry.entity;
          event.x = entry.x;
          event.y = entry.y;
          event.z = entry.z;
          event.i = entry.i;
          event.j = entry.j;
          event.k = entry.k;
          event.r = entry.r;
          event.flags = entry.flags;
          event.reason = 0;
          event.player = 0u;
          event.amount = entry.health;
          event.dx = 0.0;
          event.dy = 0.0;
          event.dz = 0.0;
          event.npc = 0u;
          event.attack = 0;
          this.OnSpawn(event);
          let spawned = this.Find(entry.id);
          if IsDefined(spawned) {
            spawned.health = entry.health;
          }
          TandemNative.Log(s"street: check: vehicle \(entry.id) is shared and had no proxy: spawned");
        }
      } else if !car.mine && !car.claimed && !car.own {
        let body = TandemTraffic.Body(this.m_game, car.body);
        if IsDefined(body) || GameInstance.GetDynamicEntitySystem().IsSpawning(car.body) {
          car.goneSince = 0.0;
        } else if car.goneSince == 0.0 {
          car.goneSince = this.m_now;
        } else if this.m_now - car.goneSince > 3.0 {
          let at = TandemStreet.Position(car.id);
          if at.W == 0.0 {
            at = new Vector4(car.x, car.y, car.z, 1.0);
          }
          at.W = 1.0;
          car.body = TandemTraffic.SpawnProxy(car.record, car.appearance, at, TandemStreet.Orientation(car.id));
          car.ready = false;
          car.wrecked = false;
          car.siren = false;
          car.seated = false;
          car.goneSince = 0.0;
          TandemNative.Log(s"street: check: vehicle \(car.id)'s body was gone: spawned again");
        }
      }
    }
    // What is no longer missing is forgotten.
    let keptIds: array<Uint32>;
    let keptSince: array<Float>;
    let i = 0;
    while i < ArraySize(this.m_missingIds) {
      if ArrayContains(ids, this.m_missingIds[i]) && !IsDefined(this.Find(this.m_missingIds[i])) {
        ArrayPush(keptIds, this.m_missingIds[i]);
        ArrayPush(keptSince, this.m_missingSince[i]);
      }
      i += 1;
    }
    this.m_missingIds = keptIds;
    this.m_missingSince = keptSince;
    // A proxy of a vehicle no longer shared (its removal was lost). Ours are
    // never in the list (we run them).
    let orphans: array<ref<TandemCar>>;
    for car in this.m_cars {
      if !car.mine && !ArrayContains(ids, car.id) && !car.claimed && TandemStreet.RunBy(car.id) != this.m_you {
        ArrayPush(orphans, car);
      }
    }
    for car in orphans {
      this.Forget(car.id);
      this.Retire(car);
      TandemNative.Log(s"street: check: vehicle \(car.id) is no longer shared: proxy removed");
    }
    // (Our own crowd's vehicles where another game runs the street are
    // held back and counted in Share.)
  }

  // ---- ours: share the vehicles around the players ----

  private func Share(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let owns = TandemArea.Owns();
    let entities = GameInstance.GetDynamicEntitySystem();
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let viewers: array<Vector4>;
    ArrayPush(viewers, player.GetWorldPosition());
    for other in system.PuppetPlayers() {
      let puppet = system.PuppetOf(other);
      if IsDefined(puppet) {
        ArrayPush(viewers, puppet.GetWorldPosition());
      }
    }
    // The vehicle our V is in is ours (Seat, VehicleState): from now on.
    let mounted: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, mounted) && IsDefined(mounted) && !ArrayContains(this.m_taken, mounted.GetEntityID()) {
      ArrayPush(this.m_taken, mounted.GetEntityID());
    }
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let orientations: array<Quaternion>;
    let velocities: array<Vector4>;
    // Our V in another world (Elsewhere.reds): nothing around it is the city's.
    let away = TandemElsewhere.AwayFromBody(this.m_game);
    // The ones we run, wherever their body is (a proxy we woke up included).
    for car in this.m_cars {
      if car.mine {
        car.seen = false;
        let body = TandemTraffic.Body(this.m_game, car.body);
        let range = TandemRaces.Racer(this.m_game, body) ? TandemTraffic.RacerRange() : TandemTraffic.DropRange();
        if !away && IsDefined(body) && !ArrayContains(this.m_taken, car.body) && TandemTraffic.Nearest(body.GetWorldPosition(), viewers) <= range {
          car.seen = true;
          this.Say(car, body, pools);
          ArrayPush(ids, car.id);
          ArrayPush(positions, body.GetWorldPosition());
          ArrayPush(orientations, body.GetWorldOrientation());
          ArrayPush(velocities, body.GetLinearVelocity());
        }
      }
    }
    // New ones only where our game runs the area around our V (deep in a
    // higher-ranked player's region, its game announces the street).
    let live: array<wref<VehicleObject>>;
    let heldBack = 0;
    for vehicle in this.m_live {
      if IsDefined(vehicle) {
        ArrayPush(live, vehicle);
        let body = vehicle.GetEntityID();
        let at = vehicle.GetWorldPosition();
        // A car of our own crowd inside a higher-ranked linked player's
        // region: that game runs the street there. Ours is held back (it
        // is announced once it has driven out, if we run our area then).
        let held = !away && !IsDefined(this.FindBody(body)) && this.HeldBack(vehicle, body, at, owns, entities);
        if held {
          heldBack += 1;
          this.AskDespawn(vehicle, body);
        }
        // A racer of our street race is ours to share wherever it drives.
        let racer = !held && !away && TandemRaces.Racer(this.m_game, vehicle);
        if (owns || racer) && !held && !away && !IsDefined(this.FindBody(body)) && this.Eligible(vehicle, body, entities) && ArraySize(this.m_cars) < TandemTraffic.MaxShared() {
          if TandemTraffic.Nearest(at, viewers) <= (racer ? TandemTraffic.RacerRange() : TandemTraffic.ShareRange()) {
            let known = new TandemCar();
            known.id = TandemStreet.MakeId(this.m_you, this.m_next);
            this.m_next += 1u;
            known.body = body;
            known.record = vehicle.GetRecordID();
            known.appearance = vehicle.GetCurrentAppearanceName();
            known.flags = this.FlagsOf(vehicle);
            known.health = 1.0;
            known.mine = true;
            known.seen = true;
            ArrayPush(this.m_cars, known);
            // A placed vehicle is named by its own id: the same in every game.
            let entity = EntityID.IsStatic(body) ? body : EntityID.FromHash(0ul);
            TandemStreet.SendSpawn(known.id, known.record, known.appearance, entity, at, vehicle.GetWorldOrientation(), known.flags);
            ArrayPush(ids, known.id);
            ArrayPush(positions, at);
            ArrayPush(orientations, vehicle.GetWorldOrientation());
            ArrayPush(velocities, vehicle.GetLinearVelocity());
          }
        }
      }
    }
    this.m_live = live;
    if heldBack != this.m_heldBack {
      this.m_heldBack = heldBack;
      TandemNative.Log(s"street: \(heldBack) crowd vehicle(s) of our own held back (inside a higher-ranked player's region)");
    }
    let kept: array<ref<TandemCar>>;
    for car in this.m_cars {
      if !car.mine || car.seen {
        ArrayPush(kept, car);
      } else {
        // Out of everyone's range: a car of our world like any other (a
        // proxy we had woken up stays one of our spawned entities: it may
        // have a driver, so it is left as it is, never reused as a proxy).
        TandemStreet.SendGone(car.id, 0);
      }
    }
    this.m_cars = kept;
    if ArraySize(ids) > 0 {
      TandemStreet.SendStates(ids, positions, orientations, velocities, viewers);
    }
  }

  // A car of our own crowd that our game keeps out of the shared world: a
  // traffic car (not parked there before, not a quest's, the police's or a
  // player's) inside the region of a linked player that outranks us, or
  // anywhere near while that player's game runs our area.
  private func HeldBack(vehicle: ref<VehicleObject>, body: EntityID, at: Vector4, owns: Bool, entities: ref<DynamicEntitySystem>) -> Bool {
    if !vehicle.IsCrowdVehicle() || !this.Eligible(vehicle, body, entities) || vehicle.IsQuest() || vehicle.IsPrevention() || vehicle.IsDestroyed() {
      return false;
    }
    if !owns {
      return true;
    }
    let unplaced = false;
    return TandemRegionRules.CoveredBy(at, TandemRegionRules.Radius(), unplaced) != 0u;
  }

  // (overlap_despawn = on, a probe) A held back car is asked to leave, once:
  // the prevention system's own despawn of a vehicle and its passengers,
  // which waits until nobody looks.
  private func AskDespawn(vehicle: ref<VehicleObject>, body: EntityID) -> Void {
    if !this.m_despawnRead {
      this.m_despawnRead = true;
      this.m_despawn = Equals(TandemNative.Setting("overlap_despawn"), "on");
    }
    if !this.m_despawn || ArrayContains(this.m_despawnAsked, body) {
      return;
    }
    if ArraySize(this.m_despawnAsked) >= 256 {
      ArrayErase(this.m_despawnAsked, 0);
    }
    ArrayPush(this.m_despawnAsked, body);
    GameInstance.GetPreventionSpawnSystem(this.m_game).RequestDespawnVehicleAndPassengers(vehicle);
    TandemNative.LogDebug(s"street: asked our crowd vehicle \(EntityID.ToDebugString(body)) to despawn (held back)");
  }

  // (test) the overlap as the street has it.
  public func OverlapReport() -> String {
    let gone = 0;
    for body in this.m_despawnAsked {
      if !IsDefined(GameInstance.FindEntityByID(this.m_game, body)) {
        gone += 1;
      }
    }
    return s"overlap: street: area \(TandemArea.Owns() ? "ours" : "another game's"), \(this.m_heldBack) crowd vehicle(s) of our own held back, \(ArraySize(this.m_despawnAsked)) asked to despawn (\(gone) gone since)";
  }

  // One of ours this round: how it stands, said when it changed.
  private func Say(car: ref<TandemCar>, body: ref<VehicleObject>, pools: ref<StatPoolsSystem>) -> Void {
    let flags = this.FlagsOf(body);
    let health = pools.GetStatPoolValue(Cast<StatsObjectID>(car.body), gamedataStatPoolType.Health, true) / 100.0;
    if AbsF(health - car.health) >= 0.02 || flags != car.flags {
      car.health = health;
      car.flags = flags;
      TandemStreet.SendStatus(car.id, health, flags);
    }
  }

  // A vehicle of the world: a car or a bike that is nobody's (not a
  // player's garage car, not a copy or a proxy we made, not one a player
  // took), and not the NCART.
  private func Eligible(vehicle: ref<VehicleObject>, body: EntityID, entities: ref<DynamicEntitySystem>) -> Bool {
    return IsDefined(vehicle as WheeledObject) && !vehicle.IsPlayerVehicle() && !entities.IsManaged(body) && !ArrayContains(this.m_taken, body);
  }

  private func FlagsOf(vehicle: ref<VehicleObject>) -> Int32 {
    let flags = 0;
    if !VehicleComponent.HasActiveDriverMounted(this.m_game, vehicle.GetEntityID()) {
      flags += 1;
    }
    if vehicle.IsPrevention() {
      flags += 2;
    }
    if vehicle.IsDestroyed() {
      flags += 4;
    }
    let state = vehicle.GetVehiclePS();
    if IsDefined(state) && state.GetSirenState() {
      flags += 16;
    }
    if vehicle.IsPerformingPanicDriving() {
      flags += 64;
    }
    if vehicle.IsAbandoned() {
      flags += 128;
    }
    return flags;
  }

  private static func Nearest(point: Vector4, viewers: array<Vector4>) -> Float {
    let best = 1000000.0;
    for viewer in viewers {
      let distance = Vector4.Distance(point, viewer);
      if distance < best {
        best = distance;
      }
    }
    return best;
  }

  // Ours: a player's game hit one of our vehicles (its proxy there). The
  // damage is theirs to compute and ours to apply; the driver reacts as to
  // any hit or bump, and the outcome goes back to everyone as state.
  private func OnHit(event: TandemStreetEvent, system: ref<TandemSystem>) -> Void {
    let car = this.Find(event.id);
    if !IsDefined(car) || !car.mine {
      return;
    }
    let body = TandemTraffic.Body(this.m_game, car.body);
    if !IsDefined(body) || body.IsDestroyed() {
      return;
    }
    let by = system.PuppetOf(event.player);
    if event.amount > 0.0 {
      GameInstance.GetStatPoolsSystem(this.m_game).RequestChangingStatPoolValue(Cast<StatsObjectID>(car.body), gamedataStatPoolType.Health, -event.amount, by, false, false);
    }
    let push = new Vector4(event.dx, event.dy, event.dz, 0.0);
    if (event.flags & 1) != 0 {
      body.PhysicsWakeUp();
      body.TandemBumped(Vector4.Length(push));
    } else {
      body.TandemShotAt(new Vector4(event.x, event.y, event.z, 1.0));
    }
    TandemNative.Log(s"street: player \(event.player) hit vehicle \(car.id) for \(event.amount) (flags \(event.flags))");
  }

  // ---- a player's game: what our V does to a vehicle another game runs ----

  // Our hit landed on a proxy: tell its owner.
  public func OurHit(vehicle: ref<VehicleObject>, hit: ref<gameHitEvent>) -> Void {
    if this.m_you == 0u {
      return;
    }
    let id = this.ProxyOfBody(vehicle.GetEntityID());
    let by = hit.attackData.GetInstigator();
    if id == 0u || !IsDefined(by) || !by.IsPlayer() {
      return;
    }
    let damage = hit.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
    if damage <= 0.0 {
      return;
    }
    let flags = 0;
    let kind = hit.attackData.GetAttackType();
    if Equals(kind, gamedataAttackType.Explosion) || Equals(kind, gamedataAttackType.PressureWave) {
      flags = 2;
    } else if Equals(kind, gamedataAttackType.Melee) || Equals(kind, gamedataAttackType.QuickMelee) || Equals(kind, gamedataAttackType.StrongMelee) {
      flags = 4;
    }
    TandemStreet.SendHit(id, damage, flags, hit.hitPosition, hit.hitDirection);
  }

  // The car our V drives and a proxy bumped into each other: our game ran
  // our car's side; the push goes to the vehicle's owner.
  public func Bumped(vehicle: ref<VehicleObject>, other: ref<VehicleObject>, impact: Float, normal: Vector3) -> Void {
    if this.m_you == 0u || !IsDefined(other) || impact <= 0.5 {
      return;
    }
    let player = GetPlayer(this.m_game);
    let mine: wref<VehicleObject>;
    if !IsDefined(player) || !VehicleComponent.GetVehicle(this.m_game, player, mine) || !IsDefined(mine) {
      return;
    }
    let proxy = mine == vehicle ? other : vehicle;
    if mine != vehicle && mine != other {
      return;
    }
    let id = this.ProxyOfBody(proxy.GetEntityID());
    if id == 0u {
      return; // ours: our own game runs both sides
    }
    // One per contact.
    let body = proxy.GetEntityID();
    let i = ArrayFindFirst(this.m_ramBodies, body);
    if i >= 0 && this.m_now - this.m_ramAt[i] < 0.4 {
      return;
    }
    if i < 0 {
      ArrayPush(this.m_ramBodies, body);
      ArrayPush(this.m_ramAt, this.m_now);
    } else {
      this.m_ramAt[i] = this.m_now;
    }
    while ArraySize(this.m_ramBodies) > 16 {
      ArrayErase(this.m_ramBodies, 0);
      ArrayErase(this.m_ramAt, 0);
    }
    // Away from our car, as hard as the game says it hit.
    let way = proxy.GetWorldPosition() - mine.GetWorldPosition();
    way.Z = 0.0;
    way = Vector4.Normalize(way) * impact;
    TandemStreet.SendHit(id, 0.0, 1, proxy.GetWorldPosition(), way);
    TandemNative.Log(s"street: our car rammed vehicle \(id) at \(impact) m/s");
  }
}

// ---- the game's own vehicles, as they come and go ----

// As the world streams in, on job threads during a load: queued only,
// taken by TandemTraffic.Drain on the game thread's tick.
@wrapMethod(VehicleObject)
protected cb func OnGameAttached() -> Bool {
  let result = wrappedMethod();
  TandemStreaming.QueueStreamed(TandemStreamKind.Vehicle(), this.GetEntityID(), n"None", true);
  return result;
}

@wrapMethod(VehicleObject)
protected cb func OnDetach() -> Bool {
  TandemStreaming.QueueStreamed(TandemStreamKind.Vehicle(), this.GetEntityID(), n"None", false);
  return wrappedMethod();
}

// Our V gets into a vehicle: a shared one becomes ours.
@wrapMethod(VehicleObject)
protected cb func OnMountingEvent(evt: ref<MountingEvent>) -> Bool {
  TandemNative.HookRan(n"Traffic.VehicleObject.OnMountingEvent");
  let result = wrappedMethod(evt);
  let child = GameInstance.FindEntityByID(this.GetGame(), evt.request.lowLevelMountingInfo.childId) as GameObject;
  if IsDefined(child) && child.IsPlayer() {
    let traffic = TandemTraffic.Of(this.GetGame());
    if IsDefined(traffic) {
      traffic.PlayerMounts(this, evt.request.lowLevelMountingInfo.slotId.id);
    }
  }
  return result;
}

// Our hit on a vehicle another game owns goes to its owner.
@wrapMethod(VehicleObject)
protected func DamagePipelineFinalized(evt: ref<gameHitEvent>) -> Void {
  wrappedMethod(evt);
  let traffic = TandemTraffic.Of(this.GetGame());
  if IsDefined(traffic) {
    traffic.OurHit(this, evt);
  }
}

// A bump between the car our V drives and a shared vehicle.
@wrapMethod(VehicleObject)
protected cb func OnVehicleBumpEvent(evt: ref<VehicleBumpEvent>) -> Bool {
  TandemNative.HookRan(n"Traffic.VehicleObject.OnVehicleBumpEvent");
  let result = wrappedMethod(evt);
  let traffic = TandemTraffic.Of(this.GetGame());
  if IsDefined(traffic) {
    traffic.Bumped(this, evt.hitVehicle, evt.impactVelocityChange, evt.hitNormal);
  }
  return result;
}

// Owner: the game's own reactions of a traffic vehicle, for a hit that
// happened on another player's screen.
@addMethod(VehicleObject)
public final func TandemBumped(impact: Float) -> Void {
  if impact > 0.0 && this.IsInTrafficPhysicsState() {
    this.HandleTrafficBump(impact);
  }
}

@addMethod(VehicleObject)
public final func TandemShotAt(from: Vector4) -> Void {
  if VehicleComponent.HasActiveDriverMounted(this.GetGame(), this.GetEntityID()) && !this.IsPrevention() && this.CanStartPanicDriving() {
    this.TriggerDrivingPanicBehavior(from);
  }
}

// A crowd car of ours given to another game: held out of its lane while it
// is a proxy there (the traffic system would drive it against the owner's
// states, which teleport it), and back into its lane when it is ours again
// ("stop" and "normal" are the game's own patterns: OnStuckEvent,
// OnCrowdSettingsEvent).
@addMethod(VehicleObject)
public final func TandemHold(held: Bool) -> Void {
  if IsDefined(this.m_crowdMemberComponent) {
    this.m_drivingTrafficPattern = held ? n"stop" : n"normal";
    this.m_crowdMemberComponent.ChangeMoveType(this.m_drivingTrafficPattern);
  }
}

// A proxy we run from now on, handed over on the move: it keeps the giver's
// speed (the push of its mass times that velocity, as Wheels.reds pushes a
// car) and joins our traffic the way a cab does when it goes free
// (DelamainTaxi.GoToFreeRoam, final.reds:217147).
@addMethod(VehicleObject)
public final func TandemDriveOn(velocity: Vector4) -> Void {
  this.PhysicsWakeUp();
  if Vector4.Length(velocity) > 0.5 {
    let impulse = new PhysicalImpulseEvent();
    impulse.radius = 1.0;
    let at = this.GetWorldPosition();
    impulse.worldPosition.X = at.X;
    impulse.worldPosition.Y = at.Y;
    impulse.worldPosition.Z = at.Z + 0.5;
    let mass = this.GetTotalMass();
    impulse.worldImpulse.X = velocity.X * mass;
    impulse.worldImpulse.Y = velocity.Y * mass;
    impulse.worldImpulse.Z = velocity.Z * mass;
    this.QueueEvent(impulse);
  }
  let ai = this.GetAIComponent();
  if IsDefined(ai) {
    let command = new AIVehicleJoinTrafficCommand();
    command.needDriver = false;
    command.useKinematic = true;
    ai.SendCommand(command);
  }
}
