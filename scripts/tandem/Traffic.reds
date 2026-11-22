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
  // Someone has sat in it here: its body is never deleted (engine-knowledge
  // §13.6; it stays in the pool, TandemPooled.seated).
  public let seated: Bool;
  // Proxies: where we last put it.
  public let x: Float;
  public let y: Float;
  public let z: Float;
  // Proxies, the consistency checks: since when its body is missing (0: not so).
  public let goneSince: Float;
  // (a proxy) how many times its body was spawned again in a row.
  public let goneTries: Int32;
  // (a proxy) how its body is moved (TandemSystem.MoveBody).
  public let move: ref<TandemMoved>;
  // (ours) stopped for a partner's V on foot in its lane; since when the
  // lane is clear again (0: it is not).
  public let yields: Bool;
  public let clearSince: Float;
  // (ours) its speed as it moved between two looks (a car the traffic
  // system drives on its lane may read no velocity of its own), and when
  // it was last looked at.
  public let speed: Float;
  public let lookedAt: Float;
}

// A body a player took (TrafficGone taken): that player's car copy here,
// for System.reds to seat its puppet in instead of spawning another.
public class TandemAdopted {
  public let player: Uint32;
  public let body: EntityID;
  public let at: Float;
}

// The body of a proxy no longer shown: parked out of the world, to be the
// body of the next vehicle of its record (traffic.md, "Proxy bodies").
public class TandemPooled {
  public let body: EntityID;
  public let record: TweakDBID;
  // The look it wears.
  public let appearance: CName;
  // When it left (real time).
  public let since: Float;
  // In its parking place: which level, where, and what holds it there.
  public let placed: Bool;
  public let slot: Int32;
  public let x: Float;
  public let y: Float;
  public let z: Float;
  public let i: Float;
  public let j: Float;
  public let k: Float;
  public let r: Float;
  public let move: ref<TandemMoved>;
  // Never the body of another vehicle (a wreck, damaged, over the pool's
  // bounds, unused too long): deleted once that is safe.
  public let spent: Bool;
  // Someone has sat in it: never deleted (engine-knowledge §13.6).
  public let seated: Bool;
  // Asked for and not here yet when its vehicle left.
  public let coming: Bool;
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
  // Proxy bodies not in use (the body pool), and what became of every proxy
  // body asked for in this run of the game (`street count`).
  private let m_pool: array<ref<TandemPooled>>;
  private let m_poolSwept: Float;
  private let m_created: Int32;
  private let m_reused: Int32;
  private let m_relooked: Int32;
  private let m_parked: Int32;
  private let m_deleted: Int32;
  private let m_lost: Int32;
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
  // Ours: how many times a car stopped for a partner's V on foot.
  private let m_yields: Int32;

  public static func ShareRange() -> Float {
    return 120.0;
  }

  public static func DropRange() -> Float {
    return 150.0;
  }

  public static func MaxShared() -> Int32 {
    return 48;
  }

  // The body pool (traffic.md, "Proxy bodies"): how many parked bodies may
  // wait for another vehicle, in all and of one record; how long one waits
  // unused; how long a body is parked before it may be deleted; where they
  // are parked (under the place they left, one level each).
  public static func PoolSize() -> Int32 {
    return 32;
  }

  public static func PoolPerRecord() -> Int32 {
    return 3;
  }

  public static func PoolIdle() -> Float {
    return 300.0;
  }

  public static func PoolDeleteAfter() -> Float {
    return 5.0;
  }

  public static func PoolDepth() -> Float {
    return 600.0;
  }

