# Progression, economy and saves in co-op

How a V's level, XP, money, items and saves behave in a session, who owns
each, what was read in the game's scripts and what tandem does about it.
Audited and built 2026-11-19, with the game machine out of reach: **nothing
in this file has run in game yet**. The engine facts and their sources are
in [`research/engine/progression.md`](../research/engine/progression.md)
(§13.30, cited by its parts below); trade between players has its own file,
[`trade.md`](trade.md).

The principle (the authority table): **a V is its player's own.** Level,
XP, perks, inventory and money live in that player's game and reach that
player's save; nothing of them is shared, copied or computed for someone
else. The world they are earned in is the host's.

## The table

"Verified" is what the game's scripts say (read offline); "built" is what
tandem does, with the scenario that checks it in game.

| Mechanic                                                           | Rule in co-op                                                                                            | Owner                                         | Verified from the scripts                                                                                                                                                                            | Built                                                                                                                                |
| ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- | --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Character level, attributes, perks, respec                         | each player's own, as in solo                                                                            | that player's game and save                   | `PlayerDevelopmentSystem` keeps one entry per attached **player**; a puppet is an NPC entity and gets none, earns nothing, answers no perk test (§13.30.3)                                           | nothing needed; nothing leaks (below, "Leaks")                                                                                       |
| Enemy level                                                        | scaled to the **mean of the players' levels** (decided 2026-11-18), by the game's own curve for each NPC | host                                          | a content assignment's curve maps a player level to the content's; the game's own debug scaling adds one additive `PowerLevel` modifier to NPCs that have one; `AddModifier` is not saved (§13.30.1) | `PlayerLevel` → `SessionLevel` → a modifier on each NPC the host shares (`Levels.reds`, `core/progress.hpp`); scenario `levels-host` |
| A proxy's level                                                    | the host's NPC's, exactly                                                                                | host says, guest applies                      | a hit is computed against the target's stats, which follow `PowerLevel`; skill XP is worth the target's level (§13.30.1, §13.30.2)                                                                   | `NpcLevel` per NPC, the same modifier on the proxy; scenario `levels-guest`                                                          |
| Damage between a V and an NPC                                      | each side by its own game's rules, on equal NPC stats                                                    | attacker's game, target's game                | `ProcessLevelDifference` has no caller in 2.50: the levels act only through the stats (§13.30.1)                                                                                                     | was (Combat.reds, Outcomes.reds); parity now holds for mixed levels                                                                  |
| Skill XP for damage                                                | each player for the share of health their own hits took                                                  | that player's game                            | queued per hit by the target's level, paid when the body is neutralized (§13.30.2)                                                                                                                   | was for guests; **done: the host's assist is paid too** (it stayed queued in the host's save)                                        |
| Kill XP, street cred, bounty                                       | the killer's; nobody else's                                                                              | the killer's game                             | `RewardKiller` pays only a `PlayerPuppet` killer: on the host a guest's kill pays nobody; on the guest `Kill(V)` on the proxy pays it (§13.30.2)                                                     | was; **done: a guest's own knock-out pays the defeat's reward** (it paid skill XP only)                                              |
| Assists                                                            | skill XP only (above); no level XP or street cred                                                        | each player's game                            | solo has no assist; the queue is per body (§13.30.2)                                                                                                                                                 | as above                                                                                                                             |
| XP markers (quickhack, utility grenade)                            | each player's own "paid once"                                                                            | that player's game                            | the marker is a status effect on the target (§13.30.2)                                                                                                                                               | **done: never forwarded to the host's NPC** (it took the host's own XP for that NPC away)                                            |
| Other XP (finishers, takedowns, movement, crafting, breach, parry) | local                                                                                                    | that player's game                            | all paid to the acting game's player (§13.30.2)                                                                                                                                                      | nothing needed                                                                                                                       |
| Quest, gig and NCPD rewards                                        | both players, once                                                                                       | each game pays its own V                      | `GiveReward` pays the local player by its own level (§13.30.2)                                                                                                                                       | was (`QuestReward`, the persistent ledger: Story.reds)                                                                               |
| Loot                                                               | rolled per player in their own game                                                                      | each player's game                            | picked-up items are scaled to the **picker** (§13.30.4); whether the NPC's level enters the native roll is not in the scripts                                                                        | was (Loot.reds). Open: measure a scaled NPC's drops in game                                                                          |
| Money                                                              | each player's own                                                                                        | that player's game                            | an item in V's inventory; bounties, shards and rewards pay the local player (§13.30.2, §13.30.5)                                                                                                     | nothing needed                                                                                                                       |
| Vendors: stock, prices, money                                      | each player's own; the NPC is the world's                                                                | each player's game (`MarketSystem`, its save) | a vendor is created per entity whose record has a vendor id; the buyer is always `GetPlayer` (§13.30.5)                                                                                              | **gap, not built**: see "Vendors" below                                                                                              |
| Ripperdocs                                                         | as vendors                                                                                               | each player's game                            | the same vendor, type ripperdoc; cyberware goes on the local V (§13.30.5)                                                                                                                            | the same gap                                                                                                                         |
| Vending machines, drop points                                      | each player's own device                                                                                 | each player's game                            | the game's own device, `GetPlayer` pays (§13.30.5)                                                                                                                                                   | nothing needed (gig hand-ins stay the host's: its quests)                                                                            |
| Crafting, upgrading, disassembling                                 | each player's own                                                                                        | that player's game                            | `CraftingSystem` holds one craft book, the local player's (§13.30.5)                                                                                                                                 | nothing needed                                                                                                                       |
| Stash                                                              | each player's own                                                                                        | that player's game                            | a device of the player's own world with its own inventory (§13.30.5)                                                                                                                                 | nothing needed; reachable only in an apartment the player's own save has                                                             |
| Owned vehicles                                                     | each player's own garage                                                                                 | that player's game                            | the native `VehicleSystem`, per save (§13.30.5)                                                                                                                                                      | was (a summoned car is its driver's; the others get a copy)                                                                          |
| Apartments                                                         | each player's own; the host's door is the host's                                                         | each player's game                            | a door's lock is persistent state of each game's own door (§13.30.5)                                                                                                                                 | **gap, not built**: a guest cannot follow the host into an apartment it has not bought (see "Doors")                                 |
| Fast travel points                                                 | each player's own map                                                                                    | that player's game                            | `FastTravelSystem`'s list is persistent per save, registered locally (§13.30.5)                                                                                                                      | was (a regroup never adds a point: Travel.reds)                                                                                      |
| Trade between players                                              | items and eddies, both agree, the host arbitrates                                                        | each player's game applies its own side       | [`trade.md`](trade.md)                                                                                                                                                                               | built, off by default (`trade = on`); scenario `trade-peer`                                                                          |
| Saves                                                              | below                                                                                                    |                                               | §13.30.6                                                                                                                                                                                             | below                                                                                                                                |

## Levels: how the mean is applied

1. Every game reads its V's `PowerLevel` once a second and tells the host
   when it changes, at each session's start and every half minute
   (`PlayerLevel`; `TandemLevels.Tick`). The level is never relayed: it is
   its player's business.
2. The host's endpoint keeps everyone's and works out the mean, rounded
   (`progress::MeanLevel`); when it changes (a join, a leave, a vanish, a
   level-up) it tells everyone (`SessionLevel{level, players}`, kept for
   late joiners). A player that has not said its level yet is left out.
3. The host's game, for each NPC it shares (`TandemLevels.Host`, once per
   NPC and again whenever the session's level or the host's own changes):
   - reads the NPC's content assignment. A **curve**: the wanted change is
     `curve(session) - curve(host)`, the game's own answer to "what level
     is this content for a player of that level" (district floors and caps
     included). A **constant**, or **no assignment**: the NPC keeps its
     level (the game does not scale it to the player either);
   - adds one additive `PowerLevel` modifier for it (`progress::NpcShift`:
     never outside 1..the stat's maximum, never twice), keeps the NPC's
     health at the same share of its new maximum, and tells the guests its
     level (`NpcLevel`, kept for late joiners until the NPC goes);
   - takes the modifier off when the NPC is no longer shared and when the
     session ends. It is a plain `AddModifier`: never saved.
4. A guest gives each proxy the host's level (`progress::ProxyShift`: one
   modifier for the difference from what its own game gave the body),
   before the host's health is applied to it.

