# Co-op campaign and world state — what scripts and RTTI expose (game 2.50)

Everything a future implementer needs to make guests follow the host's
campaign and world: facts, journal, phone and notifications, quest runtime,
persistent world state, time and weather, police, crowds, saves and rewards.
It extends [`engine-knowledge.md`](engine-knowledge.md) §7 and §9 (what the
campaign fork and NCO did); it does not repeat them.

**Status: desk research, 2026-11-18. Nothing here has been tested in co-op.**
Every item is a script or RTTI fact read from the sources below, tagged
`[2.5-rtti]` (present in the live RTTI dump of 2.50), `[2.5-script]` (present
in the decompiled 2.50 scripts) or `[2.5-CW]` (Codeware's source for 2.5).
Sentences marked _(inferred)_ are conclusions, not observations. Promote a
fact to `[2.5-verified]` in `engine-knowledge.md` once it is checked in game.

## Sources

<!-- prettier-ignore -->
| Cited as | What |
| --- | --- |
| `final.reds:LINE` | decompiled vanilla scripts, `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` |
| `rtti: Class` | `tools/re/run tools/re/rtti.py class Class` on `.cache/re/rtti.json` (live dump, game 2.50) |
| `@ 0x14…` | native handler address from that dump (image base as shipped; Proton loads at preferred base) |
| `CW path` | Codeware source (MIT), `~/.cache/tandem/ref/codeware`, checkout `60c4ab5` (2026-11-14) |
| `names.txt` | `.cache/re/names.txt`, the game's CName pool |
| `§7`, `§9`, `§11` | sections of `engine-knowledge.md` |

Reading the RTTI dump:

- It was taken on the player's modded game: classes of other mods are in it
  (`Torrent.*`, `CBP_*`, `CazanuLimbSurvival.*`, `LoadBegone.*`, Codeware's
  additions). A name being in the dump does not make it vanilla. When a
  function is in the dump but not in `final.reds`, it comes from a plugin;
  the ones used below are Codeware's and say so.
- `native, no handler` in `rtti.py` output means the function is flagged
  native but the dump found no handler pointer. All **static** natives
  (`GameInstance.Get…`, `GameTime.*`, `PersistentID.*`) and all Codeware
  natives print that way: it is a dump limitation, not a missing function.
- Handler addresses are for reverse engineering (`asm.py`, `xref.py`), not for
  calling: by AGENTS.md, call by name (RTTI / redscript) first.

## Contents

1. [What the engine itself meant to replicate](#1-what-the-engine-itself-meant-to-replicate)
2. [Facts](#2-facts)
3. [Journal](#3-journal)
4. [Objectives on the HUD: mappins](#4-objectives-on-the-hud-mappins)
5. [Phone, messages, shards, codex, notifications, tutorials](#5-phone-messages-shards-codex-notifications-tutorials)
6. [Quest runtime: nodes, spawns, teleports, prefabs, entity references](#6-quest-runtime-nodes-spawns-teleports-prefabs-entity-references)
7. [Persistent world state: devices, loot, vendors, fast travel, stash](#7-persistent-world-state-devices-loot-vendors-fast-travel-stash)
8. [Time of day and weather](#8-time-of-day-and-weather)
9. [Police: the prevention system](#9-police-the-prevention-system)
10. [Crowds and traffic](#10-crowds-and-traffic)
11. [Saves and checkpoints](#11-saves-and-checkpoints)
12. [Progression and rewards](#12-progression-and-rewards)
13. [Summary table](#13-summary-table)
14. [Open questions needing an in-game test](#14-open-questions-needing-an-in-game-test)

---

## 1. What the engine itself meant to replicate

The shipped game still carries the state classes of CDPR's cancelled
multiplayer. They are **not usable as a transport** (nothing drives them),
but they are CDPR's own answer to "what does a host send for this system",
and they match the authority table in `ARCHITECTURE.md` closely. `[2.5-rtti]`

<!-- prettier-ignore -->
| System (`: gameIReplicatedGameSystem`) | Replicated-state class | Fields |
| --- | --- | --- |
| `questQuestsSystem` | `questQuestsSystemReplicatedState` | `replicatedQuestPrefabs: [questQuestPrefabsEntry{nodeRef: worldGlobalNodeRef, loadingMode: worldQuestPrefabLoadingMode}]` |
| `gameJournalManager` | `gameJournalManagerSharedState` | `entryData: [gameJournalSharedStateData{pathHash: Uint32, entryState: gameJournalEntryState}]`, `trackedQuestPath: Uint32` |
| `gameTimeSystem` | `gameTimeSystemReplicatedState` | `paused: Bool`, `gameTime: GameTime` |
| `gamemappinsMappinSystem` | `gamemappinsMappinSystemReplicatedState` | `mappinState: [gameNewMappinID]`, `mappinWithJournalState: [Uint32]` |
| (inside the mappin system) | `gamemappinsQuestMappinManagerReplicatedState` | `dynamicQuestMappinRepInfo: [{journalPathHash: Uint32, entity: whandle:entEntity}]` |
| `questWorldStateSystem` | `questWorldStateSystemReplicatedState` | `nodeVisibilityMapArray: [{globalNodeRef, visible}]`, `isInMirrorsAreaMapArray`, `nodeCollisionMapArray`, `prefabVariants: [{…, variantNameKey, show}]` |
| `gameGodModeSystem`, `gamePingSystem` | (`gamePingSystemReplicatedState`) | — |
| devices | `DoorReplicatedState`, `SecurityTurretReplicatedState`, `WindowBlindersReplicatedState` (`: gameDeviceReplicatedState`) | — |

Reading: CDPR replicated the **journal as (path hash → state) pairs plus the
tracked entry**, time as **(paused, seconds)**, the world as **quest prefabs
loaded, node visibility, node collision and prefab variants**, and quest
mappins as **(journal path hash → entity)**. Facts have no replicated state:
_(inferred)_ in their design guests never evaluated quest conditions, which is
tandem's design too. The same shapes are proposed as tandem's messages below.

Also left over in scripts: `PlayerPuppet.RegisterRemoteMappin()` using
`MappinSystem.RegisterRemotePlayerMappin(data, playerObject)` with
`t"Mappins.CPO_RemotePlayerMappinDefinition"` and
`gamedataMappinVariant.CPO_RemotePlayerVariant` (`final.reds:45502-45508`) —
a ready-made minimap/world marker for the other players (milestone 8);
`questMultiplayerTeleportPuppetNodeDefinition` with
`teleportAllPlayers: Bool` (`rtti: questMultiplayerTeleportPuppetParams`);
and `gamePlayerSpawnParams{spawnPoint, recordId, gender, spawnTags,
useSpecifiedStartPoint, isSpectator, nickname}`.

---

## 2. Facts

### 2.1 API

`QuestsSystem` (`final.reds:46537-46555`, RTTI name `questQuestsSystem`, size
0x300, `GameInstance.GetQuestsSystem(game)` at `final.reds:14212`):

<!-- prettier-ignore -->
| Function | Handler | Notes |
| --- | --- | --- |
| `GetFact(factName: CName) -> Int32` | `0x1402b0b68` | unset fact reads 0 |
| `GetFactStr(factName: String) -> Int32` | `0x14184d134` | for names built at run time |
| `SetFact(factName: CName, value: Int32)` | `0x140e35ce8` | |
| `SetFactStr(factName: String, value: Int32)` | `0x141907130` | |
| `RegisterListener(factName: CName, listener: IScriptable, funcName: CName) -> Uint32` | `0x140255dac` | callback is `func(value: Int32)`; returns an id |
| `UnregisterListener(factName: CName, id: Uint32)` | `0x1411f6d44` | |
| `RegisterEntity(factName: CName, entityId: EntityID) -> Uint32` | `0x140dd7fb8` | the entity receives a `FactChangedEvent` |
| `UnregisterEntity(factName: CName, callbackId: Uint32)` | `0x140ed9f44` | |
| `GetGameplayTimerData(out seconds: Float, out percent: Float)` | `0x142ca0708` | the quest countdown timer |
| `ExecuteNode(node: questNodeDefinition, opt inputSocket: CName)` | Codeware | `CW scripts/Quest/QuestsSystem.reds`; see §6.1 |

Global helpers (all go through the functions above): `SetFactValue(game,
factName, factCount) -> Bool` (`final.reds:51977`), `GetFact(game, factName)
-> Int32` (`:52014`), `AddFact(game, factName, opt factCount) -> Bool` which
is **read + add + set, not atomic** (`:120033-120044`). Vanilla scripts call
them 208, 428 and 36 times; `.SetFact(` directly 109 times.

Two ways to listen:

- **`RegisterListener`**: the callback gets **only the new value**, e.g.
  `OnFactChanged(value: Int32)` (`final.reds:310447`, `:483427`), so one
  listener object per fact is needed to know which fact changed. This is what
  the campaign fork did (§7), with one watcher per fact, 16,164 of them.
- **`RegisterEntity`**: the entity gets `FactChangedEvent` whose
  `GetFactName() -> CName` (`final.reds:125072-125074`, handler
  `0x140c28f8c`) names the fact; the value is then read with `GetFact`
  (`final.reds:44930-44938`). **One entity can watch any number of facts.**
  `PlayerPuppet` already does it: `RegisterToFacts()` /
  `CreateFactCallbackData(factName)` (`final.reds:44877-44905`) store
  `FactCallbackData{m_factName, m_callbackID}` and
  `PlayerPuppet.OnFactChangedEvent(evt: FactChangedEvent)`
  (`final.reds:44930`) handles them. Devices do the same from their PS:
  `RegisterEntity(factName, PersistentID.ExtractEntityID(this.GetID()))`
  (`final.reds:28521`). _(inferred)_ A `@wrapMethod(PlayerPuppet)` on
  `OnFactChangedEvent` plus `RegisterEntity(fact, player.GetEntityID())` for
  every watched fact replaces 16 k watcher objects with 16 k registrations
  and one handler. The event is queued to the entity, so it arrives later
  than the write: read the value in the handler and accept that two quick
  writes can be seen as one.

Native layer (Codeware, `[2.5-CW]`, already in §7): `FactManager*` at
`QuestsSystem + 0xF8`; vtable `+0x10 GetFact(store, FactID)`, `+0x18
SetFact(store, FactID, value)`; `FactID = FNV1a32(name)`; then **`FactStore*
data[11]`**, each `{u64 unk00; Map<FactID, int32> data}`; store 1 holds the
named facts, stores 2..10 the quest-graph ones (`CW
src/Red/QuestsSystem.hpp:114-192`). Consequences _(inferred)_:

- **Every set fact can be enumerated** by walking `data[1]`'s map: the join
  snapshot does not need a name list (it yields hashes, which is all
  `SetFact` by id needs).
- **Every fact change can be observed** by hooking the `FactManager` vtable
  slot `+0x18`, with no listener at all. This is a vtable slot: it needs the
  startup layout check from ARCHITECTURE.md ("Talking to the engine safely").
- Whether a native `SetFact` through the vtable fires script listeners and
  quest conditions the way `QuestsSystem.SetFact` does is **not known**.

### 2.2 Naming

Facts are lower-case snake names, prefixed by their quest: `q101_done`,
`sq030_braindance_active`, `mq058_done`, `holo_setup_active`,
`ep1_standalone`, `ue_metro_*` (metro), `unlock_car_hud_dpad`. Names built at
run time exist:

- phone calls: `"phonecall_" + lower(a) + "_with_" + lower(b)`, value =
  `questPhoneTalkingState` {Ended 0, Initializing 1, Talking 2, Rejected 3},
  written with `SetFactStr` (`final.reds:240841-240851`, `:241122-241127`);
- clue inspection: `"inspected_" + clueName` (`final.reds:241354`);
- Johnny possession: `GetPossessedByJohnnyFactName()` (`final.reds:50020`).

A name list built from quest graphs (the fork's approach) misses these; the
`FactStore` walk does not.

### 2.3 Which scripts write facts (and must not be mirrored blindly)

204 distinct literal fact names are written by vanilla scripts and 243 read
(counted over `final.reds`). The written ones fall in groups; **the fork
excluded script-written facts on purpose** (§7) and this is why:

<!-- prettier-ignore -->
| Group | Examples | Co-op treatment _(inferred)_ |
| --- | --- | --- |
| per-player combat telemetry | `gmpl_npc_killed_by_player`, `gmpl_player_dodged`, `player_killed_npc`, `player_fired_a_weapon` (`final.reds:51665`) | host's value only; guests never send |
| save migration flags ("retrofix") | `IconicReworkCompleted`, `Perks2`, `Skills2`, `WardrobeInitialized`, `ConsumablesUpgraded`, … (CamelCase names) | **never sync**: they describe one save |
| tutorials | `*_tutorial`, `tutorial_*`, `disable_tutorials`, `tutorials_were_disabled` | local; guest may set `disable_tutorials` |
| load marker | `game_was_loaded` set to 1 on player attach and back to 0 one second later (`final.reds:40203-40212`) | **never sync** |
| police | `wanted_level`, `wanted_chase_active` (`final.reds:139593-139609`), `prevention_quest_disabled` | host's; follows from §9 |
| HUD toggles written by quests, read by HUD | `unlock_car_hud_dpad`, `dpad_hints_visibility_enabled`, `q000_started`, `disable_curve_bars` (listeners at `final.reds:269935-…`) | sync: they drive the guest's HUD |
| UI gates read by scripts | `map_blocked`, `radial_hub_menu_enabled`, `block_death_menu`, `q307_hub_disabled_except_journal`, `transmog_enabled` | sync except `block_death_menu` (§11) |
| metro | `ue_metro_active_station`, `ue_metro_next_station`, `ue_metro_show_line_*` | host's |
| progress counters | `maExperienceSum`, `stsExperienceSum`, `number_of_ripperdocs_visited`, `*_skillbook_bought_counter`, `courier_missions_done` | per player or host: decide per fact |
| quest logic written from scripts | `q305_*` (bunker), `mq058_opened_vvc`, `sq024_current_race_player_position`, `q112_oda_*`, `Kab08Minigame_program_uploaded` | host's; guest input becomes a request to host |

Other writers that are not quest graphs: devices with fact operations
(`FactInvoker`, `SFactOperationData`, `FactsDeviceOperation`,
`CPOMissionDevice.SetFact`), effectors (`SetFactEffector`,
`ToggleFactEffector`, `SetFactBasedOnClearAreaEffector`,
`final.reds:304813-304869`), hacking minigame rewards
(`MinigameAction_Record.FactName()/FactValue()`, `final.reds:19970-19973`),
internet pages (`JournalInternetPage.GetFactsToSet() ->
[JournalFactNameValue]`, `final.reds:258214`) and the AI
(`AISubActionAddFact_Record`). On a guest these run in the guest's game: a
guest hacking a terminal sets the fact **locally**.

Readers worth knowing: `FactValuePrereq` (`final.reds:310458-310507`,
prerequisites in TweakDB), `CheckQuestFact` (AI condition),
`GameplayFactCondition`, `scnCheckFact*` (scene interrupt/return conditions),
`questFactsDBCondition` (quest pause conditions).

### 2.4 Persistence

Facts live in the save: `QuestsSystem_OnGameRestored` is in the address list
(`CW src/Red/Shared/AddressLib.hpp:295`, hash 2048921710). Nothing in scripts
persists them. A guest's own save therefore carries its last mirrored facts
_(inferred)_: on every join the host's snapshot must overwrite, including
"fact is now 0" (the fork's rule, §7).

### 2.5 Co-op mapping

- **Host reads and sends**: at join, the full named-fact store (native walk)
  or `GetFact` over a name list; then one `FactChanged{id or name, value}`
  per change on the Event channel, from a native `SetFact` hook or from
  `RegisterEntity` on the host's player. Filter out the "never sync" groups.
- **Guest applies**: `SetFact`/`SetFactStr` only when the value differs.
  Listeners registered by the guest's own HUD scripts then fire by
  themselves, which is the point.
- **Guest must stop**: its quest graphs writing facts (quest-free world, §7).
  Facts written by the guest's gameplay scripts (devices, minigames,
  effectors) must be turned into **requests to the host** or they diverge
  silently; the simplest detector is the same hook on the guest, comparing
  against "did this write come from the network".

---

## 3. Journal

### 3.1 Manager API

`JournalManager` (`final.reds:196627-196955`, RTTI `gameJournalManager`, size
0x300, `GameInstance.GetJournalManager(game)` at `final.reds:14264`).

Queries (all `const`, all `[2.5-rtti]`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `GetQuests(context: JournalRequestContext, out entries: [wref<JournalEntry>])` | `0x142be0588` |
| `GetMetaQuests(context, out entries)` | `0x142be01b4` |
| `GetContacts(context, out entries)` | `0x141206dfc` |
| `GetConversations(contactEntry, out conversations)` | `0x142bdeeec` |
| `GetMessagesAndChoices(conversationEntry, out messages, out choiceEntries)` | `0x142bdfbcc` |
| `GetFlattenedMessagesAndChoices(contactEntry, out messages, out choiceEntries)` | `0x1409ba544` |
| `GetOnscreens(context, out entries: [JournalOnscreensStructuredGroup], opt fallbackGroupName: CName)` | `0x142be02a0` |
| `GetTarots`, `GetCodexCategories`, `GetBriefings`, `GetInternetSites`, `GetInternetPages` | — |
| `GetChildren(parentEntry, filter: JournalRequestStateFilter, out childEntries)` | `0x1409ba430` |
| `GetParentEntry(childEntry) -> wref<JournalEntry>` | `0x1409bafac` |
| `GetEntry(hash: Uint32) -> wref<JournalEntry>` | `0x1409ba0b8` |
| `GetEntryByString(uniquePath: String, className: String) -> wref<JournalEntry>` | `0x142bdf528` |
| `GetEntryHash(entry) -> Int32` | `0x140fa60fc` |
| `GetEntryState(entry) -> gameJournalEntryState` | `0x141164cf4` |
| `GetEntryTimestamp(entry) -> GameTime` | `0x141247db8` |
| `IsEntryVisited(entry) -> Bool` / `SetEntryVisited(entry, value: Bool)` | `0x140362300` / `0x142be0d10` |
| `GetTrackedEntry() -> wref<JournalEntry>` / `IsEntryTracked(entry) -> Bool` | `0x1409bb0e0` / `0x142be0b54` |
| `GetObjectiveCurrentCounter(entry: wref<JournalQuestObjective>) -> Int32` / `GetObjectiveTotalCounter` | `0x140dc3c58` / `0x140dc3d58` |
| `GetIsObjectiveOptional(entry: wref<JournalQuestObjectiveBase>) -> Bool` | `0x140dc3ab0` |
| `GetQuestType(entry) -> gameJournalQuestType` | `0x140ed71a0` |
| `GetDistanceToNearestMappin(entry: wref<JournalQuestObjective>, opt filter) -> Float` | `0x142bdf180` |
| `GetPointOfInterestMappinHashFromQuestHash(hash: Uint32) -> Uint32` | `0x142be050c` |
| `HasAnyDelayedStateChanges() -> Bool` | `0x142be0b0c` |
| `GetEntries(request: script_ref<JournalRequestContext>) -> [wref<JournalEntry>]` | Codeware (`CW scripts/Quest/JournalManager.reds`): every entry in one call |

Mutations:

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `ChangeEntryState(uniquePath: String, className: String, state: gameJournalEntryState, notifyOption: JournalNotifyOption) -> Bool` | `0x142bde5e4` |
| `ChangeEntryStateByHash(hash: Uint32, state: gameJournalEntryState, notifyOption: JournalNotifyOption)` | `0x142bde814` |
| `TrackEntry(entry: wref<JournalEntry>)` | `0x142be0de4` |
| `UntrackEntry()` | `0x1421de690` |
| `TrackPrevNextEntry(next: Bool)` | `0x142be0ed4` |
| `RegisterScriptCallback(obj: IScriptable, functionName: CName, type: gameJournalListenerType)` | `0x1409f26d4` |
| `UnregisterScriptCallback(obj: IScriptable, functionName: CName)` | `0x140249760` |

**Correction to §7**: on 2.50 `ChangeEntryState` and `ChangeEntryStateByHash`
are ordinary `public final native` script functions
(`final.reds:196707-196714`) and vanilla scripts call them: shards
(`final.reds:19911`, `:41437`, `:223787`), phone messages (`:348244`), codex
links (`:360667`). The "import-only" note (`1610ed5e`) was about 2.31.

Not usable: `CreateScriptedQuestFromTemplate`, `DeleteScriptedQuest`,
`SetScriptedQuest*` are **empty script stubs** returning false
(`final.reds:196746-196801`). The journal cannot be extended at run time
through them.

Enums `[2.5-rtti]`:

<!-- prettier-ignore -->
| Enum | Values |
| --- | --- |
| `gameJournalEntryState` | Undefined 0, Inactive 1, Active 2, Succeeded 3, Failed 4 |
| `gameJournalNotifyOption` | Undefined 0, DoNotNotify 1, Notify 2 (script alias `JournalNotifyOption`) |
| `gameJournalChangeType` | Undefined 0, Direct 1, Indirect 2, IndirectDependent 3 (script alias `JournalChangeType`) |
| `gameJournalListenerType` | State 0, Visited 1, Tracked 2, Untracked 3, Counter 4, StateDelay 5, ObjectiveOptional 6, ChoiceEntry 7 |
| `gameJournalQuestType` | MainQuest 0, SideQuest 1, MinorQuest 2, StreetStory 3, CyberPsycho 4, Contract 5, VehicleQuest 6, ApartmentQuest 7, CourierQuest 8, CourierSideQuest 9 |
| `gameJournalEntryUserState` | Undefined 0, Inactive 1, Active 2, Succeeded 3, Failed 4, Read 5, Open 6 |

Request filters: `JournalRequestContext{stateFilter:
JournalRequestStateFilter{inactive, active, succeeded, failed: Bool};
classFilter}` with static helpers `CreateQuestTypeRequestFilter(self, game,
includeMainQuests, includeSideQuests, includeStreetStories,
includeCyberPsycho, includeContracts)`,
`CreatePlayerLevelBasedQuestRequestFilter`,
`CreateQuestDistanceRequestFilter` (`final.reds:197012-197035`).

### 3.2 Callbacks

Every listener type calls back with the same four arguments
(`final.reds:258180-258185`, `:341545-341550`, `:406017-406022`):

```
cb func X(hash: Uint32, className: CName,
          notifyOption: JournalNotifyOption, changeType: JournalChangeType)
```

`StateDelay` adds a fifth, `delay: Float` (`final.reds:348814-348820`).
`className` is the RTTI name, e.g. `n"gameJournalQuest"`,
`n"gameJournalPhoneMessage"`, `n"gameJournalInternetPage"`.

<!-- prettier-ignore -->
| Listener type | Fires when | Vanilla users (register line) |
| --- | --- | --- |
| `State` | any entry changes state | notification queue `:341338`, quest tracker `:405950`, phone `:348699`, browser `:258172` |
| `StateDelay` | a delayed change is scheduled | messenger "typing…" `:348702` |
| `Visited` | `SetEntryVisited` | `:328042`, `:341341`, `:475699` |
| `Tracked` | tracked entry changes | world map `:358232`, quest tracker `:405953`, `:409830` |
| `Untracked` | (no vanilla user) | — |
| `Counter` | objective counter changes | quest tracker `:405956` |
| `ObjectiveOptional` | objective optional flag changes | quest tracker `:405959` |
| `ChoiceEntry` | a phone reply choice changes | messenger `:348202` |

`notifyOption == Notify` is what makes `JournalNotificationQueue` show the
"new quest / quest updated / completed" toast
(`final.reds:341545-341566`). A guest applying a state with `Notify` gets the
same toast as the host; a join snapshot must use `DoNotNotify`.

### 3.3 Entry classes

All extend `gameJournalEntry` (`id: String`, `GetId()`, `GetEditorName()`;
script alias `JournalEntry`, `final.reds:196957`); containers extend
`gameJournalContainerEntry` (`entries: [handle]`), files
`gameJournalFileEntry`. `[2.5-rtti]`

<!-- prettier-ignore -->
| Class (script alias drops `game`) | Parent chain | Fields / getters |
| --- | --- | --- |
| `gameJournalQuest` | FileEntry | `title`, `type: gameJournalQuestType`, `recommendedLevelID`, `districtID`; `GetTitle(journalManager)`, `GetType()` |
| `gameJournalQuestPhase` | ContainerEntry | `locationPrefabRef: NodeRef` |
| `gameJournalQuestObjectiveBase` | ContainerEntry | `description`, `counter: Uint32`, `optional`, `locationPrefabRef`, `districtID`, `itemID`; `GetDescription()`, `HasCounter()` |
| `gameJournalQuestObjective`, `…SubObjective` | QuestObjectiveBase | — |
| `gameJournalQuestDescription`, `…TitleModifier`, `…GuidanceMarker` | Entry | — |
| `gameJournalQuestMapPinBase` | ContainerEntry | `enableGPS: Bool`; `GetCaption()` |
| `gameJournalQuestMapPin` | QuestMapPinBase | `slotName`, `mappinData: gamemappinsMappinData`, `offset: Vector3`, `uiAnimation`, **`reference: gameEntityReference`** |
| `gameJournalQuestMultiMapPin`, `…PointOfInterestMapPin` | QuestMapPinBase | — |
| `gameJournalQuestMapPinLink`, `gameJournalQuestCodexLink` | Entry | `path: gameJournalPath`; `GetLinkPathHash() -> Uint32` |
| `gameJournalMetaQuest`, `…MetaQuestObjective` | FileEntry / Entry | `GetTitle()`; data via `GetMetaQuestData(gamedataMetaQuest)` |
| `gameJournalContact` | FileEntry | `name`, `type: gameContactType`, `avatarID`, `isCallableDefault`; `IsKnown(jm)`, `IsCallable(jm)` |
| `gameJournalPhoneConversation` | ContainerEntry | `GetTitle()` |
| `gameJournalPhoneMessage` | Entry | `text`, `attachment: gameJournalPath`, `sender: gameMessageSender`, `delay: Float`, `isQuestImportant` |
| `gameJournalPhoneChoiceGroup`, `…ChoiceEntry` | Container / Entry | `GetText()`, `IsQuestImportant()` |
| `gameJournalOnscreen` (**shards**), `…OnscreenGroup` | Entry / FileEntry | `tag`, `title`, `description`, `iconID` |
| `gameJournalCodexCategory`, `…CodexGroup`, `…CodexEntry`, `…CodexDescription` | — | `title`, `imageId`, `linkImageId` |
| `gameJournalTarot`, `…TarotGroup` | Entry / FileEntry | `index`, `name`, `description`, `imagePart`, `isMinorArcana` |
| `gameJournalEmail`, `…EmailGroup`, `gameJournalFile`, `…FileGroup` | — | computer mail and files |
| `gameJournalInternetSite`, `…InternetPage` | FileEntry / Entry | `GetAddress()`, `GetFactsToSet() -> [JournalFactNameValue]` |
| `gameJournalBriefing` + sections | FileEntry | — |
| `gameJournalPointOfInterestMappin`, `…PointOfInterestGroup` | Entry | open-world activity markers; phase via `gamedataMappinPhase` |
| `gameJournalContactModifierEntry`, `gameJournalImageEntry`, `gameJournalFolderEntry`, `…PrimaryFolderEntry`, `…RootFolderEntry` | — | structure |

`gameJournalPath{realPath: String, className: CName, fileEntryIndex: Int32}` with
`GetHash() -> Uint32` (`@ 0x142be4d2c`) is how quest nodes name an
entry. _(inferred)_ The `Uint32` hash used by `GetEntry`,
`ChangeEntryStateByHash` and the callbacks is this path hash: it depends only
on the journal resources, so it is the same in every game with the same
game version and DLC. `GetEntryHash` returns it as `Int32`; cast with
`Cast<Uint32>` as vanilla does (`final.reds:348241`).

### 3.4 Quest nodes that drive the journal (host side, for reference)

`questJournalNodeDefinition.type: questIJournal_NodeType` `[2.5-rtti]`:

<!-- prettier-ignore -->
| Node type | Fields |
| --- | --- |
| `questJournalEntry_NodeType` | `path: gameJournalPath`, `sendNotification: Bool` |
| `questJournalQuestEntry_NodeType` | `path`, `sendNotification`, `trackQuest: Bool`, `optional: Bool`, `version` |
| `questJournalTrackQuest_NodeType` | `path` |
| `questJournalBulkUpdate_NodeType` | `path`, `requiredEntryType`, `requiredEntryState`, `newEntryState`, `sendNotification`, `propagateChange` |
| `questJournalQuestObjectiveCounter_NodeType` | `path` (counter value comes from a fact) |
| `questJournalQuestSetObjectiveOptional_NodeType`, `…SetLockQuestObjective…`, `…PushPopQuestObjective…` | `path`, … |
| `questJournalChangeMappinPhase_NodeType` | `path`, `phase: gamedataMappinPhase`, `notifyUI: Bool` |
| `questJournalContact_NodeType` | contact known/callable |

Three things move without a `State` callback and need their own message:
**objective counters** (`Counter` listener; no script setter exists, see
§14), **optional flag** (`ObjectiveOptional`), and **POI mappin phase**.

### 3.5 Co-op mapping

- **Host reads and sends**: `RegisterScriptCallback(this, n"…",
gameJournalListenerType.State)` → `JournalState{hash, state, notify}`;
  same for `Tracked` (send `GetEntryHash(GetTrackedEntry())`, 0 when none)
  instead of the fork's 0.25 s poll, and for `Counter` /
  `ObjectiveOptional`. Snapshot at join: Codeware `GetEntries` or
  `GetQuests` + recursive `GetChildren`, sending every entry whose state is
  not Inactive, as CDPR's `gameJournalSharedStateData{pathHash, entryState}`.
- **Guest applies**: `GetEntry(hash)`; skip unknown and unchanged;
  `ChangeEntryStateByHash(hash, state, notify)`; `TrackEntry(entry)` once the
  entry is Active (fork's ordering rule, §7). Apply parents before children
  _(inferred: an objective under an inactive quest is not shown)_.
- **Guest must stop**: tracking on its own. The guest UI calls `TrackEntry`
  from the journal menu and map (`final.reds:341206`, `:358957`, `:360380`,
  `:372580`) and `TrackPrevNextEntry(true)` from a key
  (`final.reds:528899`). Either leave tracking per player (a guest may follow
  a different objective; only the host's default is mirrored) or wrap those.
  Per-player tracking is the natural co-op behaviour and costs nothing.
- Entries the guest changes itself: reading a shard, opening a message,
  visiting an entry. These are per player (`Visited`, shard `Active`) and
  must **not** be overwritten by the snapshot rule "host's state wins" for
  `gameJournalOnscreen` unless shards are meant to be shared.

---

## 4. Objectives on the HUD: mappins

### 4.1 How a quest marker comes to exist

An objective's markers are **journal entries**: `gameJournalQuestMapPin`
children of the objective, each with a `reference: gameEntityReference` (what
to stick to), `slotName`, `offset` and `mappinData`. _(inferred from the
class layouts and the replicated state in §1)_ When the objective becomes
Active the mappin system creates a `gamemappinsQuestMappin` per map-pin entry
and resolves the reference to an entity or a world position. So **a guest
that mirrors journal states gets quest markers without a mappin message**,
provided the reference resolves on the guest:

- references to placed nodes (NodeRef to a world node, an area, a door)
  resolve everywhere;
- references to **quest-spawned NPCs and vehicles** resolve only if the
  guest's proxy answers to the same reference (§6.4). CDPR's replicated
  state carried exactly this: `dynamicQuestMappinRepInfo{journalPathHash,
entity}`. tandem needs the same pair as a message when the entity is a
  proxy with a different EntityID.

`questMappinManagerNodeDefinition{path: gameJournalPath,
disablePreviousMappins: Bool}` is the quest node that activates a map pin
entry directly (mirrored by the fork's QuestMirror, §7).

### 4.2 Mappin system API

`MappinSystem` (`final.reds:51096-51154`, RTTI `gamemappinsMappinSystem`,
size 0x184c0, `GameInstance.GetMappinSystem(game)`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `RegisterMappin(data: MappinData, position: Vector4) -> NewMappinID` | `0x140dd07c8` |
| `RegisterMappinWithObject(data, object: wref<GameObject>, opt slotName: CName, opt offset: Vector3) -> NewMappinID` | `0x140dd0984` |
| `RegisterRemotePlayerMappin(data, playerObject: wref<GameObject>) -> NewMappinID` | `0x142d452a4` |
| `RegisterVehicleMappin`, `RegisterGrenadeMappin`, `RegisterFastTravelMappin(data, fastTravelData)` | — |
| `UnregisterMappin(id)`, `SetMappinActive(id, active)`, `SetMappinPosition(id, position: Vector4)` | `0x141311938`, `0x1409b72bc`, `0x142d45460` |
| `ChangeMappinVariant(id, newVariant: gamedataMappinVariant)`, `SetMappinScriptData(id, scriptData)` | `0x142d44c54`, `0x141333c20` |
| `GetMappins(targetType: gamemappinsMappinTargetType, out mappins: [IMappin])`, `GetMappin(id) -> IMappin` | `0x142d44e98`, `0x1422126f0` |
| `GetMappinFromObjective(phaseEntry, objectiveEntry) -> wref<IMappin>`, `GetMappinFromQuest(questEntry)` | `0x142d44d00`, `0x142d44df4` |
| `GetQuestMappinPositionsByObjective(objectiveHash: Uint32, out positions: [Vector3]) -> Bool` | `0x142d451b8` |
| `GetQuestMappinPosition(mappinHash: Uint32, out position: Vector3) -> Bool` | `0x142d450e0` |
| `GetPointOfInterestMappinSavedState(hash, out mappinPhase: Uint16, out mappinVariant: Uint16, out active: Bool) -> Bool` | `0x142d44f68` |
| `SetMappinTrackingAlternative(mappinID, targetMappinID)`, `GetManuallyTrackedMappinID()`, `UntrackMappin()` | `0x1412acb6c`, `0x1422126c0`, `0x142212870` |
| `GetAllMappins() -> [IMappin]` | Codeware |

`gamemappinsMappinTargetType` {World 0, Minimap 1, Map 2}.
`MappinData{mappinType: TweakDBID, variant: gamedataMappinVariant, active,
debugCaption, localizedCaption, visibleThroughWalls, scriptData}`.
`IMappin` (`final.reds:217361-217401`): `IsQuestMappin()`,
`IsQuestNPCMappin()`, `IsQuestEntityMappin()`, `IsQuestAreaMappin()`,
`IsPlayerTracked()`, `GetWorldPosition()`, `GetEntityID()`, `GetPhase() ->
gamedataMappinPhase` {CompletedPhase 0, DefaultPhase 1, DiscoveredPhase 2,
UndiscoveredPhase 3}, `GetVariant()`, `GetNewMappinID()`.
`QuestMappin.GetJournalPathHash() -> Uint32` (`final.reds:276338`) links a
mappin back to its journal entry.

Codeware native layer `[2.5-CW]`: mappin data lock at `MappinSystem + 0x198`,
sorted `MappinData` array at `+0x1A0`, vfuncs `SetPoiMappinPhase` /
`GetPoiMappinPhase`, `PointOfInterestMappin_SetPhase` hash 803085170,
`MappinSystem_GetMappinData` 3603701972, `MappinSystem_GetPoiData`
2918455690 (`CW src/Red/MappinSystem.hpp`).

### 4.3 Co-op mapping

- **Host sends**: nothing for ordinary quest markers (they follow the
  journal). For a marker whose target is a host-spawned entity:
  `QuestMappinTarget{journalPathHash, NetId}`; or, as a fallback that needs
  no entity, the positions from `GetQuestMappinPositionsByObjective(hash)`
  at a low rate.
- **Guest applies**: fallback markers with `RegisterMappin(data, position)` /
  `SetMappinPosition`; real ones by making the proxy resolvable (§6.4).
- **Guest must stop**: nothing; without quests the guest creates no quest
  mappins of its own.
- **Open-world markers** (`gameJournalPointOfInterestMappin`, gigs, NCPD
  scanner hustles, fast-travel points) are discovered per game as sectors
  stream in. Their phase is saved (`GetPointOfInterestMappinSavedState`).
  Whether "gig completed" marks follow the host is a decision; the API to
  force a phase is Codeware-level only.

---

## 5. Phone, messages, shards, codex, notifications, tutorials

For each: the entry point to **suppress** on a guest (so the guest's own game
does not start it) and to **mirror** (so the guest sees the host's).

### 5.1 Phone calls

- Quest side: `questPhoneManagerNodeDefinition` with node types
  `questCallContact_NodeType{caller, addressee: gameJournalPath, phase:
questPhoneCallPhase, mode: questPhoneCallMode, prefabNodeRef,
applyPhoneRestriction, blockSkipByLine, isRejectable, showAvatar,
visuals}`, `questSendMessage_NodeType{msg: gameJournalPath,
sendNotification}`, `questOpenMessage_NodeType`,
  `questCloseMessage_NodeType`, `questAddRemoveContact_NodeType`,
  `questRemoveAllContacts_NodeType`, `questSetPhoneStatus_NodeType`,
  `questSetPhoneRestriction_NodeType`, `questMinimize_NodeType`,
  `questSetCustomStyle_NodeType`. `[2.5-rtti]`
- Engine → script: the node queues `questTriggerCallRequest{caller,
addressee: CName, callPhase, callMode, isPlayerTriggered, isRejectable,
showAvatar, visuals}` (`final.reds:241157-241166`) to the `PhoneSystem`
  scriptable system; `PhoneSystem.OnTriggerCall` (`final.reds:240569`)
  plays `ui_phone_incoming_call` / `ui_phone_initiation_call` and calls
  `TriggerCall`, which writes `PhoneCallInformation` to the `UI_ComDevice`
  blackboard. Other requests: `questSetPhoneStatusRequest{status: CName}`
  (`:241153`), `questMinimizeCallRequest` (`:241198`), `PickupPhoneRequest`,
  `UsePhoneRequest`, `PhoneTimeoutRequest`, `TalkingTriggerRequest`.
- Native manager: `questPhoneManager` (`GameInstance.GetPhoneManager(game)`,
  `final.reds:14326`): `ApplyPhoneCallRestriction(apply: Bool, blockSkipByLine:
Bool)` (`@ 0x142c7b734`; the script declaration at `final.reds:241181` has
  only the first parameter), `IsPhoneCallActive() -> Bool` (`@ 0x142a22f68`).
- Blackboard `UI_ComDeviceDef` (`final.reds:241130-241151`):
  `PhoneCallInformation: Variant`, `openMessageRequest: Uint`,
  `closeMessageRequest: Int`, `showingMessage`, `isDisplayingMessage`,
  `ContactsActive`, `SmsMessengerActive`, `MessageToOpenHash: Int`,
  `PhoneEnabled: Bool`, `PhoneVisibility: Variant`, `PhoneStyle_*`.
- `questPhoneCallInformation{callMode, isAudioCall, contactName: CName,
isPlayerCalling, isPlayerTriggered, callPhase, isRejectable, showAvatar,
visuals}`; enums `questPhoneCallPhase` {Undefined 0, IncomingCall 1,
  StartCall 2, EndCall 3}, `questPhoneCallMode` {Undefined 0, Audio 1,
  Video 2}, `questPhoneCallVisuals` {Default 0, Somi 1}, `questPhoneStatus`
  {Available 0, NotAvailable 1, Busy 2, Minimized 3}. `[2.5-rtti]`
- Call state is published as facts `phonecall_<a>_with_<b>` (§2.2): the
  quest graph waits on them.

Co-op: a call is a **scene** with audio; presenting the UI on a guest is the
blackboard write (§7 has NCO's experience: block at `OnTriggerCall`, not at
`SetCallInfo`, and stop the ringtone on every exit). **Host sends**
`PhoneCall{contact, phase, mode, visuals}` from a wrap of
`PhoneSystem.OnTriggerCall`; **guest applies** by queuing the same
`questTriggerCallRequest` to its own `PhoneSystem` — but the voice lines
come from the scene the host runs, so this belongs with "scenes for guests"
(open problem in ../architecture/story.md). **Guest must stop**: answering or
rejecting (it would write the `phonecall_*` fact locally); the host's choice
decides.

### 5.2 Text messages

Messages are journal entries (`gameJournalContact` →
`gameJournalPhoneConversation` → `gameJournalPhoneMessage` /
`gameJournalPhoneChoiceGroup` → `gameJournalPhoneChoiceEntry`). A message
"arrives" when its entry becomes Active; the messenger UI reacts to the
`State`, `StateDelay` and `ChoiceEntry` journal callbacks
(`final.reds:348795-348830`, `:348202-348213`). **Mirroring the journal
mirrors the messages.** A reply is a choice entry the player activates: on a
guest it must become a request to the host (it advances the host's quest),
or be disabled. Opening a message marks it Active/visited locally
(`final.reds:348239-348257`): per player.

### 5.3 Shards and readables

A shard is a `gameJournalOnscreen` entry. Picking up or reading one calls
`ChangeEntryState(path, "gameJournalOnscreen", Active, Notify)` from the
player's own scripts (`PlayerPuppet`, `final.reds:41431-41442`; `ReadAction`,
`:223779-223795`; hacking minigame, `:19908-19990`) and shows it with a
`NotifyShardRead{title, text, m_imageId}` UI event (`final.reds:223821`,
queued with `GameInstance.GetUISystem(game).QueueEvent`). These are **item
driven and per player**: no suppression needed. Quest-given shards arrive
through the journal mirror. Decide whether the shard collection is shared
(host's `State` events for `gameJournalOnscreen` applied on guests) or per
player (filter that class out).

### 5.4 Codex and tarot

- Journal part: `gameJournalCodexCategory/Group/Entry/Description`,
  `gameJournalTarot`; unlocked by state change, mirrored with the journal.
  A tarot becoming Active raises `TarotCardAdded` on the UI
  (`final.reds:341567-341576`).
- `CodexSystem` scriptable system (`final.reds:402585-402771`): a separate,
  TweakDB-record based codex with requests `CodexUnlockRecordRequest`,
  `CodexLockRecordRequest`, `CodexAddRecordRequest`,
  `UnlockCodexPartRequest`; state `m_codex: [SCodexRecord]`. Small; mirror
  by forwarding those requests if needed.

### 5.5 On-screen notifications

<!-- prettier-ignore -->
| What | Quest node (host) | Script entry point (guest can call) |
| --- | --- | --- |
| quest toast (new/updated/done) | journal node with `sendNotification` | follows `ChangeEntryStateByHash(…, Notify)` through `JournalNotificationQueue.OnJournalUpdate` (`final.reds:341545`) |
| warning / simple message | `questWarningMessage_NodeType{message, localizedMessage, duration, show, instant, type}` | blackboard `UI_Notifications.WarningMessage` ← `SimpleScreenMessage{isShown, duration, message, isInstant, type}` (`final.reds:72993-73010`); vanilla example `PreventionSystem.ShowMessage` (`:141397-141406`) |
| on-screen message | `questShowOnscreen_NodeType{message, localizedMessage, duration, show}` | blackboard `UI_Notifications.OnscreenMessage` |
| custom quest notification | `questShowCustomQuestNotification_NodeType{customQuestNotificationData{header, desc, icon, fluffHeader}}` | blackboard `UI_CustomQuestNotification.data` (`final.reds:312670`); UI event `CustomNotificationEvent{header, description}` (`:341580`) |
| level-up banner | `questShowLevelUpNotification_NodeType{levelUpData}` | `LevelUpNotificationQueue` (`final.reds:343216`) |
| narrative event, location name | `questShowNarrativeEvent_NodeType`, `questSetLocationName_NodeType` | native |
| progress bar, countdown timer | `questProgressBar_NodeType`, `questCountdownTimer_NodeType` | `UIGameData.QuestTimerInitialDuration/CurrentDuration`; `QuestsSystem.GetGameplayTimerData` |
| HUD entry visibility, input hints | `questSetHUDEntryVisibility_NodeType`, `questInputHint_NodeType` | native |
| fades | `questSetFadeInOut_NodeType` (render FX manager) | native |
| item received | — | `ItemsNotificationQueue` (`final.reds:339288`), fed by the local inventory |

All quest UI nodes are subtypes of `questIUIManagerNodeType` under
`questUIManagerNodeDefinition`; the fork did **not** mirror that node class
(§7 list), so host-only warnings and timers were invisible to guests.
_(inferred)_ For the script-reachable rows the cheap mirror is: host wraps
the blackboard write or UI event, guest repeats it.

### 5.6 Tutorials

- `questTutorial_NodeType{subtype: questITutorial_NodeSubType}` under the UI
  manager node; `questTutorialManager`
  (`GameInstance.GetTutorialManager(game)`, `final.reds:14322`):
  `RequestToCloseOverlay(overlayId: Uint32)` only.
- Popups go through `UIGameData.Popup_Data: Variant`, `Popup_Settings:
Variant`, `Popup_IsShown: Bool`, `Popup_IsModal: Bool`
  (`final.reds:217723-217726`), shown by `TutorialPopupGameController`
  (`:403212`), which **pauses the game** (`m_gamePaused`).
- Script-triggered tutorials are gated by facts: `disable_tutorials`,
  `block_combat_scripts_tutorials`, and one `*_tutorial` fact each (§2.3).

Co-op: guests should not get modal, pausing tutorials. **Guest**: set
`disable_tutorials = 1` locally and keep it out of the fact sync; the quest
driven ones never start because the guest runs no quests.

---

## 6. Quest runtime: nodes, spawns, teleports, prefabs, entity references

### 6.1 Node classes

Graph nodes are `questNodeDefinition` (`id: Uint16`) subclasses; "manager"
nodes carry a typed payload in `type` (or `actions`). Full list from `rtti.py
find 'quest.*NodeDefinition$'` (82 classes) `[2.5-rtti]`:

<!-- prettier-ignore -->
| Group | Node definitions |
| --- | --- |
| flow | `questStartNodeDefinition`, `questEndNodeDefinition`, `questInputNodeDefinition`, `questOutputNodeDefinition`, `questPhaseNodeDefinition`, `questEmbeddedGraphNodeDefinition`, `questLogicalAnd/Xor/HubNodeDefinition`, `questSwitchNodeDefinition`, `questFlowControlNodeDefinition`, `questCutControlNodeDefinition`, `questRandomizerNodeDefinition`, `questDeletionMarkerNodeDefinition`, `questPlaceholderNodeDefinition` |
| waiting | `questPauseConditionNodeDefinition`, `questConditionNodeDefinition`, `questMinigameSateListenerNodeDefinition` |
| state | `questFactsDBManagerNodeDefinition` (`questSetVar_NodeType{factName: String, value: Int32, setExactValue: Bool}`), `questJournalNodeDefinition`, `questMappinManagerNodeDefinition`, `questCheckpointNodeDefinition`, `questRewardManagerNodeDefinition`, `questAchievementManagerNodeDefinition`, `questItemManagerNodeDefinition` |
| scenes | `questSceneNodeDefinition{sceneFile, sceneLocation, …}`, `questSceneManagerNodeDefinition`, `questPhoneManagerNodeDefinition`, `questAudioNodeDefinition`, `questAudioCharacterManagerNodeDefinition`, `questVoicesetManagerNodeDefinition`, `questRecordingNodeDefinition` |
| spawning | `questSpawnManagerNodeDefinition`, `questDynamicSpawnSystemNodeDefinition`, `questPopulactionControllerNodeDefinition` (sic), `questCrowdManagerNodeDefinition`, `questInstancedCrowdControlNodeDefinition` |
| characters | `questCharacterManagerNodeDefinition`, `questEntityManagerNodeDefinition`, `questPuppetAIManagerNodeDefinition`, `questBehaviourManagerNodeDefinition`, `questForcedBehaviourNodeDefinition`, `questClearForcedBehavioursNodeDefinition`, `questSendAICommandNodeDefinition`, `questCombatNodeDefinition`, `questMovePuppetNodeDefinition`, `questTeleportPuppetNodeDefinition`, `questRotateToNodeDefinition`, `questUseWorkspotNodeDefinition`, `questEquipItemNodeDefinition`, `questUnequipItemNodeDefinition`, `questPuppeteerNodeDefinition` |
| vehicles | `questVehicleNodeDefinition`, `questTeleportVehicleNodeDefinition` |
| world | `questWorldDataManagerNodeDefinition`, `questEnvironmentManagerNodeDefinition`, `questTimeManagerNodeDefinition`, `questTriggerManagerNodeDefinition`, `questInteractiveObjectManagerNodeDefinition`, `questTransformAnimatorNodeDefinition`, `questEventManagerNodeDefinition`, `questFXManagerNodeDefinition`, `questRenderFxManagerNodeDefinition`, `questVisionModesManagerNodeDefinition` |
| game / UI | `questGameManagerNodeDefinition`, `questUIManagerNodeDefinition`, `questMinigameNodeDefinition` |
| CDPR MP | `questMultiplayerAIDirectorNodeDefinition`, `questMultiplayerChoiceTokenNodeDefinition`, `questMultiplayerHeistNodeDefinition`, `questMultiplayerJunctionDialogNodeDefinition`, `questMultiplayerTeleportPuppetNodeDefinition` |
| debug | `questDebugShowMessageNodeDefinition`, `questTestNodeDefinition` |

Payload types by manager (the ones relevant to world state):

<!-- prettier-ignore -->
| Manager node | Payload types |
| --- | --- |
| `questSpawnManager…` | `questSpawner_NodeType{spawnerReference: NodeRef}`, `questSpawnSet_NodeType{reference: NodeRef, entryName, phaseName}`, `questCommunityTemplate_NodeType{spawnerReference, communityEntryName, communityEntryPhaseName}`, `questScene_NodeType`, `questLegacy_NodeType` (each wrapped in `questSpawnManagerNodeActionEntry{type}`) |
| `questDynamicSpawnSystem…` | `questDynamicVehicleSpawn_NodeType{VehicleData: [TweakDBID], waveTag: CName, spawnDirectionPreference, distanceRange: Vector2}`, `questDynamicVehicleDespawn_NodeType`, `questDynamicVehicleDespawnAll_NodeType` |
| `questCrowdManager…` | `questCrowdManagerNodeType_ControlCrowd{debugSource, action: questControlCrowdAction{Disable 0, Enable 1}, distantCrowdOnly}`, `questCrowdManagerNodeType_EnableNullArea{areaReference: NodeRef, enable}` |
| `questPopulactionController…` | `enable: Bool` on a referenced spawner object |
| `questWorldDataManager…` | `questShowWorldNode_NodeType{objectRef: NodeRef, isPlayer, show, componentName}`, `questTogglePrefabVariant_NodeType{params: [{prefabNodeRef: NodeRef, variantStates: [{name: CName, show: Bool}]}]}`, `questManageCollision_NodeType`, `questDisableTeleportingMonitoring_NodeType` |
| `questEnvironmentManager…` | `questPlayEnv_SetWeather{weatherID: TweakDBID, source: CName, blendTime: Float, priority: Uint32, reset: Bool}`, `questPlayEnv_NodeType{params: {enable, envParams: rRef:worldEnvironmentAreaParameters, blendTime}}`, `questPlayEnv_OverrideGlobalLight`, `questPlayEnv_ForceRelitEnvProbe` |
| `questTimeManager…` | `questSetTime_NodeType{source: CName, hours, minutes, seconds: Int32}`, `questPauseTime_NodeType{source, pause: Bool}`, `questShiftTime_NodeType{timeShiftType, preventVisualGlitch, hours, minutes, seconds: Uint32}` |
| `questGameManager…` | `questTimeDilation_NodeType` (params `…_World`, `…_Player`, `…_Puppet`, `…_Entity`), `questGameplayRestrictions_NodeType{action, source, restrictionIDs: [TweakDBID]}`, `questContentTokenManager_NodeType`, `questLootPurge_NodeType`, `questReplacer_NodeType`, `questRumble_NodeType`, `questSetTimer_NodeType`, `questControlObject_NodeType` |
| `questInteractiveObjectManager…` | `questDeviceManager_NodeType` (params `{objectRef: NodeRef, slotName, entityRef: gameEntityReference, deviceControllerClass: CName, deviceAction: CName, actionProperties: [{name, value: Variant, min, max}]}`), `questHackingManager_NodeType`, `questSetInteractionState_NodeType`, `questElevator_ManageNPCAttachment_NodeType`, `questSetConveyorState_NodeType`, `questCyberdrill_NodeType` |
| `questItemManager…` | `questAddRemoveItem_NodeType`, `questTransferItem_NodeType`, `questInjectLoot_NodeType`, `questLootTokenManager_NodeType`, `questReInitContainers_NodeType`, `questResetContainers_NodeType`, `questSetLootInteractionAccess_NodeType{objectRef, accessible}`, `questSetItemTags_NodeType`, `questDropItemFromSlot_NodeType` |
| `questRewardManager…` | `questGiveReward_NodeType{rewards: [TweakDBID]}` |
| `questEntityManager…` | `…AddDevelopmentPoint_NodeType{value: Int32, type: gamedataDevelopmentPointType}`, `…LevelUpProficiency_NodeType{type}`, `…SetStat…`, `…MountPuppet…`, `…ChangeAppearance…`, `…ToggleComponent…`, `…SetDestructionState…` |
| `questSceneManager…` | `questSetTier_NodeType{tier: GameplayTier, usePlayerWorkspot, useEnterAnim, useExitAnim, forceEmptyHands, …}`, `questSetTier2/3/4Params…`, `questSetFOV…`, `questPlayerLookAt…`, `questNPCLookAt…` |
| `questCheckpoint…` | on the node itself: `saveLock, ignoreSaveLocks, pointOfNoReturn, endGameSave, retryOnFailure: Bool`, `additionalEndGameRewardsTweak: [TweakDBID]` |
| `questPhaseNodeDefinition` | `phaseGraph`, `phaseInstancePrefabs: [questQuestPrefabEntry]`, `unfreezingTriggerNodeRef: NodeRef`, `phaseResource: raRef:questQuestPhaseResource`, `saveLock: Bool` |

`questUniversalRef{entityReference: gameEntityReference, refLocalPlayer:
Bool, mainPlayerObject: Bool}` is how nodes point at "an entity or the
player" (teleports, loot injection).

Running a node from outside a phase: Codeware exposes it to scripts as
`QuestsSystem.ExecuteNode(node, opt inputSocket)` and uses it itself for
`ToggleNode` and `ToggleVariant` (§6.3) — i.e. **executing stateless manager
nodes outside their phase is proven on 2.5 for world-data nodes** `[2.5-CW]`.
It is still unproven for scene nodes (§7's open question). The native recipe
and hashes are in §7 (`QuestPhaseInstance_ExecuteNode` 3227858325,
`QuestsSystem_CreateContext` 3144298192). Others in Codeware's list:
`QuestPhaseInstance_ExecuteGraph` 779107884, `QuestPhaseInstance_Initialize`
3423879019, `QuestRootInstance_Start` 797843833,
`QuestLoader_ProcessPhaseResource` 790570700, `QuestLoader_PhasePreloadCheck`
1567104722 (`CW src/Red/Shared/AddressLib.hpp:285-295`).

### 6.2 Spawn systems

<!-- prettier-ignore -->
| System (getter) | Script-visible API |
| --- | --- |
| `gameCommunitySystem` (`GetCommunitySystem`, `final.reds:14168`) | `EnableDynamicCrowdNullArea`, `DisableCrowdNullArea`, `ChangeDensityModifier`, `ResetDensityModifier` (§10). Nothing about communities. |
| `gamePopulationSystem` (size 0x20d0) | none |
| `gameDynamicSpawnSystem` (`GetDynamicSpawnSystem`, `:14206`) | `GetNumberOfSpawnedUnits() -> Int32`, `IsEntityRegistered(id: EntityID) -> Bool`, `IsInUnmountingRange(position: Vector3) -> Bool`; script callbacks `SpawnRequestFinished(DSSSpawnRequestResult)`, `SpawnCallback(spawnedObject: GameObject)` (`final.reds:189584-189652`) |
| `gamePreventionSpawnSystem` (`GetPreventionSpawnSystem`, `:14204`) | §9 |
| `gameEntityStubSystem`, `gameEntitySpawnerEventsBroadcaster` | none / events |
| `gameWorldBoundarySystem` (size 0x88) | none (see below) |
| Codeware `WorldStateSystem` (`GameInstance.GetWorldStateSystem()`) | below |

Codeware's `WorldStateSystem` (`CW scripts/World/WorldStateSystem.reds`,
`CW src/App/World/WorldStateSystem.cpp`) `[2.5-CW]`:

```
IsReady() -> Bool
GetCommunity(community: NodeRef) -> ref<CommunityWrapper>
ActivateCommunity(community: NodeRef, opt entry: CName)
DeactivateCommunity(community: NodeRef, opt entry: CName)
ResetCommunity(community: NodeRef)
SetCommunityPhase(community: NodeRef, entry: CName, phase: CName)
GetPopulationSpawner(spawner: NodeRef) -> ref<PopulationSpawnerWrapper>
ActivatePopulationSpawner(spawner: NodeRef)
DeactivatePopulationSpawner(spawner: NodeRef)
ResetPopulationSpawner(spawner: NodeRef)
ToggleNode(node: NodeRef, state: Bool)          // = TogglePrefab
ToggleVariant(ref: NodeRef, name: CName, state: Bool)
```

with `CommunityWrapper{GetEntries(), GetActiveEntityIDs(),
GetTemplateData()}`, `CommunityEntryWrapper{IsActive(), GetName(),
GetPhases(), GetSpawnedEntityIDs(), GetSpawningEntityIDs(),
GetRestoredEntityIDs(), GetReservedEntityIDs()}`,
`PopulationSpawnerWrapper{IsActive(), IsInitialized(), GetRecordID(),
GetAppearanceName(), GetTransform(), GetSpawnedEntityIDs(), …}`. The native
side is `gameICommunitySystem` vtable slots (`CW
src/Red/CommunitySystem.hpp:87-128`): `ActivateCommunity 0x1B0`,
`DeactivateCommunity 0x1B8`, `SetCommunityPhase 0x1C0`, `ResetCommunity
0x1C8`, `ActivateSpawner 0x1D8`, `DeactivateSpawner 0x1E0`, `ResetSpawner
0x1E8`, `GetCommunity 0x228`, `GetSpawner 0x260`, then
`CommunitySystem_Update` (hash 1559565515) `(system, true)`. Community and
spawner ids are the `uint64` of `ResolveNodeRef(nodeRef)`.

This is the **read side tandem needs on the host** ("which entity IDs did
this quest community spawn") and the **off switch on the guest**
(`DeactivateCommunity` / `DeactivatePopulationSpawner` on anything a guest
would have spawned by itself). Codeware also has `OpenWorldSystem`
(`GetActivities()`, `StartActivity(name, opt force)`), relevant for gigs and
NCPD events later.

World population data: `worldPopulationSpawnerNode{objectRecordId: TweakDBID,
appearanceName, spawnOnStart: Bool, alwaysSpawned: gameAlwaysSpawnedState,
spawnInView: gameSpawnInViewState, prefetchAppearance, isVehicle}`;
`gamePopulationEntityPriority` {Quest 0, Community 1, Crowd 2};
`populationSpawnerObjectCtrlAction` {Undefined 0, Activate 1, Deactivate 2,
Reactivate 3, ResetKillCount 4}; community templates
`communityCommunityTemplateData`, `communitySpawnEntry`,
`communitySpawnPhase`, registry `worldCommunityRegistryNode`. `[2.5-rtti]`

### 6.3 Quest prefabs, node visibility, world boundary

- A quest phase lists the prefabs it needs: `questPhaseNodeDefinition.
phaseInstancePrefabs: [questQuestPrefabEntry]`; the engine loads them while
  the phase runs (`questQuestPrefabsHandler : worldIQuestPrefabStateListener`,
  `worldQuestPrefabLoadingMode`). Script listeners exist:
  `QuestPrefabStateListener`, `PhotoModeQuestPrefabStateListener`.
  **A guest that runs no quest phases never loads quest prefabs**: quest-only
  interiors, props and their placed NPCs are missing on the guest (NCO saw
  the same, §7: "quest-scoped interiors only stream during their quest").
  This is the hardest gap in "guests run no quests" and exactly what
  `questQuestsSystemReplicatedState.replicatedQuestPrefabs` existed for. No
  script or Codeware API loads a quest prefab by NodeRef; it needs RE
  (`worldRuntimeSystemPrefabInstancing`, `questQuestPrefabsHandler`, size
  0xF0).
- Showing/hiding placed nodes and prefab variants **is** reachable: Codeware
  `ToggleNode` builds a `questShowWorldNode_NodeType{objectRef, show}` in a
  `questWorldDataManagerNodeDefinition` and executes it; `ToggleVariant`
  does the same with `questTogglePrefabVariant_NodeType` (`CW
src/App/World/WorldStateSystem.cpp:149-188`). The state lives in
  `questWorldStateSystem` (size 0x1f8, no script functions) — the system
  whose replicated state is the node visibility / collision / variant maps
  of §1.
- `gameWorldBoundarySystem`, `gameWorldBoundaryNode`,
  `gameCyberspaceBoundaryNode`: no script functions. The only script hook is
  `AutoDriveSystem.OnReachedWorldBoundary() -> Bool`; the boundary shows up
  for the player as save lock reason `Boundary` (§11). The "turn back" zone
  is engine driven and per player: nothing to sync.

### 6.4 Entity identity and references

- `EntityID` is one `hash: Uint64` (`rtti: entEntityID`). Script statics
  (`final.reds:14685-14697`): `IsDefined`, `IsDynamic`, `IsStatic`,
  `ToDebugString`, `GetHash(id) -> Uint32`; Codeware adds `FromHash(hash:
Uint64)` and `ToHash(id) -> Uint64` (`CW scripts/Entity/EntityID.reds`).
  **`EntityID.IsStatic` / `IsDynamic` is the test for ARCHITECTURE.md's two
  identity classes**: static = placed (same hash in every game), dynamic =
  spawned at run time (needs a `NetId`). _(inferred)_ Community and
  population-spawner NPCs get dynamic IDs; verify per source (§14).
- `PersistentID{entityHash: Uint64, componentName: CName}` (`rtti:
gamePersistentID`): statics `IsDefined`, `IsDynamic`, `IsStatic`,
  `IsEntity`, `IsComponent`, `GetComponentName`, `ExtractEntityID`
  (`final.reds:26204-26220`), global `CreatePersistentID(entityID, opt
componentName)` (`:97178`), casts from `EntityID`, `GlobalNodeRef`,
  `GlobalNodeID`; Codeware `ForEntity`, `ForComponent`, `ToHash`.
- `NodeRef` (authoring-time path hash) → `GlobalNodeRef{hash: Uint64}`:
  `ResolveNodeRef(id: NodeRef, context: GlobalNodeRef) -> GlobalNodeRef`
  (`final.reds:189738`), `ResolveNodeRefWithEntityID(id: NodeRef, context:
EntityID) -> GlobalNodeRef` (`:124862`), `GlobalNodeRef.IsDefined`,
  `GameInstance.GetNodeTransform(game, nodeRef: GlobalNodeRef, out
transform) -> Bool` (`:14350`), `GameInstance.FindEntityByID(game,
entityId) -> Entity` (`:14346`), `GameInstance.FindWaypointsByTag(game,
tag, out waypoints: [Vector4])` (`:14344`).
- `gameEntityReference` (script `EntityReference`, size 0x38): `reference:
NodeRef`, `names: [CName]` (community entry names), `dynamicEntityUniqueName:
CName`, `slotName`, `sceneActorContextName`, `type:
gameEntityReferenceType` {EntityRef 0, Tag 1, SlotID 2,
  SceneActorContextName 3}. Resolution:
  - `GetFixedEntityIdsFromEntityReference(puppetRef, game, out
outEntityIds: [EntityID])` — native (`final.reds:154092`);
  - `GetGameObjectFromEntityReference(reference, game, out target) -> Bool`
    — script: the first resolved id through `FindEntityByID`, or the local
    player when the reference equals `CreateEntityReference("#player", [])`
    (`final.reds:179640-179656`);
  - `CreateEntityReference(nodeRefString, communityEntryNames) ->
EntityReference` — native (`:179658`).

  So a quest names a spawned NPC as **(community NodeRef, entry name)**, not
  by EntityID. _(inferred)_ That pair is stable across games and is the
  natural wire name for "the NPC the quest calls Jackie"; the host maps it to
  its entity with `GetFixedEntityIdsFromEntityReference` and to a `NetId`.
  On the guest the proxy is not in the community, so native resolution fails
  there: anything on the guest that resolves references (quest mappins,
  mirrored nodes) needs either the guest to spawn the proxy **through the
  same community entry** (Codeware `ActivateCommunity` with AI then turned
  off) or a hook on reference resolution. That choice is the core design
  question of milestone 5/6.

### 6.5 Teleports

- `TeleportationFacility` (`GameInstance.GetTeleportationFacility(game)`,
  `final.reds:192801-192805`): `Teleport(objectToTeleport: GameObject,
position: Vector4, orientation: EulerAngles)` (`@ 0x1413d0020`),
  `TeleportToNode(objectToTeleport, nodeRef: NodeRef)` (`@ 0x1418c8030`).
- Quest nodes: `questTeleportPuppetNodeDefinition{entityReference:
questUniversalRef, params: questTeleportPuppetParamsV1{destinationRef,
destinationOffset}, lookAtAction, playerLookAt}`,
  `questTeleportVehicleNodeDefinition`, `questTeleport_NodeType` (vehicle
  manager), the device action `QuestForceTeleportToFloor` (lifts,
  `LiftControllerPS.OnQuestForceTeleportToFloor`).
- Leftover native `RepTeleportPlayer(player: wref<GameObject>, pos: Vector3,
rot: EulerAngles)` (global, "replicated teleport"; unverified what it does
  without a net session).

Co-op: the host's quest teleports the **host's** V. **Host sends**
`Regroup{position, yaw, reason}` when its V is teleported by a quest (wrap is
not possible on the native node; detect as the fork did, position jump
between frames, or observe `questTeleportPuppet` in an `ExecuteNode` hook).
**Guest applies** `Teleport(player, pos, rot)` near the host. This is the
"regroup" of milestone 4.

### 6.6 Co-op mapping for the quest runtime

- **Host sends**: quest spawns as proxies (`NetId`, record, appearance,
  reference name), despawns, node visibility and prefab variant changes
  (`{GlobalNodeRef, visible}`, `{prefab NodeRef, variant, show}`), device
  actions (§7), regroup teleports.
- **Guest applies**: proxies; `ToggleNode` / `ToggleVariant`; `Teleport`.
- **Guest must stop**: all phases (quest-free gamedef, §7); community and
  population spawners that are `spawnOnStart` and would create a second copy
  of a host-owned NPC (`DeactivateCommunity`, `DeactivatePopulationSpawner`,
  or globally §10).

---

## 7. Persistent world state: devices, loot, vendors, fast travel, stash

### 7.1 The persistency system

`GamePersistencySystem` (`final.reds:27519-27553`, RTTI
`gamePersistencySystem`, size 0x290, `GameInstance.GetPersistencySystem`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `QueuePSEvent(targetID: PersistentID, psClassName: CName, evt: Event)` | `0x1408025a0` |
| `QueuePSDeviceEvent(action: DeviceAction)` | `0x141073a10` |
| `QueueEntityEvent(targetEntity: EntityID, evt: Event)` | `0x140c594e0` |
| `GetConstAccessToPSObject(targetID: PersistentID, psClassName: CName) -> PersistentState` | `0x140802424` |
| `ForgetObject(targetID: PersistentID, notifyEntity: Bool)` | `0x140abf394` |
| `GetPersistentBool/Int/Float(targetEntity: PersistentID, psClassName, varName: CName)` | `0x142a34940` / `…4634` / `…47b8` |
| `SetPersistentBool/Int/Float(targetEntity, psClassName, varName, newValue)` | `0x142a34e6c` / `…4b24` / `…4cc4` |
| `EntityProxy_DisablePhysics(targetEntity: EntityID, disable: Bool)` | `0x14119ec0c` |

`PersistentState` (`final.reds:14118-14126`): `GetID() -> PersistentID`,
`GetPersistencySystem()`, `GetGameInstance()`,
`ForcePersistentStateChanged()`. Hierarchy: `gamePersistentState` →
`gameObjectPS` (entities: `GameObject.GetPS()`, `GetPersistentID()` =
`Cast<PersistentID>(GetEntityID())`, `final.reds:11449-11524`) and
`gameComponentPS` → `gameDeviceComponentPS` → `SharedGameplayPS` →
`ScriptableDeviceComponentPS` → one `…ControllerPS` per device type. Fields
marked `persistent` in a PS class are what the save stores. Native hooks
known to Codeware: `PersistencySystem_SetPersistentStateData` 546121377,
`PersistentObject_InitializeState` 3442350580.

Key property _(inferred from the API shape, and the reason PS exists)_: a
**PS object exists and accepts events while its entity is streamed out**.
`QueuePSEvent(id, className, evt)` needs no entity handle. So the host can
address any device in the world by `(PersistentID, class name)` and the
guest can apply it even if the device is not loaded yet; the entity picks up
its PS when it streams in.

### 7.2 Device actions are events on a PS

Everything a player, a quickhack or a quest does to a device is a
`DeviceAction` (`final.reds:26111-26126`: `actionName: CName`,
`clearanceLevel`, `SetUp(ps)`, `GetProperties() -> [DeviceActionProperty]`,
`GetPersistentID()`, `GetDeviceClassName() -> CName`) sent to the PS:

- `ScriptableDeviceComponentPS.ExecutePSAction(action, opt layerTag)`
  registers the requester and calls `QueuePSDeviceEvent(action)`
  (`final.reds:34110-34117`); the overload with a `PersistentState` calls
  `QueuePSEvent(ps.GetID(), ps.GetClassName(), action)` (`:34119-34126`).
- `BaseScriptableAction.CompleteAction(game)` ends in
  `QueuePSDeviceEvent(this)` (`final.reds:28124-28128`).
- The PS handles it in `On<ActionClass>(evt) -> EntityNotificationType`
  (e.g. `LiftControllerPS.OnQuestForceTeleportToFloor`) and the return value
  decides whether the entity is notified to play the visual part.
- Quests use the **same path**: `questDeviceManager_NodeType` names
  `deviceControllerClass`, `deviceAction` and properties (§6.1), served by
  115 `Quest…` action classes (`QuestForceEnabled`, `QuestForceON/OFF`,
  `QuestForcePower/Unpower`, `QuestForceAuthorizationEnabled`, door
  `QuestForceOpen…`, `QuestForceTeleportToFloor`, `QuestCustomAction`, …,
  `final.reds:107690-…`, `:121532-121873`).

Example PS state, `DoorControllerPS` `[2.5-rtti]`: `isOpened @0x2d8`,
`isLocked @0x2d9`, `isSealed @0x2da`, `alarmRaised`, `isBusy`, `isLiftDoor`,
`isPlayerAuthorised`, `openingTokens: [EntityID]`.

Co-op mapping _(inferred)_:

- **Host sends**: `DeviceAction{PersistentID (entityHash + componentName),
psClassName, action class name, properties}` for every action its PS
  objects accept. One hook covers player, quickhack and quest sources: the
  native `QueuePSDeviceEvent` / `QueuePSEvent` (handlers above), or script
  wraps of `ExecutePSAction` and `CompleteAction` (these miss
  `questDeviceManager` nodes, which enter natively).
- **Guest applies**: build the action by class name, `SetUp` on the PS from
  `GetConstAccessToPSObject`, set properties, `QueuePSDeviceEvent`.
  Fallback for simple state: `SetPersistentBool(id, class, n"isOpened", …)`
  — it changes the stored field but _(inferred)_ does not run the action's
  side effects or notify the entity.
- **Guest → host**: a guest opening a door runs the action in its own game
  first (principle 2: your V never waits). It must also be sent to the host
  and relayed, with the host's order winning on conflict.
- **Guest must stop**: nothing by default; devices have no AI. Exceptions
  are devices that **decide** things: security systems, turrets and cameras
  (they target players and raise alarms: host-owned, like NPCs),
  `CommunityProxyPS` (links NPCs to access points), and devices that write
  facts (§2.3).
- Device identity: placed devices have static EntityIDs, so the
  `PersistentID` is the same in every game.

Related systems with their own saved state: `gameDestructionPersistencySystem`
(destroyed objects), `gameTransformAnimatorSaveSystem` and
`gameMovingPlatformsSavedState` (moving platforms, lifts),
`gameEffectSpawnerSaveSystem`, `gameAnimationPersistentDataSystem`. None has
script functions.

### 7.3 Loot containers and drops

- Classes: `gameLootContainerBase : gameObject` (`lootQuality`,
  `hasQuestItems`, `isIconic`, `lootTables: [TweakDBID]`, `useAreaLoot`,
  `contentAssignment`, `isIllegal`, `containerType`, `wasLootInitalized`;
  natives `IsIllegal()`, `IsEmpty()`, `IsLogicReady()`) →
  `gameContainerObjectBase` (`lockedByKey: TweakDBID`) →
  `gameContainerObjectSingleItem`, `ShardCaseContainer`,
  `LootContainerObjectAnimatedByTransform`; `gameLootSlot` (`dropChance`,
  `lootState: gameLootSlotState{Looted 1, Unavailable 2}`); `gameLootBag`
  (bodies' bags); `gameLootObject` (`lootID`) → `gameItemDropObject`
  (`spawnedItemID`, `GetItemEntityID()`, `GetItemObject()`).
- PS: `gameLootContainerBasePS{markAsQuest, isDisabled, isLocked,
shouldHideLockedUI, started}` with events
  `SetContainerStateEvent{isDisabled}`, `ToggleContainerLockEvent{isLocked}`.
- Contents are an **inventory on the container entity**, generated on first
  attach from loot tables (`wasLootInitalized`; `GameObject` events
  `OnInventoryEmptyEvent`, `OnItemAddedEvent`, `OnItemRemoveddEvent`). Read
  with `TransactionSystem.GetItemList(obj, out itemList)`; moved with
  `TransferItem`, `TransferAllItems`, `TakeItem(newOwner, itemToLoot)`.
- `LootManager` (`GameInstance.GetLootManager`, `final.reds:202205-202229`):
  `GenerateLoot(lootID: TweakDBID, out itemList: [ItemModParams], opt
gameNode)`, `SpawnItemDrop(obj, itemID, opt position: Vector4, opt
rotation: Quaternion) -> Bool` (`@ 0x142b01828`),
  `SpawnItemDropOfManyItems(obj, itemInstructions: [DropInstruction{itemID,
quantity}], dropItemName: CName, position, rotation) -> Bool`
  (`@ 0x142b019bc`), `SpawnThrowableItemDrop`, `GetDefaultDropItemName()`,
  `PurgeLoot()`.
- `ContainerManager` (`GameInstance.GetContainerManager`,
  `final.reds:315453-315457`): `InjectLoot(id: EntityID, itemID, quantity:
Uint32, opt dynamicTags)`, `InjectLootModParams(id, params)`.
- Events: `ItemLootedEvent` (to the item object, `final.reds:52845`,
  `:295251`), `gameOnLootEvent`, `gameOnLootAllEvent`,
  `gameeventsLootedItemEvent`, `gameRegenerateLootEvent`,
  `gameeventsReloadLootEvent`.
- Quest control: `questInjectLoot_NodeType`, `questReInitContainers…`,
  `questResetContainers…`, `questLootTokenManager_NodeType`
  (`questLootTokenState` {Enabled, Disabled, Sealed, Unsealed}),
  `questSetLootInteractionAccess_NodeType`, `questLootPurge_NodeType`.

Co-op (ARCHITECTURE.md: "loot drops — host; each player loots their own
copy"):

- **Placed containers** have static IDs and roll loot per game: each player
  already has "their own copy" with no code. Only quest state needs sync:
  lock/disable (`gameLootContainerBasePS` events, through §7.2) and quest
  items injected by the host (`InjectLoot` on the guest with the same
  `EntityID`).
- **NPC drops** come from the host's NPC dying. **Host sends** `LootDrop{
NetId of the body, position, [itemID TweakDBID + quantity]}` (read the
  body's inventory after death). **Guest applies**
  `SpawnItemDropOfManyItems(player, instructions, GetDefaultDropItemName(),
pos, rot)` or fills the proxy body's inventory. Item IDs carry a random
  seed (`gameItemID`): send the TweakDBID and let the guest roll, or send the
  seed to keep stats identical (decide).
- **Guest must stop**: its proxies generating their own loot on death (the
  proxy "dies" by host order; loot generation is part of the NPC death
  pipeline — needs checking, §14).
- **Quest items** (`hasQuestItems`, `markAsQuest`): only the host's pickup
  advances the quest (the quest waits on an inventory condition in the
  host's game). A guest picking up its copy does nothing; decide whether the
  guest's pickup becomes a "give to host" request.

### 7.4 Vendors

`MarketSystem : IMarketSystem : ScriptableSystem` (`final.reds:130218-130588`):
`persistent m_vendors: [Vendor]` (stock, sold items, price multipliers live
in the save); natives `AddVendorHashMap(key: EntityID, tweakID, vendor)`,
`GetVendorHashMap(key)`, `GetVendorTBIDHashMap(key: TweakDBID)`,
`ClearVendorHashMap()`; statics `GetVendor`, `GetVendorMoney`,
`GetVendorItemsForSale`, `GetBuyPrice`, `OnVendorMenuOpen`, `Money() ->
ItemID`; requests `AttachVendorRequest`, `DeattachVendorRequest`,
`BuyRequest`, `BuybackRequest`, `SellRequest`, `DispenseRequest`,
`AddItemToVendorRequest`, `SetVendorPriceMultiplierRequest`. Quest side:
`questVendorPanel_NodeType`.

Co-op: stock is generated and saved **per game**. With money and inventory
per player (authority table), per-player vendors are consistent and free.
Only quest-driven vendor changes (`AddItemToVendorRequest`,
`SetVendorPriceMultiplierRequest`) would be forwarded from the host. Nothing
to suppress.

### 7.5 Fast travel

`FastTravelSystem` scriptable system (`final.reds:50235-50901`): `persistent
m_fastTravelNodes: [FastTravelPointData]`, `persistent m_fastTravelLocks:
[FastTravelSystemLock]`; `FastTravelPointData{mappinID, pointRecord:
TweakDBID, markerRef: NodeRef, requesterID: EntityID, isEP1}`. Requests:
`RegisterFastTravelPointRequest{pointData, requesterID}`,
`RegisterFastTravelPointsRequest`, `UnregisterFastTravelPointRequest`,
`PerformFastTravelRequest{pointData, player}`, `EnableFastTravelRequest`,
`RemoveAllFastTravelLocksRequest`, `FastTravelMenuToggledEvent`,
`UpdateFastTravelPointRecordRequest`. Statics: `AddFastTravelLock(reason:
CName, game, opt statusEffectID)`, `RemoveFastTravelLock`,
`ManageFastTravelLock(enable, reason, game, …)`,
`RemoveAllFastTravelLocks(game)` (`final.reds:50764-50786`). Queries:
`GetFastTravelPoints()`, `HasFastTravelPoint`, `IsFastTravelEnabled()`.
`PerformFastTravel` shows a loading screen, then **requests an autosave**
(`m_requestAutoSafeAfterLoadingScreen`, `:50323`, `:50723-50735`) and sets
metro facts.

Co-op (milestone 4, "regroup on fast travel"): **host sends**
`FastTravel{pointRecord}` from a wrap of `OnPerformFastTravelRequest`;
**guest applies** by queuing its own `PerformFastTravelRequest` to the same
point (it gets the loading screen and streaming for free) or a plain
teleport. **Guest must stop** travelling alone while tethered:
`FastTravelSystem.AddFastTravelLock(n"tandem", game)`. Discovered points are
per save; unlocked points can be mirrored by replaying
`RegisterFastTravelPointRequest`, or left per player.

### 7.6 Apartment and stash

`Stash : InteractiveDevice` (`final.reds:132145-…`), `StashControllerPS`,
action `OpenStash` → `Stash.OnOpenStash`; `EGameplayRole.PlayerStash`,
`GameObject.IsPlayerStash()` (`:12696`). _(inferred)_ All player stashes show
one shared inventory stored in the save, so it is **per player** like the
rest of the inventory: no sync. Apartments are unlocked by facts and quest
prefabs (door facts, e.g. `q302_squat_sh_unlocked` at `final.reds:295911`):
they follow the fact mirror, with the prefab caveat of §6.3.

---

## 8. Time of day and weather

### 8.1 Game time

`TimeSystem` (`final.reds:53842-53921`, RTTI `gameTimeSystem`, size 0x6ebc0,
`GameInstance.GetTimeSystem`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `GetGameTime() -> GameTime` | `0x1412aec64` |
| `GetGameTimeStamp() -> Float` | `0x1418432ac` |
| `GetSimTime() -> EngineTime` | `0x140ebc124` |
| `SetGameTimeBySeconds(seconds: Int32)` | `0x142aa9cd8` |
| `SetGameTimeByHMS(hours: Int32, minutes: Int32, seconds: Int32, opt reason: CName)` | `0x142aa9d50` |
| `SetGameTime()` (RTTI only; same handler as `SetGameTimeBySeconds`) | `0x142aa9cd8` |
| `SetPausedState(paused: Bool, source: CName)` — **RTTI signature**; the script decl has no parameters (`final.reds:53918`) | `0x142aa9ea0` |
| `IsPausedState() -> Bool` | `0x142aa972c` |
| `RealTimeSecondsToGameTime(seconds: Float) -> GameTime` | `0x1418f0bd8` |
| `SetTimeDilation(reason, dilation, opt duration, easeInCurve, easeOutCurve, opt listener)` / `UnsetTimeDilation(reason, opt easeOutCurve)` | `0x1407d62b0` / `0x1407d64f4` |
| `SetTimeDilationOnLocalPlayerZero(…)` / `UnsetTimeDilationOnLocalPlayerZero(…)` / `SetIgnoreTimeDilationOnLocalPlayerZero(ignore)` | `0x1408a0c90` / `0x1408a0aa0` / `0x14131b2f8` |
| `IsTimeDilationActive(opt reason) -> Bool`, `GetActiveTimeDilation(opt reason, opt ignoreCurves) -> Float` | `0x140e35e58`, `0x141190c94` |
| `RegisterListener(entity, eventToDelay, expectedTime: GameTime, repeat: Int32, sendOldNoifications) -> Uint32`, `RegisterDelayedListener`, `RegisterIntervalListener`, `RegisterScriptableSystemIntervalListener`, `UnregisterListener(id)` | — |

`GameTime` is **one `Int32` of seconds since day 0** (`rtti: GameTime`,
`seconds @0`): statics `MakeGameTime(days, hours, minutes, seconds)`,
`GetSeconds`, `Days`, `Hours`, `Minutes`, `Seconds`, `IsAfter`. It fits the
protocol as an integer; no float is involved. Also
`GameInstance.GetGameTime(game)`, `GetEngineTime`, `GetSimTime`,
`GetPlaythroughTime` (`final.reds:14146-14152`).

Who changes time in vanilla: the wait menu,
`TimeskipGameController.Apply()` → `SetGameTimeByHMS(hours + skip, minutes,
seconds, n"ui_menu_timeskip")` (`final.reds:383716-383732`; hours may exceed
23, i.e. the call normalizes); quest nodes `questSetTime_NodeType`,
`questShiftTime_NodeType`, `questPauseTime_NodeType{source, pause}` (many
story scenes pin the clock); sleeping (quest). The rate of game time
against real time is not exposed (no multiplier in scripts or RTTI); use
`RealTimeSecondsToGameTime(1.0)` to measure it.

Co-op (milestone 4):

- **Host sends** `WorldClock{gameTimeSeconds: Int32, paused: Bool,
hostClockUs}` every few seconds and immediately after a jump (detect: the
  difference between two samples disagrees with elapsed real time; or wrap
  `TimeskipGameController.Apply`).
- **Guest applies** `SetGameTimeBySeconds(seconds + drift since
hostClockUs)` only when it is off by more than a threshold (a set every
  frame would retrigger time listeners and lighting blends _(inferred)_),
  and `SetPausedState(paused, n"tandem")` to follow quest clock pins.
- **Guest must stop**: the wait menu (`TimeskipGameController`; or accept it
  as a request to the host), and any time dilation that slows the world
  (../architecture/time.md "slowed and stopped time"): the engine already separates
  world dilation from `…OnLocalPlayerZero` (the local player), which is the
  hook for "a V's time effects are local".
- Time listeners (`RegisterIntervalListener`) on the guest fire on the
  guest's clock: shops opening, lights, community phases by time of day
  (`communityTimePeriod`) follow once the clock follows.

### 8.2 Weather

`WeatherSystem` in scripts (`final.reds:252163-252175`), RTTI
`worldWeatherScriptInterface` (size 0x50), `GameInstance.GetWeatherSystem`
(`final.reds:14314`). **Vanilla exposes only rain read-outs**:

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `RegisterWeatherListener(callback: WeatherScriptListener) -> Uint32` | `0x14087072c` |
| `UnregisterWeatherListener(callbackId: Uint32)` | `0x1413e9070` |
| `GetRainIntensity() -> Float` | `0x14297140c` |
| `GetRainIntensityType() -> worldRainIntensity` {NoRain 0, LightRain 1, HeavyRain 2} | `0x142971424` |
| `GetRainMoisture() -> Float` | `0x14297143c` |
| `GetRainPuddles() -> Float` | `0x142971478` |

`WeatherScriptListener` (`final.reds:252103-252107`):
`OnRainIntensityChanged(rainIntensity: Float)`,
`OnRainIntensityTypeChanged(rainIntensityType: worldRainIntensity)`; one
vanilla user, `CityLightSystem` (`:251530`).

Setting and reading the state is **Codeware** (`CW
scripts/World/WeatherSystem.reds`, `CW src/App/World/WeatherSystemEx.cpp`)
`[2.5-CW]`, visible in the RTTI dump as natives on
`worldWeatherScriptInterface`:

```
SetWeather(weather: CName, opt blendTime: Float, opt priority: Uint32) -> Bool
ResetWeather(opt forceRestore: Bool, opt blendTime: Float) -> Bool
GetWeatherState() -> ref<worldWeatherState>
GetEnvironmentDefinition() -> ref<worldEnvironmentDefinition>
```

How Codeware does it (facts for a native implementation without the script
layer): `worldRuntimeSystemWeather*` at `worldWeatherScriptInterface + 0x40`
(also `RuntimeScene + 0x280` as a handle); address hashes
`RuntimeSystemWeather_SetWeatherByName` **2334794340** `bool (system, CName
weather, CName source, float blendTime, 0, uint32 priority)`,
`RuntimeSystemWeather_SetWeatherByIndex` **2987664085**,
`RuntimeSystemWeather_GetWeatherState` **2679119876** `(system,
Handle<worldWeatherState>& out)`,
`RuntimeSystemWeather_GetEnvironmentDefinition` **1471948702**; the bool at
`system + 0x8C` is "cycle weather": `SetWeather` clears it (the weather
stays), `ResetWeather` sets it again and, with `forceRestore`, sets index 0
(`CW src/Red/RuntimeScene.hpp:34-81`). `worldRuntimeSystemWeather` is size
0x230 in the 2.50 dump.

`worldWeatherState` `[2.5-rtti]`: `name: CName @0xC0`, `minDuration`,
`maxDuration`, `probability`, `transitionDuration` (curves over the day),
`environmentAreaParameters`, `effect`. Vanilla state names in the name pool:
`24h_weather_sunny`, `24h_weather_rain`, `24h_weather_fog`,
`24h_weather_pollution`, `24h_weather_rain_toxic_rain`,
`24h_weather_sandstorm`, `24h_weather_light_clouds`, `24h_weather_cloudy`,
`24h_weather_heavy_clouds`, plus quest states; the pool on the game machine
also holds states added by the player's weather mods, so **take the name
from `GetWeatherState().name` on the host, never from a hard-coded list**,
and expect a guest without the same mods to refuse an unknown name
(`SetWeather` returns false).

Quest and area control: `questPlayEnv_SetWeather{weatherID: TweakDBID
(gamedataWeather_Record), source, blendTime, priority, reset}`,
`questPlayEnv_NodeType` (area environment override), `worldWeatherAreaNotifier`
(trigger areas that force a weather), `questWeather_ConditionType`. The fact
`ue_metro_weather` is read by scripts. Environment as a whole
(`worldRuntimeSystemEnvironment`, size 0x210; `worldEnvironmentDefinition`;
`worldEnvironmentAreaParameters`) has no script functions: area parameters
are applied by trigger areas per player position, i.e. they follow the
guest's own V correctly without sync.

Co-op (milestone 4):

- **Host sends** `Weather{name: CName as string or hash, blendTime}` when
  `GetWeatherState().name` changes (poll every second or two; there is no
  state-change callback, only the rain listeners) and in the join snapshot.
- **Guest applies** `SetWeather(name, blendTime, priority)`; this also stops
  the guest's own weather cycle (cycle flag cleared), which is the "guest
  must stop" part. On leaving the session: `ResetWeather(true)`.
- Priority: quest weather uses its own `priority`; choose one above the
  cycle and note that a weather **area** on the guest can still override
  locally (correct: it is tied to position).
- Rain wetness (`GetRainMoisture`, `GetRainPuddles`) evolves from the state
  over time; _(inferred)_ it converges by itself once the state matches.

---

## 9. Police: the prevention system

Two layers:

1. `PreventionSystem` — a **script** `ScriptableSystem`
   (`final.reds:138624-143058`, ~4,400 lines): crime score, heat stage, star
   UI, deciding what to spawn and when.
2. `PreventionSpawnSystem` — **native** (`final.reds:189163-189344`, RTTI
   `gamePreventionSpawnSystem`, size 0x368,
   `GameInstance.GetPreventionSpawnSystem`): finds spawn points, spawns and
   despawns units, vehicles, roadblocks and AVs.

Plus `PreventionSystemHackerLoop` (vehicle hacking at high heat),
`PoliceRadioSystem`, `PoliceAgentRegistry` (`PreventionSystem.GetAgentRegistry`).

### 9.1 State and how to read it

<!-- prettier-ignore -->
| What | Where |
| --- | --- |
| heat stage | `m_heatStage: EPreventionHeatStage` {Heat_0 … Heat_5, Size 6, Invalid 7}; `GetHeatStage()`, `GetHeatStageAsInt() -> Uint32` (`final.reds:138892-138902`) |
| chasing | `IsChasingPlayer()` = heat ≠ Heat_0 (`:138856`); static `PreventionSystem.IsChasingPlayer(game)` (`:142191`) |
| star UI state | `GetStarState() -> EStarState` (Default / Active / blinking / grey), `m_forceEternalGreyStars` (persistent) |
| enabled / locked | `IsSystemEnabled()` (`m_systemEnabled`, persistent), `IsSystemLocked()`, `m_systemLockSources: [CName]` (persistent) |
| crime score | `m_totalCrimeScore: Float`, `m_crimeScoreMultiplierByQuest` (persistent) |
| quest blocks | `m_blockOnFootSpawnByQuest`, `m_blockVehicleSpawnByQuest`, `m_blockReconDroneSpawnByQuest` (persistent), `m_minHeatLevel`, `m_maxHeatLevel` |
| UI blackboard | `UI_WantedBar` (`final.reds:156450-156460`): `CurrentWantedLevel: Int`, `DeescalationStages: Int`, `CurrentChaseState: Name`, `BlinkingStarsDurationTime: Float`, `IsDogtown: Bool` |
| facts | `wanted_level` (the stage), `wanted_chase_active` (0/1) (`:139587-139609`), `prevention_quest_disabled` |
| last known position | `GetLastKnownPlayerPosition()`, `GetLastKnownPlayerVehicle()` |

`ChangeHeatStage(newHeatStage, reason)` (`final.reds:140733-140767`, private)
is the single place the stage changes: it clamps to min/max, writes
`UI_WantedBar.CurrentWantedLevel` and calls `OnHeatChanged`. The **simplest
host read is a blackboard listener on `UI_WantedBar.CurrentWantedLevel`**
(vanilla HUD code does it, `final.reds:92337`, `:155782`) or a wrap of
`ChangeHeatStage`.

### 9.2 Inputs (requests) and how to force it

All are `ScriptableSystemRequest`s queued to `n"PreventionSystem"`
(`PreventionSystem.QueueRequest(game, request, opt delay)`,
`final.reds:142278`):

<!-- prettier-ignore -->
| Request | Effect |
| --- | --- |
| `SetWantedLevel{m_wantedLevel: EPreventionHeatStage, m_forceGreyStars, m_resetGreyStars, m_forcePlayerPositionAsLastCrimePoint, m_forceIgnoreSecurityAreas}` (`:199475`) | **the quest way to force a stage**: `OnSetWantedLevel` (`:141996-142023`) enables the system and calls `ChangeHeatStage(level, "QuestEvent")`; `Heat_0` while chasing queues a forced de-escalation |
| `TogglePreventionSystem{sourceName: CName, isActive: Bool}` (`:199462`) | adds/removes a named lock; any lock → `execInstructionOff()` = clear everything then disable (`:141967-141988`, `:142693`) |
| `PreventionConsoleInstructionRequest{instruction: EPreventionSystemInstruction, heatStage}` (`:199612`) | `Safe 0` (reset to no heat, despawn), `Active 1` (set stage), `Off 2`, `On 3`, `ReconPhaseOn/Off` (`:142531-142556`) |
| `PreventionForceDeescalateRequest{fakeBlinkingDuration, telemetryInfo}` (`:143060`) | drop to no heat with blinking stars |
| `PreventionMinMaxHeatLevels{minLvl, maxLvl, isDefault}` (`:199617`) | clamp the stage |
| `ClearPreventionSystemLocks`, `PreventionConsoleLockRequest` | remove all locks / toggle the hard lock |
| `TogglePreventionCrowdSpawns{toggle}` | → native `TogglePreventionCrowdSpawns` (police walking in crowds) |
| `TogglePreventionFreeArea`, `ToggleQuestPreventionTrigger`, `TogglePreventionGlobalQuestObjective`, `SetPreventionDifficulty`, `SetPreventionPath`, `SetBlockShootingFromVehicle` | quest controls |
| `PreventionDamageRequest`, `PreventionCrimeWitnessRequest`, `PreventionCombatStartedRequest`, `PreventionVehicleStolenRequest`, `PreventionVisibilityRequest`, `PreventionRegisterRequest`, `PreventionSecurityAreaRequest`, `PreventionPoliceSecuritySystemRequest` | **the crime inputs** (`OnDamageInput`, `:140641`; statics `CreateNewDamageRequest`, `CrimeWitnessRequestToPreventionSystem`, `CombatStartedRequestToPreventionSystem`, `:142293-142376`) |
| `PreventionUnitSpawnedRequest`, `PreventionUnitDespawnedRequest`, `PreventionTickRequest`, timers | internal |

`execInstructionSafe(reason)` (`final.reds:142630-142668`) shows what "clear"
means: despawn all police, zero the crime score, neutral attitudes, cancel
spawn requests, `ChangeHeatStage(Heat_0)`, reset the star UI and the
`wanted_level` fact.

### 9.3 Native spawn system

Selected functions (full list: `rtti: gamePreventionSpawnSystem`, 50
functions) `[2.5-rtti]`:

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `TogglePreventionActive(isActive: Bool)` | `0x141197534` |
| `TogglePreventionCrowdSpawns(toggle: Bool)` | `0x14184300c` |
| `RequestUnitSpawn(recordID: TweakDBID, spawnTransform: WorldTransform) -> Uint32` | `0x140fdfe80` |
| `RequestChaseVehicle(vehicleRecordID, passengersRecordIDs: [TweakDBID], strategy: BaseStrategyRequest) -> Uint32` | `0x1414aabd8` |
| `RequestRoadBlockadeSpawn(vehiclessRecordIDs, spawnDistanceRange: Vector2, numberNPCsPerCar: Uint32) -> Uint32` | `0x142bc2e80` |
| `RequestAVSpawn(recordID, spawnDistanceRange, useOffTrafficPoints) -> Uint32`, `RequestAVSpawnAtLocation(recordID, location: Vector3)`, `RequestAVSpawnPoints(scriptable, functionName, …)` | `0x142bc2840`, `0x142bc2950`, `0x142bc2a38` |
| `CancelSpawnRequest(requestID)`, `CancelAllSpawnRequests()` | `0x142bc249c`, `0x142bc2490` |
| `RequestDespawn(entityID)`, `RequestDespawnVehicleAndPassengers(vehicle)`, `RequestDespawnAll(shouldUseAggressiveDespawn: Bool)` | `0x142bc2cb4`, `0x142bc2d90`, `0x142bc2d20` |
| `GetNumberOfSpawnedPreventionUnits() -> Int32`, `IsEntityRegistered(id) -> Bool`, `TryGetVehicleType(id, out DynamicVehicleType) -> Bool` | `0x142bc25f0`, `0x14044cad4`, `0x141202a14` |
| `RegisterEntityDeathCallback(scriptable, functionName: String, entityID)`, `MarkAsDead(entityID)` | `0x140ab3020`, `0x142bc27d8` |
| `ToggleFreeArea(areaReference: NodeRef, enable)`, `TogglePreventionQuestTrigger(areaReference, enable)`, `IsPointInPreventionFreeArea(point: Vector4) -> Bool` | `0x142bc2fb0`, `0x142bc310c`, `0x14192b4e0` |
| `FindPursuitPointsRangeAsync(…)`, `IsPlayerOnHighway()`, `IsPlayerInDogTown()`, `GetNearestRoadFromPlayerInfo(out info)`, `NotifyPlayerMounted(isPlayerMounted)`, `ReinitAll()` | — |
| script callbacks called by native: `SpawnRequestFinished(requestResult: SpawnRequestResult)`, `DespawnCallback(entityID)`, `VehicleEarlyInit`, `RoadblockadeNPCEarlyInit`, `OnEnterPreventionQuestTrigger(disablePreventionSystem: Bool)` | `final.reds:189272-189339` |

Saved native state: `gamePreventionSpawnSystemSavedState{areaIds: [Uint64]}`.
The API is written around **"the player"** (`IsPlayerOnHighway`,
`GetIntersectionInFrontOfPlayerPos`, `m_player: wref<PlayerPuppet>`,
`GetLastKnownPlayerPosition`): single-target by construction.

### 9.4 Co-op mapping

Authority: host ("police heat — host — applied").

- **Host reads and sends** `PoliceHeat{stage, starState, blinkDuration}`
  from the `UI_WantedBar` blackboard / `ChangeHeatStage` wrap. Police units
  are host NPCs and vehicles: they reach guests as ordinary proxies
  (milestone 5), nothing special.
- **Guest applies**: display only. Write `UI_WantedBar.CurrentWantedLevel`
  (and `CurrentChaseState`, `BlinkingStarsDurationTime`) and the
  `wanted_level` / `wanted_chase_active` facts so the guest's HUD, minimap
  and scripts that read them agree. Do **not** queue `SetWantedLevel` on the
  guest: it would enable the guest's own system and spawn police.
- **Guest must stop** its own prevention entirely:
  `TogglePreventionSystem{sourceName: n"tandem", isActive: false}` at join
  (the lock list is `persistent`: it is written into the guest's save —
  remove it with `isActive: true` on leave, and re-assert at every join),
  and/or native `TogglePreventionActive(false)`. With the system off the
  guest's crimes are not scored locally.
- **Guest crimes count on the host**: forward the crime inputs. A guest
  hitting a host NPC already reaches the host as a hit event (ARCHITECTURE,
  combat); the host's `ScriptedPuppet` hit handling then raises
  `PreventionDamageRequest` in the host's game naturally. Crimes that are
  not hits on NPCs (stealing a vehicle, witnessed weapon fire, security
  areas) need `PreventionVehicleStolenRequest` /
  `PreventionCrimeWitnessRequest{criminalPosition}` forwarded.
- Limitation: the host's system chases **the host's V** (last known
  position, spawn points "in front of player", de-escalation when the host
  escapes). A guest who commits the crime far from the host gets stars but
  no police near them. Acceptable while players are tethered; a per-player
  wanted level would need the script system reworked to track several
  targets (`SetLastKnownPlayerPosition(game, pos)` is a public static,
  `final.reds:142214`: the host could at least point police at the guest).
- Saving is locked while chased (`PreventionSystem.IsSavingLocked()` =
  `IsChasingPlayer()`, `:138868`); host only, guests are locked anyway (§11).

---

## 10. Crowds and traffic

What exists `[2.5-rtti]` / `[2.5-script]`:

<!-- prettier-ignore -->
| Lever | Where | Notes |
| --- | --- | --- |
| `CommunitySystem.ChangeDensityModifier(modiefier: Float)` (sic) / `ResetDensityModifier()` | `final.reds:315514-315516`, `@ 0x142bcd748` / `0x142bcd998` | **never called by vanilla scripts**; a global crowd density scale _(inferred from the name)_; untested |
| `CommunitySystem.EnableDynamicCrowdNullArea(areaLocalBBox: Box, areaLocalToWorld: WorldTransform, savable: Bool, duration: Float) -> Uint64` / `DisableCrowdNullArea(areaId: Uint64)` | `final.reds:315505-315512`, `@ 0x142bcd848` / `0x142bcd7e4` | a box in which no crowd spawns; `savable` writes it to the save — pass false |
| quest node `questCrowdManagerNodeType_ControlCrowd{action: Disable/Enable, distantCrowdOnly, debugSource}` | `questCrowdManagerNodeDefinition` | the quests' "turn crowds off"; executable through Codeware `ExecuteNode` _(inferred, same family as §6.3)_ |
| quest node `questCrowdManagerNodeType_EnableNullArea{areaReference: NodeRef, enable}` | same | toggles a placed `worldCrowdNullAreaNode` |
| `questInstancedCrowdControlNodeDefinition{crowdVariantTag: CName, enable}` | — | instanced (decorative) crowds |
| `questPopulactionControllerNodeDefinition{enable}` | — | one spawner object |
| Codeware `DeactivateCommunity` / `DeactivatePopulationSpawner` | §6.2 | per community / spawner |
| `PreventionSpawnSystem.TogglePreventionCrowdSpawns(false)` | §9.3 | police in crowds |
| `gameCrowdMemberComponent` on each crowd NPC; AI conditions `IsCrowdNPC`, `NPCIsCrowdPrereq` | — | to recognise a crowd NPC on the host |
| user setting crowd density (gameplay/performance) | not in scripts (no `CrowdDensity` string in `final.reds`) | engine-side option; reachable only through `UserSettings` by group/var name — needs the name |

**Traffic has no switch.** `TrafficSystem` (script,
`final.reds:317434-317446`, RTTI `worldTrafficScriptInterface`,
`GameInstance.GetTrafficSystem`) has two queries only:
`IsPathIntersectingWithTraffic(path, distanceFromTraffic, out intersection)
-> Bool` and `FindEntitiesNearPlane(…)`. The runtime system is
`worldRuntimeSystemTraffic` (size 0x92d0, no RTTI functions). There is no
`vehicleTrafficSystem` or `gameTrafficSystem` class. Traffic lanes and
"null areas" exist as data (`worldTrafficNullAreaCollisionResource`,
`worldTrafficNullAreaDynamicBlockade`, `worldTrafficLanePersistent`), and
vehicles expose `IsInTrafficPhysicsState()`, `HasTrafficSlot()`. The
quest-side lever for vehicles is `questDynamicSpawnSystem…` (waves) and
`questJoinTraffic_NodeType`.

Community and crowd timing: `communityTimePeriod`,
`gameCrowdPhaseTimePeriod`, `communityECommunitySpawnTime` — phases by time
of day, another reason the guest's clock must follow (§8.1).

Co-op mapping (authority: "spawns: crowds, traffic, encounters — host —
guests' own spawning is suppressed"):

- **Guest must stop** spawning crowd pedestrians and traffic. Candidates in
  order of cost: (1) `ChangeDensityModifier(0.0)` — one call, untested,
  probably pedestrians only; (2) `ControlCrowd{Disable}` through
  `ExecuteNode`; (3) a large dynamic null area around the guest's V, moved
  as it moves; (4) native work on `worldRuntimeSystemTraffic` for vehicles.
  None is verified; **traffic suppression has no known script or Codeware
  path** and is the open item of ../architecture/npcs.md's "suppressing guests' own
  world simulation".
- **Host sends** crowd and traffic entities as proxies with a budget
  (distance and relevance), like other NPCs.
- **Guest applies** proxies. Parked cars are placed
  (`worldCompiledCrowdParkingSpaceNode`): _(inferred)_ rolled per game, so
  they differ between games unless spawned from the host's list.

---

## 11. Saves and checkpoints

There is **no `gameSaveSystem` class** in RTTI. Saving is reached through:

<!-- prettier-ignore -->
| Entry point | Where |
| --- | --- |
| `gameAutoSaveSystem` (`GameInstance.GetAutoSaveSystem`, `final.reds:14320`, `:203129-203135`): `RequestCheckpoint() -> Bool` (`@ 0x142b4a398`), `RequestForcedCheckpoint() -> Bool` (`@ 0x142199c50`), `InvalidateAutoSaveRequests()` (`@ 0x142b4a390`) | autosaves |
| `inkISystemRequestsHandler` (`GetSystemRequestsHandler()` on UI controllers, `final.reds:272564-272743`): `QuickSave()` (`@ 0x14279128c`), `ManualSave(saveName: String)`, `OverrideSave(saveId)`, `LoadSavedGame(saveId: Int32)`, `LoadLastCheckpoint(onlySamePlaythrough: Bool)` (`@ 0x142790f6c`), `HasLastCheckpoint() -> Bool` (`@ 0x142790dd4`), `RequestSavesForLoad()`, `RequestSavesCountSync() -> Int32`, `GetLatestSaveMetadata()`, `DeleteSavedGame(saveId)`, `GotoMainMenu()`, `PauseGame()`, `UnpauseGame()`, `IsGamePaused()`, `StartNewGame(state)` | menus |
| `GameObject.RequestAutoSave(isForced)`, `RequestAutoSaveWithDelay(value, maxAttempts, isForced)`, `OnAutoSaveEvent(evt: AutoSaveEvent)` (`final.reds:14091-14115`); `MenuUIUtils.RequestAutoSave(player, opt delay)` (`:236456`) | script helpers |
| quest node `questCheckpointNodeDefinition{saveLock, ignoreSaveLocks, pointOfNoReturn, endGameSave, retryOnFailure, additionalEndGameRewardsTweak}` | story checkpoints |
| `questPhaseNodeDefinition.saveLock`, `questSaveLock_ConditionType` | quest-held locks |

(§7 said 2.31 scripts expose only `LoadLastCheckpoint`; on 2.50
`LoadSavedGame(saveId)` and `ManualSave(saveName)` are declared as natives.)

Who requests autosaves in scripts: fast travel
(`FastTravelSystem.RequestAutoSave`, `final.reds:50734`), sleeping/consuming
(`PlayerPuppet`, `:41521`), drop points (`DropPointControllerPS`, `:202621`),
menus (`:236460`); plus the engine's periodic autosave and quest checkpoints.

### 11.1 Save locks

- Query: `GameInstance.IsSavingLocked(game, out locks: [gameSaveLock]) ->
Bool` (`final.reds:14340`); `gameSaveLock{reason: gameSaveLockReason}`
  with reasons Nothing 0, Combat 1, Scene 2, Quest 3, **Script 4**,
  Boundary 5, MainMenu 6, LoadingScreen 7, PlayerStateMachine 8,
  PlayerState 9, Tier 10, NotEnoughSlots 11, NotEnoughSpace 12,
  PlayerOnMovingPlatform 13, SyncToMusicIsEnabledInAnyActiveScene 14,
  PhotoMode 15, GameServices 16. The pause menu checks it before saving and
  shows the reasons (`final.reds:340423-340448`).
- **Every `ScriptableSystem` can veto saving**: the base class declares
  `private func IsSavingLocked() -> Bool { return false; }`
  (`final.reds:48411`) and four systems override it — `PreventionSystem`
  (chased, `:138868`), `TakeOverControlSystem` (controlling a device,
  `:154130`), `BraindanceSystem` (`:247782`) and `SaveLocksManager`
  (`:195157`). _(inferred)_ the engine polls these and reports them as
  reason `Script`. The campaign fork used exactly this override (§7).
- `SaveLocksManager` (`final.reds:195154-195196`): `m_saveLocks: [CName]`
  (**not persistent**); statics `RequestSaveLockAdd(game, reason: CName)` and
  `RequestSaveLockRemove(game, reason)` queue a `SaveLockRequest{operation:
EItemOperationType, reason}`. Vanilla reasons: `n"ReplayTutorial"`,
  `n"PersonalLink"`, `n"PlayerBeingHacked"`, `n"Autodrive"`.
- Quest checkpoints can carry `ignoreSaveLocks`: _(inferred)_ a script lock
  does not stop those. Irrelevant on a guest (no quests run).

### 11.2 Death and "load last checkpoint"

On death the player state machine opens the death menu unless the fact
**`block_death_menu == 1`** (`HighLevelTransition.IsDeathMenuBlocked`,
`final.reds:283760-283764`). The death menu offers `LoadLastCheckpoint(true)`
when `HasLastCheckpoint()` (`:335186-335232`).

### 11.3 Co-op mapping

../architecture/world.md: "the host's save is the world; guests keep their character
progression in their own saves."

- **Guest must stop**:
  - saving world state it does not own while in a session:
    `SaveLocksManager.RequestSaveLockAdd(game, n"tandem")` at join (re-add
    after every load, the list is not persistent) or a tandem
    `ScriptableSystem` overriding `IsSavingLocked()`; plus
    `GetAutoSaveSystem().InvalidateAutoSaveRequests()` for requests already
    queued. But a guest's **character** progress must reach its save at some
    point: lock during play and save once on leaving, after restoring what
    tandem changed (facts, journal, prevention lock, weather, time) — or
    accept that the guest's save holds a mirrored world (it is overwritten
    at the next join anyway, §2.4). Decide; the second is simpler.
  - loading on death: set `block_death_menu = 1` locally (keep it out of the
    fact sync in both directions) and run tandem's downed/revive flow
    (milestone 8) instead of the death menu.
- **Host**: saves normally. A host **load** (`LoadLastCheckpoint`,
  `LoadSavedGame`) rewinds the world: guests need a fresh snapshot
  afterwards, like a join. Detect on the host with Codeware's session
  events (§7, `Session/Ready`).
- **Host death** in single player reloads the checkpoint. In co-op that
  would rewind everyone: block the host's death menu too and decide the rule
  (all downed → host reloads → resnapshot).

---

## 12. Progression and rewards

### 12.1 What is per player

Authority table: "a V: inventory, money, XP, perks — that player's game
(save) — not shared". The systems, all per game and saved with the player:

- `PlayerDevelopmentSystem` (`final.reds:79162-80038`): `persistent
m_playerData: [PlayerDevelopmentData]`; queries `GetProficiencyLevel(owner,
type: gamedataProficiencyType)`, `GetDevPoints(owner, type:
gamedataDevelopmentPointType)`, `GetPerkLevel`, `GetAttributes`,
  `GetLifePath`; requests (`PlayerScriptableSystemRequest`, carry `owner`):
  `AddExperience{m_amount: Int32, m_experienceType: gamedataProficiencyType,
m_debug}` (`:177888`), `QueueCombatExperience`,
  `ProcessQueuedCombatExperience`, `SetProficiencyLevel`,
  `LevelUpProficiency`, `AddDevelopmentPoints{amountOfPoints,
developmentPointType}` (native class, `:222798`), `BuyPerk`, `BuyNewPerk`,
  `SetAttribute`, `BuyAttribute`, `SetProgressionBuild`,
  `questSetProgressionBuildRequest`, `questSetLifePathRequest`.
- Money is an item: `MarketSystem.Money() -> ItemID`;
  `TransactionSystem.GiveMoney(target, amount, currency: CName)`
  (`@ 0x142aa3534`), `RemoveMoney`, `TransferMoney`, and plain
  `GiveItem(obj, itemID, amount)` (`@ 0x1408876b8`).
- Street cred is the proficiency `gamedataProficiencyType.StreetCred`; level
  is `gamedataProficiencyType.Level`.

### 12.2 How rewards are given

One function: **`RPGManager.GiveReward(gi, rewardID: TweakDBID, opt amount:
Int32, opt target: StatsObjectID, opt moneyMultiplier: Float)`**
(`final.reds:37406-37636`). From the `RewardBase_Record` it gives, **to the
local player** (`GetLocalPlayerControlledGameObject()`):

- items: `Items()` → `TransactionSystem.GiveItem(player, …, quantity *
amount)` (`:37461-37469`);
- recipes: `AddRecipeRequest` to the `CraftingSystem` (`:37470-37482`);
- experience: `Experience()` → an `ExperiencePointsEvent{amount, type,
isDebug}` queued **on the player** (`:37483-37561`), scaled by the
  player's own `XPbonusMultiplier` / `StreetCredXPBonusMultiplier` stats and
  by content level against the player's level
  (`CalculateMinorActivityReward`, `CalculateStreetStoryReward`,
  `CalculateEP1Reward`);
- money: `CurrencyPackage()` → `GiveItem(player, currencyItemID, quantity)`
  (`:37570-37596`);
- achievements, photo-mode items, and an `NCPDJobDoneEvent` UI event
  (`:37602-37616`).

Quest rewards reach it through one event: the quest node
`questGiveReward_NodeType{rewards: [TweakDBID]}` sends a native
**`RewardEvent{rewardName: TweakDBID}`** (`final.reds:248392-248394`, RTTI
`questRewardEvent`) to the player, handled by

```
PlayerPuppet.OnRewardEvent(evt: RewardEvent)        // final.reds:43468
  → RPGManager.GiveReward(this.GetGame(), evt.rewardName)
```

Other reward sources call `GiveReward` directly: kills
(`KillRewardEvent` → `OnKillRewardEvent`, `final.reds:21941`, on the NPC:
XP for the kill), hacking minigames (`:19907`, `:19947-19957`), scavenging
(`GiveScavengeReward`, `:37655`), device actions, `EffectExecutor_GivePlayerReward`.
Quest nodes that bypass rewards: `questEntityManagerAddDevelopmentPoint_NodeType`,
`questEntityManagerLevelUpProficiency_NodeType`, `questAddRemoveItem_NodeType`
(quest items into the host's inventory), `questSetProgressionBuild`.

### 12.3 Co-op mapping

- **Host sends** `QuestReward{rewardID: TweakDBID}` from a
  `@wrapMethod(PlayerPuppet) OnRewardEvent` (after calling the original).
  TweakDBIDs are stable across games (CRC32 + length, §9).
- **Guest applies** `RPGManager.GiveReward(game, rewardID)` (or queues a
  `RewardEvent` on its own player): the guest gets the items, money and XP
  **scaled for its own level and perks**, into its own save. No totals are
  ever copied between players.
- **Kill XP**: the kill happens on the host's NPC, so `OnKillRewardEvent`
  runs in the host's game and rewards the host. Forward a share to the
  guests that took part (the host knows who hit: hit events come from
  guests) as `AddExperience` requests or reward IDs. On the guest the proxy
  must **not** grant kill XP a second time when it plays its death.
- **Dev points and proficiency level-ups given by quest nodes** have no
  script event; observe them on the host through the
  `PlayerDevelopmentSystem` request handlers (`OnDevelopmentPointsAdded`,
  `:79940`; `OnLevelUpProficiency`, `:79823`) and forward as the same
  requests with the guest's player as `owner`.
- **Quest items** given to the host (keys, shards the quest later checks)
  stay with the host; a guest copy is cosmetic.
- **Guest must stop**: nothing; without quests the guest's game issues no
  quest rewards.
- Level differences: content scales to the **host's** level (NPCs are the
  host's). Rewards scale per receiver, so a low-level guest is not
  over-rewarded by the formula, but fights at the host's difficulty.

---

## 13. Summary table

<!-- prettier-ignore -->
| State | Owner | Host reads with | Guest applies with | Guest must stop |
| --- | --- | --- | --- | --- |
| quest facts | host | native `FactManager` store walk + `SetFact` vslot hook, or `RegisterEntity` + `FactChangedEvent` | `QuestsSystem.SetFact` / `SetFactStr` if different | quest graphs; local script writes become requests |
| per-player facts (tutorial, retrofix, `game_was_loaded`, `block_death_menu`) | each player | — | — | never synced |
| journal entry states | host | `RegisterScriptCallback(…, State)`; snapshot `GetEntries` / `GetQuests` + `GetChildren` | `ChangeEntryStateByHash(hash, state, notify)` | — |
| tracked objective | host default; per player allowed | `RegisterScriptCallback(…, Tracked)` + `GetTrackedEntry` | `TrackEntry(GetEntry(hash))` | optional: guest's own `TrackEntry` |
| objective counter / optional | host | `Counter`, `ObjectiveOptional` callbacks; `GetObjectiveCurrentCounter` | no script setter (open) | — |
| quest map markers | host (via journal) | none; for spawned targets `QuestMappin.GetJournalPathHash` + entity | automatic from journal; fallback `RegisterMappin` / `SetMappinPosition` | — |
| phone call | host | wrap `PhoneSystem.OnTriggerCall` | queue `questTriggerCallRequest` (with scenes) | answering/rejecting locally |
| messages, shards, codex, tarot | host (journal) | journal callbacks | journal apply | replies → request to host |
| warnings / on-screen messages | host | wrap blackboard writes (`UI_Notifications`, `UI_CustomQuestNotification`) | same blackboard writes | — |
| tutorials | each player | — | — | `disable_tutorials = 1` locally |
| quest spawns (NPCs, vehicles) | host | Codeware `GetCommunity(…).GetActiveEntityIDs()`, entity spawn events; `EntityID.IsDynamic` | proxy from announcement (`NetId`) | communities / spawners (`DeactivateCommunity`, …) |
| node visibility, prefab variants | host | (hook on `questWorldDataManager` node execution — open) | Codeware `ToggleNode`, `ToggleVariant` | — |
| quest prefabs (interiors) | host | `questPhaseNodeDefinition.phaseInstancePrefabs` (open) | no API (open) | — |
| quest teleport / regroup | host | position jump; node hook | `TeleportationFacility.Teleport(player, pos, rot)` | — |
| device state (doors, lifts, terminals) | host; guest acts optimistically | hook `QueuePSDeviceEvent` / `QueuePSEvent`; `GetConstAccessToPSObject` | rebuild action + `QueuePSDeviceEvent`; fallback `SetPersistentBool/Int/Float` | deciding devices (security systems, turrets) |
| loot containers (placed) | each player (own copy) | PS events for lock/disable | `QueuePSEvent`; `ContainerManager.InjectLoot` for quest items | — |
| loot drops (NPC) | host | body inventory `TransactionSystem.GetItemList` | `LootManager.SpawnItemDropOfManyItems` | proxies rolling their own loot |
| vendors, stash | each player | — | — | — |
| fast travel | host leads | wrap `FastTravelSystem.OnPerformFastTravelRequest` | queue `PerformFastTravelRequest` or teleport | `FastTravelSystem.AddFastTravelLock(n"tandem", game)` |
| game time | host | `TimeSystem.GetGameTime()` (`GameTime.GetSeconds`), `IsPausedState()` | `SetGameTimeBySeconds`, `SetPausedState(paused, source)` | wait menu; world time dilation |
| weather | host | Codeware `GetWeatherState().name` | Codeware `SetWeather(name, blend, priority)`; `ResetWeather(true)` on leave | own weather cycle (stopped by `SetWeather`) |
| police heat | host | `UI_WantedBar.CurrentWantedLevel` listener / `PreventionSystem.GetHeatStage()` | write `UI_WantedBar` + `wanted_level` facts (display only) | `TogglePreventionSystem{n"tandem", false}`; `TogglePreventionActive(false)` |
| crimes by guests | host | host-side hit handling; forwarded `Prevention…Request`s | — | local scoring (off with the system) |
| crowds | host | `gameCrowdMemberComponent` on NPCs → proxies | proxies | `ChangeDensityModifier(0)` / `ControlCrowd` / null area (open) |
| traffic | host | vehicles → proxies | proxies | no known switch (open) |
| saves | host (world); each player (character) | — | — | `SaveLocksManager.RequestSaveLockAdd(game, n"tandem")`; `block_death_menu` |
| quest rewards | per receiver | wrap `PlayerPuppet.OnRewardEvent` → reward TweakDBID | `RPGManager.GiveReward(game, rewardID)` | — |
| kill XP | per receiver | `OnKillRewardEvent` on host NPCs | `AddExperience` request | proxy deaths granting XP |
| XP, perks, money, inventory | each player | — | — | — |

---

## 14. Open questions needing an in-game test

Facts

1. Does `RegisterEntity(fact, playerEntityID)` scale to thousands of facts on
   one entity, and how late does `FactChangedEvent` arrive (same frame, next
   frame)? Are two writes in one frame delivered as two events?
2. Does a `SetFact` through the native `FactManager` vtable (`+0x18`) fire
   script listeners, entity events and quest conditions like
   `QuestsSystem.SetFact`? Is the vtable slot hit by **every** writer (quest
   `questSetVar`, scripts, scene nodes)? Check slot and layout on 2.50 first.
3. Is `FactStore* data[1]`'s map safe to walk on the game thread without the
   quest system lock (`QuestsSystem + 0x60`)?
4. Which facts does a quest-free guest world still write by itself (list
   them by running the hook on a guest for ten minutes)?

Journal

5. Is the entry hash (`GetEntryHash`, callback `hash`) identical in two
   games with the same version and DLC? With different languages?
6. Does `ChangeEntryStateByHash` on a guest with no running quests create
   the quest mappins and the tracker entry as it does on the host? Does
   applying a child before its parent work?
7. Objective counters: is there any way to set them without the quest node
   (they derive from a fact — which one; does setting that fact update the
   counter on the guest)?
8. Does `TrackEntry` on the guest survive the next host state change, or
   does the engine retrack (the fork tracked only once the entry was Active)?

Mappins and references

9. Does a quest mappin whose `reference` names a community NPC attach to a
   guest proxy spawned outside the community? If not: does Codeware
   `ActivateCommunity` on the guest give an NPC that the reference resolves
   to, and can its AI be turned off cleanly?
10. `EntityID.IsStatic` for: community NPCs, population-spawner NPCs, quest
    spawner NPCs, parked vehicles, crowd, traffic. This decides which
    entities need a `NetId`.

Quest runtime

11. Quest prefabs: with the guest running no phases, what exactly is missing
    in a quest location (geometry, collision, props, NPCs)? Is there an
    engine call that loads a quest prefab by `GlobalNodeRef`
    (`questQuestPrefabsHandler`, `worldRuntimeSystemPrefabInstancing`)?
12. Do Codeware `ToggleNode` / `ToggleVariant` work on a guest in a
    quest-free world, and how does the host observe the same changes
    (hook on `QuestPhaseInstance_ExecuteNode` filtered to
    `questWorldDataManagerNodeDefinition`)?
13. What does the leftover `RepTeleportPlayer(player, pos, rot)` do?

Persistent state

14. Does `QueuePSEvent` / `QueuePSDeviceEvent` reach a PS whose entity is
    not streamed in on the guest, and is the visual state correct when it
    streams in later?
15. Can a `DeviceAction` be rebuilt on the guest from (class name, action
    name, properties), or must the script factory (`ActionXxx()` on the PS)
    be used per device type? Does `GetConstAccessToPSObject` return a PS the
    action can be `SetUp` on?
16. Do `SetPersistentBool/Int/Float` notify the entity, and for which
    classes are they enough (doors: `isOpened`, `isLocked`, `isSealed`)?
17. Does a proxy NPC killed by host order generate loot on the guest? Where
    is loot generated in the death pipeline?

Time and weather

18. `SetGameTimeBySeconds` every N seconds: visible lighting pops? Which
    threshold is invisible? Do time listeners fire twice when time is set
    slightly backwards?
19. `TimeSystem.SetPausedState(paused, source)`: does the RTTI signature
    work from redscript (the vanilla declaration has no parameters — a
    mismatched `native func` declaration breaks script compilation; declare
    it in `Natives.reds` style through the plugin instead)?
20. Codeware `SetWeather` on the guest: does a guest's local weather area or
    quest weather override it; which `priority` wins against the cycle; what
    happens with a state name the guest lacks (weather mods on one side)?
21. How fast is game time against real time
    (`RealTimeSecondsToGameTime(1.0)`), and is it constant?

Police

22. With `TogglePreventionSystem{n"tandem", false}` on the guest: do writes
    to `UI_WantedBar.CurrentWantedLevel` show stars on the guest's HUD, or
    does the HUD also check `IsSystemEnabled()`?
23. Does the lock survive in the guest's save (`m_systemLockSources` is
    persistent) and does removing it on leave restore normal police?
24. Do host police units react to a guest's puppet as a criminal (attitude,
    targeting) or only to the host's V?

Crowds and traffic

25. `CommunitySystem.ChangeDensityModifier(0.0)`: does it remove
    pedestrians, vehicles, both, or only new spawns? Is it saved?
26. `questCrowdManagerNodeType_ControlCrowd{Disable}` through Codeware
    `ExecuteNode`: effect, persistence, and whether it covers traffic.
27. Is there any engine switch for `worldRuntimeSystemTraffic` (config var,
    user setting, vtable call)? Until found, traffic suppression is unsolved.

Saves

28. Does a `ScriptableSystem.IsSavingLocked()` override stop autosaves and
    quick saves alike (reason `Script`)? Does
    `InvalidateAutoSaveRequests()` drop a pending one?
29. With `block_death_menu = 1`, what state is a dead V left in, and can it
    be revived by script (status effects, `gameGodModeSystem`)?
30. After a host `LoadLastCheckpoint`, which Codeware session event fires
    and in what order relative to the facts being restored (when is it safe
    to take the new snapshot)?

Rewards

31. Does `RPGManager.GiveReward` on a guest for a quest reward ID give
    sensible amounts when the guest never ran the quest (content-level
    scaling reads `rewardID + .contentAssignment`, not quest state)?
32. Does a proxy's death on the guest queue a `KillRewardEvent` (double XP)?
