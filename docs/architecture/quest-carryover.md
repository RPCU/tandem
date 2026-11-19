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

<!-- prettier-ignore -->
| Tier | What | In the guest's own save | Risk |
| --- | --- | --- | --- |
| 1 | XP, level, street cred, skill progress, perk points, money, loot, crafting parts, ammo, non-quest reward items (iconics included), achievements | kept: they are in the V's running game and reach the save with the first save after the session | low |
| 2 | gigs (`StreetStory`, `Contract`), cyberpsycho sightings, courier jobs, NCPD scanner hustles | **credited** now (recorded, never paid twice); **completed in the guest's world** only after spike C1 proves each type (stage B) | medium to high: a half-set completion soft-locks |
| 3 | main, side and minor quests; quests that unlock a vehicle or an apartment (`VehicleQuest`, `ApartmentQuest`); everything a quest decides (romance, endings) | recorded ("done in co-op with NAME"); nothing applied; quest props removed on leave; an optional skip offer much later, never for main quests | low as proposed |

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

**Decided by the user 2026-11-19, and built** (`Story.reds`, `Loot.reds`):
when a quest prop (`TandemLoot.QuestProp`: tag `Quest`, or a keycard)
comes into a guest's inventory in a session, whether shared by the host
or picked up from a proxy's corpse, it is noted as a session item, in a
persistent list of the story system, unless it is a second copy that
co-op takes back out at once. Nothing is noted for a host, a player
alone, or a guest apart (its own gig's props are its own). Leaving the
session (by choice, or because the host vanished: both run
`GuestLeave`) takes every noted item back out, first thing in the leave
step and so before saving unlocks (`tools/game.sh cmd story items` says
how many are held). **A crash** leaves
nothing to do: no save was made in the session, so the last save has
neither the items nor the list. Should a save ever hold them (a session
that did not end cleanly and saved anyway), the list is in that save too
and the items are removed when it is next loaded, once its world plays
(2 s after the load). The same check, outside a session, catches a leave
that found no V to take them from. An item already gone (used, sold) is
not looked for again.
Iconics and every other reward stay. A host keeps its quest items: they
are its story's. Not yet run in game.

### Tier 2: self-contained open-world content

How each type's completion is stored (`E§13.29`):

<!-- prettier-ignore -->
| Type | Stored as | Setting it from outside |
| --- | --- | --- |
| NCPD hustle | a child phase of `open_world_minor_activities`; its point-of-interest mappin's phase (`CompletedPhase`); its community and loot containers; facts | Codeware models all of it and can _reset_ one (`OpenWorldSystem`); marking one done is the same list the other way. No script API for the mappin phase (`SetPoiMappinPhase` is native) |
| gig, cyberpsycho, courier | a quest: its phase graph's position, journal entries (quest, objectives), its `*_done` facts, fixer counters (facts the fixer's phase counts), the district bar (`GetMetaQuestData`) | journal and facts have script setters; **the phase graph does not**: a gig marked done in the journal still sits at its start node and starts when the guest walks in |
| fixer reputation, calls, reward car | driven by the fixer's own phase from the counters above | follows if the counters are right; a wrong counter gives the fixer's reward twice or never |

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

## Whose gig is it

**Decided by the user 2026-11-19: rule (a), and the helper's copy stays
playable at home.** Built so far, behind "always grouped" (so today's
behaviour is unchanged): the freeze by ownership, the journal and phone
rules below, and the spike's tooling. The one switch is
`TandemStorySystem.SetApart(apart)` (script, guest only; leaving sets it
back): `TandemStory.Grouped(!apart)` for the freeze, and the gig journal
entries change hands. Only the test commands `quest apart` and
`quest grouped` call it today.

