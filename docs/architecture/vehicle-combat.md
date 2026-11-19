# Vehicle combat

Damage and destruction, guns in and on vehicles, a guest on someone else's
gun, collisions between players' cars and with NPCs, NPC vehicles chasing
a player, bikes, and every hard way out of a vehicle. Engine facts and
their tags: [`research/engine/vehicles.md`](../research/engine/vehicles.md)
§13.35. **Nothing here has run in the game yet** (written while the game
machine was in use): the core, the mock player and the tests are done and
pass, the scripts compile, the plugin builds; the scenarios at the end are
what to run first.

## The rule

One rule for every vehicle, the authority table's: **the game that
simulates a vehicle owns it, its damage included.**

| Vehicle                                                  | Owner (simulates, decides its damage) | Others                                           |
| -------------------------------------------------------- | ------------------------------------- | ------------------------------------------------ |
| a car a player drives                                    | that player's game                    | a copy, moved by `VehicleState`, damaged as told |
| a car a player left (parked, or rolling on after a bail) | still that player's game              | its copy, parked where the owner says            |
| a traffic, parked or police car, a chase car             | the owner of its area (`AreaOwner`)   | a proxy (`traffic.md`)                           |
| the gunner seat of a vehicle                             | the vehicle's owner gives it          | the gunner aims and fires from its copy          |
| a shot, a ram, a push, a blast                           | the game of the V who made it         | its outcome as state from the vehicle's owner    |
| what hits a V (a car, a bullet from a car)               | that V's game                         | shown on its puppet                              |

A vehicle is named the same way in every game (`proto::VehicleRef`): a
player's car by that player's id, a shared world vehicle by
`kSeatInTraffic | TrafficId` (the encoding `NpcSeat` already uses), so
`OwnerOfVehicle` answers "whose is it" from the name alone, and every
request is routed to that game by the host.

## What travels

Appended to the protocol (`protocol.hpp`, after `Crime`); no existing
message changed, no version bump.

| Message         | Channel         | From → to                            | What                                                                                                                                |
| --------------- | --------------- | ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| `VehicleDamage` | Event (kept)    | owner → all                          | health, burning / wreck / drowned, flat tyres, broken windows, the destruction grid (16 cells), parts off, when it blew up, by whom |
| `VehicleHit`    | Event, one-shot | a player's game → owner              | damage, where, a tyre or a window, explosion or melee                                                                               |
| `VehicleExit`   | Event, one-shot | the V's game → all                   | bail, thrown, knocked off a bike, dead, blown out; whether the car rolls on                                                         |
| `VehicleCombat` | Event (kept)    | the V's game → all                   | driver combat, a passenger leaning out, the vehicle's own weapons, a gunner seat; the weapon                                        |
| `MountedFire`   | State           | the shooter's game → all             | one shot of a vehicle's own weapon, its slot, where it was aimed                                                                    |
| `GunnerRequest` | Event, one-shot | a V's game → the vehicle's owner     | take, or leave, the gunner seat                                                                                                     |
| `Gunner`        | Event (kept)    | owner → all                          | who holds the vehicle's gunner seat                                                                                                 |
| `GunnerAim`     | State, 20 Hz    | the gunner's game → all              | yaw and pitch relative to the vehicle, trigger held                                                                                 |
| `VehicleBump`   | Event, one-shot | a driver's game → the other driver's | the push our car gave theirs, when (host clock)                                                                                     |
| `NpcRam`        | Event, one-shot | the driver's game → the NPC's owner  | our car ran into one of its NPCs: the car's velocity, knocked down or lethal                                                        |
| `Chase`         | Event (kept)    | area owner → all                     | one of its vehicles chases a player (kNoPlayer: over)                                                                               |

## Damage and destruction

- **The owner reads, everyone shows.** The game that owns a car reads how
  it stands four times a second (`Wheels.reds`, `SampleOwn`): its health
  pool, the game's damage stage (fire), wreck, drowned, the flat tyre; and
  as the game reports them, its dents (`VehicleComponent.OnGridDestruction`,
  the 16-cell destruction grid) and every part that comes off
  (`OnVehicleOnPartDetached`). It sends `VehicleDamage` when something
  changed (health by 2 % or more); the session keeps the latest of each
  vehicle for late joiners and hashes it into the digests.
- **Every copy the same way.** A copy's health pool follows (the game's
  own smoke, then fire); flat tyres are popped (`ToggleBrokenTire`), the
  parts detached (`DetachPart`), the grid set
  (`SetDestructionGridPointValues`, the first 15 cells the body's layer,
  the last the roof's, as vanilla's quest event does).
