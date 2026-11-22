# Traffic and world vehicles

One Night City has one street: the car one player swerves around is the car
the other sees. Until now only the cars players drive and the quest cars
they ride were shared; each game had its own traffic, or none where its
crowd is off. This is the design for the rest: moving traffic, parked cars,
police cars, wrecks.

Engine facts and their tags are in
[`research/engine/traffic-police.md`](../research/engine/traffic-police.md).
**Nothing here has run in the game yet** (it was written while the game
machine was in use): the core, the mock player and the tests are done and
pass; the scripts compile; the scenarios at the end are what to run first.

## The rule

| State                                              | Owner                                                                      | Others                                                        |
| -------------------------------------------------- | -------------------------------------------------------------------------- | ------------------------------------------------------------- |
| a traffic car, a parked car, a police car, a wreck | the game that announced it (the owner of its area then), until a hand-over | a proxy: a copy moved where the owner says                    |
| a placed vehicle (static id, the same everywhere)  | the same                                                                   | their own instance of it, moved and damaged as the owner says |
| who sits in it (driver, passengers, officers)      | the vehicle's owner (they are its NPCs, handed over with it)               | NPC proxies seated in the vehicle's proxy (`NpcSeat`)         |
| a vehicle a player got into                        | that player's game, from then                                              | the usual car copy (`Seat`, `VehicleState`)                   |
| the player's own car in a crash                    | that player's game                                                         | its copy                                                      |
| the traffic car in that crash                      | its owner                                                                  | proxy                                                         |

One decider per vehicle, as for NPCs: nobody simulates a competing copy.
Where a higher-ranked player's game runs the street, the others' crowd (and
with it their traffic) is off or thinned, and their own cars there are held
back, as for NPCs (`regions.md` §6).

### Who owns an area

The owner of the NPCs around a player owns the vehicles there too: the
area owner of per-player regions ([`regions.md`](regions.md), "The area
owner": a player's own game, unless its V is deep in the region of a linked
player that outranks it). Traffic follows the same owner, by construction,
because nothing here asks "am I the host". The code asks one question in
one place:

- core: `Endpoint::AreaOwner(player)` and `OwnsArea()` (`endpoint.hpp`);
- scripts: `TandemArea.Owns()` and `OwnsAround(player)` (`Traffic.reds`),
  which ask `TandemRegions.OwnsArea()` and `AreaOwner(player)`.

### Several owners, and hand-overs

Built in `core/src/endpoint_traffic.cpp` (`tests/traffic_regions_test.cpp`).
A world vehicle is owned the way an NPC is (`regions.md` §3, §4), with the
same rules and the same thresholds:

- **Who announces.** A game announces a new vehicle of its world only while
  it owns the area around its V (`OwnsArea`, checked by `SendTraffic*`): deep in a higher-ranked linked
  player's region it adds nothing (that game announces the street). The id
  carries the announcer (`MakeTrafficId(owner, counter)`: its player id in
  the top byte), so two owners never collide.
- **Who runs it** is the announcer until the host says otherwise
  (`TrafficOwnership{id, owner}`); every endpoint keeps the map
  (`Endpoint::TrafficOwnerOf`, `VehicleOwner`; scripts `TandemStreet.RunBy`,
  `TandemWheels.OwnerOf`). Ownership is sticky: players meeting move nothing.
