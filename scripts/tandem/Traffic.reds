// World vehicles: traffic, parked cars, police cars (docs/architecture/traffic.md).
// One game runs them: the owner of the area (TandemArea: the host today). It
// announces each vehicle near a player (TandemStreet.SendSpawn), says where
// they all are ten times a second and how each stands (health, wreck,
// siren). The other games show each one as a proxy: a copy of the same
// record moved to where the owner says, or their own instance when it is a
// placed vehicle (the same EntityID in every game).
//
// A vehicle a player gets into becomes that player's car under the usual
// rule (Vehicles.reds): the first claim wins (TandemStreet.Claim), and
// every game hands the body it has (the real car at the owner's, the proxy
// elsewhere) to that player's seat as its car copy.

// Who runs the world around a player: its NPCs, its traffic, its police.
// The host's game today, everywhere; with per-player regions
// (docs/research/coop-regions.md; in progress on its own branch) the owner of the player's group. Traffic
// and police are written against this alone.
public abstract class TandemArea {
  // Our game runs the world around our V.
  public static func Owns() -> Bool {
    return TandemNative.IsHost();
  }

  // Our game runs the world around that player's V (its puppet here).
  public static func OwnsAround(player: Uint32) -> Bool {
    return TandemNative.IsHost();
  }
}

public class TandemCar {
  public let id: Uint32;
  // Owner: the real vehicle. Others: the proxy (a copy we spawned, or our
  // own instance of a placed vehicle).
  public let body: EntityID;
  public let record: TweakDBID;
  public let appearance: CName;
  // proto::kTraffic*: 1 parked, 2 police, 4 destroyed, 8 burning, 16 siren,
  // 32 lights, 64 panic, 128 abandoned.
  public let flags: Int32;
  public let health: Float;
  // Owner: listed this round.
  public let seen: Bool;
  // Others: our own instance of a placed vehicle (never spawned, never deleted).
  public let own: Bool;
  // Others: its body exists and shows how the owner says it stands.
  public let ready: Bool;
  public let wrecked: Bool;
  public let siren: Bool;
  // Our V is getting in: we no longer move it (the claim is on its way).
  public let claimed: Bool;
  // Someone has sat in it here: its body is never deleted (engine-knowledge §13.6).
  public let seated: Bool;
  // Others: where we last put it.
  public let x: Float;
  public let y: Float;
  public let z: Float;
  // Others, the consistency checks: since when its body is missing (0: not so).
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
  // Others, the checks: vehicles shared that we do not show, since when.
  private let m_missingIds: array<Uint32>;
  private let m_missingSince: array<Float>;
  // Rams already told, by body: one hit per contact, not one per frame.
  private let m_ramBodies: array<EntityID>;
  private let m_ramAt: array<Float>;
  private let m_ownLeft: Int32;

  public static func ShareRange() -> Float {
    return 120.0;
  }

  public static func DropRange() -> Float {
    return 150.0;
  }

