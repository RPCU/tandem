# Quest carry-over: what a guest brings home

**Status: proposal, 2026-11-19.** Desk research and one tested core module
(`core/src/carryover.cpp`); nothing here ran in game. It re-evaluates the
story architecture (`story.md`, `../research/coop-quests-design.md`) for the
user's request: _a player joins to help another in their world, and brings
back what they earned when they play solo again; the host's progress should
show in the guest's quests where that is safe; careful with a host that is
further ahead._

Engine facts are in `../research/engine/story.md` §13.29, cited as `E§13.29`.

## What does not change

The host's game runs the story. A guest's own story waits (the per-phase
quest freeze), its journal shows the host's, and it is put back when the
guest leaves. That stays: it is what keeps two stories from running in one
game. Carry-over is about **what is written into the guest's own save
afterwards**, nothing else.

## The rule in one paragraph

A guest's V keeps everything that is the V's (tier 1). Its own world is
changed by co-op only where the game's own content is self-contained
(tier 2), and only once an in-game spike shows it can be done completely.
Its own story is never advanced by co-op (tier 3): it is told what it did
with whom, is not paid twice, and holds none of the host's quest props.
Every decision is recorded in a **ledger that lives in the guest's own
save**.

## Tiers

| Tier | What                                                                                                                                                        | In the guest's own save                                                                                                                       | Risk                                             |
| ---- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| 1    | XP, level, street cred, skill progress, perk points, money, loot, crafting parts, ammo, non-quest reward items (iconics included), achievements             | kept: they are in the V's running game and reach the save with the first save after the session                                               | low                                              |
| 2    | gigs (`StreetStory`, `Contract`), cyberpsycho sightings, courier jobs, NCPD scanner hustles                                                                 | **credited** now (recorded, never paid twice); **completed in the guest's world** only after spike C1 proves each type (stage B)              | medium to high: a half-set completion soft-locks |
| 3    | main, side and minor quests; quests that unlock a vehicle or an apartment (`VehicleQuest`, `ApartmentQuest`); everything a quest decides (romance, endings) | recorded ("done in co-op with NAME"); nothing applied; quest props removed on leave; an optional skip offer much later, never for main quests | low as proposed                                  |

The tier of a quest comes from the game's own sorting, the journal's
quest type (`JournalManager.GetQuestType`, `E§13.29`); `carry::TierOf` is
the table. An NCPD hustle has no journal quest: it is tier 2 with kind
`Unknown`.

### Tier 1: the V's own

Already true by construction: rewards are given by the guest's game to the
guest's V (`RPGManager.GiveReward`), kills and loot are per player
(`loot.md`). Achievements come with reward records (`GiveReward` sends
them, `E§13.29`). What is missing is only durability: the save after the
session. That is the `progression` branch's work (`Saves.reds`,
`core/progress.hpp`: stay, save or reload on leave), not repeated here.

**One correction to today's behaviour.** `loot.md` shares every item
tagged `Quest`, every keycard and every iconic into every V's inventory
so that no quest breaks in the session. The iconics are tier 1 and stay.
The **quest props** (tag `Quest`, keycards) must not reach the guest's
save: the game's quests test the player's inventory by item id or tag
(`questInventory_ConditionType`, `E§13.29`), so a key from the host's quest
could open the guest's own quest later, or a shard could satisfy an
objective out of order. Rule: they are **session items**. The guest holds
them while in the session (so the host's doors open for it too), and they
are taken back out when it leaves, before saving unlocks
(`Ledger::NoteSessionItem`, `TakeSessionItems`). Its own story gives its
own copy when it gets there.

### Tier 2: self-contained open-world content

How each type's completion is stored (`E§13.29`):

| Type                                | Stored as                                                                                                                                                                            | Setting it from outside                                                                                                                                                                |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| NCPD hustle                         | a child phase of `open_world_minor_activities`; its point-of-interest mappin's phase (`CompletedPhase`); its community and loot containers; facts                                    | Codeware models all of it and can _reset_ one (`OpenWorldSystem`); marking one done is the same list the other way. No script API for the mappin phase (`SetPoiMappinPhase` is native) |
| gig, cyberpsycho, courier           | a quest: its phase graph's position, journal entries (quest, objectives), its `*_done` facts, fixer counters (facts the fixer's phase counts), the district bar (`GetMetaQuestData`) | journal and facts have script setters; **the phase graph does not**: a gig marked done in the journal still sits at its start node and starts when the guest walks in                  |
| fixer reputation, calls, reward car | driven by the fixer's own phase from the counters above                                                                                                                              | follows if the counters are right; a wrong counter gives the fixer's reward twice or never                                                                                             |

So tier 2 completion by writing state is **not safe today**. The journal
and the facts alone leave the quest graph where it was, and a quest graph
that disagrees with its journal is exactly how saves break. Two ways
forward, to try in this order:

1. **Hustles first (spike C1a).** They are the smallest and Codeware
   already lists every piece of state. On a solo game: mark one hustle
   done from outside (mappin phase, community off, the facts its phase
   waits on), save, load, walk there. Done when: it stays done, no
   enemies, the district bar counts it.
