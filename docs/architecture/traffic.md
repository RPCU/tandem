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
  linked too. A guest linked with nobody sends no states, only its reliable
  record (the host has it whole the moment a link forms). Requests (`TrafficHit`,
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

| Message                | Channel         | From → to                              | What                                                                       |
| ---------------------- | --------------- | -------------------------------------- | -------------------------------------------------------------------------- |
| `TrafficSpawn`         | Event           | owner → all (kept)                     | id, record, appearance, static entity id or 0, transform, flags            |
| `TrafficStates`        | State, 10 Hz    | owner → all                            | the vehicles that move: id, position, rotation, velocity (28 bytes)        |
| `TrafficStatus`        | Event           | owner → all (kept)                     | health 0..1 and the lasting flags (wreck, siren, parked, panic…)           |
| `TrafficGone`          | Event           | owner → all                            | out of range, removed, or **taken** by a player                            |
| `TrafficHit`           | Event, one-shot | a player's game → owner                | damage and, for a ram, the push given                                      |
| `TrafficClaim`         | Event, one-shot | a player's game → owner                | "my V is getting into this one"                                            |
| `TrafficSet`           | Event           | host → one guest; a guest owner → host | opens a resync: every vehicle shown to it now; from a guest, its whole set |
| `NpcSeat`              | (existing)      | owner → all (kept)                     | `owner` with the top bit set names a shared vehicle, not a player          |
| `TrafficOwnership`     | Event           | host → all who see it (kept)           | who runs a vehicle now (absent: its announcer)                             |
| `TrafficHandover`      | Event, one-shot | host → owner                           | give it to `to`                                                            |
| `TrafficHandoverState` | Event, one-shot | giver → host → receiver                | place, orientation, velocity, health, flags (inherited: the host's record) |
| `TrafficHandoverDone`  | Event, one-shot | receiver or giver → host               | runs it now (or cannot: it goes); a giver's refusal                        |

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

| How it diverges                                           | What the player would see                            | Guard                                                                                                                                                      |
| --------------------------------------------------------- | ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| a late joiner                                             | an empty street                                      | spawns (at the latest transform), statuses and seats are kept and sent with the NPC domain                                                                 |
| a spawn, a removal or a status lost (a bug, a full queue) | a missing car, a ghost, a car that should be a wreck | the shared set and each status are hashed into the NPC digest; a resync opens with `TrafficSet`                                                            |
| sent twice (a resync, a duplicate)                        | two cars in one place                                | a known id is not announced again; statuses equal to the copy are dropped; hits and claims are one-shots                                                   |
| a state overtakes its `TrafficGone` (other channel)       | a car back from nowhere                              | states only for known ids                                                                                                                                  |
| the owner's world is rebuilt (fast travel)                | cars drawn where the old street was                  | `TrafficStates` share the NPC states' numbering: `Epoch::firstNpcSeq` fences both                                                                          |
| a hit or a claim aimed at the owner's old world           | a wrong car damaged or taken                         | behind the epoch fence, like `NpcHit`                                                                                                                      |
| hits queued behind a loading screen                       | a car wrecked the moment the world is back           | dropped after 3 s                                                                                                                                          |
| a proxy's body streamed out or lost in our fast travel    | an invisible car                                     | the script's check against the session's record, once a second: spawned again after 3 s                                                                    |
| the owner vanishes                                        | frozen cars                                          | the session ends on the 5 s timeout; proxies go with it, our crowd and traffic come back                                                                   |
| a guest vanishes in a car it took                         | —                                                    | it was its car: it stays parked with its puppet's car copy, as any car of a player that left                                                               |
| a guest vanishes mid-claim                                | a car nobody drives                                  | the claim was granted or not; if granted the body is its (parked) car, if not nothing changed                                                              |
| a proxy that had an occupant must go                      | a crash (engine-knowledge §13.6)                     | never deleted: kept out of range and reused for the next vehicle of its record                                                                             |
| a guest owner's spawn or removal lost                     | a car missing for its linked players, or a ghost     | its vehicles are hashed into the `OwnedDigest` with its NPCs; twice different, it sends its whole set (`TrafficSet` from the guest, its spawns, statuses)  |
| a hand-over step lost                                     | a car nobody runs, or run twice                      | one owner at every moment (the host moves it when the giver's state arrives); each step retried 3 times; a repeated state is answered with the same `Done` |
| the receiver cannot run it, or leaves mid hand-over       | a frozen car                                         | it goes for everyone (`TrafficGone` away); a receiver that vanished is an owner that vanished                                                              |
| a request aimed at the old owner                          | a hit or a claim lost                                | routed by the session's owner map at the host: the new owner gets it                                                                                       |
| a guest's word about a vehicle it does not run            | a car moved or wrecked by two games                  | the host drops it (and the referee flags it: `authority.not-owner`)                                                                                        |

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

`tools/scenario.sh traffic-guest traffic-host wheels-guest-area` (with
`police-guest police-host`, see `police.md`).

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
- **wheels-guest-area**: the game hosts; a mock guest 400 m away owns its
  area and runs three cars of its own (the first driven by its NPC, chasing
  us once we meet). It walks up (its cars appear as proxies, its chase
  reaches us), then off, leaving them by our V: handed over to our game.
  Checks the proxies, the chase, the hand-over in and the driver inherited
  in its seat, nothing refused. Look for: no car popping or doubling when
  it changes hands, the driven one driving on in our traffic (or stopping:
  note which), the driver still at the wheel, the giver's car on the mock's
  side (a proxy now) not fighting its new owner.
