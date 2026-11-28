# Garages: owned vehicles in co-op

Each player's garage is their own; a vehicle is simulated by the game that
moves it; what a quest gives while two players play it together is earned
by both, once. Asked for by the user on 2026-11-19 ("think about how to
handle car ownership and rewards too"); researched and partly built the
same day with the game machine out of reach: **nothing here has run in
game**. Engine facts: [`research/engine/progression.md`](../research/engine/progression.md)
§13.38.9. Related: [`vehicles.md`](vehicles.md) (driven cars and their
copies), [`transit.md`](transit.md) (rides and quest vehicles),
[`quest-carryover.md`](quest-carryover.md) (the ledger),
[`progression.md`](progression.md) (saves).

## What the game does (read in its scripts)

- The garage is native: `VehicleSystem` (`GetPlayerUnlockedVehicles`,
  `IsVehiclePlayerUnlocked`, `SpawnPlayerVehicle`, `EnablePlayerVehicle`),
  saved per save in `vehicleGarageComponentPS` (the unlocked list, each
  vehicle's health, the favourites). The list of garage vehicles is
  TweakDB `Vehicle.vehicle_list.list`.
- **No script unlocks a vehicle, and no reward record carries one.** Quest,
  gig, fixer and gift cars are unlocked by a quest node,
  `questEnablePlayerVehicle_NodeType`. The autofixer takes the money and
  sets the offer's `OwnershipFact`; a quest listening to that fact runs the
  same node.
- **Quests can test the garage**: `questVehicleAvailable_ConditionType`
  (any car, any bike, a specific vehicle). The autofixer tests only its
  facts: a car unlocked without its `OwnershipFact` is still for sale.
- Summoning is a native call gated by facts, quest locks and the garage
  state (`VehicleSystem.IsSummoningVehiclesRestricted`); a destroyed
  vehicle waits a repair cooldown, then comes back whole.
- Delamain's taxi uses the garage machinery for a temporary car and
  unregisters it: never an unlock.

## The rules

| #   | Case                                                           | Rule                                                                                                                                                                                                                                                                                                                                                                                                                                 | Built                                                                                                                                                                                                                                                                                |
| --- | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1   | A guest's own garage                                           | its own, summoned by the game's own call in the host's world; the game that moves a vehicle owns it                                                                                                                                                                                                                                                                                                                                  | nothing in the way (tandem never touches the garage on summon); `garage` command logs the summoned car and whether summoning is allowed; scenario `garage-guest`                                                                                                                     |
| 1a  | The summoned car seen by the others                            | seen from its arrival: driving in on its own, then parked, as its owner's car copy; the owner's game owns it (its moves are that player's `VehicleState` stream while its V is on foot); the same copy is the one the owner's `Seat` later sits in; summoned again, the same copy moves; another car of theirs (or none out): a copy of that one, and the old copy goes as the garage takes its own, out of sight (research §13.168) | **built, not run in game** ("Built, tested, to check in game", below): `OwnCar` (appended, protocol 27), `Summons.reds`; scenarios `garage-summon`, `garage-summon-host`, `garage-summon-other`                                                                                      |
| 1b  | Its owner leaves or vanishes                                   | the copy stays parked where it was (copies are never deleted: deleting one froze the game); never saved                                                                                                                                                                                                                                                                                                                              | was                                                                                                                                                                                                                                                                                  |
| 2   | Driving a partner's car                                        | by its **owner's word**, `tandem.ini` `lend`: `crew` (the default: any partner in the session, the owner aboard or not), `owner` (only with the owner aboard), `never`. The game at the wheel drives it and says where it is; every other game moves its copy there, and the owner's game its real car; the garage stays the owner's                                                                                                 | **built, not run in game**: `CarLend` (each game's word, kept by the host for late joiners), `sync::MayDrive`; `Vehicles`/`System.reds` `CheckWheel` (refused: out of the seat, with a notice), `FollowLentCars`, parked copies remember their owner; scenario `garage-lend`         |
| 2a  | A partner's car taken while they are away, damaged, destroyed  | under `crew`: the driver's game simulates it; the owner's game moves its real car to the driver's state when it has it loaded (a car streamed out there: the driver's puppet is hidden until it is); damage stays the driver's game's; destroyed: the owner's next summon repairs it (vanilla)                                                                                                                                       | follows from 2                                                                                                                                                                                                                                                                       |
| 2b  | A partner's car left in the world when its owner leaves        | stays parked, as 1b; it is no one's: nobody can drive it off (`MayDrive` needs the owner in the session)                                                                                                                                                                                                                                                                                                                             | built (a gone player's parked copies lose their owner)                                                                                                                                                                                                                               |
| 3   | A vehicle a quest or gig gives while both take part            | the guest has earned it too, once: **credited in its ledger** (its own save); **unlocked in its own garage only with `vehicle_rewards = keep`** (decision 1)                                                                                                                                                                                                                                                                         | the host's game watches its own garage (once a second) and tells the guests that took part (`VehicleUnlock`, the reward rule: within 150 m of the host as the quest moved); the guest credits it once and, with `keep`, unlocks it (`VehicleSystem.EnablePlayerVehicleID`, Codeware) |
| 3a  | The guest's own story later gives the same vehicle             | nothing duplicated: a garage holds a vehicle once                                                                                                                                                                                                                                                                                                                                                                                    | by construction                                                                                                                                                                                                                                                                      |
| 3b  | A vehicle a quest borrows and then gives (a mission car)       | the same as 3: credited when the host's garage gets it, not before                                                                                                                                                                                                                                                                                                                                                                   | —                                                                                                                                                                                                                                                                                    |
| 3c  | Missable or choice-dependent vehicles                          | the host's choice gives it to the guest too (they earned it together)                                                                                                                                                                                                                                                                                                                                                                | decision 2                                                                                                                                                                                                                                                                           |
| 4   | Free finds (a car found in a cave, a fixer's gift after a set) | the same as 3: they are quest nodes too, so "took part" decides                                                                                                                                                                                                                                                                                                                                                                      | as 3                                                                                                                                                                                                                                                                                 |
| 5   | Purchases (autofixer)                                          | each player's own, with their own money; nothing shared                                                                                                                                                                                                                                                                                                                                                                              | the host's purchase is never sent (the purchase is noted and what arrives in the next 30 s is "bought"); a guest buys on its own phone, which needs no NPC                                                                                                                           |
| 6   | Phantom Liberty vehicle contracts and car deliveries           | gigs: they belong to the world of the player who starts them (`quest-carryover.md`, "Whose gig is it"); the helper is paid by the reward rule; a reward car is case 3                                                                                                                                                                                                                                                                | money rewards: was (`QuestReward`); the discount is a fact of the owner's world (not carried)                                                                                                                                                                                        |
| 7   | Persistence                                                    | credited and kept vehicles are ledger entries in the guest's save (`m_ledger`, `Story.reds`); a kept vehicle is in its garage, saved by the game; both are written by the first save after the session (`progression.md`) and lost together by a crash                                                                                                                                                                               | built                                                                                                                                                                                                                                                                                |
| 8a  | Both players summon the same model                             | two cars, two copies: fine                                                                                                                                                                                                                                                                                                                                                                                                           | was                                                                                                                                                                                                                                                                                  |
| 8b  | The host's quest vehicle                                       | never in a guest's garage (it is not the host's garage either until the quest unlocks it)                                                                                                                                                                                                                                                                                                                                            | `transit.md`                                                                                                                                                                                                                                                                         |
| 8c  | Delamain                                                       | a ride, never an unlock; the No. 21 cab is case 3                                                                                                                                                                                                                                                                                                                                                                                    | —                                                                                                                                                                                                                                                                                    |
| 8d  | Three players                                                  | each guest has its own "took part" and its own ledger                                                                                                                                                                                                                                                                                                                                                                                | by construction                                                                                                                                                                                                                                                                      |

## Why "credited" by default

Unlocking a vehicle in a guest's garage is world state of a story quest:

- a later quest of the guest's own story may test that vehicle
  (`questVehicleAvailable_ConditionType`) and take the wrong branch;
- the autofixer would still sell it (its `OwnershipFact` is not set);
- the quest that gives it in the guest's own story still runs: harmless
  (a garage holds a vehicle once), but its "new vehicle" moment is gone.

None of these breaks a save; the first changes a story. So the safe default
records the vehicle as earned (`credit`, decided: Decisions). With `keep`
the vehicle is unlocked the game's own way and the ledger says so (`kept`), so a later tool can take it back out.

## Decisions

Decided by the user 2026-11-19 (the recommended defaults):

1. **Vehicle rewards: credit.** A host-quest car is credited to a guest,
   not unlocked (`vehicle_rewards = credit`, the default); switch the
   default to `keep` once one unlock has been seen in game (a quest car
   arriving in a guest's garage, summoned, saved, loaded).
2. **The host's choice gives the guest a car its own choices would not:**
   yes, they earned it together.
3. **Driving a partner's car: `lend = crew`** (anyone in the session) by
   default; `owner` for players who want their car only with them aboard.
4. **A partner's car taken when they are away:** allowed under `crew`; the
   owner's garage is untouched and its next summon brings the car back.
5. **The autofixer's ownership fact when a car is kept:** not set until the
   offers' facts are mapped to vehicle records in game (they are TweakDB
   data).

## Built, tested, to check in game

- Core: `VehicleUnlock` (appended, a story message to one guest, a
  one-shot); the ledger's vehicles (`carry::Ledger::NoteVehicle`,
  `KeepVehicle`, packed with the rest). Tests: `tests/carryover_test.cpp`
  (once, kept, through the save, merged), `tests/progress_test.cpp` (to
  the guest named only, never from a guest, never to a late joiner).
- Scripts: `scripts/tandem/Garage.reds` (the host's watch, the guest's
  credit, the purchase wrap, `tools/game.sh cmd garage [summon]`).
- Driving a partner's car: `proto::CarLend` (appended; per-player state
  on the combat path with the levels: kept by every endpoint, the host's
  copy for late joiners and resyncs, a repeat applied nowhere, forgotten
  when the player leaves or vanishes; not a one-shot), `sync::MayDrive`
  (`core/include/tandem/vehicle.hpp`). Tests: `tests/progress_test.cpp`
  (the rule; each player's word reaching everyone once, a late joiner, a
  vanish, a word in another's name). Mock player: `--lend crew|owner|never`.
- The summoned car (row 1a; `scripts/tandem/Summons.reds`, protocol 27):
  - **Ours**: the car on `VehicleSummonData.SummonedVehicleEntityID` is
    said as `OwnCar` (its record and hint, where it stands; per-player
    state through `SendCombat`, as `CarPaint`: the host stamps who, keeps
    everyone's for late joiners and resyncs, a repeat is applied nowhere,
    no car out is kept nowhere, it goes with its player) when it is
    another car, when it starts to move with our V on foot
    (`kOwnCarMoving`: the game's `SummonState` EnRoute, or moving), when
    the game's summon ends (Arrived or AlreadySummoned: `kOwnCarArrived`,
    every copy honks and flashes once the game's way, `HonkAndFlash`, a
    bike put on its stand; research §13.165), and when it has stood still
    1 s elsewhere than said (where it stands).
    While it moves its transform goes as our `VehicleState` (30 Hz, the
    compact form and relevance rate of a driven car: one stream per player
    as before, so `tests/budget_test.cpp` is unchanged). Not while our V
    sits in a car (that car's stream), a car we bailed from rolls on
    (`Wheels.reds`), or a partner drives it (lent, rule 2: the driver's
    game says). It is also our own car a partner may drive while we are
    away from it (`TandemSystem.NoteOwnCar`).
  - **Theirs**: each partner's car out gets one copy: the copy of that car
    we have (the one its owner drove, `LastCarOf`), else one spawned where
    its owner says within 150 m of our V (`TandemSummons.Range`, as
    traffic). It is that player's parked copy (`TandemSystem.ParkCopy`):
    its owner's `Seat` takes it (`Reseat` takes a parked copy of that
    player's car of that record within 30 m before it spawns one, which
    also ended a second copy each time an owner got back into a car it had
    left), a partner drives it by the owner's word, the paint goes on it
    (`Paint.reds`). While the word says it moves, the copy follows that
    player's stream (`TandemVehicles.Follow`, the kinematic move; a jump
    over 8 m a teleport), once the stream is drawn within 25 m of where
    the car started or moves from where it stood when the word came
    (before that it may still be the player's last car's, at rest); when
    it stops it is put where its owner says for 1 s, then left to rest.
    No copy could be spawned: `summoned car unseen` (a fallback: it
    appears when its owner gets in).
  - **Old**: its owner's garage has another car out, or none: the copy goes
    the way the garage takes a summoned car it no longer needs (its sweep,
    research §13.168: beyond a distance of V, outside the camera's view,
    nobody in it), here by our V and our camera: beyond 60 m
    (`TandemSummons.GoneDistance`; the garage's own distance is not read
    yet), not in `CameraSystem.IsInCameraFrustum`, nobody seated in it for
    5 s (`FreeSeconds`: a copy freed 0.3 s after an unmount froze the game,
    research §13.101), the car of no one here (no puppet drives it, no
    leaving puppet holds it: `TandemSystem.RemoveOldCopy`), deleted by
    `TandemEntities.Delete`. Until then it stands where it was; that car
    summoned again takes it back (one body per garage car, §13.162).
  - Limits: an owner on foot whose car is still driving in when it gets
    into another car (a cab, a partner's car): the summoned car's stream
    stops (one stream per player: the car its V sits in), its copy settles
    where it was last drawn, and is put where the car stands once its
    owner is on foot again (the word of a car at rest elsewhere). Rare (a
    summon given up within its few seconds of driving in), and the
    drive-on needs a second stream per player (`tests/budget_test.cpp`):
    left. Each game removes an old copy by its own V's distance and view,
    not by the owner's: a copy can go here while the owner's game still
    has the car (and comes back, spawned, if the owner gets in again).
  - `tools/game.sh cmd garage cars`: what we say and show (`garage cars
face old|away`: our V turned toward the first old copy, or away); `test
query summons copies|at|off|lag|seated|arrivals|ours|old|removed|oldcopies|oldseen`:
    the game's own state of the first partner's summoned car (its copies of
    that record, its place, how far from its owner's word and from the
    stream's drawn place, whether its owner's puppet sits in it), of ours,
    and of the old copies (how many are here, how many went, the copies of
    the first one's record, whether our camera sees it). Mock player (drive
    mode): `--summon S` (driven in from 50 m, parked 4 m from it),
    `--summon-again S`, `--summon-board S`, `--summon-other S` (another
    car, `--other-vehicle R`, once it stands on foot by the first).
- Scenarios (the verdict from both games' state, not log lines alone):
  `garage-summon` (the game hosts, the mock player's car is summoned,
  driven in, summoned again, then boarded: one copy at every step, on the
  stream within 1 m while it drives, parked within 1 m of where the mock
  player's car stopped, its puppet seated in that copy),
  `garage-summon-other` (the mock player drives its summoned car, gets
  out 80 m away and summons another: the old copy stands while our V
  faces it, goes within 10 s once our V faces away, no copy of its record
  left, one of the new car's),
  `garage-summon-host` (the game summons its own car: the mock player
  hears it parked within 1 m of where our game has it), `garage-guest` (summon as a guest; a vehicle the mock host's
  garage "gets" with `--unlock-vehicle`, credited once in our ledger and
  not unlocked with the default `credit`), `garage-lend` (the mock host
  says `never`: the `garage` command logs it). The real unlock path needs a
  quest that gives a car in a session, and driving a partner's car needs
  two games: both by hand (decision 1, rule 2).