- **One explosion, the game's own.** A wreck carries the host-clock time
  it blew up. A copy that hears of it within 3 s
  (`vehiclecombat::kFreshExplosionUs`) sets its health to the minimum and
  the game runs its own end of a vehicle (blast, fire, sound, parts
  flying); one that hears later (a late joiner, a resync, a copy spawned
  after) is put there as a wreck (`OnArrival`): never blown up twice,
  never on arrival. Each V near the blast is hurt by the explosion its own
  game runs (the target's game decides every hit).
- **Hits are the hitter's to compute, the owner's to apply.** Our V's hit
  on a copy of a player's car is measured by our game (weapon, perks) and
  sent to the car's driver (`VehicleHit`), whose game applies it to the
  real car with our puppet as the instigator; a tyre our shot popped on the
  copy is sent as that tyre. A shared world vehicle's damage and rams stay
  `TrafficHit` (`traffic.md`); only its tyres travel as `VehicleHit`, and
  its owner answers with `VehicleDamage` (its health and wreck stay
  `TrafficStatus`'s).
- **A parked car is still its driver's.** The copy of a car its driver left
  is kept with whose it was (`TandemSystem.LastCarOwnerOf`), so a shot at a
  parked car still goes to the right game.

## Shooting from vehicles

- **Hands.** Driver combat and a passenger leaning out are the V's own
  weapons: their shots are `Shot` (the puppet's muzzle, report, shells),
  their hits the shooter's game's, as on foot. The pose stream draws the
  seated body. `VehicleCombat` says which state the V is in (from the
  player state machine's `Vehicle` value: Combat 2, Turret 5,
  DriverCombat 6) and with what, for the others' puppets and for the owner
  of the vehicle a gunner sits in.
- **The vehicle's own weapons** (2.0's mounted guns and missiles, the
  Basilisk's cannon). The game tells every shot to the vehicle
  (`VehicleComponent.OnWeaponShootEvent`, a `VehicleMountedWeaponShootEvent`
  with the weapon and its slot). Our game sends `MountedFire` for the ones
  our V fires: from our car, or from the gunner seat of someone else's.
  The others play it on their copy (`NPCShoot` at the target), and nothing
  a copy causes does anything to their world (below): the shooter's game
  decides its hits.

## A guest on someone else's gun

1. The guest's V takes the gunner seat of a copy (the game's
   `GunnerEvents`; or `tools/game.sh cmd wheels gun` in a test). Its game
   asks the vehicle's owner (`GunnerRequest`), through the host.
2. The owner's side of the session gives the seat to the first V that asks
   (`vehiclecombat::Turrets`: one seat per V, never the vehicle's own
   driver, a second asker refused and told who has it) and says so to
   everyone (`Gunner`). A V that lost the race is taken out of the seat
   again by its own game.
3. The gunner aims from its copy (`GunnerAim`, 20 Hz, yaw and pitch from
   the camera relative to the vehicle; the host relays only the holder's)
   and fires: its shots are `MountedFire`, its hits its own game's. Aim and
   fire feel local (principle 2).
4. The seat is free again when the gunner leaves it, when its game or the
   owner's vanishes (the host frees it on the timeout), or when the owner's
   game cannot seat the gunner's puppet (`FreeGunner`).

Not done: the owner's real turret does not turn or fire for show (no
by-name call found that turns a turret or plays its muzzle without a real
shot; a real one on the owner's game would be a second, unwanted hit).

## Collisions

- **Each game simulates the side it owns, never the other.** Our car is a
  real car on our game; another player's is a copy, a body put where its
  driver's game says. The driver's game runs the whole impact on its own
  car (bounce, damage, sound): nothing waits for the network.
- **Two players' cars.** Each driver's game runs its own car's side against
  the other's copy, and sends the push its car gave (`VehicleBump`, from
  the game's `VehicleBumpEvent`). The other game applies the push to its
  real car (a `PhysicalImpulseEvent`, the push times the car's mass) only
  when it did not feel that contact itself: a push is held 400 ms
  (`BumpLedger::kWindowUs`), and our own car touching the sender's copy
  within that window either side cancels it (the copy is drawn a little in
  the past, so our side may come first or second). One push per contact,
  never two; a push older than 1.5 s or above 25 m/s is dropped.
- **A car into an NPC.** On the driver's game the proxy is hit by the
  game's own vehicle hit (the damage goes to the NPC's owner as
  `NpcStrike`, as any hit on a proxy); the physical half goes as `NpcRam`
  with the car's velocity, to the game that runs that NPC (with per-player
  regions, `regions.md`, the owner the session names for it): it knocks its real NPC down and throws it
  along that velocity (ragdoll and impulse). Knock-down from 4 m/s; the
  game's own kill thresholds (25 m/s, 20 for crowd and civilians) say
  lethal.
- **What a copy causes is not ours.** On every game, a hit whose source is
  a copy of another player's car, or whose instigator is another player's
  puppet sitting in a vehicle (a copy's guns), does nothing to our world
  (`TandemWheelsSync.CausedElsewhere`, and `OnVehicleHit` dropped for a
  copy): that player's game decides it. Not dropped: the hits our game
  makes in a remote player's name on purpose (the host applying a guest's
  strike from its puppet, `Outcomes.reds`; a forwarded area hit). The one
  exception is our own V: what hits our V is our game's (a copy running
  over our V on foot hurts it here).
- Traffic: rams on world vehicles and their drivers' reactions are
  `traffic.md`'s (`TrafficHit` with the push).

## NPC vehicle chases

Built on the traffic branch's area owner, not beside it. A chase car is a
shared world vehicle (`TrafficSpawn`, `TrafficStates`) with its NPCs
seated (`NpcSeat`); its owner is the area's owner. The game's own AI does
the chasing: `InVehicleCombatDecorator` drives the vehicle after its NPCs'
combat target, which on the owner's game can be another player's puppet.
The owner wraps `ChaseNewTarget` and `Deactivate` and says who is chased
(`Chase{vehicle, target}`); the session keeps it while it lasts and ends it
when the vehicle or the target goes. On the target's game the chase car is
a proxy driving after its V; its collisions with our car are our car's to
run, its passengers' shots at our V `NpcShotAt` (the target's game fires
and decides them).