- **What travels where.** The owner's word on its vehicles (spawn, states,
  status, gone, damage, chases) goes to the host, which checks the sender
  runs the vehicle, keeps the record and passes it to the players linked
  with that owner (`ShareTraffic`); its own game shows the vehicle when it is
  linked too. A guest linked with nobody sends its reliable record and its
  states once a second (`regions::kApartStatesUs`), which the host keeps
  and passes to nobody: the record is whole, and where its vehicles are, the
  moment a link forms. Requests (`TrafficHit`,
  `TrafficClaim`, a tyre's `VehicleHit`, an `NpcRam` on a rider) go to the
  vehicle's owner, whoever it is, through the host; the owner answers a claim
  and every game hears `TrafficGone{taken}`.
- **A link forms or breaks**: the newly linked player is sent the owner's
  vehicles (spawns, owners, statuses, damage, chases); a broken link is a
  `TrafficSet` of what it still sees, and the rest goes (`OnLinksChanged`).
- **Hand-over** (host, every 250 ms, `ArbitrateTraffic`): a vehicle whose
  owner's V is farther than 80 m (`kKeep`) and with a player linked with the
  owner within 55 m (`kTake`) goes to the highest-ranked such player
  (`regions::Taker`, on the vehicle's last place), never from an owner that
  holds its region nor while a gunner is on its gun. The steps are the NPC
  hand-over's, one message each (`TrafficHandover` to the giver,
  `TrafficHandoverState` giver → host → receiver, `TrafficHandoverDone`
  back, `TrafficOwnership` to everyone who sees it), retried 3 times after
  1 s, a refusal left alone 5 s, a receiver that cannot run it lets it go
  for everyone (`TrafficGone` away). **The riders go with it**: every NPC
  seated in the vehicle and owned by the giver is moved to the receiver in
  the same pass, still seated (no hand-over asks for a seated NPC), its
  chase's target as its target.
- **No pop, no duplicate.** Both games already have a body: the giver's real
  car becomes its proxy (stopped in its lane: `TandemHold`, the crowd's own
  `stop` pattern, then moved by the new owner's states); the receiver's proxy
  becomes the real car, put where the giver left it with its health, woken
  up with the giver's velocity and sent back into traffic the way a cab goes
  free (`TandemDriveOn`: an impulse, then `AIVehicleJoinTrafficCommand`,
  research §13.70). A parked car or a wreck stays where it stands. Its driver
  wakes up in its seat (Npcs.reds keeps a rider seated on `Take`).
- **An owner that vanishes, or whose world is rebuilt**: its vehicles go to
  the highest-ranked player linked with it within 80 m of each
  (`regions::Heir`, `kHandoverInherited`: the record's place and status);
  the rest go when it vanished, or stay its own after a fast travel (its game
  says what became of them).

## Options weighed

**1. The owner shares its traffic as proxies** (chosen). What NPCs already
do: announce once, stream a transform, apply the lasting state. Cost is
bounded by a budget (below). It needs the others' own traffic off, which
the crowd switch already does for guests (their streets are empty today),
and it makes collisions, theft and wrecks plain cases of "the owner
decides, the others ask".

**2. Deterministic traffic seeded per region, authoritative only near
players.** Rejected. The traffic system is native with no script surface
(two queries, no seed, no spawn, no step), so "the same traffic from the
same seed" would be a rewrite of it, not a setting. And traffic reacts to
everything (a V on the road, a gunshot, a wreck, a light) within seconds:
two simulations diverge the moment either player is near, which is exactly
when it matters. It is the "each game simulates its copy and we sync the
result" the architecture forbids.

**3. Parked cars by their world ids.** Used, as far as it is true. A
placed vehicle has a static `EntityID`, the same in every game: the owner
announces it with that id (`TrafficSpawn::entity`) and the others take
their own instance over as its body, with no copy and no pop. But most
parked cars are the crowd system's (parking spaces filled per game, dynamic
ids): those are shared as proxies like moving traffic, flagged parked, and
cost one message each until someone moves them.

## What travels

Appended to the protocol (`protocol.hpp`); no existing message changed.

| Message                | Channel         | From → to                                   | What                                                                                                                                                 |
| ---------------------- | --------------- | ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `TrafficSpawn`         | Event           | owner → all (kept)                          | id, record, appearance, static entity id or 0, transform, flags                                                                                      |
| `TrafficStates`        | State, 10 Hz    | owner → all                                 | the vehicles that move: id, position, rotation, velocity (28 bytes)                                                                                  |
| `TrafficStatus`        | Event           | owner → all (kept)                          | health 0..1 and the lasting flags (wreck, siren, parked, panic…)                                                                                     |
| `TrafficGone`          | Event           | owner → all                                 | out of range, removed, or **taken** by a player                                                                                                      |
| `TrafficHit`           | Event, one-shot | a player's game → owner                     | damage and, for a ram, the push given                                                                                                                |
| `TrafficHack`          | Event, one-shot | a player's game → host → every other player | a vehicle quickhack (brakes, explode, accelerate) and its length: the owner applies it, everyone marks it (`devices.md`, "A partner's hack, marked") |
| `TrafficClaim`         | Event, one-shot | a player's game → owner                     | "my V is getting into this one"                                                                                                                      |
| `TrafficSet`           | Event           | host → one guest; a guest owner → host      | opens a resync: every vehicle shown to it now; from a guest, its whole set                                                                           |
| `NpcSeat`              | (existing)      | owner → all (kept)                          | `owner` with the top bit set names a shared vehicle, not a player                                                                                    |
| `TrafficOwnership`     | Event           | host → all who see it (kept)                | who runs a vehicle now (absent: its announcer)                                                                                                       |
| `TrafficHandover`      | Event, one-shot | host → owner                                | give it to `to`                                                                                                                                      |
| `TrafficHandoverState` | Event, one-shot | giver → host → receiver                     | place, orientation, velocity, health, flags (inherited: the host's record)                                                                           |
| `TrafficHandoverDone`  | Event, one-shot | receiver or giver → host                    | runs it now (or cannot: it goes); a giver's refusal                                                                                                  |

"Owner → all" reads "owner → the players linked with it, through the host".

### Budget

- **Range**: a vehicle is shared within 120 m of any player and dropped
  beyond 150 m (`traffic::InRange`: no flicker at the edge). At most 48 at
  once (`kMaxShared`): what a game is asked to show.
- **A packet** holds at most 32 entries of 28 bytes: position as floats,
  the rotation in 6 bytes (the quaternion's i, j, k in 1/32767 with r ≥ 0,
  under 0.01° of error), the velocity in cm/s. Ten a second is **under
  9 kB/s per guest whatever the street holds**; a quiet street is a few
  hundred bytes.
- **What goes in** (`traffic::Budget`, tested): only vehicles that moved.
  The nearest three quarters of a packet go every round; the rest share
  what is left, longest waiting first, so a far car is refreshed less
  often and never starved. A vehicle that stops is said once more at rest
  (its last state had a velocity: the others would run it on), then leaves
  the stream, with a keepalive every 2 s. A parked car costs nothing until
  something moves it.
- **Drawing**: each vehicle has its own track (`sync::VehicleTrack`, the
  one player cars use), drawn 200 ms in the past (two samples of a 10 Hz
  stream around it), extrapolated up to 500 ms along its velocity when a
  far car only gets every third packet, then held. Proxies are moved by
  the engine's kinematic vehicle move, a teleport only for a jump
  (`vehicles.md`, "How a copy is moved"); one at rest is left alone.
- **Occupants** are NPCs of the owner, shared by the existing NPC range
  (60 m): beyond it a car's proxy has nobody at the wheel, which at that
  distance is behind a windscreen. Inside it the driver is an NPC proxy
  seated with `NpcSeat`, and costs what any NPC costs.

## Collisions: who simulates the impact

Each game simulates the side it owns, and never the other.

- **The player's car** is its driver's: on that game it is a real,
  physical car hitting a proxy, which stands like a wall for the instant
  (a body that is put where it is told). The driver's game runs the whole
  impact on its own car: the bounce, the damage, the sound. Nothing waits
  for the network (principle 2).
- **The traffic car** is its owner's. Two things reach the owner: the
  copy of the player's car arrives at the same place a moment later
  (drawn in the past, as always) and pushes the real traffic car with its
  own body; and the driver's game sends a `TrafficHit` flagged collision
  with the push it gave (from the game's own `VehicleBumpEvent`: the
  velocity change, away from the player's car). The owner wakes the real
  car's physics and runs the game's own reaction to a bump in traffic
  (`HandleTrafficBump`: the driver honks, swerves, panics or is knocked
  out, by the same thresholds as alone). The outcome comes back to
  everyone as state: where the car ends up, its health, its driver.
- Damage is computed where the hit is seen: bullets and blasts from a
  player's game go as `TrafficHit` with health points (that game's weapon
  and perks), and the owner applies them to the real vehicle's health
  pool, with that player's puppet as instigator. The proxy takes the hit
  locally at once, so the smoke starts when the player sees the hit; the
  owner's `TrafficStatus` then says what it really is.
- Two players ramming the same traffic car: two pushes on the one real
  car, in the order the owner gets them.
- A traffic car hitting a player's parked car, or a player on foot: the
  owner's car is moved by the owner; the parked car belongs to whoever
  drove it last (its copy elsewhere is not pushed), and a V on foot is
  hurt on its own game by the proxy that hits it: the proxy is moved with
  its owner's velocity (the kinematic move), so the game's own hit runs
  (a knockdown, thrown aside); teleported, it had no speed and carried a V
  on its hood. To check in game.
- **Our traffic stops for a partner's V** (`TandemTraffic.Yield`, ten times
  a second on the cars we run; written 2026-11-22, not run in game). The
  native traffic system brakes for our own V by itself and drove through a
  partner's puppet (a host's V run over by a guest's car, 2026-11-22); its
  test for what is in a lane has no script surface (a native of
  `worldRuntimeSystemTraffic`, not decompiled), so the owner's script
  does it: a driven crowd car of ours (not in a panic, not the police,
  not a wreck) with a partner's V on foot in the lane ahead (1.8 m to each
  side, from its nose to 1.5 s of its speed ahead, 5 to 25 m) gets the
  crowd's own `stop` pattern (what `OnStuckEvent` sets) and its horn, and
  `normal` again a second after the lane is clear. A puppet in a seat or
  hidden is not on foot. `tools/game.sh cmd street lanes` lists our driven
  cars and the stops so far; `street count` ends with the stops.
- **No hand-over onto a V** (`regions::ClearOf`, `ArbitrateTraffic`): a
  vehicle within 4 m (`kHandoverClear`, from its origin: half a car and a
  couple of metres) of the V that would take it stays its owner's, asked
  again on a later pass. Handed over, it woke up as a real car of the
  receiver's traffic and drove on with that V on its hood (750 m,
  2026-11-22). Tested (`tests/traffic_regions_test.cpp`,
  `tests/regions_test.cpp`).