  public static func MaxShared() -> Int32 {
    return 48;
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

  public func Attached(vehicle: ref<VehicleObject>) -> Void {
    if IsDefined(vehicle) && !ArrayContains(this.m_live, vehicle) {
      ArrayPush(this.m_live, vehicle);
    }
  }

  public func Detached(vehicle: ref<VehicleObject>) -> Void {
    ArrayRemove(this.m_live, vehicle);
  }

  public func Tick(now: Float, you: Uint32, system: ref<TandemSystem>) -> Void {
    this.m_now = now;
    this.m_you = you;
    if you == 0u {
      return;
    }
    this.TakeEvents(system);
    if TandemArea.Owns() {
      if now - this.m_last >= 0.1 {
        this.m_last = now;
        this.Share(system);
      }
    } else {
      this.Drive();
      if now - this.m_lastCheck >= 1.0 {
        this.m_lastCheck = now;
        this.Check();
      }
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

  // The session is over: the owner's vehicles go, ours are our own again.
  public func End() -> Void {
    if !TandemArea.Owns() {
      for car in this.m_cars {
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
    }
    let role = TandemArea.Owns() ? "shared by us" : "shown as proxies";
    return s"count: \(ArraySize(this.m_cars)) \(role) (\(parked) parked, \(wrecks) wrecks, \(own) our own placed), \(ArraySize(TandemStreet.Shared())) in the session's record, \(ArraySize(this.m_live)) vehicles in our world, \(ArraySize(this.m_taken)) taken by players, \(ArraySize(this.m_retired)) proxies kept for reuse";
  }

  // The shared vehicle a body is (0: none).
  public func IdOfBody(body: EntityID) -> Uint32 {
    for car in this.m_cars {
      if car.body == body {
        return car.id;
      }
    }
    return 0u;
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
      }
    }
  }

  // Others: a vehicle the owner shares from now on.
  private func OnSpawn(event: TandemStreetEvent) -> Void {
    if TandemArea.Owns() || IsDefined(this.Find(event.id)) {
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
      TandemNative.Log(s"street: vehicle \(car.id) \(TDBID.ToStringDEBUG(car.record)) (flags \(car.flags)) spawning at (\(event.x), \(event.y), \(event.z))");
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
      if !TandemArea.Owns() {
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
    if TandemArea.Owns() && IsDefined(body) {
      this.Evict(body);
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

  // Others: a proxy no longer shown. Deleted when nobody ever sat in it,
  // else kept for the next vehicle of its record (a body that has had an
  // occupant must not be deleted, §13.6); our own placed vehicle stays.
  private func Retire(car: ref<TandemCar>) -> Void {
    if car.own || !EntityID.IsDefined(car.body) {
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
    if !IsDefined(car) || TandemArea.Owns() {
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

  // Others: the proxy as the owner says it stands. `joining`: it stood so
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

  // ---- others: the proxies follow ----

  private func Drive() -> Void {
    let teleports = GameInstance.GetTeleportationFacility(this.m_game);
    for car in this.m_cars {
      if !car.claimed {
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

  // Others, once a second: our proxies against the session's record
  // (docs/ARCHITECTURE.md, Consistency). A vehicle shared with no proxy is
  // spawned; a proxy the owner no longer shares goes; a proxy whose body is
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
      } else if !car.claimed && !car.own {
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
    // A proxy of a vehicle no longer shared (its removal was lost).
    let orphans: array<ref<TandemCar>>;
    for car in this.m_cars {
      if !ArrayContains(ids, car.id) && !car.claimed {
        ArrayPush(orphans, car);
      }
    }
    for car in orphans {
      this.Forget(car.id);
      this.Retire(car);
      TandemNative.Log(s"street: check: vehicle \(car.id) is no longer shared: proxy removed");
    }
    // Our own traffic should be off with our crowd (Npcs.reds): what is
    // left of it is counted, for the log.
    let left = 0;
    for vehicle in this.m_live {
      if IsDefined(vehicle) && vehicle.IsCrowdVehicle() && !IsDefined(this.FindBody(vehicle.GetEntityID())) {
        left += 1;
      }
    }
    if left != this.m_ownLeft {
      this.m_ownLeft = left;
      TandemNative.Log(s"street: \(left) crowd vehicle(s) of our own world around, \(ArraySize(this.m_cars)) shared");
    }
  }

  // ---- owner: share the vehicles around the players ----

  private func Share(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
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
    let mine: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, mine) && IsDefined(mine) && !ArrayContains(this.m_taken, mine.GetEntityID()) {
      ArrayPush(this.m_taken, mine.GetEntityID());
    }
    for car in this.m_cars {
      car.seen = false;
    }
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let orientations: array<Quaternion>;
    let velocities: array<Vector4>;
    let live: array<wref<VehicleObject>>;
    // Our V in another world (Elsewhere.reds): nothing around it is the city's.
    let away = TandemElsewhere.AwayFromBody(this.m_game);
    for vehicle in this.m_live {
      if IsDefined(vehicle) {
        ArrayPush(live, vehicle);
        let body = vehicle.GetEntityID();
        if !away && this.Eligible(vehicle, body, entities) {
          let at = vehicle.GetWorldPosition();
          let known = this.FindBody(body);
          let range = IsDefined(known) ? TandemTraffic.DropRange() : TandemTraffic.ShareRange();
          if TandemTraffic.Nearest(at, viewers) <= range {
            let flags = this.FlagsOf(vehicle);
            let health = pools.GetStatPoolValue(Cast<StatsObjectID>(body), gamedataStatPoolType.Health, true) / 100.0;
            if !IsDefined(known) && ArraySize(this.m_cars) < TandemTraffic.MaxShared() {
              known = new TandemCar();
              known.id = TandemStreet.MakeId(this.m_you, this.m_next);
              this.m_next += 1u;
              known.body = body;
              known.record = vehicle.GetRecordID();
              known.appearance = vehicle.GetCurrentAppearanceName();
              known.flags = flags;
              known.health = 1.0;
              ArrayPush(this.m_cars, known);
              // A placed vehicle is named by its own id: the same in every game.
              let entity = EntityID.IsStatic(body) ? body : EntityID.FromHash(0ul);
              TandemStreet.SendSpawn(known.id, known.record, known.appearance, entity, at, vehicle.GetWorldOrientation(), flags);
            }
            if IsDefined(known) {
              known.seen = true;
              if AbsF(health - known.health) >= 0.02 || flags != known.flags {
                known.health = health;
                known.flags = flags;
                TandemStreet.SendStatus(known.id, health, flags);
              }
              ArrayPush(ids, known.id);
              ArrayPush(positions, at);
              ArrayPush(orientations, vehicle.GetWorldOrientation());
              ArrayPush(velocities, vehicle.GetLinearVelocity());
            }
          }
        }
      }
    }
    this.m_live = live;
    let kept: array<ref<TandemCar>>;
    for car in this.m_cars {
      if car.seen {
        ArrayPush(kept, car);
      } else {
        TandemStreet.SendGone(car.id, 0);
      }
    }
    this.m_cars = kept;
    TandemStreet.SendStates(ids, positions, orientations, velocities, viewers);
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

  // Owner: a player's game hit one of our vehicles (its proxy there). The
  // damage is theirs to compute and ours to apply; the driver reacts as to
  // any hit or bump, and the outcome goes back to everyone as state.
  private func OnHit(event: TandemStreetEvent, system: ref<TandemSystem>) -> Void {
    let car = this.Find(event.id);
    if !IsDefined(car) || !TandemArea.Owns() {
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

  // ---- a player's game: what our V does to a shared vehicle ----

  // Our hit landed on a vehicle another game owns: tell its owner.
  public func OurHit(vehicle: ref<VehicleObject>, hit: ref<gameHitEvent>) -> Void {
    if this.m_you == 0u || TandemArea.Owns() {
      return;
    }
    let id = this.IdOfBody(vehicle.GetEntityID());
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

  // The car our V drives and a shared vehicle bumped into each other: our
  // game ran our car's side; the push goes to the vehicle's owner.
  public func Bumped(vehicle: ref<VehicleObject>, other: ref<VehicleObject>, impact: Float, normal: Vector3) -> Void {
    if this.m_you == 0u || TandemArea.Owns() || !IsDefined(other) || impact <= 0.5 {
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
    let id = this.IdOfBody(proxy.GetEntityID());
    if id == 0u {
      return;
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

@wrapMethod(VehicleObject)
protected cb func OnGameAttached() -> Bool {
  let result = wrappedMethod();
  let traffic = TandemTraffic.Of(this.GetGame());
  if IsDefined(traffic) {
    traffic.Attached(this);
  }
  return result;
}

@wrapMethod(VehicleObject)
protected cb func OnDetach() -> Bool {
  let traffic = TandemTraffic.Of(this.GetGame());
  if IsDefined(traffic) {
    traffic.Detached(this);
  }
  return wrappedMethod();
}

// Our V gets into a vehicle: a shared one becomes ours.
@wrapMethod(VehicleObject)
protected cb func OnMountingEvent(evt: ref<MountingEvent>) -> Bool {
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
