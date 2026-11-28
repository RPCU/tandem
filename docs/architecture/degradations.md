# No silent fallback

The rule (the user, 2026-11-22: "we shouldn't have silent failure"; the sync
must be reliably the intended one): whatever runs on a lesser path than the
intended one **registers it**, and the registry is what the player, the
other players, the log and the tests read. A session that runs degraded
without both players knowing, or a test that passes on a fallback path, is a
failure.

Code: the catalogue and the registry `core/include/tandem/degraded.hpp`
(`core/src/degraded.cpp`, `tests/degraded_test.cpp`); the game's one registry
`plugin/src/degradations.{hpp,cpp}`; the scripts' natives
`plugin/src/degraded_natives.cpp` (`DegradedNatives.reds`); what the player
sees `scripts/tandem/Degraded.reds`; the tools `tools/degraded.py`.

## What registers

- A **state** holds for a while: an engine feature this game version turned
  off (`engine-access.md`: `engine::FeatureOff` registers the feature under
  its own name), a path `tandem.ini` switched off, a path the scripts found
  not to work. Listed while it holds, with its first reason.
- A **fallback** is taken for one body, once (a prompt drawn instead of the
  game's own, a vehicle teleported instead of moved): counted, with its last
  reason.
- **Core**: co-op cannot be right without it. **Degraded**: it works, not as
  intended. A fallback is never core.
- **Chosen**: the state is the player's own `tandem.ini` switch (`pose`,
  `quest_freeze`, `native_prompts`). No failure, still
  listed and told, and never hiding a real one: a state that is off for a
  reason nobody chose replaces a chosen one, not the other way round.

One call each, from any thread (they take one lock of their own and write a
log line; a hook on a job thread may call them):

```swift
TandemDegraded.State("vehicle move", "the vehicle layout check failed");  // until Over
TandemDegraded.Chosen("quest freeze", "tandem.ini quest_freeze = off");
TandemDegraded.Over("vehicle move");
TandemDegraded.Fallback("prompt drawn", s"\(label): no prompt for 3 s"); // one body, once
```

```cpp
Degradations::Get().Start("scene privacy", reason);   // End(id)
Degradations::Get().Fallback("no room", reason);
```

A new id is a row of the catalogue (`core/src/degraded.cpp`, appended), a
text in `Text.reds` in English and French (`Tandem-Degraded-Id-<id with
dashes>`) and a row of the table below: `task degraded:check`
(`tools/degraded.py lint`, in CI) refuses an id used and not in the
catalogue, one registered as the other kind, one without its texts or its
row, and one nothing registers. An id the catalogue lacks at run time is
still listed, as `unknown id` (and fails every scenario).

## What follows from an entry

|               | Core state                                                                                                                             | Degraded state                                                                                                           | Chosen state                     | Fallback                                            |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ | -------------------------------- | --------------------------------------------------- |
| Session       | **none**: nothing is hosted or joined                                                                                                  | runs                                                                                                                     | runs                             | runs                                                |
| Log           | error when it starts                                                                                                                   | warning when it starts                                                                                                   | warning                          | warning the first time of an id, debug after        |
| This player   | the Co-op page's popup and the notice `Session-CoreOff`; in a session: `Degraded-Off`, then the session is left (`Degraded-CoreEnded`) | notice `Degraded-Off` at its start and at each session's start                                                           | notice `Degraded-Chosen`         | notice `Degraded-Fallback`, the first time of an id |
| Kept visible  | the Co-op page lists only "Write a bug report"                                                                                         | the status panel: "Runs without: …"                                                                                      | "Runs without (your setting): …" | "Fell back N time(s): …"                            |
| Other players | (no session)                                                                                                                           | notice `Degraded-Other` ("NAME's game runs without: …"), a status line while it holds, `Degraded-OtherBack` when it ends | the same                         | not told                                            |
| A scenario    | fails                                                                                                                                  | fails unless `expect_degraded ID` / `allow_fallback ID`                                                                  | the same                         | fails unless `allow_fallback ID`                    |