## Bikes, thrown out, bailing out

- **Leaving the hard way** is told by the game's own exit states, which run
  for our V only: `SpeedExitingEvents` (jumped out on the move),
  `CollisionExitingEvents` (thrown out by a crash; off a bike, knocked
  off), `DeathExitingEvents` (died in the seat), any other
  `ImmediateExitWithForceEvents` (ejected); destroyed while seated is
  blown out (`vehiclecombat::Classify`). Our game sends `VehicleExit`
  before its `Seat` says it is on foot.
- **The car rolls on, still ours.** A driver who leaves a car faster than
  2 m/s (`Rolls`) keeps it: its game goes on sending the car's
  `VehicleState` until it is slower than 0.5 m/s, or 12 s are over, or it
  gets into another vehicle. The others keep their copy following
  (`kExitRolling`) instead of parking it where the driver got out.
- **The V** is drawn by its own pose stream and position: the bail roll,
  the throw, the fall off a bike are the real V's animation, played on its
  puppet. A bike is a vehicle like any other (`Seat`, `VehicleState`: the
  lean is in its orientation).

## Inside the plugin

- `core/include/tandem/vehicle_combat.hpp` (`core/src/vehicle_combat.cpp`):
  the rules — grid packing, which damage state is newer, explode or wreck
  on arrival, a hit applied by the owner (the mock player's cars), exit
  kinds and rolling, ram flags, `BumpLedger`, `Turrets`.
- `core/src/endpoint_vehicles.cpp`: the session — who may say what, routing
  to a vehicle's owner, the host's record for late joiners (player cars and
  gunner seats with the players, shared vehicles' damage and chases with
  the NPCs), the digests, resyncs, and what goes when a player or a shared
  vehicle does.
- `plugin/src/wheels_natives.cpp` (`TandemWheels`, declared in
  `scripts/tandem/WheelsNatives.reds`): the game's side of it; the
  runtime's queues (`WheelsEvent`), the latest damage, gunner and aim of
  each vehicle, the bump ledger; stale hits, rams, exits and shots dropped
  (1.5 s, 1 s, 0.5 s).
- `scripts/tandem/Wheels.reds` (`TandemWheelsSync`): every hook above.
  `tools/game.sh cmd wheels` reports it; `wheels shoot` and `wheels gun`
  (`wheels ungun`) act on the first remote car copy, for scenarios.

## Consistency

| How it diverges                                    | What the player would see                        | Guard                                                                                                                                                                       |
| -------------------------------------------------- | ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| a late joiner, a copy spawned later                | an undamaged copy of a wreck                     | damage kept by the host and sent with the players (cars) or the NPCs (shared vehicles); the script re-applies it to a new body as a wreck                                   |
| a damage state lost (a bug, a full queue)          | a car whole on one screen, burning on the other  | in the players and NPC digests; a resync sends it again                                                                                                                     |
| sent twice, or an old one late                     | a car repaired then damaged again                | equal states dropped; a state older than the kept one (its revision) never applied; revisions count from the host clock's millisecond, so a game back from a crash is newer |
| an explosion heard after a loading screen          | a car blowing up the moment the world comes back | only within 3 s of its time; else a wreck put there                                                                                                                         |
| a hit applied twice, or behind a loading screen    | double damage; a car wrecked on arrival          | one-shot; behind the epoch fence (`VehicleHit`, `NpcRam`); dropped after 1.5 s                                                                                              |
| two games simulating one collision                 | a double bounce, double damage                   | each game its own car's side; a push applied only when not felt (`BumpLedger`); a copy's hits do nothing here (`CausedElsewhere`)                                           |
| two players asking for one gun                     | two gunners                                      | the owner's `Turrets` gives the first; the loser hears who has it and its game unseats it                                                                                   |
| the gunner or the owner vanishes                   | a seat held for ever                             | the host frees the gunner's seats and the vehicle's on the timeout (`ForgetVehiclesOf`), the owner's arbitration follows                                                    |
| a gunner's aim from a V without the seat           | a turret turned by two players                   | the host relays only the holder's aim; the endpoint sends ours only while the seat is ours                                                                                  |
| a chase whose target leaves, or whose vehicle goes | a car chasing nobody, on the guests' screens     | ended by the session (target gone; `ForgetTraffic`); a resync says which shared vehicles chase nobody                                                                       |
| a driver vanishes while its car rolls              | a copy rolling for ever                          | the copy follows its driver's states for 12 s at most; a vanished player's car is parked as it stood                                                                        |
| a player leaves                                    | its weapons-in-a-vehicle state kept              | forgotten with it on every game; a resync says who has none                                                                                                                 |

## Stages

Built in this change:

1. **Protocol and session**: the eleven messages, the routing, the host's
   record, late joiners, digests and resyncs, the epoch fence, vanishes.
   Tests: `tests/vehicle_combat_test.cpp` (the rules, and a real host and
   guests over loopback: damage and late joiners, hits to the driver only,
   the gunner race and a vanished gunner, a guest's own vehicle's seat,
   seat combat, exits and mounted shots, bumps, rams and the epoch fence,
   chases, healing by the digests, duplicates), `tests/protocol_test.cpp`.
2. **Mock player**: `--car-damage N`, `--exit-at N --exit-kind K`,
   `--bump N`, `--shoot-cars N`, `--gunner N [--gunner-seat SLOT]`,
   `--ram-npcs N`, `--chase N` (`testing.md`); `tests/peer_wheels.sh`
   runs a mock host and guests through all of it.
3. **Plugin**: `wheels_natives.cpp`, the runtime's queues and state.
4. **Scripts**: `Wheels.reds`, `WheelsNatives.reds`; `TandemSystem`
   keeps whose each parked copy was.
5. **Scenarios**: `wheels-guest`, `wheels-host`, `wheels-bike` (below).

Not built, in the order I would do them:

- The owner's real turret following the gunner's aim, and its shots for
  show (no by-name call known; the research file says what to probe).
- Windows broken by a hit (the message carries them; nothing reads a
  window's state yet).
- A guest's summoned car (`SpawnPlayerVehicle`) shared as its own: today
  any car a V drives is shared as that V's.
- Chases and the shared vehicles of a guest that owns its own region
  (`regions.md`): the session still takes world vehicles and chases from
  the host only (`traffic.md`, "Who owns an area", the hand-over).
- Quickhacks on vehicles stay `VehicleHack` (`combat.md`); a remote
  driver's car-to-NPC rams by an AI car (a guest's Delamain) are the ride's
  game's like any car.

## Scenarios to run in game

`tools/scenario.sh wheels-guest wheels-bike wheels-host` (the last from
the driver's seat of a car on a street with people around).

- **wheels-guest**: the game joins a mock host that drives round V. Our
  shot (`wheels shoot`) and our V on its gun (`wheels gun`) go to it; its
  street car chases V; its car is dented, loses a tyre and its bumper,
  burns and blows up once on our copy; it bails out and the copy rolls on.
  Look for: the dents and the bumper on the copy matching the mock's
  grid, the tyre flat, the fire, one explosion with its sound, the wreck
  rolling to a stop.
- **wheels-bike**: the host rides a bike and is knocked off it. Look for:
  the bike going on alone and falling, the rider's puppet thrown.
- **wheels-host**: the game hosts at the wheel; a mock guest shoots our
  car, pops a tyre, takes our gun and rams our NPC. Look for: our car's
  damage and tyre, the NPC knocked down along the guest car's way.
