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
  // Proxies: its body exists and shows how the owner says it stands, since
  // when (our clock).
  public let ready: Bool;
  public let readyAt: Float;
  public let wrecked: Bool;
  public let siren: Bool;
  // Proxies: our V drives it remotely (Drive leaves it to our game).
  public let remote: Bool;
  // Proxies: its headlights as we last set them (0 not yet, 1 off, 2 on).
  public let lamps: Int32;
  // When its headlights were last checked against its owner's (Drive).
  public let lampsAt: Float;
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
  // The moving vehicles of ours near a V (Share): where they are goes 30
  // times a second between two rounds (ShareNear; traffic.md, Near either V).
  private let m_lastNear: Float;
  private let m_nearBodies: array<wref<VehicleObject>>;
  private let m_nearIds: array<Uint32>;
  private let m_nearViewers: array<Vector4>;
  private let m_lastCheck: Float;
  private let m_now: Float;
  // TandemSync.Rebuilds() as Check last saw it, and when it moved.
  private let m_streetRebuilds: Int32;
  private let m_rebuiltAt: Float;
  // (street list) proxies whose body came or went in sight within 50 m,
  // and rounds a removal of ours waited for being in sight.
  private let m_popIn: Int32;
  private let m_popOut: Int32;
  private let m_popWaited: Int32;
  // Our own removed this round (Remove: a few a round).
  private let m_removedAt: Float;
  private let m_removedNow: Int32;
  // Proxy bodies asked for in this quarter second (MaySpawn).
  private let m_spawnAt: Float;
  private let m_spawnNow: Int32;
  private let m_spawnTickAt: Float;
  private let m_spawnTick: Int32;
  // (test: vehicle lamps sync off) no headlight read or set.
  private let m_lampsOff: Bool;
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
  private let m_despawnMode: String;
  // How many of our own vehicles were taken out of our world (Remove).
  private let m_removed: Int32;
  // Ours: how many times a car stopped for a partner's V on foot.
  private let m_yields: Int32;
  // Ours: how many shared vehicles gave their place to a nearer one.
  private let m_swapped: Int32;

  public static func ShareRange() -> Float {
    return 120.0;
  }

  // traffic.hpp kNearRange: within this of a V a car of ours goes 30 times
  // a second (the native keeps the moving ones, nearest first).
  public static func NearRange() -> Float {
    return 50.0;
  }

  public static func NearInterval() -> Float {
    return 0.03;
  }

  public static func DropRange() -> Float {
    return 150.0;
  }

  // 96: a Kabuki crossing had 71 vehicles of ours within the share range,
  // most of them parked (they cost nothing until they move), and the cap
  // left 23 of them out (2026-11-23; 48 before).
  public static func MaxShared() -> Int32 {
    return 96;
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
    for mounted in TandemStreaming.TakeStreamed(TandemStreamKind.Mounted()) {
      this.PlayerMounts(mounted.entity, mounted.cls);
    }
    this.TakeEvents(system);
    // Ours: announced while we own our area, said for as long as we run them.
    if now - this.m_last >= 0.1 {
      this.m_last = now;
      this.m_lastNear = now;
      this.Share(system);
    } else if now - this.m_lastNear >= TandemTraffic.NearInterval() {
      this.m_lastNear = now;
      this.ShareNear();
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
    this.m_swapped = 0;
    this.m_removed = 0;
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
    return s"count: \(ArraySize(this.m_cars)) (\(mine) run by us, \(ArraySize(this.m_cars) - mine) shown as proxies; \(parked) parked, \(wrecks) wrecks, \(own) bodies of our own world), area \(area), \(ArraySize(shared)) proxies in the session's record, \(ArraySize(this.m_live)) vehicles in our world, \(ArraySize(this.m_taken)) taken by players, \(ArraySize(this.m_retired)) proxies kept for reuse, \(this.m_given) given, \(this.m_taken2) taken over, \(bodies) proxy body(ies) here, \(this.m_yields) stop(s) for a player on foot, \(this.m_swapped) swapped for a nearer one, \(this.m_removed) of our own removed (another game's street)";
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

  // A car our V took (it got in): a player's car from then on, never shared.
  public func Taken(body: EntityID) -> Bool {
    return ArrayContains(this.m_taken, body);
  }

  // (test, RemoteView.reds) the nearest vehicle body we know within
  // `within` metres: a shared one (ours or a proxy) first, else one of our
  // world's.
  public func NearestBody(at: Vector4, within: Float) -> ref<VehicleObject> {
    let best: ref<VehicleObject>;
    let bestFar = within;
    for car in this.m_cars {
      let body = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(body) {
        let where = body.GetWorldPosition();
        let far = Vector4.Distance(at, where);
        if far < bestFar {
          best = body;
          bestFar = far;
        }
      }
    }
    if IsDefined(best) {
      return best;
    }
    for vehicle in this.m_live {
      if IsDefined(vehicle) {
        let where = vehicle.GetWorldPosition();
        let far = Vector4.Distance(at, where);
        if far < bestFar {
          best = vehicle;
          bestFar = far;
        }
      }
    }
    return best;
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

  // How long its body has stood here as a proxy shown (0: not yet; a
  // vehicle of ours: long enough).
  public func ShownFor(id: Uint32) -> Float {
    let car = this.Find(id);
    if !IsDefined(car) {
      return 0.0;
    }
    if car.mine {
      return 1000.0;
    }
    return car.ready ? this.m_now - car.readyAt : 0.0;
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
      car.body = this.Reuse(car.record, car.appearance, at, turn);
      // Not into a car of our own that stands there (the same parked car or
      // wreck in both worlds): ours goes first, out of sight, and the
      // check below spawns the proxy then (two bodies in one place collide
      // and burn).
      if !EntityID.IsDefined(car.body) && !this.OwnNear(at) && this.MaySpawn() {
        car.body = TandemTraffic.SpawnProxy(car.record, car.appearance, at, turn);
      }
      TandemNative.Log(s"street: vehicle \(car.id) \(TDBID.ToStringDEBUG(car.record)) of player \(TandemStreet.RunBy(car.id)) (flags \(car.flags)) spawning at (\(event.x), \(event.y), \(event.z))");
    }
    ArrayPush(this.m_cars, car);
  }

  // A proxy's life is its owner's: immortal here (the god mode the game
  // gives a quest's car tagged Immortal) until its owner says it is
  // destroyed, or until it is ours to run.
  public static func Spare(game: GameInstance, body: ref<VehicleObject>, on: Bool) -> Void {
    let gods = GameInstance.GetGodModeSystem(game);
    let id = body.GetEntityID();
    let has = gods.HasGodMode(id, gameGodModeType.Immortal);
    if on && !has {
      gods.AddGodMode(id, gameGodModeType.Immortal, n"TandemProxy");
    } else if !on && has {
      gods.RemoveGodMode(id, gameGodModeType.Immortal, n"TandemProxy");
    }
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

  // A few proxy bodies asked for at a time: some seventy asked for in one
  // tick (our V come into an area another game shares 96 vehicles of) took
  // the game down there, twice (2026-11-23, 0xc0000005 at +0x22CA324, a lock
  // taken through null under the spawns). Eight a quarter second, two a
  // tick (an area of 96 shared in 3 s; four a quarter second before, 6 s:
  // traffic.md, Proxy bodies).
  private func MaySpawn() -> Bool {
    if this.m_now - this.m_spawnAt >= 0.25 || this.m_now < this.m_spawnAt {
      this.m_spawnAt = this.m_now;
      this.m_spawnNow = 0;
    }
    if this.m_now != this.m_spawnTickAt {
      this.m_spawnTickAt = this.m_now;
      this.m_spawnTick = 0;
    }
    if this.m_spawnNow >= 8 || this.m_spawnTick >= 2 {
      return false;
    }
    this.m_spawnTick += 1;
    this.m_spawnNow += 1;
    return true;
  }

  // A vehicle of our own world we do not share stands within 5 m, or the
  // body of another proxy within 2.5 m (copies spawned onto each other
  // flickered and burnt on one game, 2026-11-23).
  // A shared car beyond our streaming (250 m): its body would stream out
  // again; asked for when our V comes near (after our fast travel, cars 770 m
  // behind were spawned again every few seconds, regroup 2026-11-27).
  private func FarFromUs(car: ref<TandemCar>) -> Bool {
    let player = GetPlayer(this.m_game);
    return IsDefined(player) && Vector4.Distance(player.GetWorldPosition(), new Vector4(car.x, car.y, car.z, 1.0)) > 250.0;
  }

  private func OwnNear(at: Vector4) -> Bool {
    for vehicle in this.m_live {
      if IsDefined(vehicle) && !IsDefined(this.FindBody(vehicle.GetEntityID())) && Vector4.Distance(vehicle.GetWorldPosition(), at) < 5.0 {
        return true;
      }
    }
    for car in this.m_cars {
      let other = !car.mine ? TandemTraffic.Body(this.m_game, car.body) : null;
      if IsDefined(other) && Vector4.Distance(other.GetWorldPosition(), at) < 2.5 {
        return true;
      }
    }
    return false;
  }

  // A retired proxy of the same record, put where the new one is.
  // Of the same look only: an appearance changed on a body the moment it is
  // reused detaches its old components while the scene detaches others (the
  // arrival crash, engine/vehicles.md §13.157, came with such changes).
  private func Reuse(record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion) -> EntityID {
    let i = 0;
    while i < ArraySize(this.m_retired) {
      let retired = GameInstance.GetDynamicEntitySystem().GetEntity(this.m_retired[i].body) as GameObject;
      let same = !IsNameValid(appearance) || IsDefined(retired) && Equals(retired.GetCurrentAppearanceName(), appearance);
      if this.m_retired[i].record == record && same {
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

  // A proxy no longer shown. Deleted when nobody ever sat in it, else kept
  // for the next vehicle of its record (a body that has had an occupant
  // must not be deleted, §13.6); a body of our own world stays (and a crowd
  // car we held drives on).
  private func Retire(car: ref<TandemCar>) -> Void {
    let gone = TandemTraffic.Body(this.m_game, car.body);
    if IsDefined(gone) && !car.own && TandemSight.Seen(this.m_game, gone, 1.5, 2.5) {
      this.m_popOut += 1;
    }
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
    TandemEntities.Delete(car.body);
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
    // Its owner's look (paint, a wreck's own): a body we reused, our own
    // instance of a placed car or a wreck we put there has ours otherwise
    // (an overturned wreck yellow here and red on its owner's, 2026-11-23).
    let mine = body.GetCurrentAppearanceName();
    let owners = IsNameValid(car.appearance) && NotEquals(car.appearance, mine);
    // Its end is its owner's word: here it shows damage (smoke, fire) and
    // never dies by itself, as the game keeps a quest's car (its record's
    // Immortal tag, VehicleComponent.SetImmortalityMode). A crash into our
    // world's cars or a fire burnt proxies out here only, their wrecks and
    // parts on one screen (2026-11-22).
    TandemTraffic.Spare(this.m_game, body, !destroyed);
    // Only on our own instance of a placed car (a spawned body has its
    // owner's look from its spec, a reused one is of the same look).
    if joining && owners && car.own {
      body.ScheduleAppearanceChange(car.appearance);
      TandemNative.LogDebug(s"street: vehicle \(car.id) takes its owner's look \(NameToString(car.appearance)) (ours \(NameToString(mine)))");
    }
    if destroyed && !car.wrecked {
      car.wrecked = true;
      if joining {
        let state = body.GetVehiclePS();
        if IsDefined(state) {
          state.SetIsDestroyed(true);
        }
        // The wreck's look is its owner's, when it said one.
        let record = body.GetRecord();
        let look = IsDefined(record) ? record.DestroyedAppearance() : n"None";
        if IsNameValid(look) && !IsNameValid(car.appearance) {
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
    let lamps = (car.flags & 32) != 0 ? 2 : 1;
    if lamps != car.lamps && !destroyed && !this.m_lampsOff {
      car.lamps = lamps;
      TandemTraffic.SetHeadlights(body, lamps == 2);
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
    // Not one our V drives remotely either (the host hands it to nobody
    // then: remote-views.md; a word that crossed its start).
    let remote = IsDefined(body) && body.IsVehicleRemoteControlled();
    if !IsDefined(car) || !car.mine || !IsDefined(body) || car.claimed || boarded || remote || ArrayContains(this.m_taken, car.body) || TandemElsewhere.AwayFromBody(this.m_game) {
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
    TandemTraffic.Spare(this.m_game, body, !car.wrecked);
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
    TandemTraffic.Spare(this.m_game, body, false);
    let inherited = (event.reason & 16) != 0;
    // Given to us because our V drives it remotely (remote-views.md): it is
    // where we drive it already, and our V's hands drive it, not traffic.
    let remote = body.IsVehicleRemoteControlled();
    if remote {
      let system = TandemSystem.Of(this.m_game);
      if IsDefined(system) {
        system.ReleaseBody(body.GetEntityID());
      }
      body.PhysicsWakeUp();
    } else if !inherited {
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
    } else if (event.flags & 5) == 0 && !remote {
      // A car on the move with a driver: it keeps the giver's speed and
      // joins our traffic (as a cab does when it goes free, final.reds:217147).
      body.TandemDriveOn(new Vector4(event.dx, event.dy, event.dz, 0.0));
    }
    car.own = false;
    TandemStreet.Answer(event.id, true);
    this.m_taken2 += 1;
    TandemNative.Log(s"street: handover in: vehicle \(car.id) from player \(event.player)\(inherited ? " (inherited)" : "")\(remote ? " (our V drives it remotely)" : ""), health \(event.amount), \(Vector4.Length(new Vector4(event.dx, event.dy, event.dz, 0.0))) m/s");
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
        // Our V drives it remotely (the take control quickhack): ours to
        // move until the host hands it to us (remote-views.md), a frame or
        // two; never pulled back to its owner's states meanwhile.
        if IsDefined(body) && body.IsVehicleRemoteControlled() {
          if !car.remote {
            car.remote = true;
            system.ReleaseBody(body.GetEntityID());
            body.PhysicsWakeUp();
            TandemNative.Log(s"street: our V drives vehicle \(car.id) remotely: no longer moved to its owner's states");
          }
          body = null;
        } else if car.remote {
          car.remote = false;
        }
        if IsDefined(body) {
          if !car.ready {
            car.ready = true;
            car.readyAt = this.m_now;
            if !car.own && TandemSight.Seen(this.m_game, body, 1.5, 2.5) {
              this.m_popIn += 1;
            }
            this.Show(car, body, true);
          }
          // The game sets a copy's lights up after it came (and its own
          // state may darken them): checked against the owner's every 2 s.
          if !this.m_lampsOff && (this.m_now - car.lampsAt >= 2.0 || this.m_now < car.lampsAt) && (car.flags & 4) == 0 {
            car.lampsAt = this.m_now;
            let lit = (car.flags & 32) != 0;
            if NotEquals(TandemTraffic.HeadlightsOn(body), lit) {
              TandemTraffic.SetHeadlights(body, lit);
            }
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
    // Our world rebuilt (a fast travel, a respawn, a time skip; as the NPCs'
    // check, Npcs.reds): the bodies it took with it come back as the first
    // of their kind, no fallback (15 after our own fast travel, regroup
    // 2026-11-27).
    let rebuilds = TandemSync.Rebuilds();
    if rebuilds != this.m_streetRebuilds {
      this.m_streetRebuilds = rebuilds;
      this.m_rebuiltAt = this.m_now;
    }
    let rebuilt = this.m_rebuiltAt > 0.0 && this.m_now - this.m_rebuiltAt < 20.0;
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
      } else if !car.mine && !car.claimed && (!car.own || !IsDefined(TandemTraffic.Body(this.m_game, car.body))) {
        // A body of our own world we gave away (a hand-over: our real car
        // became its proxy) that our crowd then took out of the world: a
        // proxy body of our making from now on, spawned as any other. The
        // parked cars a host handed to a guest by the test field were gone
        // on the host and shown on the guest (20 of them, 2026-11-22).
        if car.own {
          car.own = false;
          car.goneSince = 0.0;
          TandemNative.Log(s"street: check: vehicle \(car.id)'s body of our own world is gone: a proxy of ours instead");
        }
        let body = TandemTraffic.Body(this.m_game, car.body);
        // A body that does not come is asked for again later each time (3 s,
        // 6, 12… up to a minute), the one before it deleted first: asked for
        // every 5 s with the old ones left to come, 181 bodies in 90 s ended
        // in the engine's crash on a riding guest (2026-11-22).
        let wait = MinF(3.0 * PowF(2.0, Cast<Float>(car.goneTries)), 60.0);
        if IsDefined(body) {
          car.goneSince = 0.0;
          car.goneTries = 0;
        } else if GameInstance.GetDynamicEntitySystem().IsSpawning(car.body) {
          car.goneSince = 0.0;
        } else if car.goneSince == 0.0 {
          car.goneSince = this.m_now;
        } else if this.OwnNear(new Vector4(car.x, car.y, car.z, 1.0)) || !EntityID.IsDefined(car.body) && !this.MaySpawn() || this.FarFromUs(car) {
          // Ours still stands there (see OnSpawn), or this moment's spawns
          // are spent: after it.
          car.goneSince = this.m_now;
        } else if this.m_now - car.goneSince > (EntityID.IsDefined(car.body) ? wait : 0.25) {
          // A body never asked for (its first was put off: MaySpawn,
          // OwnNear) is no respawn.
          let first = !EntityID.IsDefined(car.body);
          TandemEntities.Delete(car.body);
          if !first {
            car.goneTries += 1;
          }
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
          if first {
            TandemNative.LogDebug(s"street: check: vehicle \(car.id)'s body asked for (put off before)");
          } else if rebuilt && car.goneTries == 1 {
            TandemNative.LogDebug(s"street: check: vehicle \(car.id)'s body went with our rebuilt world: spawned again");
          } else {
            TandemNative.Log(s"street: check: vehicle \(car.id)'s body was gone for \(wait) s: spawned again (\(car.goneTries) in a row)");
            TandemDegraded.Fallback("street body respawned", s"vehicle \(car.id)'s body was gone for \(wait) s (\(car.goneTries) in a row)");
          }
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

  // Between two rounds: where the cars of ours near a V are now (the list
  // Share made). Only reads them, and one native call.
  private func ShareNear() -> Void {
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let orientations: array<Quaternion>;
    let velocities: array<Vector4>;
    let n = ArraySize(this.m_nearIds);
    let i = 0;
    while i < n {
      let body = this.m_nearBodies[i];
      if IsDefined(body) {
        ArrayPush(ids, this.m_nearIds[i]);
        ArrayPush(positions, body.GetWorldPosition());
        ArrayPush(orientations, body.GetWorldOrientation());
        ArrayPush(velocities, body.GetLinearVelocity());
      }
      i += 1;
    }
    if ArraySize(ids) > 0 {
      TandemStreet.SendNearStates(ids, positions, orientations, velocities, this.m_nearViewers);
    }
  }

  private func Share(system: ref<TandemSystem>) -> Void {
    ArrayClear(this.m_nearBodies);
    ArrayClear(this.m_nearIds);
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
        // The crowd reuses a vehicle entity for another car (its record
        // changes under the same id): the one we shared is gone, the new one
        // is announced as any other (a Quartz shared, a Hella on the screen
        // that runs it, 2026-11-23).
        if IsDefined(body) && body.GetRecordID() != car.record {
          body = null;
        }
        // One of ours standing where another game runs our area (shared
        // before that game's region came over it): that game's own car
        // stands there, so ours goes from both worlds, as a new one of ours
        // would (OneSided; a wreck yellow here and red there, two cars in
        // one place, 2026-11-23). One that drives leaves by itself.
        if !away && !owns && IsDefined(body) && !car.seated && !ArrayContains(this.m_taken, car.body) && (this.FlagsOf(body) & 1) != 0 && this.OneSided(body, car.body, entities) && this.Remove(body, car.body) {
          body = null;
        }
        let nearest = IsDefined(body) ? TandemTraffic.Nearest(body.GetWorldPosition(), viewers) : 1000000.0;
        if !away && IsDefined(body) && !ArrayContains(this.m_taken, car.body) && nearest <= range {
          car.seen = true;
          this.Say(car, body, pools);
          this.Yield(car, body, afoot);
          ArrayPush(ids, car.id);
          ArrayPush(positions, body.GetWorldPosition());
          ArrayPush(orientations, body.GetWorldOrientation());
          ArrayPush(velocities, body.GetLinearVelocity());
          if nearest <= TandemTraffic.NearRange() {
            ArrayPush(this.m_nearBodies, body);
            ArrayPush(this.m_nearIds, car.id);
          }
        }
      }
    }
    // New ones only where our game runs the area around our V (deep in a
    // higher-ranked player's region, its game announces the street).
    let live: array<wref<VehicleObject>>;
    let heldBack = 0;
    let waiting: ref<VehicleObject>;
    let waitingNear = 0.0;
    for vehicle in this.m_live {
      if IsDefined(vehicle) {
        let body = vehicle.GetEntityID();
        let at = vehicle.GetWorldPosition();
        // A car of our own crowd inside a higher-ranked linked player's
        // region: that game runs the street there. Ours is held back (it
        // is announced once it has driven out, if we run our area then),
        // and with overlap_despawn = remove (the default) taken out of our
        // world, with whoever sits in it: no car exists here that the game
        // running the street does not have (a riding guest's own traffic
        // drove beside the host's and crashed into its copies, 2026-11-22).
        // So is any other car of our own making there (a parked crowd car),
        // when another game runs our area.
        let held = !away && !IsDefined(this.FindBody(body)) && !vehicle.IsVehicleRemoteControlled() && (this.HeldBack(vehicle, body, at, owns, entities) || !owns && this.OneSided(vehicle, body, entities));
        let removed = false;
        if held {
          heldBack += 1;
          removed = this.Remove(vehicle, body);
          if !removed {
            this.AskDespawn(vehicle, body);
          }
        }
        if !removed {
          ArrayPush(live, vehicle);
        }
        // A racer of our street race is ours to share wherever it drives; so
        // is a car our V drives remotely (remote-views.md).
        let racer = !held && !away && (TandemRaces.Racer(this.m_game, vehicle) || vehicle.IsVehicleRemoteControlled());
        if (owns || racer) && !held && !away && !IsDefined(this.FindBody(body)) && this.Eligible(vehicle, body, entities) {
          let near = TandemTraffic.Nearest(at, viewers);
          if near <= (racer ? TandemTraffic.RacerRange() : TandemTraffic.ShareRange()) {
            if ArraySize(this.m_cars) < TandemTraffic.MaxShared() {
              let known = this.Announce(vehicle, body, at);
              ArrayPush(ids, known.id);
              ArrayPush(positions, at);
              ArrayPush(orientations, vehicle.GetWorldOrientation());
              ArrayPush(velocities, vehicle.GetLinearVelocity());
            } else if !racer && (!IsDefined(waiting) || near < waitingNear) {
              // The share is full: the nearest one left out may take the
              // place of a far one (below).
              waiting = vehicle;
              waitingNear = near;
            }
          }
        }
      }
    }
    this.m_live = live;
    // The nearest first: with the share full (kMaxShared), a vehicle of ours
    // left out nearer a V than the farthest one shared by 20 m or more (no
    // flicker at the edge) takes its place, one a round. Parked cars by the
    // start of a drive held the share full while the traffic around the
    // riders went unshared (a ride's host: 53 vehicles, 48 shared,
    // 2026-11-22).
    if IsDefined(waiting) {
      let far: ref<TandemCar>;
      let farNear = 0.0;
      for car in this.m_cars {
        let farBody = car.mine && car.seen ? TandemTraffic.Body(this.m_game, car.body) : null;
        if IsDefined(farBody) && !TandemRaces.Racer(this.m_game, farBody) {
          let d = TandemTraffic.Nearest(farBody.GetWorldPosition(), viewers);
          if d > farNear {
            far = car;
            farNear = d;
          }
        }
      }
      if IsDefined(far) && waitingNear + 20.0 <= farNear {
        far.seen = false;
        let i = ArraySize(ids) - 1;
        while i >= 0 {
          if ids[i] == far.id {
            ArrayErase(ids, i);
            ArrayErase(positions, i);
            ArrayErase(orientations, i);
            ArrayErase(velocities, i);
          }
          i -= 1;
        }
        let at = waiting.GetWorldPosition();
        let swapped = this.Announce(waiting, waiting.GetEntityID(), at);
        ArrayPush(ids, swapped.id);
        ArrayPush(positions, at);
        ArrayPush(orientations, waiting.GetWorldOrientation());
        ArrayPush(velocities, waiting.GetLinearVelocity());
        this.m_swapped += 1;
        TandemNative.LogDebug(s"street: vehicle \(far.id) (\(farNear) m from the nearest V) gives its place to a nearer one (\(waitingNear) m)");
      }
    }
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
    this.m_nearViewers = viewers;
    if ArraySize(ids) > 0 {
      TandemStreet.SendStates(ids, positions, orientations, velocities, viewers);
    }
  }

  // A vehicle of our world shared from now on (TrafficSpawn).
  private func Announce(vehicle: ref<VehicleObject>, body: EntityID, at: Vector4) -> ref<TandemCar> {
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
    return known;
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

  // (test: `street list`) every shared vehicle as we have it: who runs it,
  // where its body is, and for a proxy how far that is from where its
  // owner says. One line each: two games' lists are compared by id.
  public func ListReport() -> Void {
    let bodies = 0;
    // Proxies wrecked here and not on their owner's game (a crash or a fire
    // of our own world's).
    let burnt = 0;
    // Where our V is (two games' lists are compared around both Vs), with
    // the world's time (lists of two games taken a moment apart).
    let player = GetPlayer(this.m_game);
    let here = IsDefined(player) ? player.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 1.0);
    TandemNative.Log(s"street list: our V at \(here.X) \(here.Y) \(here.Z)");
    for car in this.m_cars {
      let body = TandemTraffic.Body(this.m_game, car.body);
      if IsDefined(body) {
        bodies += 1;
        if !car.mine && body.IsDestroyed() && (car.flags & 4) == 0 {
          burnt += 1;
        }
        let at = body.GetWorldPosition();
        let off = -1.0;
        if !car.mine {
          let to = TandemStreet.Position(car.id);
          if to.W != 0.0 {
            to.W = 1.0;
            off = Vector4.Distance(at, to);
          }
        }
        let velocity = body.GetLinearVelocity();
        TandemNative.Log(s"street list: \(car.id) \(car.mine ? "ours" : "proxy") \(TDBID.ToStringDEBUG(car.record)) \(NameToString(body.GetCurrentAppearanceName())) at \(at.X) \(at.Y) \(at.Z) yaw \(body.GetWorldYaw()) flags \(car.flags) off \(off) v \(velocity.X) \(velocity.Y) lamps \((!this.m_lampsOff && TandemTraffic.HeadlightsOn(body)) ? 1 : 0) wreck \(body.IsDestroyed() ? 1 : 0)");
      } else {
        TandemNative.Log(s"street list: \(car.id) \(car.mine ? "ours" : "proxy") \(TDBID.ToStringDEBUG(car.record)) no body");
      }
    }
    // The vehicles of our own world nobody else is shown, within 150 m of
    // our V, and why (held back in another game's area, not shared by kind,
    // farther than the share range from every V, the share's cap).
    // (Not tandem's own spawns: the players' car copies and the proxies.)
    let unshared = 0;
    let owns = TandemArea.Owns();
    let entities = GameInstance.GetDynamicEntitySystem();
    for vehicle in this.m_live {
      if IsDefined(vehicle) && !IsDefined(this.FindBody(vehicle.GetEntityID())) && !ArrayContains(this.m_taken, vehicle.GetEntityID()) && !entities.IsManaged(vehicle.GetEntityID()) && !vehicle.IsPlayerMounted() {
        let at = vehicle.GetWorldPosition();
        if Vector4.Distance(at, here) <= TandemTraffic.DropRange() {
          let body = vehicle.GetEntityID();
          let why = this.HeldBack(vehicle, body, at, owns, entities) ? "held back" : !this.Eligible(vehicle, body, entities) ? "not shared" : !owns ? "another game's area" : ArraySize(this.m_cars) >= TandemTraffic.MaxShared() ? "cap" : "not yet";
          let velocity = vehicle.GetLinearVelocity();
          TandemNative.Log(s"street list: own \(TDBID.ToStringDEBUG(vehicle.GetRecordID())) at \(at.X) \(at.Y) \(at.Z) unshared (\(why)) v \(velocity.X) \(velocity.Y)");
          unshared += 1;
        }
      }
    }
    TandemNative.Log(s"street list: \(ArraySize(this.m_cars)) vehicle(s), \(bodies) with a body, \(unshared) of our own unshared within \(TandemTraffic.DropRange()) m, \(burnt) proxies wrecked here only; in sight within 50 m since the start: \(this.m_popIn) came, \(this.m_popOut) went, \(this.m_popWaited) removal round(s) of ours waited for it");
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

  // A car of our own making where another game runs the street, beside
  // the crowd's (HeldBack): a parked crowd car of ours, a parked police
  // car; that game's world has its own there. Not a placed one (the same
  // in every game: shared by its own id), nor a quest's, nor a police car
  // with its crew aboard (a unit on the move: Police.reds). A wreck of ours
  // goes too, only the population system's way (it spawned it: FindStub,
  // RemoveEntity: TandemWheels.DisposeBySystem, else left), out of sight:
  // left, it was one game's wreck and parts while the other's street had its
  // own car there. (27 disposed in one tick with a burning wreck among them
  // took the game down before the dispose gate and the stub path, §13.157.)
  private func OneSided(vehicle: ref<VehicleObject>, body: EntityID, entities: ref<DynamicEntitySystem>) -> Bool {
    return !EntityID.IsStatic(body) && this.Eligible(vehicle, body, entities) && !vehicle.IsQuest() && (!vehicle.IsPrevention() || !VehicleComponent.HasActiveDriverMounted(this.m_game, body));
  }

  // (overlap_despawn = remove, the default) Out of our world at once: the
  // vehicle and the NPCs in its seats (our crowd's driver and passengers;
  // never a player or a puppet). True when it was removed.
  private func Remove(vehicle: ref<VehicleObject>, body: EntityID) -> Bool {
    this.ReadDespawn();
    if !Equals(this.m_despawnMode, "remove") || vehicle.IsPlayerMounted() {
      return false;
    }
    // Never before the player's eyes: it goes once out of sight.
    if TandemSight.Seen(this.m_game, vehicle, 1.5, 2.5) {
      this.m_popWaited += 1;
      return false;
    }
    // Not in the 5 s after our crowd's density changed: the traffic system
    // takes its own cars away then, and the game died in the tick we
    // disposed of one just after our crowd went off (2026-11-23, the same
    // crash as below, our V arriving in another game's area).
    let system = TandemSystem.Of(this.m_game);
    let npcs = IsDefined(system) ? system.Npcs() : null;
    if IsDefined(npcs) && npcs.DensityFor() < 5.0 {
      return false;
    }
    // Two a second: 27 parked cars disposed in one tick (our V just come
    // into another game's area) and the game died in that tick, and again
    // at 30 a second (2026-11-23, 0xc0000005 at +0x22CA324, a lock taken
    // through null; a burning wreck among them the first time).
    if this.m_now - this.m_removedAt < 0.5 && this.m_now >= this.m_removedAt {
      return false;
    }
    this.m_removedAt = this.m_now;
    this.m_removedNow += 1;
    let system = TandemSystem.Of(this.m_game);
    let mounting = GameInstance.GetMountingFacility(this.m_game);
    let seats: array<wref<VehicleSeat_Record>>;
    VehicleComponent.GetSeats(this.m_game, vehicle, seats);
    for seat in seats {
      let slotId: MountingSlotId;
      slotId.id = seat.SeatName();
      let info = mounting.GetMountingInfoSingleWithIds(EntityID.FromHash(0ul), body, slotId);
      let npc = EntityID.IsDefined(info.childId) ? GameInstance.FindEntityByID(this.m_game, info.childId) as ScriptedPuppet : null;
      if IsDefined(npc) {
        if npc.IsPlayer() || (IsDefined(system) && system.PlayerOfPuppet(npc.GetEntityID()) != 0u) {
          return false;
        }
      }
    }
    for seat in seats {
      let slotId: MountingSlotId;
      slotId.id = seat.SeatName();
      let info = mounting.GetMountingInfoSingleWithIds(EntityID.FromHash(0ul), body, slotId);
      let npc = EntityID.IsDefined(info.childId) ? GameInstance.FindEntityByID(this.m_game, info.childId) as ScriptedPuppet : null;
      if IsDefined(npc) {
        TandemWheels.Dispose(npc);
      }
    }
    this.m_removed += 1;
    TandemNative.LogDebug(s"street: our \(TDBID.ToStringDEBUG(vehicle.GetRecordID())) \(EntityID.ToDebugString(body)) removed (another game runs the street here)");
    return vehicle.IsDestroyed() ? TandemWheels.DisposeBySystem(vehicle) : TandemWheels.Dispose(vehicle);
  }

  private func ReadDespawn() -> Void {
    if !this.m_despawnRead {
      this.m_despawnRead = true;
      this.m_despawnMode = TandemNative.Setting("overlap_despawn");
      this.m_despawn = Equals(this.m_despawnMode, "ask");
    }
  }

  // (overlap_despawn = ask) A held back car is asked to leave, once:
  // the prevention system's own despawn of a vehicle and its passengers,
  // which waits until nobody looks.
  private func AskDespawn(vehicle: ref<VehicleObject>, body: EntityID) -> Void {
    this.ReadDespawn();
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
    // Driven remotely (the take control quickhack): driven, no driver in it.
    if !VehicleComponent.HasActiveDriverMounted(this.m_game, vehicle.GetEntityID()) && !vehicle.IsVehicleRemoteControlled() {
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
    if !this.m_lampsOff && TandemTraffic.HeadlightsOn(vehicle) {
      flags += 32;
    }
    if vehicle.IsPerformingPanicDriving() {
      flags += 64;
    }
    if vehicle.IsAbandoned() {
      flags += 128;
    }
    return flags;
  }

  // Its headlights, as its controller's state has them (on or high beams):
  // a crowd car lights them itself by the hour and the weather, a copy of
  // it here does not (one car's lit on its owner's screen, dark on the
  // other, 2026-11-23).
  public static func HeadlightsOn(vehicle: ref<VehicleObject>) -> Bool {
    // Lit: a light mode, and the car on (a parked crowd car has the mode On
    // and its state Disabled: dark).
    let state = TandemTraffic.ControllerOf(vehicle);
    return IsDefined(state) && NotEquals(state.GetHeadLightMode(), vehicleELightMode.Off) && Equals(state.GetState(), vehicleEState.On);
  }

  private static func ControllerOf(vehicle: ref<VehicleObject>) -> ref<vehicleControllerPS> {
    let id = CreatePersistentID(vehicle.GetEntityID(), n"VehicleController");
    return GameInstance.GetPersistencySystem(vehicle.GetGame()).GetConstAccessToPSObject(id, n"gamevehicleControllerPS") as vehicleControllerPS;
  }

  // The owner's headlights on our body: the controller's mode, and its lamps
  // (the quests' own toggle, VehicleLightQuestToggleEvent).
  private static func SetHeadlights(vehicle: ref<VehicleObject>, on: Bool) -> Void {
    let state = TandemTraffic.ControllerOf(vehicle);
    // Lit takes the mode and the car on: a copy came Disabled (a parked
    // crowd car's state) and stayed dark with the mode On (2026-11-23).
    if IsDefined(state) {
      state.SetHeadLightMode(on ? vehicleELightMode.On : vehicleELightMode.Off);
      if on && NotEquals(state.GetState(), vehicleEState.On) {
        state.SetState(vehicleEState.On);
      }
    }
    let lamps = vehicle.GetAccessoryController();
    if IsDefined(lamps) {
      lamps.ToggleLights(on, vehicleELightType.Head);
    }
    TandemNative.LogDebug(s"street: vehicle \(EntityID.ToDebugString(vehicle.GetEntityID())) headlights \(on ? "on" : "off") (controller state \(IsDefined(state)), lamps \(IsDefined(lamps)))");
  }

  // (test: vehicle lamps [on|off|engine|mode]) the three vehicles nearest
  // our V: who runs it, its controller's light mode and state; on/off: the
  // quests' toggle of its headlights; engine: its controller's state On;
  // mode: its light mode On.
  public func LampsCommand(command: String) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let here = player.GetWorldPosition();
    let words = StrSplit(command, " ");
    let what = ArraySize(words) >= 3 ? words[2] : "";
    if Equals(what, "sync") {
      this.m_lampsOff = ArraySize(words) >= 4 && Equals(words[3], "off");
      TandemNative.Log(s"vehicle lamps: headlights followed \(!this.m_lampsOff)");
      return;
    }
    let said = 0;
    let used: array<Uint32>;
    while said < 3 {
      let best: ref<TandemCar>;
      let bestAt = 30.0;
      for car in this.m_cars {
        let body = TandemTraffic.Body(this.m_game, car.body);
        if IsDefined(body) && !ArrayContains(used, car.id) && Vector4.Distance(body.GetWorldPosition(), here) < bestAt {
          best = car;
          bestAt = Vector4.Distance(body.GetWorldPosition(), here);
        }
      }
      if !IsDefined(best) {
        break;
      }
      ArrayPush(used, best.id);
      let body = TandemTraffic.Body(this.m_game, best.body);
      let state = TandemTraffic.ControllerOf(body);
      if Equals(what, "on") || Equals(what, "off") {
        let lamps = body.GetAccessoryController();
        if IsDefined(lamps) {
          lamps.ToggleLights(Equals(what, "on"), vehicleELightType.Head);
        }
      } else if Equals(what, "engine") && IsDefined(state) {
        state.SetState(vehicleEState.On);
      } else if Equals(what, "mode") && IsDefined(state) {
        state.SetHeadLightMode(vehicleELightMode.On);
      }
      let mode = IsDefined(state) ? EnumInt(state.GetHeadLightMode()) : -1;
      let run = IsDefined(state) ? EnumInt(state.GetState()) : -1;
      TandemNative.Log(s"vehicle lamps: \(best.id) \(best.mine ? "ours" : "proxy") \(TDBID.ToStringDEBUG(best.record)) \(Cast<Int32>(bestAt)) m, flags \(best.flags), mode \(mode), state \(run), accessory \(IsDefined(body.GetAccessoryController()))\(StrLen(what) > 0 ? s": \(what)" : "")");
      said += 1;
    }
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
  // Ours from now on: out of the kinematic state our proxies are held in
  // (TandemSystem.MoveBody), then awake.
  let system = TandemSystem.Of(this.GetGame());
  if IsDefined(system) {
    system.ReleaseBody(this.GetEntityID());
  }
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