  public static func PoolLevel() -> Float {
    return 10.0;
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
  // streams in: VehicleObject.OnGameAttached, OnDetach); and the parked
  // bodies, which outlive a session.
  public func Drain() -> Void {
    this.Keep();
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
    for mounted in TandemStreaming.TakeStreamed(TandemStreamKind.Mounted()) {
      this.PlayerMounts(mounted.entity, mounted.cls);
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
    let bodies = 0;
    for car in this.m_cars {
      if !car.mine && IsDefined(TandemTraffic.Body(this.m_game, car.body)) {
        bodies += 1;
      }
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
    let shared = TandemStreet.Shared();
    return s"count: \(ArraySize(this.m_cars)) (\(mine) run by us, \(ArraySize(this.m_cars) - mine) shown as proxies; \(parked) parked, \(wrecks) wrecks, \(own) bodies of our own world), area \(area), \(ArraySize(shared)) proxies in the session's record, \(ArraySize(this.m_live)) vehicles in our world, \(ArraySize(this.m_taken)) taken by players, \(this.m_given) given, \(this.m_taken2) taken over, \(bodies) proxy body(ies) here, \(this.m_yields) stop(s) for a player on foot, \(this.PoolReport())";
  }

  // (test) what became of every proxy body asked for in this run of the
  // game, and the pool now.
  public func PoolReport() -> String {
    let ready = 0;
    let spent = 0;
    let kept = 0;
    let coming = 0;
    for entry in this.m_pool {
      if entry.coming {
        coming += 1;
      } else if !entry.spent {
        ready += 1;
      } else if entry.seated {
        kept += 1;
      } else {
        spent += 1;
      }
    }
    return s"bodies: \(this.m_created) created, \(this.m_reused) re-used (\(this.m_relooked) with another look), \(this.m_parked) parked, \(this.m_deleted) deleted, \(this.m_lost) lost; pool \(ArraySize(this.m_pool)) (\(ready) ready, \(spent) to delete, \(kept) kept for good, \(coming) not here yet)";
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

  // From VehicleObject.OnMountingEvent (queued: it may run on a job thread),
  // for our own V: a shared vehicle becomes ours if we are first (the answer
  // is a Gone event, taken).
  private func PlayerMounts(body: EntityID, slot: CName) -> Void {
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
      let parked = this.Obtain(car, at, turn);
      TandemNative.Log(s"street: vehicle \(car.id) \(TDBID.ToStringDEBUG(car.record)) of player \(TandemStreet.RunBy(car.id)) (flags \(car.flags)) \(parked ? "takes a parked body" : "spawning") at (\(event.x), \(event.y), \(event.z))");
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
    return TandemEntities.Create(spec);
  }

  // ---- the body pool (traffic.md, "Proxy bodies") ----
  // A proxy that leaves is not deleted and the next one not created: its
  // body is parked out of the world and becomes the body of the next
  // vehicle of its record. Only a body that stands as it spawned is used
  // again; the others, and the ones over the pool's bounds, are deleted
  // later, one at a time, never while one is being created (§13.140) and
  // never one someone sat in (§13.6).

  // The body of a vehicle shown from now on: a parked one of its record put
  // there (true), else a new one asked of the game.
  private func Obtain(car: ref<TandemCar>, position: Vector4, orientation: Quaternion) -> Bool {
    let pooled = this.Take(car.record, car.appearance, position, orientation);
    if IsDefined(pooled) {
      car.body = pooled.body;
      car.seated = pooled.seated;
      this.m_reused += 1;
      return true;
    }
    car.body = TandemTraffic.SpawnProxy(car.record, car.appearance, position, orientation);
    this.m_created += 1;
    return false;
  }

  // A parked body of this record, out of the pool and put at the new
  // vehicle's place (a discontinuity: a teleport, and the first move of its
  // new TandemCar is one too, TandemSystem.MoveBody). One that wears the
  // look asked for first; another is asked to change its look. Its state is
  // the new vehicle's to write (Drive: `ready` is false on a new TandemCar,
  // so Show runs once); what Show does not write is why only a body that
  // stands as it spawned is taken (Pristine, Sound).
  private func Take(record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion) -> ref<TandemPooled> {
    let found = -1;
    let body: ref<VehicleObject>;
    // One that wears the look asked for first, then any of the record.
    let pass = 0;
    while pass < 2 && found < 0 {
      let i = 0;
      while i < ArraySize(this.m_pool) && found < 0 {
        let candidate = this.m_pool[i];
        let same = !IsNameValid(appearance) || Equals(candidate.appearance, appearance);
        if !candidate.spent && !candidate.coming && candidate.record == record && (pass == 1 || same) {
          body = TandemTraffic.Body(this.m_game, candidate.body);
          // (Not here: gone from where it was parked, the sweep sorts it out.)
          if IsDefined(body) {
            if !this.Sound(body) {
              // Something happened to it where it was parked.
              candidate.spent = true;
            } else if !this.Occupied(body) {
              found = i;
            }
          }
        }
        i += 1;
      }
      pass += 1;
    }
    if found < 0 {
      return null;
    }
    let entry = this.m_pool[found];
    ArrayErase(this.m_pool, found);
    if IsNameValid(appearance) && NotEquals(body.GetCurrentAppearanceName(), appearance) {
      body.ScheduleAppearanceChange(appearance);
      this.m_relooked += 1;
    }
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(body, position, Quaternion.ToEulerAngles(orientation));
    return entry;
  }

  // A proxy as its owner had it when it left: never damaged, burning or a
  // wreck, no tyre or dent said of it (Wheels.reds), not rammed by our car.
  private func Pristine(car: ref<TandemCar>) -> Bool {
    if car.wrecked || (car.flags & 12) != 0 || car.health < 0.99 {
      return false;
    }
    let damage = TandemWheels.DamageOf(TandemWheels.StreetOf(car.id));
    if damage.known {
      return false;
    }
    return !ArrayContains(this.m_ramBodies, car.body);
  }

  // A body as it spawned, as far as a script reads it: not destroyed,
  // exploded or drowned, its health whole (a hit of ours on a proxy lands on
  // its own health at once), no flat tyre.
  private func Sound(body: ref<VehicleObject>) -> Bool {
    if body.IsDestroyed() {
      return false;
    }
    let state = body.GetVehiclePS();
    if IsDefined(state) && (state.GetIsDestroyed() || state.GetIsSubmerged() || state.GetHasExploded()) {
      return false;
    }
    let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(body.GetEntityID()), gamedataStatPoolType.Health, true);
    if health < 99.0 {
      return false;
    }
    let wheeled = body as WheeledObject;
    return !IsDefined(wheeled) || wheeled.GetFlatTireIndex() < 0;
  }

  // Someone sits in it: each seat read as the seat check reads it (Evict).
  private func Occupied(body: ref<VehicleObject>) -> Bool {
    let seats: array<wref<VehicleSeat_Record>>;
    VehicleComponent.GetSeats(this.m_game, body, seats);
    let mounting = GameInstance.GetMountingFacility(this.m_game);
    for seat in seats {
      let slotId: MountingSlotId;
      slotId.id = seat.SeatName();
      let info = mounting.GetMountingInfoSingleWithIds(EntityID.FromHash(0ul), body.GetEntityID(), slotId);
      if EntityID.IsDefined(info.childId) {
        return true;
      }
    }
    return false;
  }

  // (test reset) No parked body waits for the next scenario's vehicles: all
  // are let go (deleted by the sweep as any other; one someone sat in
  // stays, never used again), and the counts start again.
  public func ResetPool() -> Void {
    for entry in this.m_pool {
      entry.spent = true;
    }
    this.m_created = 0;
    this.m_reused = 0;
    this.m_relooked = 0;
    this.m_parked = 0;
    this.m_deleted = 0;
    this.m_lost = 0;
  }

  // Over the pool's bounds the bodies parked longest are let go (the sweep
  // deletes them); one someone sat in stays.
  private func Bound(record: TweakDBID) -> Void {
    let all = 0;
    let same = 0;
    for entry in this.m_pool {
      if !entry.spent {
        all += 1;
        if entry.record == record {
          same += 1;
        }
      }
    }
    for entry in this.m_pool {
      if !entry.spent && !entry.seated {
        if entry.record == record && same > TandemTraffic.PoolPerRecord() {
          entry.spent = true;
          same -= 1;
          all -= 1;
        } else if all > TandemTraffic.PoolSize() {
          entry.spent = true;
          all -= 1;
          if entry.record == record {
            same -= 1;
          }
        }
      }
    }
  }

  // Into its parking place: 600 m under where it left, a level of its own
  // (no two parked bodies touch), out of sight and out of every collision.
  // Held there by the move every other proxy is moved by (a first move: a
  // teleport; then nothing while it rests there, the kinematic move when it
  // sagged 5 cm).
  private func Place(entry: ref<TandemPooled>, body: ref<VehicleObject>, system: ref<TandemSystem>) -> Void {
    let slot = 0;
    let free = false;
    while !free {
      free = true;
      for other in this.m_pool {
        if other.placed && other.slot == slot {
          free = false;
        }
      }
      if !free {
        slot += 1;
      }
    }
    let at = body.GetWorldPosition();
    let turn = body.GetWorldOrientation();
    entry.slot = slot;
    entry.x = at.X;
    entry.y = at.Y;
    entry.z = -TandemTraffic.PoolDepth() - TandemTraffic.PoolLevel() * Cast<Float>(slot);
    entry.i = turn.i;
    entry.j = turn.j;
    entry.k = turn.k;
    entry.r = turn.r;
    entry.placed = true;
    entry.move = new TandemMoved();
    system.MoveBody(entry.move, body, new Vector4(entry.x, entry.y, entry.z, 1.0), turn);
  }

  // Every tick, session or not: a body that left is parked once nobody sits
  // in it (until then it stands where it was), a parked one is held in its
  // place; twice a second the sweep.
  private func Keep() -> Void {
    if ArraySize(this.m_pool) == 0 {
      return;
    }
    let system = TandemSystem.Of(this.m_game);
    if !IsDefined(system) {
      return;
    }
    for entry in this.m_pool {
      let body = TandemTraffic.Body(this.m_game, entry.body);
      if IsDefined(body) {
        entry.coming = false;
        if entry.placed {
          system.MoveBody(entry.move, body, new Vector4(entry.x, entry.y, entry.z, 1.0), new Quaternion(entry.i, entry.j, entry.k, entry.r));
        } else if !this.Occupied(body) {
          this.Place(entry, body, system);
        }
      }
    }
    let now = TandemClock.Real(this.m_game);
    if now - this.m_poolSwept >= 0.5 || now < this.m_poolSwept {
      this.m_poolSwept = now;
      this.Sweep(now);
    }
  }

  // Twice a second: a body unused for PoolIdle is let go; one let go (or
  // never fit to be used again) is deleted once it has been parked for
  // PoolDeleteAfter, exists (never one being created: §13.140) and nobody
  // sits in it, one deletion a sweep; one someone sat in is never deleted
  // (§13.6); one that is gone (a save loaded, streamed out) or never came
  // is forgotten.
  private func Sweep(now: Float) -> Void {
    let entities = GameInstance.GetDynamicEntitySystem();
    let kept: array<ref<TandemPooled>>;
    let deleted = false;
    for entry in this.m_pool {
      let keep = true;
      if !entities.IsManaged(entry.body) {
        keep = false;
        this.m_lost += 1;
      } else {
        let body = TandemTraffic.Body(this.m_game, entry.body);
        if !IsDefined(body) {
          if !entities.IsSpawning(entry.body) && now - entry.since > TandemEntities.WaitLimit() {
            TandemEntities.Delete(entry.body);
            keep = false;
            this.m_lost += 1;
            TandemNative.LogDebug(s"street: pool: body \(EntityID.ToDebugString(entry.body)) is not here (\(entry.coming ? "it never came" : "gone from its parking place")): forgotten");
          }
        } else {
          if !entry.spent && !entry.seated && now - entry.since > TandemTraffic.PoolIdle() {
            entry.spent = true;
          }
          if entry.spent && !entry.seated && entry.placed && !deleted && now - entry.since >= TandemTraffic.PoolDeleteAfter() && entities.IsSpawned(entry.body) && !this.Occupied(body) {
            TandemEntities.Delete(entry.body);
            deleted = true;
            keep = false;
            this.m_deleted += 1;
            TandemNative.LogDebug(s"street: pool: body \(EntityID.ToDebugString(entry.body)) deleted, parked \(now - entry.since) s");
          }
        }
      }
      if keep {
        ArrayPush(kept, entry);
      }
    }
    this.m_pool = kept;
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
    this.Unyield(car);
    if car.mine && IsDefined(body) {
      this.Evict(body, system);
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
  // door, with the game's own reaction to a hijack. Each seat is read as the
  // seat check reads it (VehicleComponent.IsSlotOccupied: the mounting
  // facility's child of that slot), not through GetAllPassengers
  // (final.reds:89773, which decompiles to a read of the first mounting
  // only) and without skipping DES-managed NPCs (a driver tandem spawned):
  // the old way put no one out of a car whose seat a player's puppet then
  // found taken (traffic-host, 2026-11-21). Players' Vs and
  // puppets stay, and an NPC another game runs (our proxy of it) is its
  // owner's to move.
  private func Evict(body: ref<VehicleObject>, system: ref<TandemSystem>) -> Void {
    let seats: array<wref<VehicleSeat_Record>>;
    VehicleComponent.GetSeats(this.m_game, body, seats);
    let npcs = IsDefined(system) ? system.Npcs() : null;
    let mounting = GameInstance.GetMountingFacility(this.m_game);
    let side = 0.0;
    let evicted = 0;
    for seat in seats {
      let slot = seat.SeatName();
      let slotId: MountingSlotId;
      slotId.id = slot;
      // (child, parent, slot): no child, this vehicle's slot.
      let info = mounting.GetMountingInfoSingleWithIds(EntityID.FromHash(0ul), body.GetEntityID(), slotId);
      let npc = EntityID.IsDefined(info.childId) ? GameInstance.FindEntityByID(this.m_game, info.childId) as ScriptedPuppet : null;
      if IsDefined(npc) {
        let shared = IsDefined(npcs) ? npcs.NpcOf(npc.GetEntityID()) : 0u;
        if npc.IsPlayer() || (IsDefined(system) && system.PlayerOfPuppet(npc.GetEntityID()) != 0u) {
          TandemNative.Log(s"street: a player stays in \(NameToString(slot))");
        } else if shared != 0u && !npcs.Owns(shared) {
          TandemNative.Log(s"street: npc \(shared) in \(NameToString(slot)) is another game's: left to its owner");
        } else {
          TandemVehicles.Unseat(this.m_game, npc, body.GetEntityID(), slot);
          side += 1.0;
          let spot = body.GetWorldPosition() - body.GetWorldRight() * (1.6 + 0.6 * side);
          GameInstance.GetTeleportationFacility(this.m_game).Teleport(npc, spot, Quaternion.ToEulerAngles(body.GetWorldOrientation()));
          let hijack = new VehicleHijackEvent();
          hijack.driverAllowedToGetAggressive = VehicleComponent.IsDriverSlot(slot);
          npc.QueueEvent(hijack);
          evicted += 1;
          TandemNative.Log(s"street: \(TDBID.ToStringDEBUG(npc.GetRecordID())) put out of \(NameToString(slot))");
        }
      }
    }
    if evicted == 0 {
      TandemNative.Log(s"street: nobody to put out of the vehicle taken (\(ArraySize(seats)) seats)");
    }
    body.QueueEvent(new StealVehicleEvent());
  }

  // A proxy no longer shown: its body goes to the pool (parked by Keep, to
  // be the next vehicle of its record, or deleted later when it is not fit
  // for that); a body of our own world stays (and a crowd car we held
  // drives on), and so does one a player got into (ours: our V).
  private func Retire(car: ref<TandemCar>) -> Void {
    if car.held {
      let held = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(held) {
        held.TandemHold(false);
      }
      car.held = false;
    }
    if car.own || !EntityID.IsDefined(car.body) || !GameInstance.GetDynamicEntitySystem().IsManaged(car.body) {
      return;
    }
    if car.claimed || ArrayContains(this.m_taken, car.body) {
      return;
    }
    // Its hack mark goes with it, not with its body's next vehicle.
    let hacks = TandemHacks.Get(this.m_game);
    if IsDefined(hacks) {
      hacks.CarGone(car.id);
    }
    let entry = new TandemPooled();
    entry.body = car.body;
    entry.record = car.record;
    entry.appearance = car.appearance;
    entry.since = TandemClock.Real(this.m_game);
    entry.seated = car.seated || GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(car.body);
    let body = TandemTraffic.Body(this.m_game, car.body);
    if IsDefined(body) {
      if this.Occupied(body) {
        entry.seated = true;
      }
      entry.spent = !this.Pristine(car) || !this.Sound(body);
      if car.siren {
        body.ToggleSiren(false);
      }
    } else {
      // Asked for and not here yet: it is parked when it comes rather than
      // deleted while it is being created (§13.140).
      entry.coming = true;
    }
    ArrayPush(this.m_pool, entry);
    this.m_parked += 1;
    this.Bound(entry.record);
    let how = entry.spent ? "not to be used again" : "ready";
    if entry.coming {
      how = "not here yet";
    }
    if entry.seated {
      how += ", someone sat in it";
    }
    TandemNative.LogDebug(s"street: pool: vehicle \(car.id)'s body \(EntityID.ToDebugString(car.body)) parked (\(how))");
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
    this.Unyield(car);
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
    let system = TandemSystem.Of(this.m_game);
    if !IsDefined(system) {
      return;
    }
    for car in this.m_cars {
      if !car.mine && !car.claimed {
        let body = TandemTraffic.Body(this.m_game, car.body);
        if IsDefined(body) {
          if !car.ready {
            car.ready = true;
            this.Show(car, body, true);
          }
          let at = TandemStreet.Position(car.id);
          // By the engine's kinematic move, a teleport for a jump; a car at
          // rest where its owner says is left alone (TandemSystem.MoveBody).
          if at.W != 0.0 {
            car.x = at.X;
            car.y = at.Y;
            car.z = at.Z;
            at.W = 1.0;
            if !IsDefined(car.move) {
              car.move = new TandemMoved();
            }
            system.MoveBody(car.move, body, at, TandemStreet.Orientation(car.id));
          } else {
            system.SettleBody(car.move, body);
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
        // A body that does not come is asked for again later each time (3 s,
        // 6, 12… up to a minute), the one before it deleted first: asked for
        // every 5 s with the old ones left to come, a riding guest had 181
        // bodies waiting after 90 s (2026-11-22; why none came: engine
        // vehicles.md §13.141. That night's crash was another: re.md §13.142).
        let wait = MinF(3.0 * PowF(2.0, Cast<Float>(car.goneTries)), 60.0);
        if IsDefined(body) {
          car.goneSince = 0.0;
          car.goneTries = 0;
        } else if GameInstance.GetDynamicEntitySystem().IsSpawning(car.body) {
          car.goneSince = 0.0;
        } else if car.goneSince == 0.0 {
          car.goneSince = this.m_now;
        } else if this.m_now - car.goneSince > wait {
          TandemEntities.Delete(car.body);
          this.m_deleted += 1;
          car.goneTries += 1;
          let at = TandemStreet.Position(car.id);
          if at.W == 0.0 {
            at = new Vector4(car.x, car.y, car.z, 1.0);
          }
          at.W = 1.0;
          car.ready = false;
          car.wrecked = false;
          car.siren = false;
          car.seated = false;
          car.move = null;
          car.goneSince = 0.0;
          this.Obtain(car, at, TandemStreet.Orientation(car.id));
          TandemNative.Log(s"street: check: vehicle \(car.id)'s body was gone for \(wait) s: spawned again (\(car.goneTries) in a row)");
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
    // The partners' Vs on foot here: our traffic stops for them (Yield).
    let afoot: array<Vector4>;
    let mounting = GameInstance.GetMountingFacility(this.m_game);
    let others = system.PuppetPlayers();
    for other in others {
      let puppet = system.PuppetOf(other);
      if IsDefined(puppet) {
        let where = puppet.GetWorldPosition();
        ArrayPush(viewers, where);
        if !mounting.IsMountedToAnything(puppet.GetEntityID()) && !TandemVisibility.IsHidden(this.m_game, other) {
          ArrayPush(afoot, where);
        }
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
          this.Yield(car, body, afoot);
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
        this.Unyield(car);
        TandemStreet.SendGone(car.id, 0);
      }
    }
    this.m_cars = kept;
    if ArraySize(ids) > 0 {
      TandemStreet.SendStates(ids, positions, orientations, velocities, viewers);
    }
  }

  // Ours: a traffic car we run stops for a partner's V on foot in its lane,
  // as the game's own traffic stops for our V (which the native traffic
  // system sees by itself; a partner's puppet it drove through: a host's V
  // was run over by a guest's car and carried 750 m, 2026-11-22). The
  // game's own way to stop a car in its lane (the `stop` pattern of
  // OnStuckEvent, final.reds:88428) and its horn; on its way again a second
  // after the lane is clear. Not a car in a panic, a wreck, the police, nor
  // one with nobody at the wheel. The lane ahead: 1.8 m to each side, from
  // the car's nose to 1.5 s of its speed ahead (5 m at least, 25 m at most).
  private func Yield(car: ref<TandemCar>, body: ref<VehicleObject>, afoot: array<Vector4>) -> Void {
    let blocked = false;
    if ArraySize(afoot) == 0 {
      car.lookedAt = 0.0;
    }
    if ArraySize(afoot) > 0 && (car.flags & 71) == 0 && body.IsCrowdVehicle() {
      let at = body.GetWorldPosition();
      let forward = body.GetWorldForward();
      let right = body.GetWorldRight();
      let velocity = body.GetLinearVelocity();
      let since = this.m_now - car.lookedAt;
      if car.lookedAt > 0.0 && since > 0.05 && since < 1.0 {
        car.speed = Vector4.Distance(at, new Vector4(car.x, car.y, car.z, 1.0)) / since;
      }
      car.x = at.X;
      car.y = at.Y;
      car.z = at.Z;
      car.lookedAt = this.m_now;
      let reach = 2.5 + ClampF(MaxF(Vector4.Length(velocity), MinF(car.speed, 60.0)) * 1.5, 5.0, 25.0);
      for spot in afoot {
        let dx = spot.X - at.X;
        let dy = spot.Y - at.Y;
        let ahead = dx * forward.X + dy * forward.Y;
        let side = AbsF(dx * right.X + dy * right.Y);
        if ahead > 0.0 && ahead <= reach && side <= 1.8 && AbsF(spot.Z - at.Z) <= 2.5 {
          blocked = true;
        }
      }
    }
    if blocked {
      car.clearSince = 0.0;
      if !car.yields {
        car.yields = true;
        this.m_yields += 1;
        body.TandemYield(true);
        if this.m_yields == 1 {
          TandemNative.Log(s"street: vehicle \(car.id) stops for a player on foot in its lane (the first of this session)");
        } else {
          TandemNative.LogDebug(s"street: vehicle \(car.id) stops for a player on foot in its lane (\(this.m_yields) so far)");
        }
      }
    } else if car.yields {
      if car.clearSince == 0.0 {
        car.clearSince = this.m_now;
      } else if this.m_now - car.clearSince > 1.0 {
        this.Unyield(car);
        TandemNative.LogDebug(s"street: vehicle \(car.id) drives on, its lane clear");
      }
    }
  }

  private func Unyield(car: ref<TandemCar>) -> Void {
    if car.yields {
      car.yields = false;
      car.clearSince = 0.0;
      let body = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(body) && !car.held {
        body.TandemYield(false);
      }
    }
  }

  // (test: `street lanes`) the cars we run that someone drives, nearest
  // first five, and how often one stopped for a partner on foot.
  public func LanesReport() -> Void {
    let player = GetPlayer(this.m_game);
    let here = IsDefined(player) ? player.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 1.0);
    let said = 0;
    for car in this.m_cars {
      if car.mine && (car.flags & 5) == 0 && said < 5 {
        let body = TandemTraffic.Body(this.m_game, car.body);
        if IsDefined(body) {
          let at = body.GetWorldPosition();
          let velocity = body.GetLinearVelocity();
          TandemNative.Log(s"street lanes: vehicle \(car.id) at \(at.X) \(at.Y) \(at.Z) yaw \(body.GetWorldYaw()), \(Vector4.Distance(at, here)) m from our V, \(Vector4.Length(velocity)) m/s, crowd \(body.IsCrowdVehicle()), stopped for a player \(car.yields)");
          said += 1;
        }
      }
    }
    TandemNative.Log(s"street lanes: \(said) driven car(s) of ours listed, \(this.m_yields) stop(s) for a player on foot in this session");
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

// Our V gets into a vehicle: a shared one becomes ours (on the tick).
@wrapMethod(VehicleObject)
protected cb func OnMountingEvent(evt: ref<MountingEvent>) -> Bool {
  let result = wrappedMethod(evt);
  let child = GameInstance.FindEntityByID(this.GetGame(), evt.request.lowLevelMountingInfo.childId) as GameObject;
  if IsDefined(child) && child.IsPlayer() {
    TandemStreaming.QueueStreamed(TandemStreamKind.Mounted(), this.GetEntityID(), evt.request.lowLevelMountingInfo.slotId.id, true);
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

// A traffic car we run, stopped in its lane for a partner's V on foot and
// sent on again (TandemTraffic.Yield): the crowd's own patterns, and the
// horn a driver gives a V in its way (VehicleComponent.PlayDelayedHonk, as
// OnVehicleDestructionEvent does, final.reds:88440).
@addMethod(VehicleObject)
public final func TandemYield(stop: Bool) -> Void {
  if IsDefined(this.m_crowdMemberComponent) {
    this.m_drivingTrafficPattern = stop ? n"stop" : n"normal";
    this.m_crowdMemberComponent.ChangeMoveType(this.m_drivingTrafficPattern);
    if stop && IsDefined(this.m_vehicleComponent) {
      this.m_vehicleComponent.PlayDelayedHonk(1.0, 0.6);
    }
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