- **The log**: `degraded: ID is off: REASON` (`is OFF, co-op cannot run
without it` for a core one, `is off by choice`), `degraded: ID is back`,
  `degraded: fallback ID: REASON`, `degraded: player N's game runs without:
A, B (chosen)`; one line when a session is entered and one when it ends,
  `session start: degraded: none` or `session end: degraded: looks, vehicle
move (chosen); fallbacks: prompt drawn x3` (a warning unless `none`; kept
  in `last-session.txt` for the bug report).
- **Core off, no session**: `Runtime::Problems` gets a sentence per core
  state that holds and nobody chose (`degraded::CoreProblem`), so
  `Runtime::Blocked` is true by the same path as a game without Phantom
  Liberty (`release.md`, "Co-op off"): `Runtime::Choose` refuses, the Co-op
  page shows its popup (`TandemDegradedSay.CoreText`, in the player's
  language: `Degraded-CoreOff`, naming the feature and the reason) and only
  "Write a bug report". A session `tandem.ini` chose is dropped at the first
  frame's check (`Runtime::RecheckChoice`) and the player reads
  `Session-CoreOff` when their V is in the game. A core feature found off
  while a session runs (the quest freeze at its first use) is said and the
  session left (`TandemDegradedSay.Tick`, `CoopLeaveSaying`). The process
  never ends for it. `compat = off` (developers) ignores the block as it
  does the others: the game then hosts or joins, and tells everyone.
- **The other players**: `proto::Degraded` (appended, protocol 19, Event
  channel, the combat path as `CarPaint`): a player and the states that hold
  in its game, each an id and two flags (`kDegradedChosen`,
  `kDegradedCore`), sorted by id, 32 at most. Each game says its own from
  its join and at every change (`NetThread`: `Registry::Wire`, sent when it
  differs from what was last said); the host stamps who, tells every other
  game (never back) and keeps everyone's for late joiners and resyncs
  (`Endpoint::Degradations`, `SendDomainsTo`); a repeat is applied nowhere;
  an empty list is kept nowhere; forgotten when the player goes. Not kept in
  a session recording (`play::KeepsSession`). The version was bumped though
  the message is appended: a game of 18 would stay silent about what it
  runs without. Tests: "what a game runs without survives encoding…"
  (`protocol_test.cpp`), "…reaches everyone once, late joiners too, and goes
  with it" (`progress_test.cpp`), "referee: what a game runs without is its
  own word, passed on, never back" (`referee_test.cpp`).
