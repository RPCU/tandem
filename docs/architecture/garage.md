# Garages: owned vehicles in co-op

Each player's garage is their own; a vehicle is simulated by the game that
moves it; what a quest gives while two players play it together is earned
by both, once. Asked for by the user on 2026-11-19 ("think about how to
handle car ownership and rewards too"); researched and partly built the
same day with the game machine out of reach: **nothing here has run in
game**. Engine facts: [`research/engine/progression.md`](../research/engine/progression.md)
§13.30.9. Related: [`vehicles.md`](vehicles.md) (driven cars and their
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

| #   | Case                                                           | Rule                                                                                                                                                                                                                                                                                                                                 | Built                                                                                                                                                                                                                                                                                |
| --- | -------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1   | A guest's own garage                                           | its own, summoned by the game's own call in the host's world; the game that moves a vehicle owns it                                                                                                                                                                                                                                  | nothing in the way (tandem never touches the garage on summon); `garage` command logs the summoned car and whether summoning is allowed; scenario `garage-guest`                                                                                                                     |
| 1a  | The summoned car seen by the others                            | today only once its owner sits in it (the copy follows `Seat`); a car driving in by itself or parked is not seen                                                                                                                                                                                                                     | **open**: share a summoned car as a parked vehicle from its arrival                                                                                                                                                                                                                  |
| 1b  | Its owner leaves or vanishes                                   | the copy stays parked where it was (copies are never deleted: deleting one froze the game); never saved                                                                                                                                                                                                                              | was                                                                                                                                                                                                                                                                                  |
| 2   | Driving a partner's car                                        | by its **owner's word**, `tandem.ini` `lend`: `crew` (the default: any partner in the session, the owner aboard or not), `owner` (only with the owner aboard), `never`. The game at the wheel drives it and says where it is; every other game moves its copy there, and the owner's game its real car; the garage stays the owner's | **built, not run in game**: `CarLend` (each game's word, kept by the host for late joiners), `sync::MayDrive`; `Vehicles`/`System.reds` `CheckWheel` (refused: out of the seat, with a notice), `FollowLentCars`, parked copies remember their owner; scenario `garage-lend`         |
| 2a  | A partner's car taken while they are away, damaged, destroyed  | under `crew`: the driver's game simulates it; the owner's game moves its real car to the driver's state when it has it loaded (a car streamed out there: the driver's puppet is hidden until it is); damage stays the driver's game's; destroyed: the owner's next summon repairs it (vanilla)                                       | follows from 2                                                                                                                                                                                                                                                                       |
| 2b  | A partner's car left in the world when its owner leaves        | stays parked, as 1b; it is no one's: nobody can drive it off (`MayDrive` needs the owner in the session)                                                                                                                                                                                                                             | built (a gone player's parked copies lose their owner)                                                                                                                                                                                                                               |
| 3   | A vehicle a quest or gig gives while both take part            | the guest has earned it too, once: **credited in its ledger** (its own save); **unlocked in its own garage only with `vehicle_rewards = keep`** (decision 1)                                                                                                                                                                         | the host's game watches its own garage (once a second) and tells the guests that took part (`VehicleUnlock`, the reward rule: within 150 m of the host as the quest moved); the guest credits it once and, with `keep`, unlocks it (`VehicleSystem.EnablePlayerVehicleID`, Codeware) |
| 3a  | The guest's own story later gives the same vehicle             | nothing duplicated: a garage holds a vehicle once                                                                                                                                                                                                                                                                                    | by construction                                                                                                                                                                                                                                                                      |
| 3b  | A vehicle a quest borrows and then gives (a mission car)       | the same as 3: credited when the host's garage gets it, not before                                                                                                                                                                                                                                                                   | —                                                                                                                                                                                                                                                                                    |
| 3c  | Missable or choice-dependent vehicles                          | the host's choice gives it to the guest too (they earned it together)                                                                                                                                                                                                                                                                | decision 2                                                                                                                                                                                                                                                                           |
| 4   | Free finds (a car found in a cave, a fixer's gift after a set) | the same as 3: they are quest nodes too, so "took part" decides                                                                                                                                                                                                                                                                      | as 3                                                                                                                                                                                                                                                                                 |
| 5   | Purchases (autofixer)                                          | each player's own, with their own money; nothing shared                                                                                                                                                                                                                                                                              | the host's purchase is never sent (the purchase is noted and what arrives in the next 30 s is "bought"); a guest buys on its own phone, which needs no NPC                                                                                                                           |
| 6   | Phantom Liberty vehicle contracts and car deliveries           | gigs: they belong to the world of the player who starts them (`quest-carryover.md`, "Whose gig is it"); the helper is paid by the reward rule; a reward car is case 3                                                                                                                                                                | money rewards: was (`QuestReward`); the discount is a fact of the owner's world (not carried)                                                                                                                                                                                        |
| 7   | Persistence                                                    | credited and kept vehicles are ledger entries in the guest's save (`m_ledger`, `Story.reds`); a kept vehicle is in its garage, saved by the game; both are written by the first save after the session (`progression.md`) and lost together by a crash                                                                               | built                                                                                                                                                                                                                                                                                |
| 8a  | Both players summon the same model                             | two cars, two copies: fine                                                                                                                                                                                                                                                                                                           | was                                                                                                                                                                                                                                                                                  |
| 8b  | The host's quest vehicle                                       | never in a guest's garage (it is not the host's garage either until the quest unlocks it)                                                                                                                                                                                                                                            | `transit.md`                                                                                                                                                                                                                                                                         |
| 8c  | Delamain                                                       | a ride, never an unlock; the No. 21 cab is case 3                                                                                                                                                                                                                                                                                    | —                                                                                                                                                                                                                                                                                    |
| 8d  | Three players                                                  | each guest has its own "took part" and its own ledger                                                                                                                                                                                                                                                                                | by construction                                                                                                                                                                                                                                                                      |

## Why "credited" by default

Unlocking a vehicle in a guest's garage is world state of a story quest:

- a later quest of the guest's own story may test that vehicle
  (`questVehicleAvailable_ConditionType`) and take the wrong branch;
- the autofixer would still sell it (its `OwnershipFact` is not set);
- the quest that gives it in the guest's own story still runs: harmless
  (a garage holds a vehicle once), but its "new vehicle" moment is gone.

None of these breaks a save; the first changes a story. So the safe default
records the vehicle as earned, and the user decides whether co-op may put
it in the garage. With `keep` the vehicle is unlocked the game's own way
and the ledger says so (`kept`), so a later tool can take it back out.

## Decisions for the user

1. **Vehicle rewards: credit or keep?** Recommended: **keep** once one
   unlock has been seen in game (a quest car arriving in a guest's garage,
   summoned, saved, loaded); credit until then (built default).
2. **The host's choice gives the guest a car its own choices would not?**
   Recommended: yes, they earned it together.
3. **Driving a partner's car**: built as proposed, `lend = crew` by
   default. Recommended: keep `crew` as the default once `garage-lend` has
   run; `owner` for players who want their car only with them aboard.
4. **A partner's car taken when they are away**: allowed under `crew`
   (built). Recommended: yes; the owner's garage is untouched and its next
   summon brings the car back.
5. **Set the autofixer's ownership fact when a car is kept**, so it is no
   longer for sale? Recommended: no until the offers' facts are mapped to
   vehicle records in game (they are TweakDB data).

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
- Scenarios: `garage-guest` (summon as a guest; a vehicle the mock host's
  garage "gets" with `--unlock-vehicle`, credited once in our ledger and
  not unlocked with the default `credit`), `garage-lend` (the mock host
  says `never`: the `garage` command logs it). The real unlock path needs a
  quest that gives a car in a session, and driving a partner's car needs
  two games: both by hand (decision 1, rule 2).