2. **Gigs by their own facts (spike C1b).** A gig's phase waits on
   conditions; if its end conditions are facts we can set (the target
   dead, the item delivered), setting them lets the guest's own graph
   finish the gig itself, with its own fixer call, and the ledger stops it
   paying twice. Done when: three gigs of different shapes (kill, steal,
   rescue) finish cleanly this way, or it is shown they cannot.

Until a type passes its spike it is **credited only**: the ledger says the
guest did it with NAME, rewards are not paid again, and the gig stays
playable in its own world. That is honest and cannot break a save.
`Ledger::Pending()` is the list stage B will apply.

"Available in the guest's world" means: its own journal has the quest not
started or active when the guest first takes part (`OwnState`, recorded
then). Content the guest's world has not unlocked (a district, Phantom
Liberty) is credited but never applied.

### Tier 3: story quests

- **Recorded**, per quest: the host's name, completed or not, rewards paid.
  Shown in the co-op menu as "done in co-op with NAME" (the journal itself
  is the guest's own again).
- **Nothing is applied**: choices, scenes, romance and prerequisites
  differ, and the guest's V was not the V in those scenes.
- **Not paid twice.** When the guest's own story reaches the quest, the
  reward records co-op already paid are skipped (`PayInOwnStory`; built in
  `Story.reds` today as a per-reward list, to be replaced by the ledger).
- **A skip offer** ("already done with NAME, skip?") only where a quest is
  provably self-contained: a later stage, behind its own spike, and never
  for main quests or anything with a choice.

## The ledger

`core/include/tandem/carryover.hpp`, tested in `tests/carryover_test.cpp`.

- **Where it lives: in the guest's own save**, as one persistent field of
  a scriptable system (`persistent let m_ledger: array<Uint64>`, the
  packed words of `Ledger::Pack`). Not in the host's save, and
  deliberately **not in a side file**. A file would outlive a save: load a
  save from before the session and the file would still say "paid", with
  the items gone. In the save, the ledger and the inventory it describes
  are written by the same save and lost by the same load. That is the
  transaction the request asks for, with no code.
- **What commits when.** Nothing is saved in a session (the guest's save
  lock). Everything the session earned sits in the running game and in the
  ledger in memory. The first save after the guest's own world is back
  writes both. A crash, a vanished host followed by a crash, or a load
  before that save loses both, together.
- **Idempotent.** Paying is keyed on (quest, reward): a resend, a rejoin
  or the same gig with another host pays once. Loading the words twice, or
  merging two ledgers of the same character, changes nothing.
- **Mid-quest.** A quest left before its end is in the ledger as taken
  part in, not completed: rewards already paid stay paid, nothing is
  pending for the guest's world.

### Interface for the script side (to build with the leave flow)

Natives on `TandemStory` (plugin, game thread), all thin calls into one
`carry::Ledger`:

| Native                                | When                                                                                                                              |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `CarryLoad(words: array<Uint64>)`     | the story system attaches (a load): the save's ledger                                                                             |
| `CarryWords() -> array<Uint64>`       | after any change: stored back into the persistent field                                                                           |
| `CarryQuest(quest, kind, own, host)`  | a guest takes part in a quest's step (`QuestReward`, or a journal change of the host's quest while near): `own` from the baseline |
| `CarryCompleted(quest)`               | the host's quest reaches Succeeded with the guest near                                                                            |
| `CarryPay(quest, reward) -> Int32`    | before `GiveReward` on a guest: 0 pay, 1 already paid, 2 done alone                                                               |
| `CarryOwnPays(quest, reward) -> Bool` | the `OnRewardEvent` wrap in the guest's own story                                                                                 |
| `CarrySessionItem(record, quantity)`  | `Loot.reds` gives a guest a shared item tagged `Quest` or a keycard                                                               |
| `CarryTakeSessionItems() -> array<…>` | the guest's leave, before the story is "back as it was": remove these from the inventory                                          |