- **On screen** (`Degraded.reds`): `TandemDegradedSay.Tick` every tick of a
  session shows what `TandemDegraded.TakeNews` queued (a state that started
  or ended, the first fallback of an id, another player's game), one
  notice at a time: entries of one kind about one game in one sentence, the
  next 6 s later (the HUD's line shows one message); the states
  that hold are queued again when a session is entered. Its notices are
  always shown (`TandemText.Importance` 2, whatever `notices` says). The
  status panel (`TandemCoop.StatusLines`, the pause menu's Co-op page and
  `cmd coop status`) keeps a line per entry, solo too.

## Why these are core

A feature is core when a session without it shows the other player
something that is not what happens, with nothing the player can do about it:

- **puppet movement**: remote players and NPC proxies stand where they
  spawned. Nothing of a shared world is left.
- **pose stream** (when `pose = on`): no body pose is read or shown; every
  puppet slides in its stock stance. `pose = off` is the documented lesser
  mode (stock animations), a choice.
- **quest nodes** (when `quest_freeze`, `scene_privacy` or `scene_replay`
  asks for the hook): no quest node is seen, so no freeze, no private
  scene, no replay, no phase notes: the story sync has nothing to stand on.
- **quest freeze** (when `quest_freeze = on`): a guest's own quests run
  beside the host's, which changes its own save's story. A host needs no
  freeze, but a game whose quest layouts differ needs another tandem either
  way, and one rule for both roles is the one the player can read.
- **The protocol** is core by the handshake: a guest of another version is
  refused in words (`release.md`, "At a join"). **The natives** are checked
  before a build leaves (`task natives:check`, CI) and the scripts' and
  build's fingerprints are compared at a join (`Install`, a warning); a
  check at run time of the installed scripts against the DLL is not done
  (open problem below).

Degraded, not core: **vehicle move** (teleports still show the car where it
is), **looks** and **look keys** (stand-in bodies), **quest prefabs** (the
guest is told `Prefabs-Missing` and watches from outside), the rest of the
table.

## The ids

`tools/degraded.py ids` prints the catalogue. Engine features keep their
names of `engine-access.md`.

<!-- prettier-ignore -->
| Id | Class | Kind | Registered by | The lesser path |
| --- | --- | --- | --- | --- |
| `puppet movement` | core | state | `engine/hooks.cpp` (layout, hashes) | remote players and NPC proxies do not move |
| `pose stream` | core | state | `engine/posestream.cpp`; chosen: `pose = off` | no body pose read or shown |
| `quest nodes` | core | state | `engine/quests.cpp` | no quest node seen: no freeze, no scene privacy or replay |
| `quest freeze` | core | state | `engine/quests.cpp`; chosen: `quest_freeze = off` | a guest's own quests run beside the host's |
| `clip lengths` | degraded | state | `engine/animation.cpp` | a puppet's gait keeps its default pace |
| `looks` | degraded | state | `engine/look.cpp` | nobody is dressed: stand-in bodies |
| `look keys` | degraded | state | `engine/look.cpp` | our own look is not read or sent |
| `third-person mark` | degraded | state | `engine/access.cpp` | a puppet's clothes may stay first-person ones |
| `quest prefabs` | degraded | state | `engine/prefabs.cpp` | a guest does not see the host's quest interiors |
| `workspots` | degraded | state | `engine/workspots.cpp` | an NPC's workspot is not read |
| `physics watch` | degraded | state | `engine/physics.cpp` | a long physics pause is not watched |
| `transform guard` | degraded | state | `engine/guards.cpp` (Wine) | the engine's own crash on an expired entity is not guarded |
| `dismemberment guard` | degraded | state | `engine/guards.cpp` | the engine's own crash on an NPC detached in the middle of an action is not guarded (`../research/engine/re.md` §13.142) |
| `slide guard` | degraded | state | `engine/guards.cpp` | the engine's own crash on an NPC proxy seated in a vehicle is not guarded (`../research/engine/npcs.md` §13.154) |
| `dispose gate` | degraded | state | `engine/guards.cpp` | the engine's own crash on a car removed while the world detaches is not guarded (`../research/engine/vehicles.md` §13.157) |
| `anim trace` | degraded | state | `engine/trace.cpp` (asked for and off) | no animation trace, no mirror |
| `vehicle move` | degraded | state | `engine/vehiclemove.cpp` (layout, hash); `System.reds` `MovePath` (5 bodies and none followed); when asked for (`vehicle_move = kinematic`, the default, or `velocity`) | the engine's move asked for, unavailable: vehicles are teleported every frame |
| `crowd spawners` | degraded | state | `engine/crowd.cpp` (code, vtable) | where another game runs the street, our own traffic and parked cars still spawn there and are removed out of sight (`../research/engine/traffic-police.md` §13.158) |
| `ragdoll state` | degraded | state | `engine/ragdoll.cpp` (code, each component's vtable slot) | bodies are neither read nor laid as their owner's lie: each copy lies as its own fall left it (`../research/engine/presence.md` §13.162) |
| `body slots` | degraded | state | `engine/slots.cpp` (layout) | a puppet's body gets no grapple slot: a grappled NPC is held in front of it, teleported each tick (`grapple held`) |
| `drawn prompts` | degraded | state | `Prompts.reds` (`prompts.tweak` not loaded; the test command `prompts off`); chosen: `native_prompts = off` | prompts on players and vendors drawn by tandem |
| `attitude relations` | degraded | state | `Attitudes.reds` `Declared` (did NOT take; "in part" is the save's run-time changes, carried by the runtime copy) | attitudes copied agent by agent (`npcs.md`) |
| `scene privacy` | degraded | state | `engine/scenes.cpp` | every scene counts as important |
| `interaction casts` | degraded | state | `engine/casts.cpp` | every interaction counts as casting its doer |
| `scene voice` | degraded | state | `story_natives.cpp` | V's voice in a replayed scene is the game's pick |
| `nameplate projection` | degraded | state | `PartyHud.reds` (no carrier on the HUD, no slot; `presence hud projection script`) | nameplates placed by our own projection, not the engine's (`presence.md`) |
| `prompt drawn` | degraded | fallback | `Prompts.reds` (`Adopt`, `Fallback`), `Body.reds` | one body's prompt is drawn |
| `vehicle teleported` | degraded | fallback | `System.reds` `MoveBody` (a body that does not follow, a move refused), `Vehicles.reds` | one vehicle teleported, not moved |
| `street body respawned` | degraded | fallback | `Traffic.reds` `Check` | a street vehicle's body was gone, spawned again |
| `proxy respawned` | degraded | fallback | `Npcs.reds` (`Check`, `Respawn`) | a proxy lost its body or did not wake up |
| `npc state polled` | degraded | fallback | `NpcAnims.reds` `Sample` | a change the events missed, caught by the 2 s poll |
| `loot not shared` | degraded | fallback | `Loot.reds` `Drop` | items dropped the game's usual way |
| `record substituted` | degraded | fallback | `plugin/src/mods.cpp` (every substitute the resolver decides), `Npcs.reds` (an attack), `Loot.reds`, `Paint.reds`, `StatusVisuals.reds`, `Trade.reds` | another game's record unknown here (`mod-compat.md`) |
| `look not received` | degraded | fallback | `System.reds` `StandIn` ("no look received") | a stand-in body; the mock player's case (allowed in every scenario) |
| `stand-in look` | degraded | fallback | `System.reds` `StandIn` (a look that has no body, still no look) | a stand-in body, or hidden |
| `partner call` | degraded | fallback | `Holocall.reds` (`PartnerContact` null: `gameJournalContact` not as measured) | a partner's link request rings as a notice and hints, not as the phone's own call (`comms.md`, "The call on screen") |
| `wanted stars` | degraded | fallback | `PartyHud.reds` `Wanted` (the game's wanted bar not found on the HUD) | a wanted partner's level written in its party entry, not drawn as the game's stars (`police.md`) |
| `corpse pose` | degraded | fallback | `Corpses.reds` (the owner: the body's ragdoll not read; a copy: its ragdoll not laid, another rig or none) | the copy lies as its own ragdoll left it (`regions.md`, "Corpses") |
| `body incomplete` | degraded | fallback | `Body.reds` (no donor, no visible object, no Character extension) | the puppet glides, or NPCs do not see it |
| `workspot not entered` | degraded | fallback | `NpcAnims.reds` `Step`, `Enter` (no component, the resource refused) | the proxy stands |
| `workspot unread` | degraded | fallback | `NpcAnims.reds` `NoteUnread` (owner: in a workspot whose instance has no resource, §13.134; or seated 3 s in a vehicle not shared, its seat unsent) | its copies stand where it sits |
| `scene camera missing` | degraded | fallback | `Scenes.reds`, `Spectate.reds` | our own eyes stay |
| `carried by placement` | degraded | fallback | `Spectate.reds` | our V placed in the car a few times a second |
| `revive ended by hand` | degraded | fallback | `Revive.reds` `CheckUp` | the death effects ended by hand |
| `race watched` | degraded | fallback | `Races.reds` `Place` | not in the race car in time: we watch |
| `v slowed as entity` | degraded | fallback | `Time.reds` `SlowSelf` | our V slowed as an entity |
| `radio not matched` | degraded | fallback | `Radio.reds` | a car's radio stays on another station |
| `device not matched` | degraded | fallback | `Devices.reds` `Look` | a device stays as ours has it |
| `police not spawned` | degraded | fallback | `Police.reds` `TakeSpawn` | units on foot fill in |
| `quest signals dropped` | degraded | fallback | `engine/quests.cpp` | signals held for a frozen story are lost |
| `no room` | degraded | fallback | `runtime.cpp`, `engine/posestream.cpp` | no slot for another puppet, pose or rig |
| `puppet not driven` | degraded | fallback | `engine/hooks.cpp` | a puppet without a move component |
| `hit shown by twitch` | degraded | fallback | `HitsTaken.reds` (the pipeline played nothing, or chose an AI task) | a hit a V took shown as the body's twitch (a hit by nobody the games know twitches by design: not registered) |

Looked at and left out, each for its reason: a rule that drops a message (a
hit with friendly fire off, a stale or repeated message, an authority
refusal) is the design, not a fallback; a seat taken or a fast travel point
missing picks another legitimate place (`Revive.reds` "beside it instead",
`Travel.reds` "teleporting"); what already has its own notice and status
line and no lesser path (`Story-NotRestored`, `Prefabs-Missing`); display
defaults of the party HUD (a field of view of 0, a health below 0).

## Tests

- **Every scenario** (`tools/scenario.sh`, `no_silent_fallback` at its end):
  `tools/game.sh degraded` asks the game (`cmd degraded`) for its registry,
  which must hold no state and no fallback the scenario did not declare, and
  the mock player's log must hold no `degraded: player N's game runs
without: ID` for an undeclared id. `test reset` forgets the fallbacks
  counted before the scenario (`TandemDegraded.ResetFallbacks`); states
  hold. A failure reads `degraded: unexpected: ID (…)` and is classed
  `degraded` by `tools/sweep.sh`.
- **Declaring**: `expect_degraded ID` (the scenario runs without ID on
  purpose: it must be in the registry at the end) and `allow_fallback ID`
  (it may be), anywhere in the scenario, ids of the catalogue
  (`task degraded:check`). Allowed everywhere: `look not received` (the mock
  player sends no look unless `--look`).
- Declared today: `prompts-drawn` (`expect_degraded "drawn prompts"`);
  `prompts-game` (drawn for a while),
  `regions-meet` (`proxy respawned`), `scene-ride-full` (`carried by
placement`), `puppet-attitude-fresh` (`attitude relations`),
  `nameplate-projection` (`nameplate projection`, asked for by command),
  `combat-melee-guest`, `npcs-hurt-guest`, `mods-unknown-held`,
  `mods-unknown-look`, `mods-unknown-vehicle`, `presence-status` (`record
substituted`).
- `tools/game.sh degraded [--expect ID]… [--allow ID]…`: the registry of a
  running game, printed; exit 1 on an entry that is neither, or on an
  expected one that is missing. `tools/game.sh engine` stays (the engine
  features alone, from the startup line).
- The mock player logs what each game tells it
  (`degraded: player N's game runs without: …`); the referee checks the
  message (`Fits`, never our own back, a known player).

## Open problems

- Not yet run in game: none of it. First: a normal start on 2.50 must say
  `session start: degraded: none`; then `task sweep`, whose `degraded`
  failures are the list of fallbacks the scenarios took without anyone
  knowing (expected candidates: `attitude relations` if the declared
  relations do not take on 2.50, `npc state polled`, `body incomplete` at a
  first spawn, `vehicle teleported` for a body the move refuses). Each is
  either fixed, or declared in its scenario with the reason.
- A check at run time that the installed scripts' natives are the DLL's
  (a stale `r6/scripts/tandem` beside a new DLL): `natives:check` runs
  before a deploy and a package, not on the player's machine.
- The texts of the ids were not read in game (lengths in the notice line,
  the French).
- A notice per first fallback may be too many on a bad day; the count is in
  the status panel either way.
