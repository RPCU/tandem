# Quest sync in co-op — the design (game 2.50)

How the story is shared in a session so that it looks and sounds like the
game, does not break anyone's playthrough or save, and stays simple enough
to build in steps. It decides what
[`coop-quests-world.md`](coop-quests-world.md) and
[`coop-scenes.md`](coop-scenes.md) left open (they hold the facts; this file
cites them as `QW §n` and `SC §n` and does not repeat them) and plans
milestone 6 ("Story") of [`../ARCHITECTURE.md`](../ARCHITECTURE.md).

**Status: design, 2026-11-18. Step 1 is written (see ARCHITECTURE "Inside
the plugin", Story) and spike S1 read offline; nothing here ran in game or
in a session yet.**

Tags, as in [`coop-npcs-combat.md`](coop-npcs-combat.md): `[S]` read in the
vanilla scripts, `[R]` read in the RTTI dump, `[C]` Codeware's source, `[K]`
a fact from an earlier project as recorded in
[`engine-knowledge.md`](engine-knowledge.md) §7,
[`campaign-fork.md`](campaign-fork.md) or [`nco-commits.md`](nco-commits.md)
(their own tag applies), `[V]` verified in game by tandem, `[I]` inferred or
proposed here. Sources: `final.reds:LINE` is
`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`; `CW path` is
`~/.cache/tandem/ref/codeware`; RTTI lines are from
`docs/research/data/rtti-natives-2.50.tsv` and `rtti-vtables-2.50.tsv`.
No code from CyberpunkMP, Night City Online or the campaign fork is used;
only their facts and failures.

## Contents

0. [The decision on one page](#0-the-decision-on-one-page)
1. [Whose quest state runs](#1-whose-quest-state-runs)
2. [What a guest sees and hears](#2-what-a-guest-sees-and-hears)
3. [Holocalls, phone calls and messages](#3-holocalls-phone-calls-and-messages)
4. [Rewards: quests done together](#4-rewards-quests-done-together)
5. [Players in different places, and solo gigs](#5-players-in-different-places-and-solo-gigs)
6. [The plan: first version and the steps after it](#6-the-plan-first-version-and-the-steps-after-it)
7. [Risks and safety rails](#7-risks-and-safety-rails)
8. [Decisions needed from the user](#8-decisions-needed-from-the-user)
9. [In-game tests owed](#9-in-game-tests-owed)

---

## 0. The decision on one page

1. **The host's game runs the story; the guest's own story is paused, not
   overwritten.** For the whole session the guest's quest graph is frozen
   (no node of its own quests executes), its saving is locked, and the
   host's story is shown on top through the game's own systems: journal,
   tracker, markers, phone, messages, subtitles, choice hubs, rewards. When
   the guest leaves, tandem puts its journal and facts back as they were at
   join, saves, and reloads that save: the guest is back in its own story
   with everything its V earned. (§1)
2. **Everything that is UI comes from mirroring state into the guest's
   vanilla systems**, so it looks native for free: the "new quest / quest
   completed" toasts, the quest tracker, the map pins, the SMS toast and its
   sound, the incoming-call ringtone and widget, the dialogue hub. (§2, §3)
3. **Voice, lip-sync, holo video and cinematic cameras exist only where a
   scene runs.** No script path plays a scene's voice line outside its
   scene ([S], [R]: `SC §4.2`, §2.5 below). So the first version gives
   guests subtitles, the host's V animating, the hub and the call widget,
   but no story voice. Real voice comes from **replaying the host's scene
   nodes on the guest** (the guest's game plays the same scene, its own V in
   the V role, the host's puppet as an extra). That needs one experiment to
   prove a scene node runs outside its phase; holocalls are the first
   target because they involve no world actors. (§2.5, §6 spike S2)
4. **Decisions in conversations belong to the host's V** (it is the V the
   host's world's scene is played with); guests see the same hub live,
   move their own cursor and can mark a suggestion the host sees. **Calls
   and texts are shared**: everyone's phone rings, whoever answers first
   picks up for the crew, whoever replies first replies. (§2.4, §3)
5. **Rewards**: each game gives its own V the reward by its own rules
   (`RPGManager.GiveReward` on the guest, scaled to the guest's level and
   perks). A guest gets a quest's reward when it **took part**: in the
   session and within 150 m of the host's V at the completion or at any
   objective update of that quest during the session. (§4)
6. **Solo gigs** (a player doing a fixer job alone while the other does
   something else) need the guest's game to run that gig's phase and NPCs
   itself, i.e. region authority. They are **not in the first version**;
   the simplest viable form (fixer gigs and NCPD hustles only, claimed
   through the host, outcome merged into the host's save) comes after
   region authority. The host can already go off and do gigs alone (its own
   game runs them). (§5.2)
7. **First version** (step 1): guest freeze + save lock + restore on leave,
   journal/tracker/markers mirror, phone calls (ring, answer by either,
   native audio-call widget with the caller's portrait, subtitles), text
   messages and replies, subtitles and dialogue hubs near the host,
   host-tier holds, rewards with "taking part". All of it testable with the
   mock player. (§6)

---

## 1. Whose quest state runs

### 1.1 The guest's situation

A guest **loads its own save** (its own V, its own story at its own point)
and then joins (`tandem.ini` `role = join`, `autoload`; ARCHITECTURE
"Saves": "what stays the guest's own, in its own save: its V"). So unlike
the campaign fork and NCO, whose guests started a quest-free new game
([K] `campaign-fork.md` §1, `engine-knowledge.md` §7 "NCO world-start
facts"), a tandem guest's game **is running its own quests** when it joins:
its active mission, its open-world gigs and hustles, its pending holocalls
(Songbird-style calls froze NCO's game for 120 s, [K] `nco-commits.md` §7).
Whatever we pick must keep those from running ahead, from ringing in the
middle of the host's story, and from being damaged.

### 1.2 Options weighed

| Option                                                                                        | Cost                                                                                     | Risk to the guest's save                                                                                       | Looks native                                                  | Verdict                                                                                          |
| --------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| A. Quest-free guest (new game with an empty gamedef, the fork's and NCO's way)                | archive + lobby; the guest's V must be rebuilt (the fork exported level, perks, items)   | none (a throwaway world)                                                                                       | journal etc. as below; **the guest loses its own V and save** | rejected: contradicts "your V is yours"                                                          |
| B. Guest's quests keep running, host facts and journal forced in                              | low                                                                                      | **high**: mirrored facts wake the guest's own graph (phases waiting on them start, scenes play, spawns happen) | chaotic (two stories at once)                                 | rejected                                                                                         |
| C. Journal-only mirror, guest's quests running                                                | low                                                                                      | medium: quest graphs also wait on journal states; the guest's own calls and scenes still fire                  | broken by the guest's own calls and messages                  | rejected                                                                                         |
| **D. Guest's quests frozen; host state mirrored into the guest's systems; restored on leave** | one native hook (2.5-verified hash) + script mirrors                                     | low, with the rails of §1.4                                                                                    | all UI native; no story voice until replay                    | **chosen**                                                                                       |
| E. Lockstep: the guest runs the same quest phases, every node gated by the host's trace       | very high (phase instancing on demand, trace protocol, prefab loading, actor duplicates) | medium                                                                                                         | best: everything plays locally                                | not now; D grows towards a limited form of it ("shadow phases", §6 step 5) for presentation only |

D keeps one story (the host's) and one copy of every world actor (the
host's NPCs as proxies, ARCHITECTURE authority table), touches the guest's
save only through state tandem can put back, and leaves a clean path to
the native voice and cameras later.

### 1.3 Freezing the guest's quests

**Lever**: hook `QuestPhaseInstance::ExecuteNode` (address hash
`3227858325`, signature `u8 (questPhaseInstance*, questNodeDefinition*,
QuestContext&, const QuestNodeSocket&, DynArray<QuestNodeSocket>&)`)
[K][C] (`campaign-fork.md` §2: hooked by the fork, called by Codeware's
`QuestPhaseExecutor`, `CW src/Red/QuestsSystem.hpp:286-292`). On a guest
in a session the hook refuses every node except those tandem itself runs
(a thread-local "tandem is executing" flag, as Codeware's own
`QuestsSystem.ExecuteNode` call needs, `CW scripts/Quest/QuestsSystem.reds`).

- What a refused node returns (spike S1, read offline 2026-11-18,
  `engine-knowledge.md` §13.12): **1**, the engine's own result for a
  disabled node: nothing is queued, the graph pass goes on, the phase
  stays active. The engine does not retry the signal, so tandem holds it
  (phase node path, node id, input socket) and gives it back through the
  root phase's `ExecutePath` when the freeze ends: the guest's story
  resumes where it stopped. Whether every kind of node resumes cleanly
  that way is the in-game part of S1. Implemented in
  `plugin/src/engine/quests.cpp`, off by default (`quest_freeze = on`).
- **Per phase, not global** (decided 2026-11-18 with the region study,
  `coop-regions.md` §1.4 point 5): the open world is quest phases too
  (communities, vendors, NCPD hustles, city scenes, fixers), and a guest
  away from the host must run its own surroundings. The filter: a phase
  keeps running when its resource, or an ancestor's on the context's
  phase stack, is one of Codeware's open-world phase resources
  (`CW src/App/Quest/QuestPhaseRegistry.cpp:12-20`) or one added in
  `tandem.ini` `quest_freeze_allow`, or when its node path extends the
  path of one (sub-phases: each NCPD hustle is a child of
  `open_world_minor_activities`); the root graph always runs. Every other
  phase (main, side, street stories, gigs) waits. Phantom Liberty's own
  open-world phases are not in Codeware's list: list them with the freeze
  log (`quest: freezing phase …`) and add them. A solo gig (§5.2) will be
  one more exception, by claim.
- Behind a layout check like every native lever (ARCHITECTURE "Talking to
  the engine safely"); on a mismatch the feature is off and **the guest is
  refused story sync** (it can still play with the host, with its own
  quests running and nothing mirrored: the pre-milestone-6 behaviour).
- On the host the same hook only **observes** (node class, resource,
  socket): it is where the host later sees fades, world-data nodes and
  scene starts that have no script event (`SC §2.1`, `QW §6.1`).

What freezing stops on the guest (all of it wanted): its main and side
quest phases, its open-world gigs, hustles and cyberpsychos, its
holocalls and quest texts, quest-driven tutorials, quest checkpoints (so
no `ignoreSaveLocks` checkpoint can save, `QW §11.1`). What keeps
working: everything that is not a quest node — the player, menus,
inventory, vendors' UI, the phone UI, devices, physics, scripts. Also
frozen, and worth knowing: systemic content built as quests (the metro
`ue_metro_*`, apartment and car-purchase flows) `[I]` — acceptable while
players are tethered; list what breaks with spike S1.

Scenes **already playing** in the guest's scene system when it joins are
not quest nodes and play on `[I]`: join only when the guest's local tier
is 1 (`PlayerPuppet.GetSceneTier`, `final.reds:44340` [S]); otherwise
wait.

### 1.4 The guest's save: lock, baseline, restore

ARCHITECTURE "Saves": the guest's world-state saving is locked in a
session and leaving puts it back in its own world. Concretely:

**At join** (before any mirror is applied):

1. Freeze on (§1.3).
2. Lock saving twice: `SaveLocksManager.RequestSaveLockAdd(game,
n"tandem")` (`final.reds:195179` [S]; its list is not persistent, so
   re-add after any load) and a tandem `ScriptableSystem` overriding
   `IsSavingLocked()` (the base declaration `final.reds:48411`, vanilla
   overrides `:138868`, `:154130`, `:195157`, `:247782` [S]; the fork's
   rail, [K] `campaign-fork.md` §5). Drop queued autosaves
   (`AutoSaveSystem.InvalidateAutoSaveRequests`, `QW §11`). Hide Save and
   Load in the pause menu (NCO did, [K] `nco-commits.md` §7). Block the
   death menu for the guest (`block_death_menu`, `final.reds:283760-283764`
   [S]): a guest must never load a checkpoint mid-session.
3. **Baseline**: record the guest's journal (every entry's state, through
   Codeware `JournalManager.GetEntries` [C], `CW
scripts/Quest/JournalManager.reds`), its tracked entry
   (`GetTrackedEntry`, `final.reds:196697` [S]) and, from step 2 on, its
   named facts (the `FactStore` walk, `QW §2.1`). In memory only.

**While in the session** nothing tandem writes reaches a save, and the
guest's own quest graph does not move, so whatever is in the journal and
facts can be put back exactly.

**At leave** (the guest quits the session, or the host is gone):

1. Close what the mirrors opened (call widget to `EndCall` and stop the
   ringtone — NCO: on every exit [K] `nco-commits.md` §7 —, dialogue hub,
   subtitles), release holds.
2. Put the journal back: every entry to its baseline state with the
   `ChangeEntryStateByHash(hash, state, DoNotNotify)` native
   (`final.reds:196714` [S]), entries not in the baseline to Inactive;
   tracked entry back (`TrackEntry`, `:196701`). Facts back (step 2 on).
   Undo the other persistent levers tandem holds (prevention lock
   `QW §9.4`, fast-travel lock `final.reds:50768`).
3. Check: a digest of the journal and facts must equal the baseline's.
   **On a mismatch, keep saving locked, say so in the HUD and the log, and
   offer only "reload my last save".** Never write a save that differs
   from the baseline in quest state.
4. Unlock saving, write one save ("back from co-op"), turn the freeze off,
   **load that save**. The load rebuilds the world from the save: proxies,
   hidden own NPCs (milestone 5 hides them with `HideIrreversibly`,
   `scripts/tandem/Npcs.reds:125`), crowd density, weather overrides — all
   session artifacts — are gone, and the guest's own quests resume where
   they stopped. The save holds the guest's own story plus everything its
   V gained (XP, money, items, perks): ARCHITECTURE's rule, with no
   character ledger to maintain.

**If the guest's game crashes** mid-session, its newest save is the one
from before the session: its quest state is safe; what its V gained in
that session is lost. Accepted for the first version; a later "safe
checkpoint" (restore, save, re-apply, under a short fade) can bound the
loss.

### 1.5 What the guest's world is while in a session

The guest's own world state (devices, loot taken, its own story's
consequences in the city) with, on top of it: the host's NPCs as proxies,
the host's time and weather (milestone 4, 5), and the host's story through
the mirrors. Differences that matter (a door the host's quest opened
before the guest joined, a quest interior that only exists in a running
phase) are handled in §2.7.

---

## 2. What a guest sees and hears

The rule behind every row: **the host's game decides, the guest's game
renders it with the same vanilla code path the host's game used**,
because the guest's vanilla UI listens to the same journal, blackboards and
systems. Where only a running scene can render something (voice,
lip-sync, holo video, scene cameras), the first version shows less and says
so; step 3 fills it by replaying the scene on the guest.

### 2.1 Objectives, journal, tracker, markers

- **Host**: one `JournalManager.RegisterScriptCallback(this, n"…",
gameJournalListenerType.State)` (`final.reds:196627` class; listener
  types `QW §3.2`) → each change as `{hash, state, notify}`; `Tracked`
  callback → the tracked entry's hash. Snapshot for a joiner: every entry
  not Inactive (Codeware `GetEntries`, or `GetQuests` + `GetChildren`
  recursively, the fork's way [K]).
- **Guest**: snapshot applied with `DoNotNotify`, parents before children
  (`QW §3.5`); the guest's **own** entries absent from the snapshot are set
  Inactive (its journal shows the host's story only; §1.4 puts them back).
  Deltas applied with the host's notify option, so
  `JournalNotificationQueue.OnJournalUpdate` (`final.reds:341545`) shows
  the same "new quest / quest completed / failed" toast with its sound
  (`PushQuestNotification`, `:341601-341640` [S]) and the quest tracker
  (`QuestTrackerGameController`, `:405918`, callbacks `:405950-405959`)
  redraws itself.
- **Tracking is per player**: the host's tracked quest is applied to a
  guest once on join and whenever the host changes it; a guest may then
  track something else (`TrackEntry` from its journal or the toast's
  action, `final.reds:341201`), as the fork allowed [K]. Natural in co-op
  and costs nothing.
- **Markers** follow the journal: a quest map pin is a journal entry under
  the objective; the mappin system creates it when the objective is
  Active (`QW §4.1`, inferred, test T3). Pins on placed things (a door, an
  area, a static NPC) resolve on the guest. Pins on **host-spawned** NPCs
  or cars do not (the proxy has another `EntityID`): step 2 sends the
  tracked objective's mappin positions from the host
  (`MappinSystem.GetQuestMappinPositionsByObjective`, `QW §4.2`) and the
  guest places a quest-variant pin there and moves it (`RegisterMappin`,
  `SetMappinPosition`).
- **Objective counters** ("3/5") have no script setter (`QW §3.4`, §14 q7):
  stale on guests in the first version; open RE item.
- **Codex, tarot, shards**: codex and tarot entries follow the journal
  mirror (tarot toast `final.reds:341567-341576`). Shards a player picks up
  are that player's (`QW §5.3`); quest-given ones arrive through the
  journal. Proposal: shards per player (decision 8 in §8).

### 2.2 Dialogue near the host (subtitles)

- **Host**: listeners on `UIGameData.ShowDialogLine` / `HideDialogLine` /
  `HideDialogLineByData` (`final.reds:217717-217719` [S]); line data
  `scnDialogLineData{id, text, type, speaker, speakerName, duration,
isPersistent}` (`SC §4.1`).
- **Guest**: the same variant written to its own `UIGameData`; the vanilla
  subtitle widget (`BaseSubtitlesGameController.OnShowDialogLine`,
  `final.reds:268284`, `SpawnDialogLine` `:268489`) draws it with the
  speaker's name. Speaker: the NPC proxy (by `NpcId`) or the host's puppet
  for V's lines; unresolved → type `OwnerlessRegular` (`SC §4.1`).
- **Who gets the line**: face-to-face lines only within earshot (25 m of
  the speaker, measured on the guest: it knows where the proxy is);
  holocall and phone lines (`scnDialogLineType.Holocall`, `:7398`) to
  everyone on the call (§3); radio and TV lines never (each game has its
  own).
- Text is in the host's language and gendered for the host's V. Resolving
  a line id in the guest's language needs the scene's subtitle resource
  (RE); with scene replay (step 3) the guest's own game produces its own
  lines in its own language. Decision 9 in §8.

### 2.3 What the guest's V does while the host is in a scene

From the host's tier (`PlayerPuppet.OnSceneTierChange`,
`final.reds:43746` [S]; `SC §1`), sent as part of that player's state:

| Host's tier              | Guest within 30 m of the host                                                                  | Guest further away           |
| ------------------------ | ---------------------------------------------------------------------------------------------- | ---------------------------- |
| 1                        | nothing                                                                                        | nothing                      |
| 2 walk-and-talk          | subtitles, hub; free                                                                           | a one-line HUD notice (§5.1) |
| 3 seated / limited       | weapons lowered (`GameplayRestriction.NoCombat`, tandem source, ref-counted)                   | notice                       |
| 4 first-person cinematic | `NoCombat` + walk speed + invulnerable (god mode, tandem source); no forced camera or teleport | notice + "join" offer (§5.1) |
| 5 cutscene               | as tier 4                                                                                      | as tier 4                    |

The host's V puppet plays its scene animation already (the pose stream
reads V after the animation job, ARCHITECTURE "Body poses" [V]), sitting,
gesturing, in the same workspot; the host's scene NPCs are proxies (walk
and idle only in the first version). Restrictions are ref-counted per
reason and removed by source name on any load (the fork's two lessons,
[K] `campaign-fork.md` §11). Fast-forward on the host is a world time
dilation (`SC §2.7`): host-only, guests' mirrored lines simply arrive
faster.

### 2.4 Dialogue with choices: who chooses

**The host's V chooses, guests take part.** The scene in the world that
counts is played with the host's V; its options (lifepath lines, attribute
checks, `Blueline`) are computed for the host's V, and the quest waits on
the host's selection (`questChoiceSection_ConditionType`, `SC §3.2`).
Anything else needs the host's game to select an option it did not pick
itself (input injection, step 3).

- **Host**: listeners on `UIInteractions.DialogChoiceHubs`,
  `ActiveChoiceHubID`, `SelectedIndex`, `LastAttemptedChoice`
  (`final.reds:195337-195347` [S]; `SC §3.1`); only the active hub counts
  (not hubs of NPCs one walks past, [K] fork). Sends the hub (title,
  options as text + type flags + icon ids, timed duration and start), the
  host's hover, and the pick (`LastAttemptedChoice`, as
  `PreventionSystem.OnPlayerChoice` reads it, `final.reds:141776`).
- **Guest** (within earshot, or on the call): the hub written to its own
  `UIInteractions.DialogChoiceHubs` (a stand-in hub id, as the fork did
  with 77000 [K]) and drawn by the vanilla `dialogWidgetGameController`
  (`final.reds:399949`, `UpdateDialogsData` `:399987`). Each player moves
  **their own** highlight (guest: input listeners on the choice scroll and
  apply actions, as the fork's stand-in did [K]); the other player's
  highlight shows as a small marker on that option (a wrap of
  `UpdateDialogsData`). A guest's apply marks a **suggestion** that the
  host sees on its own hub; only the host's apply commits. When the host
  picks, both hubs close and the scene goes on.
- Timed choices: the host's timer decides; the guest's stand-in hub has no
  native time provider (`SC §3.5`) and shows no bar in the first version.
- Later options (decision 1 in §8): hand the choice to a guest for one
  conversation ("your call"), applied by input injection on the host
  (Codeware `InkSystem_ProcessInputEvents` `673454961` [C], the fork's
  technique; its `RawInputData` stride is broken on 2.5 [K]); a timed vote
  (the fork's: 15 s, 5 s for timed dialogue [K]). Not in the first version.

### 2.5 Cinematic scenes: actors, cameras, voice

First version (no unproven parts): §2.2 + §2.3 + §2.4. The guest stays in
its own body and camera, sees the host's V perform, reads the lines, sees
the hub. No voice, no NPC gestures or lip-sync, no scene cameras. Honest
cost: by ARCHITECTURE's "seen and heard" rule a scene is not done until it
is heard.

Why there is no shortcut to voice: no script or RTTI function plays a
scene line's voice on an entity (`SC §4.2`; the RTTI dump has
`locVoiceoverMap` and `gameuiOnscreenVOPlayerController` classes but no
callable natives for them [R], and `gameGameAudioSystem.Play` takes Wwise
events, not lines [R]). Lip-sync is driven by the scene's line events.

**Step 3, scene replay** (needs spike S2): the host's `ExecuteNode` hook
sees each `questSceneNodeDefinition` it runs (scene resource hash, marker,
input socket; `SC §2.1`) and sends its address (`<resource path hash>/<phase
node ids>/<node id>/<socket>`, the fork's addressing [K]). The guest runs
that one node through the tandem bypass of §1.3. The guest's game then
plays the scene itself, **with the guest's own V in the V role**: it hears
the voices, sees lip-sync and cameras, in its own language and with its own
V's voice — each player is V in their own game, the usual convention of
co-op story games. Around it:

- **Actors**: scene actors found in the world or spawned by the scene
  exist locally on the guest for the scene's length; the host's proxy of
  the same NPC is hidden meanwhile and shown again (at the host's
  position) when the scene ends. Actors the guest's frozen world cannot
  provide (a community in another phase) → that scene falls back to the
  first-version presentation.
- **The other V**: on each game the other player's puppet is placed as an
  extra beside the scene (a free sit or stand spot, or a half circle
  facing the speaker; the fork's "extras" [K], found through Codeware's
  `worldStreamingSector.GetNodeSetup*` [C], not the fork's raw offsets).
- **Choices** in the guest's copy: the host's pick is applied to the
  guest's hub by option id (scene hash, choice node id, option index) via
  input injection; an option the guest's V lacks (lifepath) → the guest's
  copy takes the nearest available one and the quest still follows the
  host (the scene's output socket comes from the host).
- **Intimate scenes** (romance, sex, some braindances): never replayed for
  guests; a fade and a HUD line instead (a tandem list of scene hashes;
  the fork had a per-scene `hide` [K]). Decision 6 in §8.

### 2.6 Braindances

First version: the host's braindance (`PlayerPuppet.OnRewindableSectionEvent`,
`final.reds:45353`; `Braindance` blackboard, `SC §5.2`) holds guests within
30 m beside the host's chair (as tier 4), mirrors subtitles and shows a HUD
line ("<host> is in a braindance"). Later (step 5): the guest plays its own
copy of the rewindable section, its clock slaved to the host's
(`JumpRewindableSection`, `SetRewindableSectionPlaySpeed`, `SC §5.4`
candidate 2), so both can scan; clues stay host facts.

### 2.7 Quest-locked doors, areas and quest interiors

- **Doors and devices a host quest changes** (`QuestForceUnlock`,
  `QuestForceOpen`, …, `DoorControllerPS` `final.reds:134529`,
  handlers `:135528-135706` [S]) enter natively through
  `questDeviceManager_NodeType` nodes (`QW §7.2`). The host's hook sees
  the `questInteractiveObjectManagerNodeDefinition` node; step 2 replays it
  on the guest like Codeware's own `ToggleNode` executes world nodes
  outside their phase ([C] `CW src/App/World/WorldStateSystem.cpp:149-188`,
  proven for world-data nodes on 2.5, `QW §6.1`). Same path for node
  visibility and prefab variants. Doors opened **before** the guest joined
  are not replayed: the fallback is regroup (teleport next to the host).
- **Areas closed by quests** (boundaries, "turn back" zones) are per player
  and position-based (`QW §6.3`): nothing to sync.
- **Quest interiors** that exist only while their phase runs (Konpeki,
  All Foods, Clouds, Arasaka Industrial Park: [K] `nco-commits.md` §7) are
  missing on a frozen guest: it cannot follow the host inside. This is the
  largest gap after voice. CDPR replicated exactly this list
  (`questQuestsSystemReplicatedState.replicatedQuestPrefabs`, `QW §1`); the
  engine side is `questQuestPrefabsHandler` (size 0xf0, a
  `worldIQuestPrefabStateListener`) and `worldRuntimeSystemPrefabInstancing`
  [R]; no script API. Step 4: RE how a phase asks for its prefabs, then
  load the host's list on the guest. Until then: when the host's V
  enters such an interior the guest gets a regroup offer only once the
  host is out again, and a HUD line; never a teleport into missing
  geometry (test T9 lists which places are affected).

### 2.8 NPCs that must talk to "V"

In the host's world the story's V is the host's V: quest NPCs look at it,
address it, follow it, and its lifepath and choices are the ones the
lines reflect. On a guest's screen that NPC (a proxy) faces the host's
puppet, which is right: the guest watches a conversation it takes part in
through the hub. With scene replay the guest's own copy addresses the
guest's V; the quest still follows the host. Interactions that start a
story conversation ("Talk") exist only on the host's NPC: a guest cannot
start one in the first version (open item: forward a guest's "Talk" as a
request, which would pull the host's V into the scene — probably never).

### 2.9 Fast travel, teleports, elevators during quests

Host-led, as milestone 4 plans: the host's fast travel
(`FastTravelSystem.OnPerformFastTravelRequest`, `final.reds:50687`) brings
the guests (their own loading screen, `QW §7.5`); a guest's own fast
travel is locked while tethered (`AddFastTravelLock(n"tandem")`,
`:50764`). A quest's own fast-travel block (`BlockFastTravel`) is on the
host's V and blocks the host. A host quest teleport (`questTeleportPuppet`,
a position jump > 150 m outside a vehicle, the fork's rule [K]) regroups
guests that were near the host before it. Story elevators are devices:
ride along, or regroup on arrival (`SC §6.6`).

### 2.10 Johnny, flashbacks, cyberspace, endings

Not in the first version beyond the generic tier rule. Host as Johnny or
another replacer (`UI_PlayerStats.isReplacer`, `SC §6.1`): the host's
puppet must become that replacer (player sync work, step 5); flashbacks and
cyberspace are another place → guests held as spectators or regrouped
(`SC §6.1`, §6.3). Johnny as an NPC beside the host's V is a host NPC and
shows on guests' screens as a proxy (decision 10 in §8). Endings and the
point of no return are host checkpoints; guests keep their own V.

---

## 3. Holocalls, phone calls and messages

Calls and texts are most of the story's connective tissue (fixers' gigs,
Judy, Panam, Takemura), they are location-independent by nature, and the
vanilla code paths are small and scriptable. **Both players receive them,
in sync, from the host's quest progress.**

### 3.1 How a call works in vanilla

[S] unless noted:

1. The quest's phone node queues a `questTriggerCallRequest{caller,
addressee, callPhase, callMode, isPlayerTriggered, isRejectable,
showAvatar, visuals}` (`final.reds:241157`) to the `PhoneSystem`
   scriptable system (`class PhoneSystem`, `:240531`).
2. `PhoneSystem.OnTriggerCall` (`:240569`) plays `ui_phone_incoming_call`
   (or `ui_phone_initiation_call`) and calls `TriggerCall` (`:240618`),
   which writes `PhoneCallInformation` to the `UI_ComDevice` blackboard,
   applies the phone call restriction for player-triggered calls and
   writes the fact `phonecall_<caller>_with_player` = `Initializing`
   (`SetPhoneFact`, `:240841`; name built at `:241123`).
3. The HUD (`NewHudPhoneGameController`, `:408140`; listener
   `OnPhoneCall` registered `:408275`, handler `:408909`) shows the
   incoming call (`:409035`) and, at `StartCall`, the audio call or the
   holocall (`:409048`, `StartAudiocall` `:409117`, `StartHolocall`
   `:409120`; the widgets `HoloAudioCallLogicController` `:380945`,
   `IncomingCallLogicController` `:382409`).
4. The player answers or declines with `PhoneInteract` (release) /
   `PhoneReject` (hold) (`:408440-408495`) → `PickupPhoneRequest` →
   `OnPickupPhone` (`:240662`) stops the ringtone and writes the fact
   `Talking` (2) or `Rejected` (3). Unanswered: the HUD's timeout
   failsafe (`StartTimeoutFailsafe`, `PhoneTimeoutRequest`, `:240702`) ends
   it.
5. **The quest graph waits on that fact**, then runs the call's scene
   (category `holocalls`, `SC §2.2`): the caller's voice, the subtitles of
   type `Holocall`, choice hubs flagged `isPhoneLockActive` (`SC §3.6`).
   A holocall's picture is a live render of the caller's actor
   (`m_HolocallRenderTexture`, `:381041`), who stands in a quest prefab
   loaded for the call (`questHolocallStateListener`,
   `questPhonePrefabsListener`: `worldIQuestPrefabStateListener`
   subclasses [R]).
6. A call the **player** makes: the dialer's `CallSelectedContact`
   (`PhoneDialerGameController`, `:387580`) queues a
   `questTriggerCallRequest{caller = Player, addressee = contact}`; the
   quest listens to `phonecall_player_with_<contact>`.

NCO's facts for 2.31 hold the same shape ([K] `nco-commits.md` §7):
block calls at `OnTriggerCall`, not later (blocking at `SetCallInfo`
trapped the player in an invisible call); `isPlayerTriggered` means "the
player triggered this quest call", not a network player; the ringtone loops
until stopped.

### 3.2 Calls in co-op

- **Host**: a wrap of `PhoneSystem.OnTriggerCall` (after the original) and
  of `OnPickupPhone` sends `PhoneCall{contact, phase, mode, visuals,
flags, answeredBy}` on every phase change; kept for late joiners while a
  call is up.
- **Guest**: presents it **without its PhoneSystem's quest side**: writes
  `PhoneCallInformation` to its `UI_ComDevice` blackboard and plays the
  ringtone itself (NCO's 2.31 method, [K]), so no `phonecall_*` fact is
  written on the guest and nothing can wake its frozen quests after it
  leaves. Its `PhoneSystem` wraps (`OnPickupPhone`, `OnUsePhone`,
  `SetPhoneFact`) do nothing local while a mirrored call is up, and
  `ui_phone_incoming_call_stop` is played on every way out.
- **Who answers**: anyone. The guest's answer or decline goes to the host
  as `PhoneAction{answer | reject}`; the host's game queues the same
  `PickupPhoneRequest` its own HUD would (`:408455`), so its quest sees
  `Talking` or `Rejected` exactly as if the host had pressed the key; the
  first answer wins, the next `PhoneCall` carries `answeredBy`, every
  phone shows the call as picked up. The host's V gets the call
  restriction as in vanilla (it is the host's quest's call).
- **During the call**: everyone sees the call widget with the caller's
  name and portrait (avatar from the contact's record). In the first
  version a guest's widget shows **every call as an audio call with
  avatar** (`callMode = Audio`, `showAvatar = true`), never an empty
  holocall window: the holo picture needs the call's prefab and scene.
  Subtitles of type `Holocall` and the call's choice hubs go to everyone in
  the session, wherever they are (§2.2, §2.4: the host chooses; the one who
  answered is a candidate for "your call", decision 2). **Voice**: none in
  the first version; step 3a replays the call's scene node on the guest
  (no world actors, the caller stands in a prefab of its own: the safest
  first scene to replay), which also gives the real holo picture.
- **Calls a guest makes**: the guest's dialer shows the host's contacts
  (the journal mirror). `CallSelectedContact` on the guest is wrapped into
  `PhoneAction{dial, contact}`; the host queues the same
  `questTriggerCallRequest{caller = Player}` on its `PhoneSystem`; the
  host's quest decides whether the contact answers; the call comes back to
  everyone through `PhoneCall`.
- **The guest's own story calls** never ring: its quests are frozen
  (§1.3). That is also what keeps the Songbird-style hang away from guests
  ([K]).

### 3.3 Text messages, replies, attachments, contacts

Messages are journal entries: contact (`gameJournalContact`) →
conversation → message / choice group → choice entry (`QW §3.3`, §5.2).
**Mirroring the journal mirrors them**, with their native notifications:

- A host quest's `questSendMessage_NodeType` makes the message Active with
  `Notify`; on the guest the mirrored change reaches
  `NewHudPhoneGameController.OnJournalUpdate` (`final.reds:408827`), which
  for `gameJournalPhoneMessage` requires `Notify`, `Active` and change type
  `Direct` (`:408843-408848`) and pushes the SMS toast with its sound
  (`ShowNewMessage` `:409460`, `PushSMSNotification` `:409494`); a new
  contact pops its own toast (`:408852-408859`). Whether
  `ChangeEntryStateByHash` reports `Direct` is test T4 `[I]`.
- The messenger redraws from the same callbacks
  (`MessengerDialogViewController.OnJournalUpdate`, `:348795`); the
  "typing…" delay comes from the host's `StateDelay` callback (`:348814`)
  and has no setter: guests see the message appear without the typing
  dots (polish item: delay the guest's apply by the host's `delay`).
- **Replies**: a reply is the player activating a choice entry:
  `ActivateReply` (`:348893`) sets the other options Inactive and the
  chosen one Succeeded, both with `Notify`; the host's quest waits on that
  state. **Whoever replies first replies for the crew**: on a guest
  `ActivateReply` is wrapped to send `PhoneReply{choiceEntry}` instead of
  applying; the host checks the entry is still an open option and applies
  the same two state changes; the mirror brings the result back to every
  phone (one round trip, under 100 ms on a normal link). The host's own
  reply goes the vanilla way.
- **Opening a message** marks it Active / visited locally
  (`PhoneMessagePopupGameController.SetupData`, `:348239-348257`;
  `SetEntryVisited`): read state is per player and is not sent back.
- **Attachments**: a message's attachment is a journal path (a shard, a
  quest, a codex entry; `GetAttachmentPathHash`, `:348232`); the host
  activates it with the message, the mirror carries it. Opening it from a
  guest's phone shows the guest's mirrored copy.
- **Contacts and callability** (`questAddRemoveContact_NodeType`,
  `gameJournalContact.isCallableDefault`, `IsCallable`) follow the journal.

### 3.4 Cost

All of §3.2 and §3.3 except voice and holo picture is script work on
known vanilla functions plus three small messages: **in the first version**.

---

## 4. Rewards: quests done together

### 4.1 How the game pays a quest

[S]: the quest node `questGiveReward_NodeType{rewards}` sends a
`RewardEvent{rewardName}` to the player (`QW §12.2`);
`PlayerPuppet.OnRewardEvent` (`final.reds:43468`) calls
`RPGManager.GiveReward(game, rewardID)` (`:37406`, body from `:37415`),
which gives the **local** player the record's items, recipes, XP (scaled by
that player's level and XP bonuses, `CalculateMinorActivityReward`,
`CalculateStreetStoryReward`, `CalculateEP1Reward`), street cred, money and
achievements. The item, XP and money toasts come from the local inventory
and progression (`ItemsNotificationQueue`, `:339288`). The fixer's
payment text is a journal message (§3.3).

### 4.2 Each game pays its own V

- **Host**: wrap `OnRewardEvent` (after the original) → `QuestReward{reward,
quest}` to each guest that took part (§4.3). `quest` is the journal hash
  of the quest the reward belongs to (§4.3), for the guest's ledger.
- **Guest**: `RPGManager.GiveReward(game, reward)` on its own V: amounts by
  its own level and perks, its own toasts, into its own inventory, kept by
  the leave save (§1.4). Never totals copied between players.
- **Idempotent**: the guest keeps `(quest, reward)` pairs received in the
  session; a resend (late join, reconnect) is ignored.
- **Not rewards**: quest items (`questAddRemoveItem`: keys, shards the
  quest checks) stay with the host (`QW §12.3`); dev points and proficiency
  level-ups given directly by quest nodes are observed in the host's
  `PlayerDevelopmentSystem` handlers and forwarded the same way (step 2).
  Some fixer payments may be item nodes adding money rather than reward
  records `[I]`: the host's node hook can forward those as money (step 2,
  test T7).
- **Kill XP** is the killer's (ARCHITECTURE "Fights").

### 4.3 "Taking part"

Kept simple and generous: **a guest takes part in a quest if it is in the
session and within 150 m of the host's V when that quest completes, or
when any objective of that quest changed state during the session.**

- The host records participants per quest: on every journal change of an
  objective (walk up `GetParentEntry`, `final.reds:196679`, to its
  `gameJournalQuest`), each guest within 150 m is added to that quest's
  set; at completion (`Succeeded`) too.
- A `RewardEvent` is attributed to the quest whose journal entry changed
  last within 10 s before it (reward and journal nodes sit together at the
  end of a quest `[I]`); if none, to "nearby": guests within 150 m get it.
- A gig the host does while the guest is across the city: the guest
  receives the journal updates and the fixer's text, not the reward.
- Decision 3 in §8: whether a guest that later plays the same quest in its
  own save is paid again (proposed: yes, like replaying in another save;
  iconic items can end up twice).

---

## 5. Players in different places, and solo gigs

### 5.1 The host starts a scene while the guest is across the city

Today players are kept together (milestone 4/5: proxies only near the
host, soft tether). Even so, a guest may be 500 m away when the host's
story moves:

- **Journal, toasts, markers, calls, texts, rewards** (by §4.3): everyone,
  anywhere. A phone call is the natural way to share the story at a
  distance.
- **A conversation or scene in the world**: only guests within earshot
  (25 m) get subtitles and hubs; a far guest gets a one-line HUD notice in
  the game's own style (`UI_Notifications.OnscreenMessage` or a custom quest
  notification, `QW §5.5`): "<host>: <quest title>", and the host on the
  map through the co-op player pin (`RegisterRemotePlayerMappin`,
  `final.reds:45502-45508`, `QW §1`).
- **A tier 4/5 cinematic** with the guest further than 100 m: a "join"
  offer (an input hint, 10 s); accepting regroups the guest next to the
  host behind a loading screen (milestone 4's regroup). Never a forced
  teleport mid-play (decision 4).
- **Story triggers fire only for the host's V**: quest triggers and
  conditions in the host's game see only the host's V. A guest arriving
  first at a mission location starts nothing. Letting guests' positions
  count is part of region authority.

### 5.2 Solo gigs

The wish: one player takes a fixer gig, a side gig or an NCPD hustle alone
while the other does something else, and the world stays one.

**The host alone** can do that already: its own game runs every gig
around its V wherever it is; guests see the journal and the fixer's texts
(§4.3: no reward if they were not there).

**A guest alone** needs much more, because the host's game simulates only
what is streamed in around the host's V (ARCHITECTURE "Region limits"):

1. **Region authority**: around the guest, the guest's game must own the
   NPCs (spawn the gig's enemies, run their AI and fights) instead of
   showing host proxies, with handover when players meet. ARCHITECTURE's
   "per-player regions" step; it does not exist yet.
2. **The gig's quest on the guest**: the guest's game runs that gig's
   phase while the rest of its quests stay frozen: a per-phase exception in
   the freeze hook (by resource path), the host's facts mirrored in full
   (the gig's start conditions read them; step 2), and the guest's own save
   having that gig still open (its phase is the guest's own: a gig done in
   the guest's save cannot run there) `[I]`.
3. **Claim**: the host's game must not run the same gig meanwhile. A
   guest's gig start (its phase about to run) asks the host for a claim;
   the host grants it if it has not started that gig, and freezes the gig's
   phase in its own game for the claim's length (the same hook, host side).
   Both starting the same gig: the claim is first come, first served; the
   other's game keeps it frozen. A gig that is part of the host's current
   main quest is never claimable.
4. **Outcome into the host's world**: while it runs and at its end, the
   guest sends what the gig changed: its facts (from the fact hook on the
   guest, filtered to writes made while the gig ran), its journal states,
   deaths of the NPCs it owned, quest-relevant items taken. The host applies
   them to its world and save, releases the claim, and lets its own (frozen)
   phase of that gig see the outcome. Whether a host phase that waits at
   the gig's start then ends cleanly from the facts alone is unknown `[I]`;
   the alternative is to leave it frozen in the host's save until the host
   passes through its end condition. This is the riskiest part.
5. **Rewards**: native: the guest's own gig phase pays the guest's V. Its
   save keeps the gig done (consistent with ARCHITECTURE: "quest progress:
   no, unless it plays that quest in its own game" — here it did), so the
   leave restore of §1.4 must **keep** the facts and journal entries the
   gig owned instead of putting them back.

Cost: region authority (milestone-sized), full two-way fact sync, a claim
protocol, partial unfreeze, outcome merge, and an exception to the leave
restore. **Not the first version.** The simplest viable form, once
region authority exists: **fixer gigs and NCPD hustles only** (no main or
side quests, no cyberpsychos chained to Regina's story), started only by a
player more than 200 m from every other player, claimed through the host,
outcome merged at the end (not progressively), the host's copy of the gig
kept frozen until its own conditions close it. Recommended order: after
scene voice (step 3) and quest interiors (step 4), together with region
authority (decision 5).

---

## 6. The plan: first version and the steps after it

Ranked by value for cost. Every step is testable with one PC: the game in
one role, `tandem-peer` in the other, scenarios checking log lines
(`tools/scenario.sh`; a human only for what must be seen or heard,
noted per test).

| #   | Step                                                                                                                                  | Value                                              | Cost                                       |
| --- | ------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------- | ------------------------------------------ |
| S1  | spike: the freeze hook (return value, what stops, what keeps working)                                                                 | gates step 1                                       | one evening of RE + a probe                |
| 1   | **first version**: freeze, save lock and restore; journal, tracker, markers; calls and texts; subtitles and hubs; tier holds; rewards | the story is shared, safely, with native UI        | medium (mostly scripts, one native hook)   |
| S2  | spike: replay one holocall scene node on a solo game                                                                                  | gates step 3                                       | small                                      |
| 2   | facts (full, two-way-safe), world nodes (doors, visibility, variants), fades and videos, story restrictions, entity pins, dev points  | doors and HUD follow the story; fewer gaps         | medium (FactManager native, node replay)   |
| 3   | scene replay: 3a holocalls (voice, holo picture), 3b dialogue scenes (voice, lip-sync, cameras, extras, choice injection)             | the story is **heard**: the biggest immersion gain | high, and gated by S2                      |
| 4   | quest interiors on guests (phase prefabs)                                                                                             | guests can follow into mission interiors           | unknown (RE of `questQuestPrefabsHandler`) |
| 5   | braindance copy, replacers (Johnny), flashbacks; "shadow phases" if step 3 needs the phase context                                    | the special sequences                              | high                                       |
| 6   | solo gigs (with region authority)                                                                                                     | play apart                                         | high (§5.2)                                |

### Spike S1 — the freeze

- Disassemble the callers of `QuestPhaseInstance_ExecuteNode`
  (`tools/re/addr.py`, `asm.py`, `xref.py`) for the meaning of its `u8`
  result; write a guest-only hook behind a layout check.
- Probe on the solo game (no session): `tools/game.sh cmd quest freeze on`
  logs refused nodes per second by class; `quest freeze off`. Checks: a
  pending quest call never rings while frozen and rings after; the quest
  tracker does not advance; the metro, vendors, apartment, car summon:
  which still work (list them). Scenario `quest-freeze.sh` (`ensure host`
  is enough: no peer).

### Step 1 — the first version

**Messages** (appended to `proto::Message`; `kProtocolVersion` bumped;
names in `NameOf`; channels in `ChannelOf`; a round trip each in
`tests/protocol_test.cpp`):

```cpp
// Story, milestone 6 step 1. Host to guests unless noted; Event channel
// unless noted; all reliable.
struct JournalChange {
  std::uint32_t hash{};  // gameJournalPath hash, as GetEntryHash
  std::uint8_t state{};  // gameJournalEntryState
  std::uint8_t notify{}; // 1: the host's change notified (toast)
};
struct Journal {         // snapshot on Bulk (kept for late joiners), deltas on Event
  bool snapshot{};       // true: the whole list; entries not in it are Inactive
  std::vector<JournalChange> entries;
};
struct Tracked {
  std::uint32_t hash{}; // 0: nothing tracked
};
struct PhoneCall {        // the host's PhoneSystem; kept while a call is up
  std::uint64_t contact{}; // CName hash of the contact's journal id
  std::uint8_t phase{};    // questPhoneCallPhase
  std::uint8_t mode{};     // questPhoneCallMode
  std::uint8_t visuals{};  // questPhoneCallVisuals
  std::uint8_t flags{};    // rejectable, showAvatar, playerCalling, playerTriggered
  PlayerId answeredBy{};   // kNoPlayer until picked up
};
struct PhoneAction {      // guest to host: answer, reject, dial
  PlayerId player{};      // set by the host from the connection
  std::uint8_t action{};
  std::uint64_t contact{};
};
struct PhoneReply {       // guest to host
  PlayerId player{};
  std::uint32_t choiceEntry{}; // the gameJournalPhoneChoiceEntry picked
};
struct Subtitle {
  std::uint64_t line{};    // scnDialogLineData.id (CRUID)
  std::uint8_t type{};     // scnDialogLineType
  std::uint8_t flags{};    // show/hide, persistent
  float duration{};
  NpcId speaker{};         // 0: none or a player
  PlayerId speakerPlayer{};
  std::string speakerName; // host's language
  std::string text;        // host's language
};
struct ChoiceOption {
  std::string text;
  std::uint16_t types{};           // gameinteractionsChoiceType bits
  std::vector<std::uint64_t> icons; // TweakDB ids of caption icons
};
struct ChoiceHub {        // the host's active dialogue hub; empty options: closed
  std::uint32_t hub{};
  std::uint8_t flags{};   // EVisualizerDefinitionFlags, phone lock
  std::string title;
  std::vector<ChoiceOption> options;
  float timedSeconds{};   // 0: not timed
  std::uint64_t timedStartUs{};
};
struct ChoiceCursor {     // either way, relayed: hover, suggest, made
  PlayerId player{};
  std::uint32_t hub{};
  std::int8_t index{};
  std::uint8_t kind{};
};
struct StoryState {       // a player's story state, from their own game
  PlayerId player{};
  std::uint8_t tier{};    // GameplayTier 1..5
  std::uint8_t flags{};   // braindance, replacer, in a call
};
struct QuestReward {
  std::uint64_t reward{}; // TweakDB id of the RewardBase record
  std::uint32_t quest{};  // journal hash of its quest (0: unknown)
};
```

Sizes: none is high-rate; the journal snapshot is a few thousand 6-byte
entries (tens of KB) on Bulk. The handshake must refuse a guest whose
content differs (journal hashes depend on the game version and on Phantom
Liberty being installed, `QW §3.3`): `Hello.gameVersion` exists; add the
expansion flag (an existing message: bump).

**Host game**: `scripts/tandem/Story.reds` (journal and tracked callbacks,
the `OnTriggerCall` / `OnPickupPhone` / `OnRewardEvent` wraps, subtitle and
`UIInteractions` listeners, tier from `OnSceneTierChange`, the
participation sets of §4.3, applying `PhoneAction` and `PhoneReply` after
checking they still apply); the node hook in observe mode.

**Guest game**: the freeze hook on; save lock, baseline, restore and the
leave save/load (§1.4); journal and tracked apply; phone presentation and
the wraps of §3.2–3.3; subtitles and stand-in hub; tier holds (§2.3);
rewards through `GiveReward`.

**Natives**: freeze on/off, its state, the journal digest; each declared in
`scripts/tandem/Natives.reds` to match `plugin/src/natives.cpp` and checked
with `task natives:check`.

**Mock player**: `tandem-peer host --story FILE` plays a scripted host: a
journal snapshot and timed deltas, a call (incoming, then talking, then
ended), subtitles, a hub, a reward; the hashes come from the real game
(`tools/game.sh cmd story dump` writes the current journal and contacts to
a file) so they resolve. `tandem-peer join --record FILE` already records
what a host sends: extend it to the new messages; `--answer`, `--reply
HASH`, `--suggest N` make the peer act as a guest.

**Scenarios**:

- `story-guest.sh` (game as guest of `peer host --story`): expect `quest:
own quests frozen`, `save: locked (tandem)`, `journal: snapshot N
entries`, `phone: incoming call from`, `subtitle: shown`, `choice: hub
shown`, `reward: given`; then `pkill tandem-peer` and expect `journal:
restored, digest matches baseline`, `save: back from co-op`, `quest: own
quests resumed`. A human checks once: the toasts look and sound right,
  the ringtone stops, the call widget shows the portrait.
- `story-host.sh` (game as host, `peer join --record`): game commands make
  the host's own vanilla paths fire (`cmd story call <contact>` queues a
  `questTriggerCallRequest`; `cmd story journal <hash> <state>`; `cmd
story reward <record>` queues a `RewardEvent` on the host's V; `cmd story
line` and `cmd story hub` write test data to the blackboards); expect the
  matching messages in the peer's log; `peer --answer` → expect the host's
  log `phone: answered by player 2` and the `phonecall_*` fact = 2.
- `story-replay.sh`: once, record a real quest session as host (a human
  plays ten minutes of a gig with `peer join --record`), then replay it to
  the game as guest (`peer host --replay`) as a regression test.

### Spike S2 — one scene on a solo game

On the solo game, with the node hook logging scene node addresses while
the player plays (`quest trace on`): pick a holocall's scene node, then
`tools/game.sh cmd story replay <address>` executes it through the tandem
bypass (Codeware's recipe: lock, context, root phase, `ExecuteNode`, [K]
`campaign-fork.md` §2). Checks: the call rings or starts, the voice plays,
the holo picture shows, the scene ends and releases V's tier; then the same
with a face-to-face scene of a finished quest. A human listens. This
answers the fork's open question ("whether a scene node executed outside
its phase plays the scene", [K]) on the smallest case.

### Step 2 — facts, world nodes, presentation nodes

- **Facts**: host snapshot by walking the named `FactStore` and deltas
  from the `FactManager` vtable `+0x18` hook (`QW §2.1`; layout from
  Codeware [C], behind a check); a deny list (`QW §2.3`: per-player
  telemetry, retrofix flags, tutorials, `game_was_loaded`,
  `block_death_menu`, police, `phonecall_*`); `Facts{snapshot, [{id,
value}]}`. Guest applies by id; baseline and restore by the same walk.
  Gives the guest's HUD gates, `ForceEmptyHands`/`ForceSafeState` during
  scenes (`SC §7.4`), interaction prerequisites (`FactValuePrereq`,
  `final.reds:310458`).
- **World nodes** replayed on the guest: device manager (doors), world data
  (node visibility, prefab variants), crowd control; and presentation
  nodes: fades (`questSetFadeInOut_NodeType`), HUD videos, warnings and
  on-screen messages, countdown timers (`SC §4.3`, §4.4, `QW §5.5`).
  `QuestNode{address}` from the host's observing hook. Fallback for a node
  that fails: a tandem ink overlay for fades.
- **Story restrictions** of the host's V that are about the moment
  (`NoCombat`, `VehicleScene`, `NoTimeSkip`) near the host (`SC §7.6`).
- **Entity-bound quest pins** (§2.1) and quest dev points (§4.2).
- Tests: `story-host.sh` grows `cmd fact set` / `cmd story node <address>`
  cases; the guest scenario checks the facts digest round trip.

### Step 3 — scene replay

3a holocalls, 3b scenes (§2.5): `SceneStart{address, hostTimeUs}`,
`SceneEnd`; actor handover (hide the proxy, show the local actor);
extras; choice application by option id through input injection; the
intimate-scene list. Tests: S2 becomes `story-scene.sh` (replay a
recorded scene start from `peer host --replay`; expect `scene: playing
<hash>`, `scene: ended`, the proxies back); a human listens per scene type.

### Step 4 — quest interiors

RE of `questQuestPrefabsHandler` and `QuestPhaseContext.prefabLoader`
(`+0x30`, [K] `campaign-fork.md` §2) for a "load this prefab" entry;
`QuestPrefabs{[nodeRef, mode]}` from the host. Test: T9's list of places.

### Step 5 — the special sequences, and shadow phases

Braindance copy (§2.6), replacers and flashbacks (§2.10). If step 3 shows
scene nodes need their own phase instance to work (prefabs, context), the
guest instantiates the host's active phases as **shadow phases**
(`QuestPhaseInstance_Initialize` `3423879019`, `ExecuteGraph`, [C]) whose
nodes the freeze hook blocks except those the host's trace allows: the
limited lockstep of option E, for presentation only.

---

## 7. Risks and safety rails

| Risk                                                                                                                                | Rail                                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A guest save written with the host's journal or facts in it (its own story overwritten)                                             | two save locks (manager reason + `IsSavingLocked` override), queued autosaves dropped, Save/Load hidden, death menu blocked; the only save is the leave save, written after the restore digest matches the baseline (§1.4)                   |
| The guest's own quests run ahead because of mirrored state                                                                          | freeze before any mirror is applied; a guest whose freeze is unavailable gets no story sync                                                                                                                                                  |
| A guest's own quest soft-locked after the session (a quest NPC hidden for good, a door left open by the host's quest, a phone fact) | the leave save is followed by a **load**, which drops every non-persistent artifact; persistent levers (prevention lock, fast-travel lock) undone before the save; phone presentation on guests never writes `phonecall_*` facts (§3.2)      |
| The freeze hook returns the wrong value (a phase takes an output, ends, or the game crashes)                                        | spike S1 before step 1; layout check; hook active only in a session on a guest                                                                                                                                                               |
| The host's quest soft-locked by a guest's request (an answer to a call no longer ringing, a reply to a closed thread)               | the host applies `PhoneAction` / `PhoneReply` only when its own state still matches (ringing call from that contact; choice entry still an open option); guests never write host facts or journal directly; guests never trigger checkpoints |
| Host death or host load rewinds the world                                                                                           | host's death menu blocked in a session (revive flow, milestone 8); after a host load, guests restore to their baseline and re-snapshot as on a new join                                                                                      |
| Different game versions or DLC: journal hashes that do not resolve                                                                  | the handshake refuses (`Hello.gameVersion` + expansion flag); unknown hashes skipped and counted in the log                                                                                                                                  |
| Double or farmed rewards                                                                                                            | rewards only from the host's `OnRewardEvent`; `(quest, reward)` idempotency per guest per session                                                                                                                                            |
| The guest's own Songbird-style holocall hanging the game (NCO's 120 s freeze)                                                       | frozen quests never ring; the host's calls reach guests as blackboard presentation, not as the guest's own call scene (until step 3a replays it deliberately)                                                                                |
| A crash loses the guest's session progress                                                                                          | accepted in step 1 (its quest state is safe); later a periodic safe checkpoint                                                                                                                                                               |
| Scene replay (step 3): two copies of an actor, a guest V standing in the host's seat, intimate scenes                               | local actor shown, proxy hidden for the scene; the other V placed as an extra; a no-replay list                                                                                                                                              |
| Solo gigs (step 6): two games finishing one gig, outcomes that conflict                                                             | host-granted claims; gigs only; the host's copy kept frozen; outcome applied by the host only                                                                                                                                                |

---

## 8. Decisions needed from the user

1. **Who chooses in conversations**: the host's V decides, guests suggest
   (proposed); or a timed vote; or the one who started or answered it
   (needs input injection on the host: step 3).
2. **Calls**: anyone may answer for the crew (proposed), or host only. And
   in a call a guest answered, does that guest make the call's choices
   (later)?
3. **Rewards**: the 150 m "taking part" rule (proposed) or session-wide;
   paid again when a guest later plays the same quest in its own save
   (proposed: yes).
4. **Apart during a cinematic**: a join offer the guest may refuse
   (proposed), a forced regroup, or nothing.
5. **Order of the big steps**: scene voice (step 3) before quest interiors
   (step 4) before solo gigs (step 6), as proposed?
6. **Intimate scenes** with replay: never shown to guests (proposed).
7. **Leaving a session reloads the guest's game** (a loading screen) so
   its own story comes back clean (proposed), instead of restoring in
   place.
8. **Shards and codex**: per player (proposed) or shared.
9. **Subtitles in the host's language** for the first version (both
   players usually share one), resolved per guest later with replay.
10. **Johnny next to the host**: shown to guests as a proxy (simplest), or
    hidden (he is in the host's V's head).

---

## 9. In-game tests owed

| #   | Question                                                                                                                          | How                                               |
| --- | --------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------- |
| T1  | `ExecuteNode`'s result: which value makes a phase wait; does refusing every node freeze cleanly; what systemic content stops      | spike S1                                          |
| T2  | The leave save then load: does the guest's own story resume exactly (tracker, pending calls, its active mission)                  | `story-guest.sh` + a human checks the journal     |
| T3  | `ChangeEntryStateByHash` on a frozen guest: tracker entry, toasts, quest map pins created; parents before children                | `story-guest.sh` with real hashes                 |
| T4  | Mirrored message: is the change type `Direct` (the SMS toast depends on it, `final.reds:408846`)                                  | `story-guest.sh`, a human sees the toast          |
| T5  | Phone presentation by blackboard only on 2.50: ring, answer key, widget, portrait, ringtone stop on every exit                    | `story-guest.sh`, a human listens                 |
| T6  | Host side: does a `PickupPhoneRequest` queued by tandem start the host's call scene as a key press does                           | `story-host.sh` with a real pending call (a save) |
| T7  | Rewards: `GiveReward` amounts on a guest for a quest it never ran (`QW §14` q31); are fixer payments reward records or item nodes | `story-host.sh` + record a gig's end              |
| T8  | Stand-in dialogue hub on a guest: drawn, caption icons, own cursor and the partner marker, stays up                               | `story-guest.sh`, a human looks                   |
| T9  | Which mission places are quest prefabs (missing on a frozen guest)                                                                | the freeze probe while walking to them, solo      |
| T10 | Scene node replay outside its phase: voice, picture, end of scene, V's tier released                                              | spike S2, a human listens                         |