Order on leave (inside `TandemStorySystem.GuestLeave`, so the
`progression` branch's `LeftOver` bit 1 still covers it): close the
mirrors, **remove session items**, restore the journal, end the freeze,
store `CarryWords`, then unlock. `Saves.reds` saves after that, by its own
rules.

## Edge cases, ruled

| Case                                                      | Rule                                                                                                                                                                                        | Why                                                                                                                                                                             |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| host further ahead                                        | the guest is told before joining ("NAME is further in the story: spoilers") when the host has 3 or more main quests more (`carry::NoteFor`); it may join                                    | spoilers are the player's call; nothing of the host's story is applied to the guest anyway                                                                                      |
| items and vehicles from quests the guest has not reached  | reward items (iconics) are kept: tier 1. Quest props are removed on leave. Vehicles and apartments are unlocked by the host's quest graph for the host only; the guest gets none            | a vehicle unlock is world state of a tier 3 quest (`VehicleSystem.EnablePlayerVehicle` from a quest node, not a reward record)                                                  |
| the host is past the point of no return                   | told before joining ("NAME is playing the ending"); it may join and watch; tier 1 only; the ending's reward screen is the host's                                                            | the ending is one save's (`inkSaveType.PointOfNoReturn`); a guest's V returns to its own world as it was                                                                        |
| Phantom Liberty on one side only                          | host with, guest without: the join is refused ("needs Phantom Liberty") until the handshake can say which content is in play; guest with, host without: fine                                | journal hashes, reward and item records of the expansion do not exist in a game without it (`IsEP1`, `IsEp1Entry`); needs a flag in `Hello` (a protocol bump, the integrator's) |
| host behind: the guest already did the quest              | no quest reward (`Pay::DoneAlone`: its own journal had it Succeeded at join); kills, loot and their XP are still its own                                                                    | "once per quest" in both directions; helping is still worth the loot                                                                                                            |
| divergent choices, romance, lifepath                      | never carried: tier 3. A choice made in the host's world is the host's                                                                                                                      | `story.md`, private scenes                                                                                                                                                      |
| a quest failed or missed in the guest's world             | credited, never applied (`OwnState::Failed` is not "available")                                                                                                                             | reviving a failed quest is writing quest state                                                                                                                                  |
| the same gig with two hosts                               | paid once (the ledger outlives the host); the first host's name stays                                                                                                                       | keyed on the quest, not the session                                                                                                                                             |
| unique rewards later given again by the guest's own story | reward records: skipped, they were paid. Items a quest hands over without a reward record: a second copy arrives; proposed: accept it for now, detect and drop iconic duplicates in stage B | duplicates are harmless to quests; dropping needs the list of which quests give items by node                                                                                   |
| leaving mid-quest, the host vanishes                      | what was paid is kept; the quest is "taken part in"; session items removed; the save follows the leave flow                                                                                 | the ledger's commit is the save                                                                                                                                                 |
| a crash                                                   | the session's gains and its ledger entries are lost together (the last save is from before)                                                                                                 | accepted in `story.md`; a periodic safe checkpoint is the later fix, and it would write both                                                                                    |
| farming by rejoining                                      | nothing to farm: (quest, reward) pays once per character, ever; repeatable content pays as the game repeats it                                                                              | the ledger is in the save, so deleting a file does not reset it                                                                                                                 |
| three or more players                                     | each guest has its own ledger and its own "took part"; nothing is shared between guests                                                                                                     | no cross-guest state                                                                                                                                                            |

## Differences from the starting proposal, and from other branches

- **The ledger is in the save, not a file** (see above). The proposal's
  "applied at leave or at the next solo load" needs no apply step for
  tier 1: the game already holds it.
- **Tier 2 is credit first, completion after a spike.** The proposal's
  "completion carries" is right as a goal; the evidence says journal and
  facts are not the whole state of a gig.
- **Quest props are removed on leave.** This narrows `loot.md`'s "quest and
  important items are every player's" to the length of the session for
  items tagged `Quest` and keycards. Iconics stay. To confirm with the
  user (decision 2).
- **`progression` branch** (uncommitted work read on 2026-11-19: `Saves.reds`,
  `core/progress.hpp`): no contradiction. It owns _when_ the guest saves;
  this owns _what must be true before_ (session items out, ledger words
  stored). Its `LeftOver` bit for the story already gates the save on
  `GuestLeave` having finished.
- **`Story.reds` today** keeps `m_rewarded` / `m_rewardedQuests`: the
  ledger replaces them (same rule, plus the host-behind case).

## Stages

| Stage | What                                                                                                                                  | Needs                                                |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| A0    | the ledger and its rules (`core/carryover`) with tests                                                                                | done                                                 |
| A1    | natives and script: ledger in the save, pay rules through it, session items removed on leave, "done with NAME" list in the co-op menu | after `progression`'s leave flow merges (same files) |
| A2    | join notes: the host's story mark in the handshake or a story message, the spoiler and ending notices, the Phantom Liberty refusal    | a protocol change                                    |
| C1    | spikes on a solo game: mark a hustle done from outside (C1a); finish a gig through its own end facts (C1b)                            | the game machine, a day                              |
| B     | apply `Pending()` in the guest's world for the types that passed C1; drop duplicate iconics                                           | C1                                                   |
| C     | skip offers for self-contained side content                                                                                           | B, and a per-quest allow-list                        |

## Decisions for the user

1. **Tier 2 now: credit only, completion after the spikes?** Recommended:
   yes. The alternative (write journal and facts now) risks the guest's
   save for a convenience.
2. **Quest props leave the guest's inventory when it leaves the session?**
   Recommended: yes (tag `Quest` and keycards only; iconics and every other
   reward stay).
3. **A host behind: no quest reward for a quest the guest already finished
   alone?** Recommended: yes, none (loot and kill XP remain). Alternative:
   a reduced reward (money and XP only).
4. **A host ahead: warn and let the guest join, or refuse beyond some
   gap?** Recommended: warn, never refuse (except the Phantom Liberty
   mismatch, which cannot work).
5. **Duplicate unique items from the guest's own story later: accept a
   second copy for now?** Recommended: accept; revisit in stage B.
6. **Skip offers (stage C) at all?** Recommended: not for main quests;
   decide after stage B for the rest.