**Interface for `regions`** (that branch's `regions.md`, "Gigs: grouped
or apart, and holding a region", read on 2026-11-19): it gives
`TandemRegions.IsApart()`, `TandemRegions.Leader()` and a
`RegionsChanged` event, and states that a guest's gig phases wait exactly
when `Leader() != you`. When it merges, the story system calls
`SetApart(TandemRegions.Leader() == You())` at each change of links or
leader, so "apart" here means "our world's gigs are the ones played where
we stand": alone, or leading the group by a hold. The hold
(`TandemRegions.Hold(true)` when a gig phase of our own world starts where
our V stands, `false` when it ends) needs a gig phase to be recognised at
its start: spike C2 first.

The user's follow-up (2026-11-19): _a gig done by a guest is a gig less
for the host: its reward and its story are gone for the host, who neither
saw nor chose it._ The players may split up and do gigs apart (decided
earlier), so the question is which world a gig done apart belongs to.

<!-- prettier-ignore -->
| Rule | For the host | For the guest | Verdict |
| --- | --- | --- | --- |
| (a) a gig belongs to the world of the player who starts it | its own gig untouched, still there to play | done in its own save natively: no carry-over | **decided** (2026-11-19) |
| (b) a guest can never start a host's gig alone (needs the host near) | nothing lost | cannot play gigs apart: contradicts "gigs apart" | the fallback until (a) is built: it is what the freeze does today |
| (c) it completes in the host's world; the host is paid too | paid for content it never saw; the gig is gone from its map | needs the outcome merged into the host's quest graph | rejected: the user's complaint exactly, and the riskiest engineering (`coop-quests-design.md` §5.2) |

### Rule (a), worked out

**Ownership.** One world owns each run of a gig: the world of the game
that runs its quest phase.

- **Apart** (each game owns the region around its V, the `regions`
  branch's design): a gig is started by the game whose V walks into it or
  takes its fixer's call. A guest alone starts **its own world's** gig; a
  host alone starts its own, as today. Symmetric.
- **Together at the start**: the game that owns the region owns the gig:
  the host's when the players are grouped (one owner per region, the core
  authority rule). The other player is a helper from the first second. No
  prompt: a prompt at every gig door would not feel like the game. A guest
  who wants the gig to be _theirs_ walks in first, alone, or the players
  swap who hosts.
- **A partner arriving later** joins the owner's run. It becomes a helper
  when it enters the owner's region: it sees the owner's gig NPCs (the
  owner's game shares them, as the host shares its NPCs today), its own
  copy of that gig is held (below), and it is paid once by the existing
  rule (`QuestReward`, "took part", the ledger).
- **The helper's own copy** stays as it was in its own world: tier 2
  credit in its ledger ("done with NAME"), playable at home, not paid
  twice. For the host helping a guest this is the answer to the user's
  worry: the host's gig is still there.

**What each sees.**

<!-- prettier-ignore -->
| Thing | Owner | Helper |
| --- | --- | --- |
| fixer's call, texts | its own phone, its own fixer phase | hears nothing of it apart; near the owner, the owner's call is shared like the host's calls today (`Phone.reds`) |
| journal and tracker | its own entries | the owner's entries for that gig while helping, then back |
| map | its own gig pin, gone when done | its own world's pin for that gig stays (its copy is not done) |
| gig NPCs, doors, loot | its own game's, shared to the helper | proxies of the owner's; per-player loot; quest props as session items |
| reward | the game's own | the same record through its own game, once |
| district and fixer progress | counts in its world | unchanged in its world |

**Does this make tier 2 carry-over unnecessary?** For a gig the guest
owns: yes. It ran in the guest's own quest graph and lands in the guest's
save like any solo gig, with no state written from outside. Tier 2 credit
remains only for the helper's copy, and stage B (applying completion
elsewhere) becomes optional comfort rather than a need. That is the main
reason to prefer (a): it replaces the riskiest stage with the game's own
code.

### Feasibility

How gigs start (`E§13.29`, and the freeze's first run in game): a gig is
a quest phase that waits on a trigger area around its place or on a
fixer's call; the fixer's calls and counters come from
`open_world_fixers`, which already keeps running on a frozen guest. In
that first run the frozen phases had **no resource of their own**
(`resource 0x0`, depth 9 to 11): they are embedded in a parent's file, so
a gig cannot be recognised by its resource path alone.

What must change:

1. **The freeze, from "story phases" to "phases this game does not
   own".** Today everything but the open-world list waits. A guest's own
   gigs must run while it is apart, and be held while it is helping.
   - _Which phases are gigs._ Proposed: by the journal. A gig's phase
     graph holds journal nodes (`questJournalNodeDefinition`) naming its
     journal quest, and the journal gives the quest's type
     (`StreetStory`, `Contract`, `CyberPsycho`, courier). The freeze
     classifies a phase once, by the first journal node found in it or
     its ancestors, and caches it by node path. Fallback and override:
     a list of gig resource paths in `tandem.ini`
     (`quest_freeze_allow` exists). Needs a spike (C2): log, for ten
     known gigs, the phase chain and its journal nodes. **Built:** the
     override list, `quest_freeze_gigs` (a phase under one of these
     resources, by its own resource, an ancestor's on the phase stack or
     a known gig phase's node path, is a gig phase; the open-world list
     wins over it), and the spike's log, `tools/game.sh cmd quest trace
on`: one `quest trace: phase` line per phase met (its node path, its
     resource, the chain of resources on its stack) and one `quest trace:
... journal` line per journal node run in it (the journal path it
     names, its class). Works with the freeze off, so a solo game can
     run it.
   - _When they run._ Gig phases run when the guest is **apart** from the
     host (its own region) and wait when it is **grouped** (the owner's
     run is the one being played). The held signals are given back when
     the guest is apart again, as the freeze does on leave. Main, side
     and minor quests wait for the whole session, as now.
   - A gig the guest started and then leaves half done, to regroup with
     the host, waits and resumes: the freeze already does that.
   - **Built** (`engine/quests.cpp`, `SetQuestGrouped`): a held signal
     remembers whether its phase was a gig's; going apart gives the gig
     ones back in the order they came, the story's stay held. The log
     says `quest: freezing gig phase` or `story phase`.
2. **The journal mirror** (`Story.reds`) hides the guest's own entries
   and shows the host's. Entries of the guest's own gigs (by quest type)
   must stay visible and trackable; the host's gig entries reach a guest
   only while it helps. **Built:** a gig entry is one whose journal quest
   is a street story, contract, cyberpsycho or courier quest
   (`GetQuestType`). Apart, the mirror leaves those alone (no host change
   applied, none hidden by a snapshot). Going apart gives every gig entry
   the mirror touched back its state from before (ours come back; the
   host's go back to hidden), and they leave the list restored on leave,
   so the leave never undoes what our own gig did apart. Going grouped
   again hides our gig entries the host's journal lacks and shows the
   host's as last heard (the guest keeps the host's journal as its
   snapshot plus every change since).
3. **The phone mirror** must tell the guest's own fixer call (its own
   phase, apart) from a mirrored one: it already does by a flag while a
   mirrored call is up; the rule to add is that a mirrored call does not
   start while the guest's own is up, and the other way round. **Built,
   apart only** (grouped, the host's call is shown as before): the host's
   call waits while our own rings or talks and is shown once it ends
   (dropped if the host's ended meanwhile); our own call that starts
   while the host's is on our phone is retried every 5 s
   (`DelaySystem.DelayScriptableSystemRequest`).
4. **Rewards and the ledger**: unchanged. `OwnState::Done` already stops
   a second payment when the host later runs a gig the guest finished at
   home, and the other way round through the host's game.

What the `regions` work must provide (asked here on 2026-11-19; that
branch's `regions.md` answers each point, see "Interface for `regions`"
above; `../research/coop-regions.md` is its research):

- **grouped or apart**, per player, as one fact both the freeze and the
  mirrors can read, with hysteresis (a gig must not flip owner at a
  doorway);
- the guest's game **owning the NPCs around its V while apart**: no
  hiding of its own NPCs there, so its gig's enemies exist; and sharing
  them to a partner who arrives (the host's NPC sharing, run by a guest);
- the owner of a region staying its owner while a gig phase of its world
  is active there, even if the other player arrives first at the next
  room;
- quest NPCs never handed over between games (its research says so
  already).

Risks: the classification spike may show that some gigs share phases
with side quests (then those stay held: rule (b) for them); a gig whose
area the two worlds disagree about (a door one world already opened) is
the owner's as its world has it, and the helper sees proxies.

**Until regions and the classification exist, rule (b) is what happens**:
a frozen guest starts nothing of its own, and gigs run in the host's
world with the guest as helper. Nothing in today's build takes a gig from
the host.

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

<!-- prettier-ignore -->
| Native | When |
| --- | --- |
| `CarryLoad(words: array<Uint64>)` | the story system attaches (a load): the save's ledger |
| `CarryWords() -> array<Uint64>` | after any change: stored back into the persistent field |
| `CarryQuest(quest, kind, own, host)` | a guest takes part in a quest's step (`QuestReward`, or a journal change of the host's quest while near): `own` from the baseline |
| `CarryCompleted(quest)` | the host's quest reaches Succeeded with the guest near |
| `CarryPay(quest, reward) -> Int32` | before `GiveReward` on a guest: 0 pay, 1 already paid, 2 done alone |
| `CarryOwnPays(quest, reward) -> Bool` | the `OnRewardEvent` wrap in the guest's own story |
| (script, built) `TandemStorySystem.NoteSessionItem(record, quantity)` | `Loot.reds`: a quest prop came into a guest's inventory (shared, or picked up) |
| (script, built) `RemoveSessionItems()` | the guest's leave, first thing; and a load of a save that holds the list. These two replace the first draft's natives `CarrySessionItem(record, quantity)` and `CarryTakeSessionItems() -> array<…>`: the list must be in the save, so it is script state |

Order on leave (inside `TandemStorySystem.GuestLeave`, so the
`progression` branch's `LeftOver` bit 1 still covers it): close the
mirrors, **remove session items**, restore the journal, end the freeze,
store `CarryWords`, then unlock. `Saves.reds` saves after that, by its own
rules.

## Edge cases, ruled

<!-- prettier-ignore -->
| Case | Rule | Why |
| --- | --- | --- |
| host further ahead | the guest is told before joining ("NAME is further in the story: spoilers") when the host has 3 or more main quests more (`carry::NoteFor`); it may join | spoilers are the player's call; nothing of the host's story is applied to the guest anyway |
| items and vehicles from quests the guest has not reached | reward items (iconics) are kept: tier 1. Quest props are removed on leave. Vehicles and apartments are unlocked by the host's quest graph for the host only; the guest gets none | a vehicle unlock is world state of a tier 3 quest (`VehicleSystem.EnablePlayerVehicle` from a quest node, not a reward record) |
| the host is past the point of no return | told before joining ("NAME is playing the ending"); it may join and watch; tier 1 only; the ending's reward screen is the host's | the ending is one save's (`inkSaveType.PointOfNoReturn`); a guest's V returns to its own world as it was |
| Phantom Liberty | **required for every player** (decided by the user 2026-11-19): a game without it cannot join, checked at join by a flag in `Hello` (protocol 7, the release work). Both own it but only the host's story has begun it: a spoiler like any other, warned (`carry::NoteFor`), allowed; Dogtown rewards are tier 1, its gigs tier 2 credit, applied only once the guest's own story has opened Dogtown | no mixed ownership to handle (`IsEP1`, `IsEp1Entry` answer the same everywhere): journal hashes, reward and item records are the same in every game |
| host behind: the guest already did the quest | no quest reward (`Pay::DoneAlone`: its own journal had it Succeeded at join); kills, loot and their XP are still its own | "once per quest" in both directions; helping is still worth the loot |
| divergent choices, romance, lifepath | never carried: tier 3. A choice made in the host's world is the host's | `story.md`, private scenes |
| a quest failed or missed in the guest's world | credited, never applied (`OwnState::Failed` is not "available") | reviving a failed quest is writing quest state |
| the same gig with two hosts | paid once (the ledger outlives the host); the first host's name stays | keyed on the quest, not the session |
| unique rewards later given again by the guest's own story | reward records: skipped, they were paid. Items a quest hands over without a reward record: a second copy arrives; proposed: accept it for now, detect and drop iconic duplicates in stage B | duplicates are harmless to quests; dropping needs the list of which quests give items by node |
| leaving mid-quest, the host vanishes | what was paid is kept; the quest is "taken part in"; session items removed; the save follows the leave flow | the ledger's commit is the save |
| a crash | the session's gains and its ledger entries are lost together (the last save is from before) | accepted in `story.md`; a periodic safe checkpoint is the later fix, and it would write both |
| farming by rejoining | nothing to farm: (quest, reward) pays once per character, ever; repeatable content pays as the game repeats it | the ledger is in the save, so deleting a file does not reset it |
| three or more players | each guest has its own ledger and its own "took part"; nothing is shared between guests | no cross-guest state |

## Differences from the starting proposal, and from other branches

- **The ledger is in the save, not a file** (see above). The proposal's
  "applied at leave or at the next solo load" needs no apply step for
  tier 1: the game already holds it.
- **Tier 2 is credit first, completion after a spike.** The proposal's
  "completion carries" is right as a goal; the evidence says journal and
  facts are not the whole state of a gig.
- **Quest props are removed on leave.** This narrows `loot.md`'s "quest and
  important items are every player's" to the length of the session for
  items tagged `Quest` and keycards. Iconics stay. Decided by the user
  2026-11-19.
- **`progression` branch** (uncommitted work read on 2026-11-19: `Saves.reds`,
  `core/progress.hpp`): no contradiction. It owns _when_ the guest saves;
  this owns _what must be true before_ (session items out, ledger words
  stored). Its `LeftOver` bit for the story already gates the save on
  `GuestLeave` having finished, and the session items go out at the top
  of `GuestLeave`. For that branch when it merges (its `Story.reds` and
  `System.reds` changes were uncommitted on 2026-11-19, so not edited
  here): its `TandemStorySystem.LeftOver` adds one condition to bit 1,
  `this.SessionItemsLeft() > 0`, so no save is made while the removal has
  not run (no V at that moment); the story's `Tick` retries it outside a
  session. Its `Begin` hunk (`CarryLoad`, the garage) and ours (the
  session items' note) sit side by side in the same function, and its
  `carryover_test.cpp` still calls `Ep1Missing`, which this branch
  removed (Phantom Liberty is required): drop those three checks when
  merging.
- **`Story.reds` today** keeps `m_rewarded` / `m_rewardedQuests`: the
  ledger replaces them (same rule, plus the host-behind case).

## Stages

<!-- prettier-ignore -->
| Stage | What | Needs |
| --- | --- | --- |
| A0 | the ledger and its rules (`core/carryover`) with tests | done |
| A1 | session items removed on leave: done (script, persistent list; scenario `story-session-items`). Still to do: natives for the ledger in the save, pay rules through it, "done with NAME" list in the co-op menu | after `progression`'s leave flow merges (same files) |
| A2 | join notes: the host's story mark in the handshake or a story message, the spoiler and ending notices (Phantom Liberty itself is required at join: the release work, protocol 7) | a story message for the mark |
| C2 | spike on a solo game: gig phases by their journal nodes. Tooling done: `tools/game.sh cmd quest trace on` logs each phase's chain and journal nodes. The freeze by ownership (gig phases run apart, wait grouped), the journal and phone rules for owned gigs: built behind "always grouped", so nothing changes until `regions` calls `TandemStorySystem.SetApart`; scenarios `quest-trace` (the spike's log), `story-gigs-apart` (the switch by hand) | the game machine for the spike; `regions` for grouped or apart |
| C1 | spikes on a solo game: mark a hustle done from outside (C1a); finish a gig through its own end facts (C1b) | the game machine, a day |
| B | apply `Pending()` in the guest's world for the types that passed C1; drop duplicate iconics | C1 |
| C | skip offers for self-contained side content | B, and a per-quest allow-list |

## Decisions

Decided by the user:

- **2. Quest props leave the guest's inventory when it leaves the
  session** (2026-11-19): items tagged `Quest` and keycards got from the
  host's quests; iconics and every other reward stay. This narrows "quest
  and important items are every player's" (2026-11-18) to the length of
  the session for those items.
- **6. A gig belongs to the world of the player who starts it**
  (2026-11-19). Grouped at the start, it is the host's, with no prompt.
- **7. A helper's own copy of a gig stays playable at home, paying nothing
  again** (2026-11-19).
- **Phantom Liberty is required for every player** (2026-11-19), enforced
  at join.

Working defaults, unconfirmed (the recommendations; they stand until the
user says otherwise):

- **1. Tier 2: credit only now, completion after the spikes** (default,
  unconfirmed). The alternative (write journal and facts now) risks the
  guest's save for a convenience.
- **3. A host behind: no quest reward for a quest the guest already
  finished alone** (default, unconfirmed); loot and kill XP remain.
  Alternative: a reduced reward (money and XP only).
- **4. A host ahead: warn and let the guest join, never refuse** (default,
  unconfirmed). This includes a host whose story is in Phantom Liberty
  when the guest's is not yet.
- **5. Duplicate unique items from the guest's own story later: accept a
  second copy for now** (default, unconfirmed); revisit in stage B.
- **8. Skip offers (stage C): not for main quests** (default,
  unconfirmed); decide after stage B for the rest.