Alone, or with everyone at the host's level, nothing is touched. Guests are
told once ("Co-op: enemies are scaled to level N").

What this does not do, on purpose: change anyone's own level, XP curve or
gear; scale a quest's XP or a vendor's stock (each game's own); move an NPC
of a fixed level (bosses the game itself does not scale).

To check in game: that health, armour and damage follow the modifier
(§13.30.1 [I]); how many NPCs have a curve (`tools/game.sh cmd progress` counts
curve / constant / none); what a scaled NPC drops.

## XP rules, as they stand

- **A hit**: the hitter's game queues skill XP for the share of health it
  took, worth the NPC's level (the same on every game now).
- **A kill or a knock-out**: the killer's game pays the kill's level XP,
  street cred and bounty by its own rules, and its queued skill XP. Every
  other player who hit that body is paid their queued skill XP (the assist)
  and nothing else. The host is "every other player" too when a guest
  lands the kill.
- **Street cred** comes with kills and bounties (the killer's), and with
  quest and gig rewards (both players, by the reward rule).
- All five skills go through the same damage queue (the game picks the
  skill from the weapon and the hit); the other sources are local actions.

## Leaks: what was checked

- A puppet (another V's body here) is an `NPCPuppet` of a tandem record,
  spawned with `persistState` and `persistSpawn` off: no development data,
  no XP, no perks of ours on it, and its items (its look, its held weapons)
  are in its own unsaved inventory.
- Perk tests on a puppet answer nothing; no path applies our perks to it
  (`research/coop-perks.md` §1.1).
- The one cross-player leak found: the XP markers (above), fixed.
- The host's own queued skill XP for NPCs a guest finished stayed in its
  **save** (the queue is persistent): fixed (paid as an assist).

## Vendors (a gap)

Each player's own stock and money, with the vendor NPC shared in the world:
that is how the game already keeps them (per game, per entity). What is
missing is the way in:

- on a guest, its own placed vendor within 60 m of its V is hidden with
  every other NPC of its own (`TandemNpcs.HideOwn`), and the host's vendor
  is a proxy: another entity, with the brain off;
- no script opens an NPC's shop (§13.30.5): the "trade" choice is a quest or
  scene node of the guest's own, now hidden, vendor.

So **a guest near the host cannot shop today**; away from the host (its own
NPCs are not hidden beyond the host's 60 m) it can. Buying itself would
work, wholly locally. The smallest fix, not built because it needs the game
to choose between two unknowns:

1. keep a guest's own vendor NPCs (`ScriptedPuppet.IsVendor()`) out of
   `HideOwn` and show no proxy for the host's vendor of the same record at
   the same place: the guest's own scene opens its own shop, with its saved
   stock; or
2. an interaction of ours on a vendor proxy that opens the vendor menu
   (`UISystem.RequestVendorMenu`, as the drop point does) on the guest's
   own hidden vendor's entity when there is one. Opening it on the proxy
   itself would wipe and restock the proxy's inventory
   (`FillVendorInventory`) and leave a dead vendor entry in the guest's
   save at every respawn.

Recommended: 1 (no new UI, the saved stock is kept, nothing lands in the
save). A vendor the host killed would still stand in the guest's game:
accepted, or hidden when the host says it died.

## Doors (a gap)

A door's lock is each game's own persistent state. A guest's door to the
host's apartment is locked unless the guest's save owns that apartment, and
tandem syncs no doors or devices. To follow the host inside, the host's
door actions would be mirrored on the guest's same door (by persistent id:
`QuestForceUnlock` / the open state) and put back on leaving
(`research/coop-quests-world.md` §7.2, `coop-quests-design.md` §2.7). It is
the device sync of the open-problems list, not a progression matter; not
built here.

## Saves

### The rules

- **The world is the host's save.** Quests, the dead, the city, the clock:
  the host's game records them, in the host's saves, as in solo. The host
  saves and autosaves freely with guests present.
- **A guest's save keeps its own story and its V.** In a session a guest
  never saves. What its V earned there (XP, street cred, money, items,
  traded items, the reward ledger) lives in the running game and reaches
  its save with the first save made after the session.
- **Nothing of co-op is ever saved**, on either side (the audit below).
- **The host's quest progress never enters a guest's story.** The reward
  ledger is the one thing kept on purpose: a quest paid in co-op pays
  nothing again in the guest's own story.

### Leaving: from the session back to a guest's own saves

`scripts/tandem/Saves.reds`, driven by `progress::Leave`
(`core/progress.hpp`, tested in `tests/progress_test.cpp`):

1. **Session**: a guest in a session. Saving is locked (three locks, one
   would do: `TandemSystem.IsSavingLocked`, the story's, the
   `SaveLocksManager` reason; queued autosaves are dropped).
2. **Cleaning**: the session is over (we left, or the host is gone). Every
   part of tandem puts our own world back, and `TandemSystem.LeftOver`
   says what is still there:

   | Left       | What                                                                                 | Put back by                                                                   |
   | ---------- | ------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------- |
   | story      | the host's journal entries, our frozen story, the story's lock                       | `TandemStorySystem.GuestLeave` (journal to its baseline, checked; freeze off) |
   | holds      | combat lock, god mode, story effects held for the host's scenes                      | `ReleaseHolds`, `TakeOffEffects`                                              |
   | difficulty | the host's difficulty                                                                | `TandemOutcomes.End`                                                          |
   | bodies     | puppets, proxies, bodies on their way out                                            | `RemoveAllPuppets`, `TandemNpcs.End`, `ProcessLeaving`                        |
   | levels     | level modifiers on our own NPCs (host)                                               | `TandemLevels.Release`                                                        |
   | world      | the host's weather (our weather cycle is off while it is on), our crowd switched off | `TandemWorld.End` (**new**: the cycle stayed off for good), `TandemNpcs.Stop` |
   | down       | our V down, out or dying                                                             | Revive.reds                                                                   |

   While anything is left, **no save**. Nothing left for 2 s: clean. Still
   something after 20 s: the lock stays for good, the player is told to
   load their last save, and only a load (or another session's clean end)
   lifts it. A save must never hold co-op state.

3. **Clean**: saving unlocks. `tandem.ini` `leave` decides what follows:
   - `stay` (the default): nothing more. The guest goes on in place, in its
     own world; the game's next autosave, or the player's save, keeps what
     the V earned.
   - `save`: one quick save, the game's own (`QuickSave()`, the one F5
     makes, with its "game saved" notice), as soon as the game's own save
     locks allow it (not in combat, not in a scene; given up after two
     minutes, with a notice).
   - `reload`: that save, then `LoadLastCheckpoint`: the world is rebuilt
     from the guest's own save. **Only with the game's word that the save
     was written** (`OnSavingComplete(success)`; no word in 30 s counts as
     a failure and nothing is loaded), and only when the player chose to
     leave: a host that is lost may be back in seconds, so that is a `save`.

Our own clock: the host's clock moves a guest's while it follows it. When
the player leaves by choice the guest's clock goes back to what it would
say had it played alone (the game time minus what the host's moved it);
when the host is lost it stays the host's until the session is back or the
player leaves (a clock that jumps twice is worse than one that is ahead).

Parked car copies stay (deleting one froze the game) and are never saved.
A guest's own NPCs that were hidden near the host stay hidden until they
stream out: `stay` and `save` live with that; `reload` does not.

### Recommendation

`reload` is the clean answer (one loading screen on leaving a session, as
any co-op game has, and the guest's world is exactly its save plus its V);
`save` is the safe middle (the V's gains are on disk within seconds of
leaving). Both **write the player's saves**, so they are off until someone
has looked at them in game with a save they can lose: the default is
`stay`, which writes nothing and is what the scenarios run. Suggested
order: check `saves-leave` (stay), then `leave = save` by hand, then
`reload`, then make `reload` the default.

### Should a guest be offered the host's quest progress?

No, not into its story. Reasons: a quest's state is thousands of facts and
phase positions the guest's own graph did not produce (the freeze exists
because writing them corrupts a save); the guest's own choices in that
quest would be overwritten by the host's; and rejoining the same host later
needs none of it (the host's save has it). What is kept instead is what the
player can see they earned: the rewards (once, by the ledger) and
everything their V picked up.

What could be offered later, as a choice at the end of a session and built
on the node-level replay of milestone 6: "mark the quests we finished
together as done in my game" for quests the guest had **not started**,
by running their own completion in the guest's graph. Not before the
story work makes that safe.

### Autosaves on the host

The host's autosaves and saves go on as in solo. They hold no co-op state
(the audit below); the level modifiers are unsaved by construction. A
host's save freezes its game for a moment: guests extrapolate its V and
NPCs through it like any hitch.

### Crashes

| Who crashes                                            | What is left                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A guest, mid-session                                   | Its newest save is from before the session (it never saved in one): its story is intact; **what its V earned in that session is lost**, and the reward ledger entries with it (paid once or never). The host's world is untouched; the guest's revive, hold, pause, trade and seat end within 5 s.                                                                                                                                                       |
| A guest, after leaving with `stay` and before any save | The same loss: `leave = save` closes this window.                                                                                                                                                                                                                                                                                                                                                                                                        |
| The host                                               | Its last autosave: the world goes back to it for everyone. The guests' session ends; their own worlds come back (the leave steps), their Vs keep what they earned since joining, **including rewards of quest steps the host's save no longer has**. When the host replays those steps, the guests' ledger pays nothing twice. Kill XP and loot from the lost stretch stay with whoever got them: a few minutes of gains, the price of no shared ledger. |
| Either, during a trade                                 | Before the commit: nothing changed hands. After it: each game that heard the commit applied it once ([`trade.md`](trade.md)).                                                                                                                                                                                                                                                                                                                            |

A guest cannot save its V mid-session today. A checkpoint would have to
put the journal back, be sure no quest signal is held by the freeze
(a save written with one held loses it on load), switch difficulty and
weather back, save, and take it all again, under a fade: possible, and the
right next step once the leave save is proven in game, since it bounds a
crash's loss to minutes.

**Quitting the game without leaving the session** (the pause menu's Exit,
Alt-F4) is a crash as far as the guest's V is concerned: the world goes
away before anything can be saved. Until the pause menu's Exit runs the
leave steps first (not built), a guest that wants to keep its gains leaves
the session from the Co-op menu, then quits.

### Saved state: the audit

Nothing co-op-only may reach a save. What was checked, on a guest (which
only saves after the steps above) and on the host (which saves any time):

| State                                                                                            | Saved?          | Why not, or what handles it                                                                                                    |
| ------------------------------------------------------------------------------------------------ | --------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Puppets, proxies, car copies, their items                                                        | no              | dynamic entities with `persistState` and `persistSpawn` off; a puppet's look and weapons are in its own inventory              |
| NPC level modifiers                                                                              | no              | `AddModifier` (the saved kind is `AddSavedModifier`); taken off at the session's end                                           |
| Attitudes of puppets and proxies                                                                 | no              | set on unsaved entities; group relations are never written (Attitudes.reds)                                                    |
| The host's difficulty on a guest                                                                 | no              | given back at the session's end; a save is locked while it is on                                                               |
| The host's journal on a guest, its frozen story                                                  | no              | restored and checked before saving unlocks; a mismatch keeps the lock                                                          |
| Restrictions and holds on our V (scene holds, god mode, ride-along, story effects, revive floor) | no              | released at the session's end; "holds" keeps the lock until they are; a load clears what a save could have kept (Transit.reds) |
| The host's weather on a guest                                                                    | no              | **fixed here**: the weather cycle is given back at the session's end, and the lock holds until it is                           |
| The host's clock on a guest                                                                      | the number, yes | harmless: a clock ahead is a time skip; put back when the player leaves by choice                                              |
| A guest's crowd switched off                                                                     | no              | given back at the session's end; "world" keeps the lock until it is                                                            |
| Status effects a guest's hits put on the host's NPCs                                             | yes, on purpose | they are what happened to the host's world                                                                                     |
| XP markers of a guest on the host's NPCs                                                         | no              | **fixed here**: no longer forwarded                                                                                            |
| The host's queued skill XP for NPCs a guest killed                                               | no longer       | **fixed here**: paid as an assist                                                                                              |
| The reward ledger                                                                                | yes, on purpose | a quest paid in co-op pays nothing again                                                                                       |
| A vendor entry for a proxy in a guest's `MarketSystem`                                           | would be        | not created today (no way to open a shop on a proxy): the reason behind the vendor recommendation above                        |
| A trade's table                                                                                  | no              | nothing moves before the commit ([`trade.md`](trade.md))                                                                       |

## Decisions for the user

1. **`leave`**: `stay` now; `save` or `reload` once checked in game with a
   spare save. Recommended end state: `reload`.
2. **The clock on leaving**: back to the guest's own timeline (built), or
   keep the host's time? Recommended: its own.
3. **Vendors**: option 1 above (a guest keeps its own vendors).
4. **The host's quests in a guest's story**: no; later an offer for quests
   the guest had not started.
5. **Assists**: skill XP only (built). Level XP and street cred for
   assists would be a new rule the game does not have: recommended no.
6. **Guest checkpoints mid-session**: after the leave save is proven.
7. **The pause menu's Exit as a guest**: run the leave steps first (a
   short "saving your V" step), or a warning. Recommended: the leave steps.
8. **NPCs with no content assignment** keep their level. If the game shows
   that most enemies have none, they should follow the mean by the plain
   difference instead (`progress::NpcShift` already does that when no curve
   is read): a one-line change after the first in-game count.

## Tests and tools

- Core: `tests/progress_test.cpp` (the mean, the NPC and proxy modifiers,
  every path of the leave steps, the levels over a session: joins,
  level-ups, leaves, a vanish, late joiners), `tests/protocol_test.cpp`.
- Mock player: `--level N` (its V's level), `host --npc-level L` (the
  level it says its NPCs have).
- In game: `tools/game.sh cmd progress` logs the leave step, our lock, the
  game's locks, what is left of a session, and the levels.
- Scenarios (not run yet): `levels-host`, `levels-guest`, `saves-leave`.
