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

| State                                              | Owner                         | Others                                                        |
| -------------------------------------------------- | ----------------------------- | ------------------------------------------------------------- |
| a traffic car, a parked car, a police car, a wreck | the owner of its area         | a proxy: a copy moved where the owner says                    |
| a placed vehicle (static id, the same everywhere)  | the owner of its area         | their own instance of it, moved and damaged as the owner says |
| who sits in it (driver, passengers, officers)      | the owner (they are its NPCs) | NPC proxies seated in the vehicle's proxy (`NpcSeat`)         |
| a vehicle a player got into                        | that player's game, from then | the usual car copy (`Seat`, `VehicleState`)                   |
| the player's own car in a crash                    | that player's game            | its copy                                                      |
| the traffic car in that crash                      | the owner of its area         | proxy                                                         |

One decider per vehicle, as for NPCs: nobody simulates a competing copy, and
a guest's own traffic stays off.

### Who owns an area

Today the host's game runs the world everywhere a session reaches: it owns
every area. With per-player regions (`regions.md`, in progress on its own
branch) the owner of the NPCs around a player owns the vehicles there too:
traffic follows the same owner, by construction, because nothing here asks
"am I the host". The code asks one question in one place:

- core: `Endpoint::AreaOwner(player)` and `OwnsArea()` (`endpoint.hpp`);
- scripts: `TandemArea.Owns()` and `OwnsAround(player)` (`Traffic.reds`).

Both answer "the host" today. What is already shaped for several owners:
a vehicle's id carries its owner (`MakeTrafficId(owner, counter)`: the
owner's player id in the top byte), so two owners never collide and a
request about a vehicle (`TrafficHit`, `TrafficClaim`) can be routed to its
owner by the id alone. What regions will have to add: the host relaying a
guest-owner's announcements and those requests (one function each in
`endpoint.cpp`, where the comment says so), and a hand-over rule. Moving
traffic is not worth handing over: when an area changes owner the old
owner's cars go (`TrafficGone`) and the new owner's own traffic is announced;
parked cars and wrecks, which a player may be looking at, are re-announced
by the new owner at the same place from the old owner's list.

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

| Message         | Channel         | From → to               | What                                                                |
| --------------- | --------------- | ----------------------- | ------------------------------------------------------------------- |
| `TrafficSpawn`  | Event           | owner → all (kept)      | id, record, appearance, static entity id or 0, transform, flags     |
| `TrafficStates` | State, 10 Hz    | owner → all             | the vehicles that move: id, position, rotation, velocity (28 bytes) |
| `TrafficStatus` | Event           | owner → all (kept)      | health 0..1 and the lasting flags (wreck, siren, parked, panic…)    |
| `TrafficGone`   | Event           | owner → all             | out of range, removed, or **taken** by a player                     |
| `TrafficHit`    | Event, one-shot | a player's game → owner | damage and, for a ram, the push given                               |
| `TrafficClaim`  | Event, one-shot | a player's game → owner | "my V is getting into this one"                                     |
| `TrafficSet`    | Event           | owner → one guest       | opens a resync: every vehicle shared now                            |
| `NpcSeat`       | (existing)      | owner → all (kept)      | `owner` with the top bit set names a shared vehicle, not a player   |

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
  far car only gets every third packet, then held. Proxies are teleported
  only when they moved.
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
  hurt on its own game by the proxy that hits it. Known gap: a proxy is
  teleported, so its "speed" when it touches a V is the game's guess, not
  the owner's; being run over by traffic on a guest is to check in game.

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
   car), elsewhere the proxy. When the player's `Seat` arrives, the
   puppet's car copy is that body (`TandemTraffic.TakeAdopted`): the car
   never pops, and the puppet sits where the driver sat.
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

## Consistency