## A car a player takes

The vehicle becomes that player's car under the existing rule ("the game
that moves the vehicle owns it"), with no copy spawned on top of it.

1. The player's V starts getting in (`VehicleObject.OnMountingEvent`). Its
   game stops moving the proxy at once (it is the player's to drive now)
   and sends `TrafficClaim`. A game that owns the area claims its own
   vehicle the same way, locally.
2. The owner's side of the session (`Endpoint::TakeTraffic`) grants the
   first claim: the vehicle leaves the shared set and **everyone** gets
   `TrafficGone{taken, player}`, the owner's game too. Whoever sat in it
   is out (`NpcSeat` cleared for all).
3. Every game hands the body it has to that player: at the owner's it is
   the real traffic car (its NPCs are put out beside the door with the
   game's own `VehicleHijackEvent`: they flee or fight as when V jacks a
   car), elsewhere the proxy. Each seat is read as the seat check reads it
   (the mounting facility's child of that slot; `GetAllPassengers` and a
   filter that spared DES-managed NPCs put no one out, 2026-11-21); a
   player stays, an NPC another game runs is its owner's. When the
   player's `Seat` arrives, the puppet's car copy is that body
   (`TandemTraffic.TakeAdopted`): the car never pops, and the puppet sits
   where the driver sat, half a second later (the driver's unmount lands
   first; not yet run in game).
4. From then on it is `Seat` + `VehicleState` from the thief, 30 Hz, as
   any car a player drives. When they get out it is a car they left
   parked.

Two players at the same door: the first claim wins; the other hears the
same `TrafficGone{taken, winner}`, and its game takes its V out of the seat
again. A claim on a vehicle that went out of range at that moment is
answered by the `TrafficGone{away}` already on its way: the claimer keeps
the car it sits in (it is theirs to drive either way).

The crime, if it is one, is the thief's: its own game ran the mount and
`StealVehicle`, and its own prevention system counts the witnesses (see
[`police.md`](police.md)).

## Drivers and passengers

They are the owner's NPCs and travel as NPCs, with the message that
already seats Panam in the host's quest car: `NpcSeat{npc, owner, seat}`.
`owner` names the vehicle; with the top bit set (`kSeatInTraffic`) the
rest is a shared vehicle's id instead of a player's. The host's share loop
sends it for any NPC it shares that sits in a shared vehicle; a guest seats
the proxy in the vehicle's proxy, takes it out when the host says so, and
deletes a seated proxy only once it is out (the rule for car copies). When
the vehicle goes or is taken, the session clears its seats for everyone.

A driver that gets out (a crash, a panic, pulled out) is an NPC on foot
from that moment: `NpcSeat` out, then its states as for any NPC.

## Explosions and wrecks

- A vehicle's health is state (`TrafficStatus`), kept for late joiners and
  in the digest. The proxy's own health pool follows it, so the damage
  stages (smoke, then fire) show by themselves on every game.
- At zero the owner's vehicle explodes the game's way. On the others the
  status says destroyed and the proxy's health is set to its minimum: the
  **game's own explosion** runs there too (the blast effect, the sound,
  parts flying, the wreck), once. Each V near it is hurt by the explosion
  its own game runs, by its own armour and difficulty: the target's game
  decides, as for every other hit.
