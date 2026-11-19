# Campaign co-op fork of CyberpunkMP: facts (for a clean-room 2.50 rewrite)

Source: `cpmp/campaign` (git, HEAD `f3b4df2`). Base = CyberpunkMP + 2.31 port (`2a938f2`), campaign
commits `5ad031a`..`f3b4df2` (22 commits, all "feat/fix(campaign)"). Built against game **2.31**,
RED4ext 1.30.0, Codeware 1.20.5, ArchiveXL 1.27.1, TweakXL 1.11.4, redscript 0.5.31, Input Loader
0.2.3 (`tools/campaign/BuildInstaller.ps1:21-31`). Facts only, no code copied.

Tags: **[2.5-verified]** = hash present in the 2.50 address list (`addr.tsv`) AND the same
hash/layout/signature is used by current Codeware (which runs on 2.5). **[2.5-present]** = hash in
the 2.50 list, but nothing confirms the signature/layout. **[2.31-only]** / **[broken-on-2.5]** /
**[unverified]** as named. Note: `addr.tsv` has a symbol for only 84 of 1.1M entries, so
"present" means the hash exists, not that its name is known.

Paths below are relative to `campaign/` unless absolute. `CW:` = `codeware/src/...`.

---

## 1. Architecture in one paragraph

Host-authoritative. First player in the lobby = host; the host's game runs every quest; guests
start a **quest-free world** and mirror state: journal + facts (scripts), "what you see/hear" quest
nodes (native `QuestMirror`), tracked objective, scene tier/position, NPC health/death. Dialog
choices are voted; the winner is applied by **synthetic key presses** into `Player::ProcessInput`.
Server (C# plugin on CyberpunkMP's .NET host) stores the latest host state and replays it to late
joiners. Transport: CyberpunkMP's GameNetworkingSockets over UDP, optionally tunnelled through
Steam P2P (`ISteamNetworkingMessages`). (`docs/CAMPAIGN.md:34-60`, commit `5ad031a`.)

Why the guest has no quests: "a running quest cannot be stopped, only prevented from starting"
(`docs/CAMPAIGN.md:43-49`). Guests start via New Game with `SetIsExpansionStandalone(true)`
(`code/assets/redscript/Plugins/CampaignLobby.reds:1021,1165`) so the game loads
`ep1\quest\ep1_standalone.gamedef`, which the mod **replaces** with a quest-free gamedef via
`zz_CampaignCoop_GuestStart.archive` (8192 B; source `code/assets/Archives/source/archive/ep1/quest/ep1_standalone.gamedef`,
1233 B; taken from Night City Online `nco 9e1832f`). Result: Night City without prologue, EP1
world, empty journal. Cost: with the mod the "Phantom Liberty" New Game button always gives the
empty start, and guests need PL installed. Guest root quest then = `ep1/quest/cyberpunk2077_ep1_standalone.quest`;
base root = `base/quest/cyberpunk2077.quest` (`docs/MISSIONS.md:63-64`). [unverified on 2.5: gamedef
format/paths may have changed; the archive is binary, no source text.]

"In game" detection: Codeware callback `Session/Ready` with `GameSessionEvent.IsPreGame() == false`;
the main menu is itself a world with a player ~8 s after launch, so `OnWorldAttached`/`OnPlayerAttach`
fire there too and are unusable (`CampaignLobby.reds:106-121`, commit `9a1e671`). Codeware 2.5 still
has `Session/Ready`, `Session/BeforeEnd`, `IsPreGame` (`CW:App/Callback/CallbackSystem.hpp`) [2.5-verified].

---

## 2. QuestsSystem / running quest nodes remotely (QuestMirror)

File: `code/client/App/World/QuestMirror.cpp` (481 lines), `.h`. Commit `7f88bec` ("stage 4b").

### Hashes and signatures

<!-- prettier-ignore -->
| Hash | Use | Signature as used | Tag |
| --- | --- | --- | --- |
| `3227858325` | `QuestPhaseInstance::ExecuteNode` — hooked (host records) and called (guest replays) | `uint8_t (PhaseInstance*, questNodeDefinition*, QuestContext&, const QuestNodeSocket& in, DynArray<QuestNodeSocket>& out)` (`QuestMirror.cpp:67-72`) | [2.5-verified] identical in `CW:Red/QuestsSystem.hpp:286-292`, `CW:Red/Shared/AddressLib.hpp:289` |
| `3144298192` | `QuestsSystem::CreateContext` | `void* (QuestsSystem*, QuestContext*, int32 a3, int8 a4, int32 a5, int32 a6)`; called `(sys, &ctx, 1, 0, -1, -1)` (`QuestMirror.cpp:63-65,348`) | [2.5-verified] same call in `CW:App/Quest/QuestPhaseExecutor.hpp:31` (Codeware comments a5 default "1000000" but passes -1) |

Related Codeware 2.5 hashes (not used by the fork, all present in 2.50 list): `QuestPhaseInstance_ExecuteGraph 779107884`,
`QuestPhaseInstance_Initialize 3423879019`, `QuestsSystem_OnGameRestored 2048921710`; Codeware also has
`ExecutePath` as vfunc `+0x120` on questPhaseInstance (`CW:Red/QuestsSystem.hpp:294-300`).

### Layouts (all identical to `CW:Red/QuestsSystem.hpp`) — [2.5-verified]

- `QuestsSystem + 0x60` = `SharedSpinLock` mutex (lock it while executing); `+0x68` = `Handle<questPhaseInstance>` root phase (`QuestMirror.cpp:57-58`; CW:220-221). Codeware also: `+0x78` NodePathHash map, `+0xF8` `FactManager*` (CW:222-223).
- `questPhaseInstance + 0x30` = `Handle<questQuestPhaseResource>`; `+0x40` = `Handle<questGraphDefinition>` (`QuestMirror.cpp:60-61`; CW:261-262). Codeware also: `+0x70` NodePath, `+0x80` NodePathHash, `+0x88` children, `+0x98` handlers.
- `QuestContext`: size `0x340`; `phaseStack` (`DynArray<questPhaseInstance*>`) at `0xF8`; `nodeID` (u16) at `0x108`; `QuestPhaseContext` at `0x110` (`QuestMirror.cpp:35-43`).
- `QuestPhaseContext`: size `0x230`, vtable, `game@08`, `prefabLoader@30` (`QuestMirror.cpp:21-33`).
- `QuestNodeSocket` = `{ CName name; uint8 unk08; }` (`QuestMirror.cpp:45-55`).
- `QuestNodeID = uint16`.

### How a node is executed on a guest (same recipe as Codeware's QuestPhaseExecutor::ExecuteNode)

`QuestMirror.cpp:335-359`: get `questQuestsSystem`; root = `*(sys+0x68)`; lock `*(sys+0x60)`;
zero a `QuestContext`; `CreateContext(sys,&ctx,1,0,-1,-1)`; `ctx.phaseStack.PushBack(root)`;
`ExecuteNode(root, node, ctx, socket{recordedName}, outSockets)`; `phaseStack.Clear()`.
Difference from Codeware: the fork passes the **recorded input socket name** (Codeware passes an
empty socket). A thread_local flag stops the replay from being re-recorded by the hook.

### Recording (host) — `QuestMirror.cpp:221-282`

- Hook ExecuteNode; if mode == Record and node's class is in the mirror list, record an address:
  `<resource path hash>/<phase node id>.<phase node id>.../<node id>/<input socket CName hash>`.
- Resource = `PhaseResource(phase)`; if null, walk `ctx.phaseStack` from the top for the nearest parent with a resource (`:233-238`).
- Phase chain = DFS from `resource->graph` through `questPhaseNodeDefinition.phaseGraph` until reaching the executing phase's graph pointer (`FindGraphPath`, `:198-218`).
- Mirrored node classes (31, `:76-108`): questSceneNodeDefinition, questSceneManagerNodeDefinition, questPhoneManagerNodeDefinition, questAudioNodeDefinition, questAudioCharacterManagerNodeDefinition, questVoicesetManagerNodeDefinition, questSpawnManagerNodeDefinition, questDynamicSpawnSystemNodeDefinition, questEntityManagerNodeDefinition, questCharacterManagerNodeDefinition, questCrowdManagerNodeDefinition, questPopulactionControllerNodeDefinition, questWorldDataManagerNodeDefinition, questEnvironmentManagerNodeDefinition, questTimeManagerNodeDefinition, questFXManagerNodeDefinition, questRenderFxManagerNodeDefinition, questInteractiveObjectManagerNodeDefinition, questTeleportPuppetNodeDefinition, questMovePuppetNodeDefinition, questRotateToNodeDefinition, questUseWorkspotNodeDefinition, questPuppetAIManagerNodeDefinition, questForcedBehaviourNodeDefinition, questClearForcedBehavioursNodeDefinition, questBehaviourManagerNodeDefinition, questSendAICommandNodeDefinition, questVehicleNodeDefinition, questTeleportVehicleNodeDefinition, questMappinManagerNodeDefinition, questRewardManagerNodeDefinition. Unknown class names are logged and skipped (`:150-156`). [unverified that all names exist on 2.5]
- NOT mirrored: facts (questSetVar), journal, control flow, conditions, phases, checkpoints.
- Dedup: scene + phone nodes are "woken again by the engine while they run": recorded only once per 5 min per address; others once per 2 s (`:110-116,257-262`).
- Scene key = address without socket, kept for the last scene node (`:253-265`) — same in every game, used to key per-scene poses.
- Script polls `TakeQuestNodes()` every 0.25 s and sends batches ≤16000 chars (`Campaign.reds:298,368-409`).
- Server forwards only to guests currently in the world; **late joiners get no old nodes** (`CampaignSession.cs:517-530`).

### Replay (guest) — `QuestMirror.cpp:396-481`

- Parse entries, `ResourceLoader::LoadAsync<graphGraphResource>(ResourcePath(hash))`, queue in order.
- `Update()` (called from NetworkWorldSystem tick, `NetworkWorldSystem.cpp:189-191`) runs the queue **in order** (a spawn before the scene that uses it). Failed load → skip; not loaded after 30 s → skip (fix `2f3dfae`: one stuck resource blocked every later scene).
- Resolve: `token->Get()->graph`, walk phase ids via `questPhaseNodeDefinition.phaseGraph`, then find node by `id`.
- Executed outside the queue lock (the node re-enters the hook).

### Open questions the author left (`docs/CAMPAIGN.md:249-252`) [unverified, never tested]

Whether a scene node executed outside its phase plays the scene; whether NPCs/objects from phase
prefabs (loaded only while the phase is active) exist on the guest; whether scene wake-ups get
reported as new starts. The whole campaign is marked "missions in test phase", `docs/MISSIONS.md`
test-notes table is empty (`:95-97`) → **nothing here is confirmed working in-game**.

---

## 3. Fact sync

Script-only, no native hook. `code/assets/redscript/Plugins/Campaign.reds`, server
`CampaignSession.cs`. Commits `5ad031a`, `d0ed043`.

- **Listener approach**: for each watched fact name, host calls `QuestsSystem.RegisterListener(StringToName(name), watcher, n"OnFactChanged")`, one `CampaignFactWatcher` object per fact because the callback receives only the value, not the name (`Campaign.reds:274-285,485-491`). Unregister with `UnregisterListener(name, id)` (`:516-522`). [unverified on 2.5]
- Value read: `QuestsSystem.GetFactStr(name)`; guest write: `SetFactStr(name, value)` only if different (`:495,580-589`).
- **Which facts** (filtering): static list of 16,164 names (11,083 base, 5,081 PL) = every fact a `.quest/.questphase/.scene` graph **writes** via `questSetVar_NodeType` (fields `factName`, `factName1`, `factName2`), name regex `^[A-Za-z0-9_.]+$`, minus dev facts regex `(^|_)(debug|dbg|test|testdef|demo)(_|$|\d)` (`tools/campaign/extract_facts.py:20-40`). Facts only read by graphs are set by game code, often per player (vehicle, combat, UI) — deliberately excluded. Sources: `basegame_1_engine`, `basegame_3_nightcity`, `basegame_4_gamedata`, `ep1_1_nightcity`, `ep1_2_gamedata` archives; WolvenKit CLI 8.20+ `unbundle -r "\.(quest|questphase|scene)$"` then `convert serialize` (`extract_facts.py:21-31,69-73`). List embedded in the server DLL; `config/campaign_facts.txt` adds (`name`) / removes (`-name`) (`CampaignSession.cs:599-627`). [2.31 data; regenerate on 2.5]
- **Authority**: host only. Server ignores fact/journal reports from anyone but `hostId` (`CampaignSession.cs:348,383,403,415`). Host = first in lobby; after campaign start only the same host _name_ can take it back (only their save has the story) (`CampaignLobby.cs:30-36,126-127`, `docs/CAMPAIGN.md:36-42`).
- **Snapshot protocol** (`docs/CAMPAIGN.md:62-75`, `CampaignSession.cs:303-342,411-425`):
  1. client `EnterWorld` → server: host gets `SetRole(true)` then `WatchFacts(names, last)` batches; guest `SetRole(false)` (role first, so guest ignores guest-only messages until it knows).
  2. host registers listeners, sends non-zero values only (`ReportFactBatch`), journal tree (`ReportJournalBatch`), then `ReportSnapshotDone`.
  3. Only then server sends guests full state: journal, then **every** watched fact incl. unset ones as `0` (so a guest whose save is further along gets facts wiped), then tracked objective.
  4. Then deltas `ReportFact`→`ApplyFact`, `ReportJournal`→`ApplyJournal`.
  - Until `snapshotReady` guests get nothing ("anything missing would be read as unset and wipe their facts"). New host / host reloading a save clears server state (`PromoteToHost`, `:303-326`).
- Batch format: one String `"key:value,key:value"`, ≤16,000 chars per RPC. RPC buffer is 64 KB, strings ≤65,535 bytes. String used instead of arrays because the C# stub generator misnames arrays of primitive types (`docs/CAMPAIGN.md:73-75`).
- Known concerns (`docs/CAMPAIGN.md:133-137`): host registers several thousand listeners at once — "if too expensive, replace with a native FactsDB write hook (RED4ext)". Facts reach a guest world whose base root quest is running: a phase waiting on a host-set fact may start on the guest (untested).
- Native alternative noted in Codeware 2.5: `FactManager*` at `QuestsSystem+0xF8`, vtable `+0x10 GetFact(store, id)`, `+0x18 SetFact(store, id, value)`, store 1 = named facts, 2..10 = graph stores, `FactID = FNV1a32(name)` (`CW:Red/QuestsSystem.hpp:114-192,223`) [2.5-verified layout per Codeware; not used by fork].

---

## 4. Journal sync and tracked objective

`Campaign.reds:411-475,526-578`; server `CampaignSession.cs:344-377,501-515`. Commit `1079f52` for tracking.

- Host: `JournalManager.RegisterScriptCallback(this, n"OnJournalEntryStateChanged", gameJournalListenerType.State)`; callback `(entryHash: Uint32, className: CName, notifyOption, changeType)` → `ReportJournal(hash, EnumInt(GetEntryState(entry)))`.
- Snapshot: `GetQuests(context{stateFilter: active,succeeded,failed; inactive=false})`, recursive `GetChildren(entry, filter, out)` for the whole tree, each as `hash:state`.
- Guest apply: `GetEntry(hash)`; skip unknown; skip if same state; `ChangeEntryStateByHash(hash, state, JournalNotifyOption.Notify)`.
- Tracked: host polls `GetTrackedEntry()` every 0.25 s, sends `GetEntryHash` on change (`ReportTracked`; 0 = none). Server keeps it, sends after the journal. Guest calls `TrackEntry` only once the entry exists and is `Active`, retrying after each journal change. Host tracking nothing (0) leaves guest's own tracking alone; guest may switch until the host's next change.
- Native Codeware 2.5 journal vfuncs for reference: `GetEntries 0x210`, `GetEntryByHash 0x220`, `ChangeEntryState 0x268`, `GetEntryState 0x2B0`, `GetEntryTimestamp 0x2C8`; hashes `JournalManager_LoadJournal 1964512489`, `JournalManager_TrackQuest 3274382593` (`CW:Red/JournalManager.hpp`, `CW:Red/Shared/AddressLib.hpp:228-233`). Fork uses script API only. [script API unverified on 2.5]

---

## 5. Scenes

### Scene tier report / bring guests over (`CampaignDialog.reds:476-571`, server `CampaignSession.cs:492-543`; commit `7a287c1`)

- Wrap `PlayerPuppet.OnSceneTierChange(newState: Int32)` (`CampaignDialog.reds:586-595`). Host → `ReportScene(tier, x, y, z)`; server stores, sends `SceneState` to guests and to late joiners (`:295-296`).
- Guest: tier ≥ `Tier3_LimitedGameplay` and > 25 m from host → `TeleportationFacility.Teleport` to host +1.5 m X. Tier ≥ `Tier4_FPPCinematic` **and the guest's own scene did not start** (local tier < 3) → hold with status effect `GameplayRestriction.NoMovement` until it ends.
- Holds are reference-counted per reason (`CampaignHolds`, `Campaign.reds:603-679`): an effect is removed only when no reason wants it (fix `d652278`: a scene ending freed a downed player).
- **Saving blocked** while held: ScriptableSystem override `private func IsSavingLocked() -> Bool` returning true (`CampaignDialog.reds:98`, `CampaignStory.reds:75`, `CampaignCombat.reds:132`; fix `2f3dfae`: a save would keep the restrictions). God modes left in a save by the campaign are removed on next load by source name (`CampaignStory.reds:161-168`, `CampaignCombat.reds:227-237`).
- Phone calls / holocalls only ring on the host originally; with QuestMirror the guest runs the phone nodes too.

### Other players during a scene: spacing (`InterpolationSystem.cpp` diff, `NetworkWorldSystem.cpp:567-577`)

While the local V is in tier ≥3, remote puppets within 1.5 m of V's position are placed 1.2 m right/left (slot = entityHash%3+1, side by parity) along V's right vector; anchor refreshed every 1 s. A "held" set of entity hashes skips interpolation entirely.

### SceneSpots + extras (stage 4c, commit `25f25ce`; `SceneSpots.cpp`, `CampaignExtras.reds`)

Purpose: every game plays the scene with its own V; the other players' puppets are posed locally as extras:

- V in a car → `MountAIEvent` (name `Mount`, `MountEventData`) on free seats `seat_front_right, seat_back_left, seat_back_right, seat_front_left`; while a scene runs, entering a vehicle is NOT sent to the server (`SetVehicleSyncPaused`; each game has its own scene car) (`CampaignExtras.reds:237-249,110`, `VehicleSystem.cpp` diff). Exit via `AIEvent "ExitVehicle"` (upstream notes it sometimes breaks orientation).
- V sitting (`WorkspotSystem.IsActorInWorkspot(player)`) → nearest free sit spots within 8 m via `AIUseWorkspotCommand{ workspotNode = NodeRef(globalNodeID) }` after teleporting the puppet to the spot (`:196,288-291`); stop with `WorkspotSystem.StopNpcInWorkspot`.
- V standing → stand spot with idle gesture within 5 m, else half circle radius 1.6 m beside/behind V, facing the current speaker (wrap `BaseSubtitlesGameController.SpawnDialogLine(lineData)` → `lineData.speaker`, `CampaignExtras.reds:528-535`). V's own spot (≤0.6 m) excluded.
- Placed puppets are held (no interpolation) until the scene ends.
- Sector source: Codeware `Resource/Ready` callback filtered `ResourceTarget.Type(n"worldStreamingSector")`, last 256 sectors (`CampaignExtras.reds:80-99`) [2.5-verified API exists in Codeware].
- Native `FindSceneSpots(sector, center, radius)` (`SceneSpots.cpp:67-111`): reads `DataBuffer` at **`worldStreamingSector + 0x40`** as an array of `worldCompiledNodeInstanceSetupInfo` (stride **0x90**: `Transform@00`, `scale@20`, `node*@50`, `globalNodeID@58`, `proxyNodeID@60`, `nodeIndex@78`); keeps nodes whose `node` IsA `worldAISpotNode` and whose `spot` IsA `AIActionSpot`, matches `spot->resource.path.hash` against a whitelist; facing = quaternion applied to (0,1,0). Output `"globalNodeID:x:y:z:fx:fy:kind;..."`.
  - Tag: **[unverified on 2.5 / risky]**. Codeware 2.5 no longer uses a raw `+0x40`; it reads `compiled.buffer.nodeSetups` from a newer RED4ext SDK and accesses nodeIndex / streamingDistance / secondaryRefPointDistance through **buffer-version-dependent** getters (`CW:App/World/NodeSetupWrapper.hpp:24-170`, `StreamingSectorEx.hpp:26-37`) — the record layout varies by sector buffer version. Transform/node/globalNodeID look stable but are unconfirmed. Clean alternative on 2.5: Codeware script API `worldStreamingSector.GetNodeSetupCount()/GetNodeSetup(i)` → `WorldNodeSetupWrapper.GetNode()/GetPosition()/GetOrientation()/GetGlobalNodeID()` (`codeware/scripts/World/worldStreamingSector.reds`, `WorldNodeSetupWrapper.reds`).
- Whitelist: 142 sit + 194 stand `.workspot` paths of the `generic` archetype, file names `<archetype>__<pose>__<action>__<nn>.workspot`; sit places {chair,couch,sofa,barstool,stool,bench,bar,table,stairs}, stand {ground,bar,wall,table,rail,lean}; excluded words gun/rifle/pistol/weapon/aim/guard/injur/cower/scared/dead/fight/cop/kneel/crouch/drunk/puke/piss/sleep/wounded/hostage/serve/work/clean/bartend/cook/repair/type/dj (`tools/campaign/extract_workspots.py:1-40`, `SceneSpotsData.inl`). Listing via `WolvenKit.CLI archive <content> <ep1> -l -w "*.workspot"`.
- Not checked: NPC occupancy of spots (only V and other players); whether `MaMuppet`/`WaMuppet` puppets accept `AIUseWorkspotCommand` / scene-car mounting (`docs/CAMPAIGN.md:281-285`).

### Per-scene poses (stage 6, commit `adde2fe`; `CampaignStory.reds`, `CampaignStory.cs`)

Server table `sceneKey=right:forward:up:yaw|...` (m, deg, relative to V where V stood when placed), shipped `campaign_scenes.txt` (empty, header only) + `config/campaign_scenes.txt` overrides, sent `ScenePoses` on world entry. `hide` = no other players in that scene (intimate scenes). `/coop scene|move|save|clear|hide|where|invite` chat commands; ChatController hands `/coop` lines as a `ChatCommandRequest` (ScriptableSystemRequest) to `CyberpunkMP.Plugins.CampaignStory` (core change because redscript cannot wrap script classes from the same compilation, `docs/CAMPAIGN.md:354-356`). A stale scene key (scene that didn't come through QuestMirror) must not reuse the last poses (`CampaignExtras.reds:70-73`, fix `d652278`).

### Host modes and regroup (`CampaignStory.reds:93-212`)

- Mode 1 braindance: `ScriptableSystemsContainer.Get(n"BraindanceSystem").GetIsInBraindance()` → guests held (NoMovement + NoWeapons) and `GodModeSystem.AddGodMode(id, Invulnerable, n"CampaignHostMode")`. Mode 2 Johnny: `PlayerPuppet.IsJohnnyReplacer()` → guests only get a message. Host leaving world → mode 0.
- Regroup: host V moved > 150 m between 0.5 s polls, not in braindance and not mounted → `ReportTeleport(from,to)`; guests ≤150 m from `from` are teleported 2 m from `to` at a random angle; mounted guests just get a message. AV flights don't count (mounted).
- Untested: `GetIsInBraindance` true for the whole BD; chat usable during scenes (`docs/CAMPAIGN.md:358-361`).

---

## 6. Dialog choice injection (ChoiceInput)

Files: `code/client/App/World/ChoiceInput.cpp` (206 lines), `CampaignDialog.reds`. Commit `7a287c1`.
Technique credited to `VedalAI/neuro-cyberpunk` via `alphanin9/SharedPunk` (MIT) `Raw/Ink/InkSystem.hpp`, `Raw/Player/Player.hpp`.

<!-- prettier-ignore -->
| Item | Value | Tag |
| --- | --- | --- |
| `Player::ProcessInput` hash | `3535541945` → `void (cpPlayer*, RawInputBuffer&)` (`ChoiceInput.cpp:66-70`) | [2.5-present] hash exists in 2.50 list; not used by Codeware, signature unverified |
| local cpPlayer | `gamePlayerManager` vtable slot **`+0x200`** returns `cpPlayer*` (`ChoiceInput.cpp:72-82`) | [unverified] (SharedPunk-derived, 2.31-era) |
| `RawInputData` (fork) | size **0x40**: `key EInputKey@00`, `action EInputAction@04`, `value float@08`, `mouseX u32@0C`, `mouseY u32@10`, `unk14@14`, `unk18 u32@18` (input processing checks it == 1), `unk20..unk38` u64 (`ChoiceInput.cpp:12-26`) | **[broken-on-2.5]**: Codeware 2.5 `CW:Red/Input.hpp:17-32` has the same fields but an extra `uint64 unk40` → size **0x48**. A 0x40-stride DynArray (filter/RemoveAt, pushed synthetic entries) is wrong on 2.5. |
| `RawInputBuffer` | vtable: `+0x00` dtor, `+0x08` `DynArray<RawInputData>& GetInputs()`; synthetic subclass fills vfuncs `+0x10..+0x88` with no-ops and `+0x90` returning 0; allocator `Memory::InputInternalAllocator` (`ChoiceInput.cpp:28-64`). Codeware: `RawInputBuffer{vtbl; DynArray<RawInputData> inputs;}` (`CW:Red/Input.hpp:34-40`) | [unverified] |

Alternative present in 2.5: Codeware `InkSystem_ProcessInputEvents 673454961` = `void (InkSystem*, uint64, RawInputBuffer&)` (`CW:Red/InkSystem.hpp:42-44`, `AddressLib.hpp:184`) [2.5-verified].

Mechanics:

- Lock: while voting, the hook strips confirm keys from the buffer and counts presses — `IK_F`, `IK_Enter`, `IK_Pad_X_SQUARE`, `IK_LeftMouse` (default `DialogConfirm` bindings from `inputUserMappings.xml`) (`ChoiceInput.cpp:96-123`). Each counted press = the host's vote for the highlighted option. Scroll keys pass.
- Inject: queue `|steps|` × `IK_Up`/`IK_Down` then `IK_F`; one key per Update, **120 ms apart**; each press = synthetic buffer with Press (value 1, unk18 = 1) + Release, sent straight to the real ProcessInput with a re-entrancy flag (`:125-206`).
- Script side (`CampaignDialog.reds`): host reads blackboard `UIInteractions.DialogChoiceHubs` (variant `DialogChoiceHubs{choiceHubs[{id, choices[ListChoiceData], timeProvider}]}`), `ActiveChoiceHubID` (int; <0 = none), `SelectedIndex`. Only an **active** hub counts (not options visible while walking past). Options joined with `|`, `~` prefix = unavailable (`gameinteractionsChoiceType.Inactive` or `CheckFailed`), timed if hub or choice has `timeProvider`. Global selection index = sum of choices of hubs before the active hub + `SelectedIndex`.
- Server opens a "dialog" vote only with ≥2 players in world: 15 s, or **5 s for timed dialogs** (vote must end before the game's own timer) (`CampaignSession.cs:130-133,447-472`). Nobody voted → winner −1 → host picks manually.
- Vote counts drawn by wrapping `dialogWidgetGameController.UpdateDialogsData(script_ref<DialogChoiceHubs>)` and appending `"  [n]"`/`"  <"` to `localizedName`; redraw by re-setting the same variant with force=true (`CampaignDialog.reds:372-397,575-584`).
- Guests: if their own QuestMirror'd scene shows the same option count they vote in their native box and each game injects the winner itself; otherwise a stand-in hub (id `77000`, title "CO-OP VOTE") written to `UIInteractions` and input via `RegisterInputListener` on `ChoiceScrollUp/ChoiceScrollDown/ChoiceApply` (`:401-474`).
- Limits (`docs/CAMPAIGN.md:287-291`): rebound `UI_MoveUp/Down`/`DialogConfirm` break injection; guests see no skill-check icons; a dialog that returns identical options without closing opens no new vote.

---

## 7. NPCs / combat (stage 5, commits `3fc8462`, `7c6570f`)

`CampaignCombat.reds`, `CampaignCombat.cs`. Every game has its own copy of each enemy (spawn nodes are mirrored), own AI; only health and death are synced.

- Host every 0.5 s, full list every 3 s within 150 m of any player: `key;static;record;x;y;z;health‰;state;hostile` with key = `EntityID.ToHash`, static = `EntityID.IsStatic`, record = `TDBID.ToNumber(GetRecord().GetID())`, health = `StatPoolsSystem.GetStatPoolValue(id, Health, perc=true)*10`, state 0 alive / 1 `IsDead()` / 2 `ScriptedPuppet.IsDefeated` (`CampaignCombat.reds:211-325`). Registry from wrapped `NPCPuppet.OnGameAttached/OnDetach` (skips crowd and CyberpunkMP puppets tagged `CyberpunkMP.Puppet`).
- Guest binding: static EntityIDs are identical in every game (placed/community NPCs); else nearest unbound same-record NPC ≤12 m (alive only); hostile unmatched ≤80 m for ≥5 s → stand-in via `DynamicEntitySystem.CreateEntity` tag `CampaignCoop.Proxy`, removed after 15 s unreported. Health down immediately, up only 3 s after the guest's own hit; copies >30 m off are teleported. Host death → `Kill(localV)` (XP/loot to everyone), defeat → `SoftKill`. Dead NPCs bind by identity only (fix `d652278`).
- Guest hits: wrapped `NPCPuppet.OnHit(gameHitEvent)` where instigator == local V; damage = `attackComputed.GetTotalAttackValue(Health) / maxHealth * 1000` (‰, so different player levels don't matter); `key:damage‰:killed` every 0.5 s → host `RequestChangingStatPoolValue(-damage)` or `Kill`.
- Players: with ≥2 in world V gets `GodMode Immortal` (source `CampaignCoop`); health ≤1 % → downed (Invulnerable `CampaignDowned`, NoWeapons/ForceCrouch/NoJump/NoMovement), others see `BaseStatusEffect.FollowerDefeatedStory` on the puppet; revive = stand ≤2.5 m for 4 s → 35 % health; 45 s bleed-out → host V dies for real, guest respawns next to host at 50 % (its own death would load its own save); everyone down → `CoopWipe` kills host V. Untested items listed `docs/CAMPAIGN.md:328-331`.

---

## 8. CoopConnect / SteamTunnel / ServerHost (commits `d639f03`, `179a6ec`)

### Steam API (`SteamApi.cpp/.h`)

- Uses the **game's own `steam_api64.dll`** via `GetModuleHandleW` + flat exports (no SDK link). Exports used (`SteamApi.cpp:57-73`): `SteamAPI_SteamFriends_v017`, `SteamAPI_SteamNetworkingMessages_SteamAPI_v002`, `SteamAPI_SteamUser_v021`, `SteamAPI_SteamNetworkingUtils_SteamAPI_v004`, `SteamAPI_ISteamUser_GetSteamID`, `SteamAPI_RegisterCallback`, `SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString`, `..._SetRichPresence`, `..._ClearRichPresence`, `..._GetPersonaName`, `SteamAPI_SteamNetworkingIdentity_SetSteamID64`, `SteamAPI_ISteamNetworkingMessages_SendMessageToUser`, `..._ReceiveMessagesOnChannel`, `SteamAPI_SteamNetworkingMessage_t_Release`, `..._AcceptSessionWithUser`, `..._CloseSessionWithUser`, `SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess`. Any missing → Steam features off. [unverified on 2.5: the versioned accessor names depend on the steam_api64.dll shipped with 2.5 — check its exports; GOG/Epic have no Steam path.]
- `CallbackBase` mimics Steamworks `CCallbackBase` (no virtual dtor; `Run(void*)`, `Run(void*, bool, uint64)`, `GetCallbackSizeBytes()`; then `uint8 flags`, `int m_iCallback`). Callbacks are dispatched by the **game's own `SteamAPI_RunCallbacks`** on the main thread (untested, `docs/CAMPAIGN.md:407-409`).
- `SteamNetworkingIdentity` packed 8: `int32 type, int32 size, union{uint64 steamId64; char[128]}` = 136 B; type `16` = SteamID. `SteamNetworkingMessage_t` start: `void* data@00, int32 size@08, uint32 conn@0C, Identity peer@10`.
- Send flags `1 | 32` = UnreliableNoNagle | AutoRestartBrokenSession (game layer already reliable). `EResult 1` = OK.

### Tunnel (`SteamTunnel.cpp`)

- Carries CyberpunkMP's GNS UDP datagrams unchanged over `ISteamNetworkingMessages` (Valve relays when no direct path). No port forwarding, no addresses.
- Channels: guest→host `0x4350`, host→guest `0x4351`. Max datagram 2048 B.
- Guest: binds a non-blocking UDP socket on `127.0.0.1:<ephemeral>`; client connects to it as a local server; datagrams forwarded to host SteamID; replies sent back to the last game source address.
- Host: session requests accepted in callback `SteamNetworkingMessagesSessionRequest_t` id **1251** (=1250+1) for anyone who knows the host's SteamID; per guest SteamID a separate loopback UDP socket so the real server (127.0.0.1:11778) sees each guest as a distinct client; guest idle 120 s → socket and session closed.
- Worker: detached thread (a joinable one would `std::terminate` at game exit), ~1000 loops/s (1 ms sleep), runs only while tunnel active; stopped by generation counter.
- Gotcha: Windows reports ICMP port-unreachable as an error on the next `recvfrom`; disabled with `WSAIoctl(SIO_UDP_CONNRESET, FALSE)` (fix `2f3dfae`).
- `InitRelayNetworkAccess()` called early to speed first connect.

### CoopConnect (`CoopConnect.cpp`)

- Bundled server port **11778** (server default config); host game retries its own server every 3 s, max 20 tries.
- Invite connect string: via Steam `--online --steamhost=<SteamID64> --coopjoin`; by address `--online --ip=<a> --port=<p> --coopjoin` with address priority `--publicip` > configured non-loopback ip > best local adapter (public > VPN 25.x/26.x/100.64/10 > LAN > loopback) (`:101-188`).
- Join while game running: callback `GameRichPresenceJoinRequested_t` id **337** (300+37), packed 8, `{uint64 friendId; char connect[256]}` (`:22-30`). Rich presence keys `connect` (same args) and `status`.
- New launch params (`App/Settings.cpp` diff): `-username` (quotes/backslashes stripped; players keyed by name; default Steam persona name), `-publicip`, `-coopjoin` (auto-join at menu), `-steamhost`.
- Leaving keeps the Steam host so JOIN SERVER returns to it (fix `2f3dfae`).

### ServerHost (`ServerHost.cpp`)

Starts `<game>\CyberpunkCoopServer\Server.Loader.exe` (outside `red4ext\plugins`, where RED4ext would load its DLLs) suspended + `CREATE_NO_WINDOW`, in a Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (dies with the game), stdout/stderr → `logs\coop-server.log`, only the log handle inherited (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), admin creds via env `CYBERPUNKMP_ADMIN_USERNAME=coop` / random 24-hex `CYBERPUNKMP_ADMIN_PASSWORD`. Needs .NET 8 runtime; a self-exit is reported with its code. Server-side fix: Swan `UnregisterLogger<ConsoleLogger>` throws without a console (`code/server/loader/Systems/WebApi.cs` diff).

---

## 9. Server CampaignSystem (C#)

`code/server/scripting/CampaignSystem/`: `Plugin.cs`, `CampaignSession.cs` (story sync, dialog votes, scenes), `CampaignLobby.cs`, `CampaignVote.cs`, `CampaignCombat.cs`, `CampaignStory.cs`, `CampaignStore.cs`, generated `Client/Plugins.CampaignClient.cs`, `Server/Plugins.CampaignServer.cs`.

- **State** (`CampaignSession.cs:141-166`): `journal: Dictionary<uint hash,int state>`, `facts: Dictionary<string,int>`, `tracked: uint`, `watchedFacts`, `hostId`, `hostInWorld`, `snapshotReady`, `dialogVote`, `sceneTier/x/y/z`. One story per server. One lock `CampaignSession.Sync` shared by RPCs and the 0.25 s timer.
- **Guard**: timer ticks and PlayerJoin/Left events are called from native code **without a catch**; an exception (e.g. locked campaign file) kills the server → wrapped in try/catch (`:183-196`, fix `9a910b7`). RPC handlers check `CampaignSync` exists (messages during world change).
- **Lobby** (`CampaignLobby.cs`): phase/mode/campaign/lifepath/host/players broadcast as one string `phase=..;mode=..;campaign=..;active=..;lifepath=..;host=..;campaigns=a/b;players=name:ready:inWorld/..;you=name` on every change (`:382-385`). Duplicate names get numbered. Player counted as started only once actually in game. Client side lives in a Codeware **ScriptableService** (`GameInstance.GetScriptableServiceContainer().GetService(...)`) because ScriptableSystems don't exist at the main menu (`CampaignLobby.reds:97-99`, `docs/CAMPAIGN.md:113-115`).
- **Vote** (`CampaignVote.cs`): one vote per player, changeable; most votes wins; tie = random among tied; timeout 15 s; no votes → fallback (dialog: −1 = host picks; lifepath: random of 3); lifepath clock starts at first ballot; closes early when everyone voted; late joiners get `OpenFor` with time left. RPCs `VoteOpened(id, kind, options, seconds)`, `VoteTally(id, "2,0,1")`, `VoteClosed(id, winner)`, `CastVote(id, option)`.
- **Lifepath vote** on the game's own `CharacterCreationBackstorySelectionMenu` (wrap `OnReleaseNomad/Street/Corpo`, labels `LifePaths.Corporate/Nomad/StreetKid`), winner forced via `m_characterCustomizationState.SetLifePath(TDBID)` + `SpawnEvent(n"OnAccept")`; last word in `characterCreationSummaryMenu.OnOutroComplete` also sets `SetIsExpansionStandalone(!isHost)` (`CampaignLobby.reds:1035-1171`).
- **Campaign save** (`CampaignStore.cs`): `campaigns/<host>-<date>.json` = `{Host, LifePath, Players[name] = {appearance (CC state base64), level, streetcred, attributes, points, perks, money, items (TweakDBID number: qty)}}`; written to a temp file then swapped. Client uploads every 2 min of play in ≤15,000-char parts (`ReportCharacterPart`/`Done`), restore via `RequestCharacter` → `CharacterPart…` → `CharacterDone(found)`. Not yet saved: equipped items, mods/quality, cyberware slots. Guest guest-restore: `CharacterCustomizationSystem.InitializeState()` → native `ImportCustomizationState(base64)` (calls `CharacterCustomizationState_Serialize 108403442` on the state at `ccSystem+0x78`) → `SetLifePath` → `SetIsExpansionStandalone(true)` → `FinalizeState()` → `StartNewGame(state)` (`CampaignLobby.reds:995-1027`).
- **Message types** — client→server (`Campaign.reds:16-44`, 27 RPCs): ReportJournal(hash,state), ReportJournalBatch, ReportFact(name,value), ReportFactBatch, ReportSnapshotDone, EnterWorld, LeaveWorld, LobbySetReady, LobbyChoose(mode,campaign), LobbyStart, CastVote, ReportCharacterPart/Done, RequestCharacter, ReportDialog(options,timed), ReportDialogClosed, ReportScene(tier,x,y,z), ReportQuestNodes, ReportTracked, ReportNpcs, ReportNpcHits, ReportDowned, ReportRevive(puppetId), ReportBledOut, ReportHostMode, ReportTeleport(from,to), SaveScenePoses(scene,poses). Server→client (`:47-214`): SetRole, ApplyJournal(+Batch), ApplyFact(+Batch), ApplyTracked, WatchFacts(names,last), LobbyState, LobbyBegin(mode,lifePath,isHost), VoteOpened/Tally/Closed, CharacterPart/Done, ReplayQuestNodes, SceneState, ApplyNpcs, ApplyNpcHits, CoopPlayers(count), PlayerDowned(puppetId,downed), Revived(health), Respawn(x,y,z), HostMode, HostTeleported, ScenePoses, CoopWipe.
- **RPC ids** (CyberpunkMP convention, verified here with FNV-1a 64): class id = FNV1a64 of the full script class name (`CyberpunkMP.Plugins.CampaignClient` = 6609351954351555900, `CyberpunkMP.Plugins.CampaignServer` = 17961805562553929976); server native func id = FNV1a64(name) (`ReportJournal` = 9452523112500597210); client script func id = FNV1a64 of redscript mangled name (`SetRole;Bool` = 1227686463201472780). Stubs were hand-generated; `-rpc` launch regenerates (`5ad031a`).
- **Authority summary**: host-only for journal/facts/tracked/quest nodes/scene/NPCs/host mode/teleport; any player for votes, hits (applied by host), downed/revive/bleed-out, scene poses ("any player may place poses while testing", `CampaignStory.cs:123`).
- Server native change: a client that spawns again (world reload) gets its old puppet removed; player username sent with each spawned character (`code/server/native/Game/Level.cpp` diff).

---

## 10. Every address hash in the fork's code

Only the 3 campaign hashes are new in the campaign commits; the rest are CyberpunkMP core
(present before `2a938f2`). All 42 are **present in the 2.50 list** (none absent). Symbol names
from `addr.tsv` exist only for 2 (marked); others named from the fork's variable names.
(`1048576` in `code/common/System/Log.cpp:28` and `8121320` in `code/common/Reverse/Memory.h:19` are constants, not hashes.)

<!-- prettier-ignore -->
| Hash | Fork name / use | File:line | 2.50 | Codeware 2.5 |
| --- | --- | --- | --- | --- |
| 3227858325 | QuestPhaseInstance::ExecuteNode (hook+call) | App/World/QuestMirror.cpp:71 | present | same (`QuestPhaseInstance_ExecuteNode`) → [2.5-verified] |
| 3144298192 | QuestsSystem::CreateContext | QuestMirror.cpp:64 | present | same (`QuestsSystem_CreateContext`) → [2.5-verified] |
| 3535541945 | Player::ProcessInput (hook+call) | App/World/ChoiceInput.cpp:69 | present | not used → [2.5-present], struct broken (see §6) |
| 386815609 | ScheduleSynchronizedAppearanceChanges | Game/CharacterCustomizationSystem.h:63 | present | `AppearanceChangeSystem_ChangeAppearance2` (same hash) |
| 633871992 | MorphTargetManager SetTargetWeightWithNormalize | Game/MorphTargetManagerComponent.h:11 | present | `MorphTargetManager_ApplyMorphTarget` (same hash) |
| 898639042 | ScriptValidator::Validate (RPC validation hook) | App/Network/Rpc/RpcValidator.cpp:185 | present | `ScriptValidator_Validate` (same) |
| 2468877568 | CRenderNode_Present_DoInternal / RelocPtr | Game/Rendering.h:16,34 | present `GpuApi::Present` | — |
| 239671909 | CRenderGlobal_Resize | Game/Rendering.h:38 | present `GpuApi::ResizeBackbuffer` | — |
| 3192982283 | CRenderGlobal_Shutdown | Game/Rendering.h:42 | present | — |
| 1054086242 | MorphTarget ResetWeights | Game/MorphTargetManagerComponent.h:14 | present | — |
| 108403442 | CharacterCustomizationState_Serialize | Game/CharacterCustomizationSystem.h:22 | present | — |
| 2710710832 | CreateHandle_CharacterCustomizationState | CharacterCustomizationSystem.h:15 | present | — |
| 1993027039 | CharacterCustomizationState_InitFromPreset | :19 | present | — |
| 56564470 | (commented) gameTransactionSystem_AddItemToSlot | :25 | present | — |
| 394336209 | GetGameSettings | :28 | present | — |
| 1713639304 | (commented) ItemID_SetUniqueCounterValue | :31 | present | — |
| 3020231866 | ItemID_Create | :33 | present | — |
| 3236045910 | GetItemRecord | :35 | present | — |
| 1934369954 | GetPlacementSlot | :38 | present | — |
| 523315084 | EvalArrayTweakDBID | :41 | present | — |
| 3648983380 | EvalCName | :44 | present | — |
| 4124714348 | ResolveSuffix | :47 | present | — |
| 1337337421 | ResolveVisualTags | :50 | present | — |
| 3029088864 | GetItemAppearanceName | :53 | present | — |
| 2624990479 | RegisterRemotePlayerMappin | Game/MappinSystem.h:8 | present | — |
| 3577160229 | RegisterMappin | Game/MappinSystem.h:12 | present | — |
| 2268141838 | IdleController_SetAnimation | Game/Entity.h:25 | present | — |
| 2624656810 | AnimationDriver::Attach | Game/Animation/AnimationDriver.cpp:11 | present | — |
| 2385973466 | AnimationDriver::Detach | :17 | present | — |
| 2132808949 | AnimationDriver::SendParameters | :23 | present | — |
| 3750304414 | AnimationController get float param | :29 | present | — |
| 316482401 | ApplyFeature (AnimationControllerComponent, AnimFeature) | Game/Animation/MultiMovementController.cpp:11 | present | — |
| 2933986803 | vehicle ForceMoveTo | App/World/InterpolationSystem.cpp:131 | present | — |
| 457775029 | vehicle ForceTransform | InterpolationSystem.cpp:132 | present | — |
| 2592348558 | vehicle EnablePhysics | InterpolationSystem.cpp:133 | present | — |
| 3713079867 | (comment only) DriveToPoint::OnUpdate_Internal reference | InterpolationSystem.cpp:137 | present | — |
| 4018412273 | move::Component AttachController | InterpolationSystem.cpp:263, VehicleSystem.cpp:268 | present | — |
| 4039776020 | vehicle SetIsPlayerControlled | App/World/VehicleSystem.cpp:269 | present | — |
| 1620777158 | vehicle SetFlags | VehicleSystem.cpp:270 | present | — |
| 1585713002 | vehicle SetKinematic | VehicleSystem.cpp:271 | present | — |
| 1160782872 | MountingFacility IsMountedToObject | App/World/NetworkWorldSystem.cpp:294 | present | — |
| 3120376212 | MountingFacility GetMountingInfo | NetworkWorldSystem.cpp:298 | present | — |

Core uses `Core::RawFunc<hash, sig>` / `Red::UniversalRelocPtr` (address library via RED4ext); hook via `Core::HookingAgent::Hook<RawFunc>` (`code/client/Core/Hooking/HookingAgent.hpp`).

Non-hash offsets used (all 2.31-era):

- `CharacterCustomizationSystem + 0x78` = customization state handle; "can never be null" (`2a938f2`) [2.31-only/unverified].
- `entity.displayName.unk08` (CString) written to set the nameplate; needs `Character.MaMuppet/WaMuppet` `displayName/fullDisplayName/alternativeDisplayName = ""` in TweakDB because the nameplate reads the record name first (`AppearanceSystem.cpp` diff, `code/assets/Tweaks/CyberpunkMP.tweak`) [2.31-only].
- `moveComponent->speed` hand-mapped offset **moved on 2.31** and reads ~3e8 → speed now computed from position delta, clamped to 0 above 20 m/s (`NetworkWorldSystem.cpp:388-408`, `6b81721`) [broken-on-2.31 already].
- Position: use `placedComponent->worldTransform`, not `localTransform` (stays near origin) (`2a938f2`).
- Scanner name: `GameObject.GetDisplayName()` is a native import that can't be wrapped; wrap `GameObject.CompileScannerChunks()` and overwrite `UI_ScannerModules.ScannerName` with a `ScannerName` chunk (`CampaignNames.reds`).

---

## 11. Gotchas / failures recorded

- RedLib type-name resolution: `NetworkWorldSystem` registered as `gameIGameSystem` (static inheritance of `NAME`) → accessor `GetGameIGameSystem`, script validation failed, RED4ext refused to start (`2a938f2`).
- Upstream had a null check on `GetCustomizationState()` that was a no-op; serializing the null instance behind it crashed on connect (`2a938f2`).
- Animation-thread hook walked a heap DynArray off-thread → replaced by mutex-guarded id set (`PuppetRegistry.h`, `2a938f2`). Known open bug: intermittent crash when a remote player spawns.
- Main menu = world with a player ~8 s after launch; lobby mistook it for the game (`9a1e671`).
- On world detach, puppets are gone with the world: forget them without DeletePuppet; server resends; spawn again after each world reload (`NetworkWorldSystem.cpp` diff 6/9).
- Quest resource that never loads blocked all later replays → 30 s timeout (`2f3dfae`).
- Saving while downed/held would persist restrictions → `IsSavingLocked` (`2f3dfae`).
- God modes left in saves → removed by source name on load (`2f3dfae`, `d652278`).
- Holds must be per reason (`d652278`); stand-ins wait 5 s for the mirrored copy; dying host stays mortal; guest whose bleed-out gets no answer gets up (`d652278`).
- Build: protobuf-cpp pinned 29.3 + abseil 20250127.0 (35.1 removed `FieldDescriptor::is_optional`, 3.19.4 too old); CppSharp 1.1.84.17100/net9.0 (older bundled Clang fails MSVC 14.44 STL); Windows SDK 10.0.22621.0 (26100 breaks); `xmake -j 4` max (C3859 PCH OOM); SdkGenerator broke in git worktrees (`2a938f2`, `00ea7d3`, `docs/CAMPAIGN.md:418-429`).
- Steam tunnel requires Steam build on both sides; uses Cyberpunk's AppID networking.
- Dialog injection uses hard-coded default keys; fails with rebound keys.
- Licence: CyberpunkMP is Tilted Phoques licence; the campaign was written as a plugin (CampaignSystem + Plugins/*.reds) to fall under the plugin exemption (`docs/CAMPAIGN.md:448-453`).

## 12. Quick 2.5 checklist for our rewrite

1. ExecuteNode/CreateContext + QuestsSystem/PhaseInstance/QuestContext layouts: reuse via Codeware-confirmed facts [2.5-verified].
2. ProcessInput 3535541945: hash exists; re-derive signature and use **0x48** RawInputData (Codeware); or drive choices via InkSystem_ProcessInputEvents 673454961, or a script/blackboard path.
3. Sector spots: use Codeware's `worldStreamingSector.GetNodeSetup*` script API instead of `+0x40`/0x90 raw layout.
4. Facts: script listeners work in principle; native option = FactManager at `QuestsSystem+0xF8` (vfunc 0x10/0x18).
5. Regenerate fact list and workspot list from 2.5 archives; re-check gamedef replacement and Steam flat-API export names on the 2.5 binaries.