| How it diverges                                           | What the player would see                            | Guard                                                                                                    |
| --------------------------------------------------------- | ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| a late joiner                                             | an empty street                                      | spawns (at the latest transform), statuses and seats are kept and sent with the NPC domain               |
| a spawn, a removal or a status lost (a bug, a full queue) | a missing car, a ghost, a car that should be a wreck | the shared set and each status are hashed into the NPC digest; a resync opens with `TrafficSet`          |
| sent twice (a resync, a duplicate)                        | two cars in one place                                | a known id is not announced again; statuses equal to the copy are dropped; hits and claims are one-shots |
| a state overtakes its `TrafficGone` (other channel)       | a car back from nowhere                              | states only for known ids                                                                                |
| the owner's world is rebuilt (fast travel)                | cars drawn where the old street was                  | `TrafficStates` share the NPC states' numbering: `Epoch::firstNpcSeq` fences both                        |
| a hit or a claim aimed at the owner's old world           | a wrong car damaged or taken                         | behind the epoch fence, like `NpcHit`                                                                    |
| hits queued behind a loading screen                       | a car wrecked the moment the world is back           | dropped after 3 s                                                                                        |
| a proxy's body streamed out or lost in our fast travel    | an invisible car                                     | the script's check against the session's record, once a second: spawned again after 3 s                  |
| the owner vanishes                                        | frozen cars                                          | the session ends on the 5 s timeout; proxies go with it, our crowd and traffic come back                 |
| a guest vanishes in a car it took                         | —                                                    | it was its car: it stays parked with its puppet's car copy, as any car of a player that left             |
| a guest vanishes mid-claim                                | a car nobody drives                                  | the claim was granted or not; if granted the body is its (parked) car, if not nothing changed            |
| a proxy that had an occupant must go                      | a crash (engine-knowledge §13.6)                     | never deleted: kept out of range and reused for the next vehicle of its record                           |

## Stages

Built in this change:

1. **Protocol and session** (`protocol.hpp`, `endpoint.cpp`,
   `traffic.hpp`): the messages, the owner's record, late joiners, claims,
   the epoch fence, the digest and resync. Tests: `tests/traffic_test.cpp`
   (packing, the budget, and a real host and guests over loopback: late
   joiner, claim race, hits, fence, heal by digest, duplicates, a vanish),
   `tests/protocol_test.cpp`, `tests/consistency_test.cpp`.
2. **Mock player**: `tandem-peer host --traffic N [--npc REC]
[--wreck-at S]` drives N cars round the guest and parks one; `join
--ram-traffic S`, `--steal-at S`.
3. **Plugin**: `street_natives.cpp` (`TandemStreet`), the runtime's queues
   and per-vehicle tracks, the packet budget on the game thread.
4. **Scripts**: `Traffic.reds` (the owner's share loop, proxies, status,
   claims and adoption, hits and bumps, checks), `NpcSeat` in shared
   vehicles (`Npcs.reds`), adoption in `System.reds`.
5. **Scenarios**: `traffic-guest`, `traffic-host` (below).

Not built, in the order I would do them:

- **Probes first** (research file §3): parked crowd cars under density 0,
  the cost of 30–40 teleports a frame, a wheel-turning proxy.
- Horn, indicators, headlights, doors (the flags have room; nothing sends
  them yet).
- The regions hand-over (above).
- AVs and the rest that is not a car or a bike are left to each game.
- A guest's own leftover vehicles (spawned before its crowd went off) are
  only counted in the log; nothing removes them (no known call does).

## Scenarios to run in game

`tools/scenario.sh traffic-guest traffic-host` (with `police-guest
police-host`, see `police.md`).

- **traffic-guest**: the game joins a mock host that drives four cars
  round V and parks one, with a driver in the first; one burns and blows
  up. Checks the proxies' count, the seated driver, one explosion, the
  wreck, and that everything goes with the host. Look for: cars sliding
  or hovering, wheels not turning, the driver's pose, the explosion's
  sound and fire, the wreck staying.
- **traffic-host**: the game hosts on a street with traffic; the mock
  guest rams a car every 4 s, then gets into one. Checks that our cars
  are announced, the ram applied to the real car, the taken car handed to
  the guest's puppet with no second copy. Look for: the rammed driver's
  reaction, the evicted driver beside the door, the puppet at the wheel
  of the same car.