- A wreck stays: it is a shared vehicle with the destroyed flag until it
  goes out of range. A late joiner, or a guest that arrives later, gets it
  as a wreck put there (destroyed state and appearance), not blown up
  again.
- Who pays for it: the owner's `VehicleComponent.OnDeath` runs with the
  instigator the owner applied the damage with (the offender's puppet),
  so the owner's V is not blamed for a car a guest blew up. The heat is
  the offender's game's (its own hit on the proxy; `police.md`).

## Proxy bodies: the pool, and the engine's own spawning

Written 2026-11-22; the scripts compile, **nothing of it has run in game**.
Engine facts: `research/engine/vehicles.md` §13.143, `re.md` §13.142.

**The churn.** Each proxy was one body created when it appeared and deleted
when it left. Two sessions of 2026-11-21 on a guest: 291 arrivals of 52
vehicle records in 12 min (16 to 44 a minute), 540 of 71 records in 15 min
(up to 129 a minute, riding). The re-use there was (`Reuse`, `m_retired`)
kept only a body someone had sat in (3 in those 12 min), where it stood,
without bound and for ever; every other body was deleted the moment its
vehicle left, so it absorbed nothing.

### (a) The body pool: done

`TandemTraffic` (`Traffic.reds`): `Retire`, `Keep`, `Place`, `Take`,
`Sweep`, `Bound`.

- **A proxy leaves**: its body goes to the pool, whatever it is (not a
  body of our own world, not one a player got into: those stay as before).
  `Keep` (every tick, session or not: the pool outlives a session) parks it
  once no seat is occupied: 600 m under the place it left, on a level of its
  own (10 m apart: no two parked bodies touch), by the move every proxy is
  moved by (`TandemSystem.MoveBody`: a first move is a teleport; then
  nothing while the body rests there, the kinematic move when it sagged
  5 cm). No script call hides a vehicle for good, puts its physics to sleep
  or takes its collisions away: the place does all three (out of sight,
  nothing to touch, no traffic near it). Until it is parked it stands where
  it was, as a body someone sat in always did.
- **A vehicle of that record arrives** (`OnSpawn`, and the check that asks
  again for a body that did not come: `Obtain`): `Take` picks a parked body
  of the record, one that wears the look asked for first, teleports it to
  the vehicle's place (a discontinuity, `vehicles.md` "How a copy is
  moved") and hands it to a new `TandemCar`. The log says `takes a parked
body` where it said `spawning`.
- **Only a body that stands as it spawned is used again** (the table
  below); any other is parked all the same and deleted later.
- **Bounds**: 32 bodies ready to be used again in all, 3 of one record,
  one unused for 300 s let go; over a bound the one parked longest goes.
  With the 48 shown at most (`kMaxShared`) that is 80 street bodies, plus
  the ones someone sat in. Why not more: a pool per record cannot absorb a
  street of 50 to 70 records. Replayed on the two sessions above (a proxy
  taken to live 20 to 90 s): these bounds re-use 24 to 60 % of the
  arrivals, 48 bodies and 4 a record 35 to 69 %, no bound at all 44 to 73 %
  with up to 143 bodies parked. The rest of the churn is the share range's
  (120 m in, 150 m out, on a street driven through), not the pool's.
- **Deleting** (`Sweep`, twice a second, one deletion a sweep, through
  `TandemEntities.Delete`): a body let go or never fit to be used again,
  parked for 5 s or more, that exists (`IsSpawned`: never one being
  created, §13.140) and has nobody in a seat. A body someone sat in is
  never deleted (§13.6): it stays in the pool, used again if it is fit,
  else parked for good. A body whose vehicle left before it came is kept
  (`not here yet`) and parked when it comes; one that does not come in
  10 s, or is gone from its parking place (a save loaded, the engine's own
  removal), is forgotten (`lost`).
- **`test reset`** lets every parked body go and starts the counts again
  (`ResetPool`).
- **`street count`** ends with the pool: `bodies: N created, N re-used (N
with another look), N parked, N deleted, N lost;` then `pool N (N ready,
N to delete, N kept for good, N not here yet)`. A line per body at
  `log = debug` (`street: pool: …`).

What a re-used body carries, against a fresh spawn's:

| State                                                                            | Fresh spawn                  | Re-used body: where it is rewritten, or why it cannot differ                                                                                                                                           |
| -------------------------------------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| place, speed                                                                     | the spec's place             | `Take`: a teleport; the new `TandemCar` has no `move`, so its first `MoveBody` is a teleport to where the owner says, then the kinematic move                                                          |
| look                                                                             | the spec's appearance        | a body of that look first; else `ScheduleAppearanceChange` (the old look shows while the new one loads: to see)                                                                                        |
| paint (CrystalCoat, `Paint.reds`)                                                | none                         | none: `CarPaint` is a player's car's, applied to its copy (`Tandem.Vehicle`); the game writes one itself only when our V mounts, and a body our V got into is never parked (`claimed`, `m_taken`)      |
| health                                                                           | `Show` once the body is here | the same call: `ready` is false on the new `TandemCar`; the body's own was 99 % or more (`Sound`)                                                                                                      |
| wreck, fire, explosion, drowned                                                  | `Show` when flagged          | the same call; a body that ever was any of them is not used again (`Pristine`: `wrecked`, flags 4 and 8; `Sound`: `IsDestroyed`, the state's destroyed, submerged, exploded)                           |
| dents, parts off, glass                                                          | none                         | no script reads them: a body is used again only with its health whole here and at its owner's (a hit lands on the health), no damage word (`TandemWheels.DamageOf`), no ram by our car (`m_ramBodies`) |
| tyres                                                                            | whole                        | `Sound`: `GetFlatTireIndex` below 0; `Pristine`: no damage word (a flat tyre is one)                                                                                                                   |
| doors, windows, lights, engine                                                   | closed, off                  | nothing of ours opens or switches a proxy's (a seat is an instant mount; flag 32 is not sent); a body a V got into is never parked                                                                     |
| siren                                                                            | off, `Show` when flagged     | `Retire` switches it off (`ToggleSiren`); `siren` is false on the new `TandemCar`, `Show` as for a fresh one                                                                                           |
| radio                                                                            | off                          | `Radio.reds` sets players' cars only                                                                                                                                                                   |
| occupants, seated NPC proxies                                                    | none                         | `Keep` parks and `Take` takes only a body with every seat empty (`Occupied`, read as `Evict` reads a seat); the NPC proxies leave by their owner's word (`Npcs.reds`: `FollowSeat`, `Remove`)          |
| `seated` (someone sat in it)                                                     | false                        | carried from the pool (`TandemPooled.seated`): still never deleted                                                                                                                                     |
| hack mark                                                                        | none                         | `Retire`: `TandemHacks.CarGone` ends the vehicle's mark at the next tick                                                                                                                               |
| `ready`, `wrecked`, `claimed`, `held`, `own`, `goneSince`, `goneTries`, `yields` | a new `TandemCar`            | a new `TandemCar` (`OnSpawn`); the check resets them before `Obtain`                                                                                                                                   |
| tyres and dents put on it (`TandemWheelsCopy`)                                   | by vehicle id                | by vehicle id: a new vehicle is a new record                                                                                                                                                           |

To settle in game (`traffic-pool`, then a drive as a guest): that a parked
body stays where it is parked (`vehicle moves` says no body fails to
follow, `street count` says `0 lost`), that none is seen or met, that one
taken back shows no trace of its last use, the pop of a changed look, and
the counts: `re-used` a third to a half of `created` plus `re-used` on a
street, `deleted` well below `created`, no `body was gone`.

**NPC proxies have no pool.** A street's NPCs are of many more records and
looks than its cars; what a use leaves on a puppet is not rewritten by the
join path and much of it cannot be read or undone from a script (dead or
defeated, a ragdoll, a hit reaction or a workspot in progress, status
effects, weapons drawn, its level and scaling modifiers, the native
controller and puppet id); a corpse stays where it fell; and a parked
puppet has no place to stand (its movement is its controller's). Each
proxy keeps its own body, deleted when it goes.

### (b) The engine keeps the body's lifetime: possible, a native, not done

With `alwaysSpawned = false` the population system despawns and respawns
a registered body by distance; its pooling update reads the place from the
body while it is attached and from its stub otherwise (§13.140), and the
engine does move stubs: one function (hash 1625104190) sets a stub's place,
called from 18 places, its own traffic among them (each update the traffic
system puts the stub of every pooled car on its traffic slot, the place
its lane has it at: §13.143). So a proxy's stub could follow its owner. It
needs a native (the stub by `FindStub`, then that function, behind a
startup check), and it would not remove the churn here: a proxy goes at
150 m because its owner says so, well inside any streaming distance, and a
respawned body is a new one, to be dressed again by `Show`. Where it would
help is what is far and still: the players' parked car copies, never
deleted today. Codeware's `DisableEntity` / `EnableEntity` are a delete
and a create that keep the id and the tags (the body is destroyed, the new
stub is at the place first asked): nothing over `TandemEntities.Delete`.

### (c) Stubs of our own with the engine's `CancelStub`: not needed

`CancelStub` (hash 303576408) only matters for a body deleted before its
stub exists. Tandem waits until it does (`TandemEntities.Delete`), and the
pool keeps such a body rather than delete it. The crash of 2026-11-22 was
not that path (§13.142): an NPC detached in the middle of an action, in the
engine's own detach job; neither the back-off of the street's check nor
`CancelStub` covers it. What would: a guard of that one read in the plugin
(`guards.cpp`, as §13.98's).

## Consistency

| How it diverges                                                                 | What the player would see                                                      | Guard                                                                                                                                                      |
| ------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| a late joiner                                                                   | an empty street                                                                | spawns (at the latest transform), statuses and seats are kept and sent with the NPC domain                                                                 |
| a spawn, a removal or a status lost (a bug, a full queue)                       | a missing car, a ghost, a car that should be a wreck                           | the shared set and each status are hashed into the NPC digest; a resync opens with `TrafficSet`                                                            |
| sent twice (a resync, a duplicate)                                              | two cars in one place                                                          | a known id is not announced again; statuses equal to the copy are dropped; hits and claims are one-shots                                                   |
| a state overtakes its `TrafficGone` (other channel)                             | a car back from nowhere                                                        | states only for known ids                                                                                                                                  |
| the owner's world is rebuilt (fast travel)                                      | cars drawn where the old street was                                            | `TrafficStates` share the NPC states' numbering: `Epoch::firstNpcSeq` fences both                                                                          |
| a hit or a claim aimed at the owner's old world                                 | a wrong car damaged or taken                                                   | behind the epoch fence, like `NpcHit`                                                                                                                      |
| hits queued behind a loading screen                                             | a car wrecked the moment the world is back                                     | dropped after 3 s                                                                                                                                          |
| a proxy's body streamed out or lost in our fast travel                          | an invisible car                                                               | the script's check against the session's record, once a second: spawned again after 3 s                                                                    |
| our V rides in a car another game moves (its copy here, teleported every frame) | no proxy of anyone's traffic, nor of their NPCs: an empty street from the seat | every vehicle another game runs is moved by the engine's kinematic move, no teleport (`vehicles.md`, "How a copy is moved"; engine-knowledge §13.141)      |
| the owner vanishes                                                              | frozen cars                                                                    | the session ends on the 5 s timeout; proxies go with it, our crowd and traffic come back                                                                   |
| a guest vanishes in a car it took                                               | —                                                                              | it was its car: it stays parked with its puppet's car copy, as any car of a player that left                                                               |
| a guest vanishes mid-claim                                                      | a car nobody drives                                                            | the claim was granted or not; if granted the body is its (parked) car, if not nothing changed                                                              |
| a proxy that had an occupant must go                                            | a crash (engine-knowledge §13.6)                                               | never deleted: parked in the body pool and used for the next vehicle of its record ("Proxy bodies")                                                        |
| a guest owner's spawn or removal lost                                           | a car missing for its linked players, or a ghost                               | its vehicles are hashed into the `OwnedDigest` with its NPCs; twice different, it sends its whole set (`TrafficSet` from the guest, its spawns, statuses)  |
| a hand-over step lost                                                           | a car nobody runs, or run twice                                                | one owner at every moment (the host moves it when the giver's state arrives); each step retried 3 times; a repeated state is answered with the same `Done` |
| the receiver cannot run it, or leaves mid hand-over                             | a frozen car                                                                   | it goes for everyone (`TrafficGone` away); a receiver that vanished is an owner that vanished                                                              |
| a request aimed at the old owner                                                | a hit or a claim lost                                                          | routed by the session's owner map at the host: the new owner gets it                                                                                       |
| a guest's word about a vehicle it does not run                                  | a car moved or wrecked by two games                                            | the host drops it (and the referee flags it: `authority.not-owner`)                                                                                        |

## Stages

Built in this change:

1. **Protocol and session** (`protocol.hpp`, `endpoint.cpp`,
   `traffic.hpp`): the messages, the owner's record, late joiners, claims,
   the epoch fence, the digest and resync. Tests: `tests/traffic_test.cpp`
   (packing, the budget, and a real host and guests over loopback: late
   joiner, claim race, hits, fence, heal by digest, duplicates, a vanish),
   `tests/protocol_test.cpp`, `tests/consistency_test.cpp`.
2. **Mock player**: `tandem-peer host --traffic N [--npc REC]
[--wreck-at S] [--traffic-at S]` drives N cars round the guest and parks
   one (announced S seconds after the guest is there: during its ride,
   with `--mode drive`); `--traffic-churn S` takes a driving car away every
   S seconds and announces a new vehicle of its record S/2 later; `join
--ram-traffic S`, `--steal-at S` (`--steal-driven`: a car with a driver).
3. **Plugin**: `street_natives.cpp` (`TandemStreet`), the runtime's queues
   and per-vehicle tracks, the packet budget on the game thread.
4. **Scripts**: `Traffic.reds` (the owner's share loop, proxies, status,
   claims and adoption, hits and bumps, checks), `NpcSeat` in shared
   vehicles (`Npcs.reds`), adoption in `System.reds`.
5. **Scenarios**: `traffic-guest`, `traffic-host` (below).

6. **Several owners** (`endpoint_traffic.cpp`): a guest that owns its area
   shares its vehicles, requests go to each vehicle's owner, hand-overs
   when players part (riders with the car), heirs when an owner goes, the
   owned digest. Tests: `tests/traffic_regions_test.cpp`; mock player
   `--mode region --region-cars N [--region-driver] [--region-chase]`;
   scenario `wheels-guest-area`.

Not built, in the order I would do them:

- **Probes first** (research §13.34, §13.70): parked crowd cars under
  density 0, the cost of 30–40 teleports a frame, a wheel-turning proxy, a
  given crowd car held in its lane (`TandemHold`), a woken proxy joining
  traffic (`TandemDriveOn`).
- Horn, indicators, headlights, doors (the flags have room; nothing sends
  them yet).
- AVs and the rest that is not a car or a bike are left to each game.
- A game's own crowd cars where another game runs the street are held
  back (never announced there) and kept from spawning (`regions.md` §6,
  "The overlap, held back": `overlap`, `overlap_despawn`); one already
  there is not removed (no known call does: the despawn probe is off by
  default).

## Scenarios to run in game

`tools/scenario.sh traffic-guest traffic-host wheels-guest-area` and `ride-traffic traffic-yield traffic-pool` (with
`police-guest police-host`, see `police.md`).

- **traffic-guest**: the game joins a mock host that drives four cars
  round V and parks one, with a driver in the first; one burns and blows
  up. Checks the proxies' count, the seated driver, one explosion, the
  wreck, and that everything goes with the host. Look for: cars sliding
  or hovering, wheels not turning, the driver's pose, the explosion's
  sound and fire, the wreck staying.
- **traffic-host**: the game hosts on a street with traffic (drake-ave:
  the test field has parked cars only); the mock guest rams a car every
  4 s, then gets into one with a driver. Checks that our cars are
  announced, the ram applied to the real car, the driver put out, the
  taken car handed to the guest's puppet at its wheel (`query mounted
puppet`) with no second copy. Look for: the rammed driver's
  reaction, the evicted driver beside the door, the puppet at the wheel
  of the same car.
- **ride-traffic** (written 2026-11-22, not yet run): the game joins a
  mock host that drives, our V rides in its car copy, and the host's four
  cars and a parked one are announced 30 s in (`--traffic-at`), during the
  ride. Checks that every proxy gets its body from the seat, that a
  vehicle of our own spawns too, and that no body is asked for again
  (`street: check: … body was gone`). Look for: the cars round us while
  we ride, the copy's motion while they appear (held every other frame).
- **traffic-yield** (written 2026-11-22, not yet run): the game hosts on a
  street with traffic, a mock guest stands in a lane (`YIELD_AT`,
  `YIELD_OFFSET`: the default spot is a guess, `cmd street lanes` helps to
  pick one). Checks that a car of ours stops for it. Look for: the car
  stopping short and honking, the queue behind it, the puppet never pushed.
- **traffic-pool** (written 2026-11-22, not yet run): the game joins a
  mock host whose street churns (`--traffic 4 --traffic-churn 8`). Checks
  that five bodies are created in all, that every vehicle announced later
  takes a parked one, nothing deleted or lost meanwhile, and that the pool
  keeps three of the record and deletes two once the host is gone. Look
  for: a car coming back with no pop, nothing left where a car went out of
  range, nothing falling past a bridge or lying under the sea.
- **wheels-guest-area**: the game hosts; a mock guest 400 m away owns its
  area and runs three cars of its own (the first driven by its NPC, chasing
  us once we meet). It walks up (its cars appear as proxies, its chase
  reaches us), then off, leaving them by our V: handed over to our game.
  Checks the proxies, the chase, the hand-over in and the driver inherited
  in its seat, nothing refused. Look for: no car popping or doubling when
  it changes hands, the driven one driving on in our traffic (or stopping:
  note which), the driver still at the wheel, the giver's car on the mock's
  side (a proxy now) not fighting its new owner.
