# Quests, facts and scenes

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 7. Quests / facts / scenes (campaign fork + NCO)

Full detail: `cpmp/notes-campaign.md`. CAMP was built on **2.31** (RED4ext 1.30.0, Codeware 1.20.5,
ArchiveXL 1.27.1, TweakXL 1.11.4, redscript 0.5.31, Input Loader 0.2.3; `tools/campaign/BuildInstaller.ps1:21-31`)
and is **untested in game** — `docs/MISSIONS.md` test-notes table is empty and `docs/CAMPAIGN.md:249-252`
leaves open whether a scene node run outside its phase plays the scene. Paths below relative to `campaign/`.

**Architecture** (`docs/CAMPAIGN.md:34-60`): host-authoritative; first player in the lobby = host; the
host's game runs every quest; guests start a **quest-free world** ("a running quest cannot be stopped,
only prevented from starting", `:43-49`) and mirror journal + facts (script), "what you see/hear" quest
nodes (native QuestMirror), tracked objective, scene tier/position, NPC health/death; dialog choices are
voted, the winner applied by synthetic key presses. Guests start New Game with
`SetIsExpansionStandalone(true)` so the game loads `ep1\quest\ep1_standalone.gamedef`, replaced by a
quest-free gamedef (`zz_CampaignCoop_GuestStart.archive`, from NCO `9e1832f`)
(`code/assets/redscript/Plugins/CampaignLobby.reds:1021,1165`). [unverified on 2.5]

- "In game" detection: Codeware `Session/Ready` with `GameSessionEvent.IsPreGame() == false`; the main
  menu is a world with a player ~8 s after launch so `OnWorldAttached`/`OnPlayerAttach` fire there too
  (`CampaignLobby.reds:106-121`, `9a1e671`). Codeware 2.5 still has these. [2.5-verified API]
- Lobby state lives in a Codeware **ScriptableService** — ScriptableSystems don't exist at the main menu
  (`CampaignLobby.reds:97-99`).

**Running quest nodes on a guest (QuestMirror)** (`code/client/App/World/QuestMirror.cpp`, `7f88bec`)

<!-- prettier-ignore -->
| Hash | Use | Signature | Tag |
| --- | --- | --- | --- |
| **3227858325** | `QuestPhaseInstance::ExecuteNode` — hooked on host (record), called on guest (replay) | `u8 (PhaseInstance*, questNodeDefinition*, QuestContext&, const QuestNodeSocket& in, DynArray<QuestNodeSocket>& out)` (`:67-72`) | [2.5-verified] = CW `QuestPhaseInstance_ExecuteNode`, `CW Red/QuestsSystem.hpp:286-292` |
| **3144298192** | `QuestsSystem::CreateContext` | `void* (QuestsSystem*, QuestContext*, i32, i8, i32, i32)`, called `(sys,&ctx,1,0,-1,-1)` (`:63-65,348`) | [2.5-verified] same call in `CW App/Quest/QuestPhaseExecutor.hpp:31` |

- Layouts (identical to CW, [2.5-verified]): `QuestsSystem+0x60` SharedSpinLock (hold while executing),
  `+0x68` `Handle<questPhaseInstance>` root phase, (CW: `+0x78` NodePathHash map, `+0xF8` `FactManager*`);
  `questPhaseInstance+0x30` `Handle<questQuestPhaseResource>`, `+0x40` `Handle<questGraphDefinition>`
  (CW: +0x70 NodePath, +0x80 NodePathHash, +0x88 children, +0x98 handlers; `ExecutePath` vfunc +0x120);
  `QuestContext` size **0x340**, `phaseStack` (DynArray<questPhaseInstance*>) @0xF8, `nodeID` u16 @0x108,
  `QuestPhaseContext` @0x110 (size 0x230: vtbl, game@08, prefabLoader@30); `QuestNodeSocket{CName name;
u8 unk08}` (`QuestMirror.cpp:21-61`).
- Replay recipe (= CW `QuestPhaseExecutor::ExecuteNode`): lock `sys+0x60`, zero a QuestContext,
  CreateContext, push root phase on `phaseStack`, ExecuteNode(root, node, ctx, socket, out), clear stack.
  CAMP passes the recorded input socket name (CW passes empty); a thread_local flag stops re-recording
  (`:335-359`).
- Node address recorded by the host: `<resource path hash>/<phase node id>.<…>/<node id>/<input socket
CName hash>`; resource from the phase, else nearest parent in `ctx.phaseStack`; phase chain by DFS from
  `resource->graph` through `questPhaseNodeDefinition.phaseGraph` (`:198-282`). Guest:
  `ResourceLoader::LoadAsync<graphGraphResource>(ResourcePath(hash))`, run strictly in order, skip a
  resource not loaded after 30 s (one stuck resource blocked every later scene, `2f3dfae`) (`:396-481`).
- 31 mirrored node classes: questScene*, questPhoneManager, questAudio*, questVoicesetManager,
  questSpawnManager, questDynamicSpawnSystem, questEntityManager, questCharacterManager, questCrowdManager,
  questPopulactionController, questWorldDataManager, questEnvironmentManager, questTimeManager,
  questFXManager, questRenderFxManager, questInteractiveObjectManager, questTeleportPuppet, questMovePuppet,
  questRotateTo, questUseWorkspot, questPuppetAIManager, questForcedBehaviour, questClearForcedBehaviours,
  questBehaviourManager, questSendAICommand, questVehicle, questTeleportVehicle, questMappinManager,
  questRewardManager (`:76-108`). NOT mirrored: facts (questSetVar), journal, control flow, conditions,
  phases, checkpoints. Scene and phone nodes are re-woken by the engine while they run → dedup 5 min per
  address, others 2 s (`:110-116`). Late joiners get no old nodes (`CampaignSession.cs:517-530`).

**Fact sync** (script only, `code/assets/redscript/Plugins/Campaign.reds`; server `CampaignSession.cs`)

- Host: one `QuestsSystem.RegisterListener(StringToName(name), watcher, n"OnFactChanged")` per fact, one
  watcher object per fact (the callback gets only the value) (`Campaign.reds:274-285,485-491`); read
  `GetFactStr`, guest writes `SetFactStr` only if different (`:495,580-589`). [unverified on 2.5]
- Watched list: 16,164 names (11,083 base + 5,081 PL) = every fact a `.quest/.questphase/.scene` graph
  **writes** via `questSetVar_NodeType` (`factName`, `factName1`, `factName2`), minus debug/test/demo
  facts; facts only _read_ by graphs are set by game code per player and deliberately excluded
  (`tools/campaign/extract_facts.py:20-40`). Regenerate from 2.5 archives (WolvenKit CLI `unbundle -r
"\.(quest|questphase|scene)$"` + `convert serialize`).
- Snapshot protocol (`docs/CAMPAIGN.md:62-75`): role first; host registers listeners, sends non-zero facts,
  journal tree, `SnapshotDone`; **only then** guests get journal, then every watched fact including unset
  ones as 0 (otherwise a guest further along keeps stale facts), then tracked objective; then deltas.
  Nothing goes to guests before the snapshot is complete. Batches = one `"k:v,k:v"` string ≤16,000 chars
  (RPC buffer 64 KB; the C# stub generator misnames arrays of primitives).
- Concern noted by the author: thousands of script listeners at once; native alternative =
  CW `FactManager*` at `QuestsSystem+0xF8`, vtable `+0x10 GetFact(store,id)`, `+0x18 SetFact(store,id,v)`,
  store 1 = named facts, 2..10 graph stores, `FactID = FNV1a32(name)` (`CW Red/QuestsSystem.hpp:114-192,223`).
  [2.5-verified layout per CW]

**Journal + tracked objective** (`Campaign.reds:411-578`, `1079f52`)

- Host: `JournalManager.RegisterScriptCallback(this, n"OnJournalEntryStateChanged",
gameJournalListenerType.State)` → `(entryHash: Uint32, className, notifyOption, changeType)`; snapshot via
  `GetQuests` (filter active/succeeded/failed) + recursive `GetChildren`. Guest: `GetEntry(hash)`, skip
  unknown/same, `ChangeEntryStateByHash(hash, state, JournalNotifyOption.Notify)`. Tracked: host polls
  `GetTrackedEntry()` every 0.25 s; guest `TrackEntry` only once the entry exists and is Active.
- CW 2.5 journal vfuncs: `GetEntries 0x210`, `GetEntryByHash 0x220`, `ChangeEntryState 0x268`,
  `GetEntryState 0x2B0`, `GetEntryTimestamp 0x2C8`; hashes `JournalManager_LoadJournal 1964512489`,
  `JournalManager_TrackQuest 3274382593` (`CW Red/JournalManager.hpp`). [2.5-verified per CW]

**Scenes** (`CampaignDialog.reds`, `CampaignExtras.reds`, `SceneSpots.cpp`)

- Wrap `PlayerPuppet.OnSceneTierChange(newState)`; host reports tier+position; guest ≥`Tier3_LimitedGameplay`
  and >25 m away → teleport next to host; ≥`Tier4_FPPCinematic` while the guest's own scene didn't start →
  `GameplayRestriction.NoMovement` until it ends (`CampaignDialog.reds:476-595`). Holds ref-counted per
  reason (`d652278`). Block saving while held with a ScriptableSystem `IsSavingLocked()` override;
  remove leftover god modes by source name on load (`2f3dfae`).
- Remote puppets within 1.5 m of V during a scene are pushed 1.2 m left/right; held puppets skip
  interpolation.
- Extras: V in a car → `MountAIEvent` puppets into free seats (`seat_front_right, seat_back_left,
seat_back_right, seat_front_left`) and pause vehicle sync (each game has its own scene car); V sitting
  → nearest free sit spot ≤8 m via `AIUseWorkspotCommand{workspotNode = NodeRef(globalNodeID)}` after
  teleporting the puppet there; standing → stand spot ≤5 m or a 1.6 m half circle facing the current
  speaker (wrap `BaseSubtitlesGameController.SpawnDialogLine`). Whether MaMuppet/WaMuppet accept
  `AIUseWorkspotCommand` is unchecked.
- `SceneSpots`: raw `worldStreamingSector + 0x40` DataBuffer of `worldCompiledNodeInstanceSetupInfo`,
  stride 0x90 (Transform@00, scale@20, node*@50, globalNodeID@58, proxyNodeID@60, nodeIndex@78), filter
  `worldAISpotNode`/`AIActionSpot` + workspot path whitelist (142 sit + 194 stand `generic` workspots).
  **[unverified/risky on 2.5]**: CW now reads node setups through buffer-version-dependent accessors
  (`CW App/World/NodeSetupWrapper.hpp:24-170`); use CW's script API
  `worldStreamingSector.GetNodeSetupCount/GetNodeSetup` instead. Sector source: CW `Resource/Ready`
  callback filtered to `worldStreamingSector`.
- Host modes: braindance (`BraindanceSystem.GetIsInBraindance()`) → guests held + Invulnerable; Johnny
  (`PlayerPuppet.IsJohnnyReplacer()`) → message only; regroup if host jumps >150 m between 0.5 s polls
  (fast travel) and isn't mounted.

**Dialog choice injection (ChoiceInput)** (`code/client/App/World/ChoiceInput.cpp`, technique from
VedalAI/neuro-cyberpunk via alphanin9/SharedPunk, MIT)

- Hook `Player::ProcessInput` **3535541945** `void (cpPlayer*, RawInputBuffer&)` (`:66-70`) — hash in
  2.50, not used by CW, signature [2.5-present, unverified]. Local cpPlayer via `gamePlayerManager`
  vtable **+0x200** (`:72-82`) [unverified].
- CAMP's `RawInputData` = **0x40** bytes {`EInputKey key@00`, `EInputAction action@04`, `float value@08`,
  `u32 mouseX@0C`, `u32 mouseY@10`, `unk14`, `u32 unk18@18` (must be 1), `u64 unk20..unk38`}; **CW on 2.5
  has the same fields plus `u64 unk40` → size 0x48** (`CW Red/Input.hpp:17-32`) → CAMP's stride is
  **[broken-on-2.5]**. `RawInputBuffer{vtbl; DynArray<RawInputData> inputs}`; vtbl +0x08 = GetInputs;
  CAMP's synthetic buffer fills +0x10..+0x88 with no-ops and +0x90 returning 0; allocator
  `Memory::InputInternalAllocator`. Alternative on 2.5: CW `InkSystem_ProcessInputEvents` **673454961**
  `void (InkSystem*, u64, RawInputBuffer&)` (`CW Red/InkSystem.hpp:42-44`). [2.5-verified]
- Mechanics: while voting strip confirm keys `IK_F`, `IK_Enter`, `IK_Pad_X_SQUARE`, `IK_LeftMouse` and count
  them as the host's vote; apply the winner as N× `IK_Up`/`IK_Down` then `IK_F`, one key per update,
  **120 ms apart**, each a Press (value 1, unk18 1) + Release buffer. Breaks with rebound keys.
- Script side: blackboard `UIInteractions.DialogChoiceHubs` (`{choiceHubs[{id, choices[ListChoiceData],
timeProvider}]}`), `ActiveChoiceHubID` (<0 = none), `SelectedIndex`; only the active hub counts;
  global index = choices of earlier hubs + SelectedIndex; `~` = unavailable
  (`gameinteractionsChoiceType.Inactive`/`CheckFailed`). Vote 15 s, **5 s for timed dialogs** (must end
  before the game's timer), only with ≥2 players in world. Counts drawn by wrapping
  `dialogWidgetGameController.UpdateDialogsData` and re-setting the variant with force=true. Guests with a
  different option count get a stand-in hub (id 77000) written to `UIInteractions`.

**Server (C#)** (`code/server/scripting/CampaignSystem/`): state = journal `Dictionary<uint,int>`, facts
`Dictionary<string,int>`, tracked, watched facts, hostId, snapshotReady, dialog vote, scene tier/pos
(`CampaignSession.cs:141-166`); host-only for journal/facts/tracked/nodes/scene/NPCs; 27 client→server
RPCs, ~25 server→client (`Campaign.reds:16-214`); timer ticks and join/leave are called from native
code **without a catch** — an exception kills the server (`:183-196`, `9a910b7`). RPC ids = FNV1a64 of
the script class name / function name (redscript mangled name for client funcs, e.g. `SetRole;Bool`).
Lifepath vote on `CharacterCreationBackstorySelectionMenu`, forced via
`m_characterCustomizationState.SetLifePath(TDBID)` + `SpawnEvent(n"OnAccept")`.

**NCO world-start facts** (`MAP:2418`)

- A game start is a `.gamedef` (root quests + spawn tag + world); 824 ship. NCO overrides
  `ep1\quest\ep1_standalone.gamedef` with `cyberpunk2077_ep1_standalone.quest` (base city, prologue
  skipped) + `ep1.quest` (non-standalone EP1 root: sets `ep1_installed=1`, enters `ep1.questphase` at
  `Base`) + `ep1_preorder.quest`, spawn tag `#q000_spwn_start` (a holding room; the server teleports
  arrivals out). Result: empty quest log, no Songbird, level 1. [2.31-verified; archive needs re-check on 2.5]
- A spawn tag is owned by its quest; keeping `#q301_spwn_ep1standalone_opener` without its quest made
  players fall through the map.
- `MainMenu → SpawnEvent(n"OnNewGame")` → preGameScenarios.script:308 routes to the EP1 branch whenever
  Phantom Liberty is installed.
- Dogtown gate = fact **`ep1_side_content >= 1`** (pause condition in
  `ep1\openworld\combat_zone_gate\combat_zone_gate.questphase`), not q301.
- `ep1_standalone` is read by 69 quest phases (metro, apartments, Dogtown community) — never touch it.
  Setting 8 `q301_*`/`holo_songbird_*` facts did NOT stop the Songbird holocall
  (`docs/songbird-holocall-hang.md:63-92`) — facts like `q301_failed` are written by the graph but never
  read. What does work (`08f1ed3b`): `songbird_holocall.questphase` gates every call variant on
  `holo_<call>_activate > 0 AND holo_setup_active < 1 AND holo_setup_started < 1`, so holding
  **`holo_setup_active = 1`** blocks Songbird and ~20 PL holocalls (Reed, Myers, Alex, Kurtz, courier);
  set it at connect, before world facts. The holocall on a new character produced a 120 s main-thread
  watchdog stall (`Watchdog timeout!`, engineWatchdog.cpp:198) with all mod threads idle — never
  root-caused; delivered by `HoloAudioCallLogicController` (scene `q301_00_holocall_hook`), refusing that
  controller's init froze the game outright (`902bfacf`, `2a11cb6b`). 2.5 mitigation
  `MpSilenceStoryHolocalls` (`3d7e8a23`).
- Fact `ep1_side_content`: game writes it =1 in 13 places, never 0; CDPR's `ep1_chicken_unlocks.questphase`
  opens Dogtown the same way (`06af8b5b`).
- The quest engine keeps running underneath; blocking the phone does not stop quest scenes; full PL
  quest stack is loaded (`ffe4c9c1`). Quest-scoped interiors (Konpeki, All Foods, Clouds, Arasaka
  Industrial Park) only stream during their quest; doors are gated on facts (`2ef73dcb`).
- 2.31 scripts expose only `LoadLastCheckpoint` (no LoadSave/LoadSaveInSlot/GetSaveSystem); the MP path
  `singleplayerMenu.script:1012` = `m_isModded ? LoadModdedSave(0) : LoadLastCheckpoint(false)` (= newest
  save) (`65cc869f`, `2b60289a`). Journal `ChangeEntryState` is import-only (`1610ed5e`). **On 2.50** `ChangeEntryState` and `ChangeEntryStateByHash` are
  ordinary script natives that vanilla code calls, and `LoadSavedGame(saveId)` / `ManualSave(saveName)`
  exist; the `SetScriptedQuest*` journal functions are stubs returning false (`coop-quests-world.md`).
  [2.5-static]
- Phone (2.31): `questPhoneCallPhase` (phoneSystem.script:9); answer = `PhoneInteract` BUTTON_RELEASED,
  decline = `PhoneReject` BUTTON_HOLD_COMPLETE, both require `callPhase == IncomingCall`
  (newHudPhoneGameController.script:512); `ui_phone_incoming_call` loops — call StopRingtone on every exit
  (`b6242c85`, `d58ad961`); `PhoneCallInformation{callPhase, contactName: CName, isAudioCall,
isRejectable, isPlayerCalling, visuals}` written to the `UI_ComDevice` blackboard presents a call
  (`1d5aec2a`).
- Phone: block calls at `PhoneSystem.OnTriggerCall` (blocking at `SetCallInfo` traps the player in an
  invisible call); phone phase enum `{Undefined, IncomingCall, StartCall, EndCall}`;
  `questTriggerCallRequest.isPlayerTriggered` means "the player triggered a quest call", not a
  multiplayer call (`MAP:927-944`, `NEW-SERVER-NETCODE-PORTING-HANDOFF.md:295-298`).
- Server world facts (`worldfacts.json`) applied client-side at join; facts open areas without faking
  quest completion.

### 13.15 Quest phases: making a node wait (2026-11-18, offline, spike S1)

Read in the 2.50 exe with `tools/re/decompile` (offline, no game). Feeds
`plugin/src/engine/quests.cpp`, the guest's quest freeze
(`coop-quests-design.md` §1.3).

- `QuestPhaseInstance::ExecuteNode` (hash 3227858325, `0x1402b3aa0`):
  returns 1 at once when the node is a disableable node in the phase's
  disabled set (`phase+0xC8`, by `node+0x40` id) or when the node's
  pre-check vfunc `+0x150(node, ctx+0x110, socket)` returns 1; otherwise
  notifies the context's listener (`ctx+0x70`, vfuncs `+0x1B8` before,
  `+0x1C0` after), runs the node (`0x14034fa80`: `ctx+0x110` vfunc `+0xE0`
  with the node id, clears the outputs, node vfunc `+0x158`), and maps the
  result through node vfunc `+0x160`; a context state byte `ctx+1 == 7`
  forces 3. [2.5-verified offline]
- Its only direct caller, `0x1402b393c`, runs one entry of
  `ExecuteGraph`'s work list (`0x140352984`, entered through
  `QuestPhaseInstance_ExecuteGraph` 779107884 = `0x1403528a4`, which
  pushes the phase on `ctx.phaseStack` at `+0xF8` around the call):
  result **0**: the node finished; a `questOutputNodeDefinition` (size
  0x58: socket name `+0x48`, type `+0x50`) ends the pass with its socket,
  any other node queues its output sockets; **2**: finished, outputs
  queued; **1 or 3**: nothing queued, the pass goes on with the other
  queued nodes. [2.5-verified offline]
- So a hook returning **1 without calling the original** makes a node act
  as a disabled one: nothing, no outputs, the phase stays active. The
  engine keeps no trace of that signal: tandem holds it (phase `NodePath`
  `+0x70`, node id, input socket CName) and gives it back when the freeze
  ends with the root phase's `ExecutePath` (vfunc `+0x120`, under the
  quests system's lock `+0x60`, after `QuestsSystem::CreateContext`
  3144298192 `(sys, ctx, 1, 0, -1, -1)`: Codeware's
  `QuestPhaseExecutor::ExecutePath`, used by its `OpenWorldSystem`). [I] —
  whether every node resumes cleanly that way is the in-game check.
- Per-phase filter: a phase is open-world when its resource
  (`phase+0x30` → `CResource.path` `+0x30`) or an ancestor's on
  `ctx.phaseStack` is one of Codeware's open-world phases
  (`QuestPhaseRegistry.cpp:12-20`), or its `NodePath` extends one of
  theirs; the root phase (empty path) always runs. [C][I]
- To check in game (`quest_freeze = on`, `tools/game.sh cmd quest freeze
on`, `quest report`, `quest freeze off`): refused-node counts by phase;
  no pending call rings and the tracker does not move while frozen; on
  `off`, the held signals resume (a call queued during the freeze rings);
  vendors, metro, apartment, car summon, NCPD hustles still work; no
  crash in the 1 s after `off`. [2.5-untested]

### 13.16 Story levers in the scripts (2026-11-18, read in final.reds, not run)

What `scripts/tandem/Story.reds`, `Phone.reds` and `Dialogue.reds` rely
on; every line is [2.5-untested] until `story-guest.sh` / `story-host.sh`
pass.

- A guest presents the host's call through its own
  `PhoneSystem.TriggerCall` (`final.reds:240618`, private: reached from an
  `@addMethod`), which writes `UI_ComDevice.PhoneCallInformation` and the
  `phonecall_*` fact; a wrap of `SetPhoneFact` (`:240841`) holds the fact
  back while a mirrored call is up. The HUD's answer goes through
  `PickupPhoneRequest` → `OnPickupPhone` (`:240662`), wrapped to send it to
  the host. The incoming widget finds the contact by journal id
  (`GetIncomingContact`, `:409000`): a contact the guest's journal lacks
  shows no name.
- Subtitles: `UIGameData.ShowDialogLine` carries `array<scnDialogLineData>`,
  `HideDialogLine` `array<CRUID>` (`:268284`, `:268327`). Scripts cannot
  build a `CRUID`: `TandemStory.LineId` / `LineValue` convert it.
- Choice hubs: `UIInteractions.DialogChoiceHubs` (`DialogChoiceHubs{
choiceHubs: array<ListChoiceHubData>}`, `:243020`, `:389618`),
  `ActiveChoiceHubID`, `SelectedIndex`, `LastAttemptedChoice`
  (`InteractionAttemptedChoice`, `:199455`). The guest's stand-in hub has
  no native backing: its cursor moves on the input actions
  `ChoiceScrollUp` / `ChoiceScrollDown` / `ChoiceApply` (names from the
  input contexts, not seen in the scripts: to confirm).
- Holds: `GameplayRestriction.NoCombat` and `BaseStatusEffect.Invulnerable`
  status effects (`:128602`); script god mode does not protect V (§13.11).
- Saving: a `ScriptableSystem` overriding `IsSavingLocked` (`:48411`) and
  `SaveLocksManager.RequestSaveLockAdd(game, n"tandem")` (`:195179`, not
  saved: re-added after a load); `AutoSaveSystem.InvalidateAutoSaveRequests`
  (`:203134`). Whether the pause menu greys out Save with only these: to
  see.
- The co-op reward ledger is a `persistent` array on `TandemStorySystem`:
  it reaches the guest's save with its next save after the session.
- Johnny is an NPC of the host's world, `Character.Silverhand`
  (`PlayerPuppet.IsTargetFriendlyNPC`, `:40624`); playing as Johnny is the
  `Character.johnny_replacer` puppet (`:39363`), `UI_PlayerStats.isReplacer`
  (`:83411`) and the fact `isPlayerPossessedByJohnny` (`:50012`). His relic
  glitches are status effects with the `JohnnySickness` tag
  (`BaseStatusEffect.JohnnySicknessLow/Medium/Heavy/MediumQuest`, `:44352`).
  `GetPlayer` is the _controlled_ object (`:39193`): as Johnny, the host's
  streamed position and pose are the replacer's.
- Songbird's calls use `questPhoneCallVisuals.Somi` (`:10735`), drawn by
  `HudPhoneGameController` (extends the native
  `SongbirdAudioCallGameController`, `:389152`, `:389380`) instead of the
  regular call widget (`:408912`).
- `PlayerPuppet.OnStatusEffectApplied` / `OnStatusEffectRemoved`
  (`:43899`, `:44474`) are where the host sees a story effect come and go;
  `ApplyStatusEffectEvent.isNewApplication`,
  `RemoveStatusEffect.isFinalRemoval` (`:203824`, `:203958`).

### 13.23 Scenes: what kind, from the game's data (2026-11-18, offline and SDK)

For private scenes (`plugin/src/engine/scenes.cpp`,
`coop-quests-design.md` §2.12).

- A quest scene starts from a `questSceneNodeDefinition` (RTTI 0x88): its
  `sceneFile` (`RaRef<scnSceneResource>`, +0x48) is the scene's resource
  path. It runs through `QuestPhaseInstance::ExecuteNode` (§13.15), which
  the engine calls again while the scene plays (the campaign fork's dedup
  of scene nodes, §7). [2.5-verified offline: RTTI size = the SDK's
  generated struct; the re-wake is [K]]
- `scnSceneResource` (RTTI 0x2D8 = SDK): `sceneCategoryTag` at +0x290,
  `scnSceneCategoryTag` = voiceset 0, mainQuests 1, sideQuests 2,
  minorQuests 3, otherQuests 4, dialoguesQuests 5, streetOpenWorld 6,
  vendorsOpenWorld 7, dancefloorsOpenWorld 8, cityOpenWorld 9,
  chatsOpenWorld 10, otherOpenWorld 11, holocalls 12, other 13;
  `actors` (`scnActorDef`, 0x1E0: `actorName` +0x1B0,
  `specCharacterRecordId` +0x1D0), `playerActors`. [SDK; sizes checked
  against `rtti-vtables-2.50.tsv`. tandem reads every field by its RTTI
  property offset, logged at the first use: `scenes: sceneFile +…`]
- No scene, node or resource field says "private", "romance" or
  "intimate", and the vanilla scripts never mention romance: the category
  tag and the actors are the only data. Romance state lives in quest
  facts the scripts do not name. [2.5-verified offline: final.reds has no
  match]
- Phone contacts carry no such mark either: a `JournalContact`
  (final.reds:196998) has a name, an avatar, `IsKnown`, `IsCallable` and
  `GetType()`, which is `gameContactType` `Caller` or `Texter` only
  (:7037); a call names its contact by journal id (the contact's last path
  segment, `judy` in `story-guest`). A contact list is the only way to tell
  a romance partner's calls (`Phone.reds` `HostOnly`). [2.5-verified
  offline]
- A loaded resource by path: `ResourceLoader::FindToken` (address hash
  0xC86F39B7 = `res::ResourceLoader::TryAcquireResourceToken_NoLock` at
  0x14027d834 on 2.50; the SDK takes the loader's token lock around it).
  Game thread only. [2.5-verified offline]
- Hiding a puppet with a fade:
  `GameObject.ToggleForcedVisibilityInAnimSystemEvent(owner, source,
visible, transitionTime)` (final.reds:14006, a
  `ToggleVisibilityInAnimSystemEvent`, per source name). [2.5-untested]
- To check in game: the log's `scenes: 0x… is private (…)` lines for a
  vendor chat, Judy's or Panam's romance scenes, a main quest scene with
  Judy (must be important); the category of a few known scenes.
  [2.5-untested]

### 13.29 What a quest's completion is made of, and what quests test (2026-11-19, scripts, SDK and RTTI read)

For `docs/architecture/quest-carryover.md`. Nothing here was run.

- Quest kinds: `gameJournalQuestType` = MainQuest 0, SideQuest 1,
  MinorQuest 2, StreetStory 3 (gigs), CyberPsycho 4, Contract 5,
  VehicleQuest 6, ApartmentQuest 7, CourierQuest 8, CourierSideQuest 9
  (final.reds:7102); `JournalManager.GetQuestType(entry)` (:196627 class).
  [2.5-verified offline]
- **Quests test the player's inventory.** `questInventory_ConditionType`
  has `isPlayer`, `itemID` (a TweakDBID), `itemTag` (a CName), `quantity`
  and a comparison; `questInventoryDuplicate_ConditionType` an `itemID`;
  the quests system also has inventory listeners
  (`questObjectInventoryListener`, `questObjectItemListener`). Quest nodes
  add and remove items (`questAddRemoveItem_NodeTypeParams`: `isPlayer`,
  `itemID`, `quantity`, `removeAllQuantity`, `tagToRemove`) and set item
  tags (`questSetItemTags_NodeType`). So an item, or any item with a tag,
  in a V's inventory can satisfy a quest condition of that V's own story.
  [SDK structs; classes in `rtti-vtables-2.50.tsv`]
- Quest items are the tag `Quest` (`itemData.HasTag(n"Quest")`,
  final.reds:52919, :68485; the tag is dynamic: crafting removes it,
  :70795); iconics are `RPGManager.IsItemIconic` (:35655).
  [2.5-verified offline]
- Rewards: `RPGManager.GiveReward` gives the record's items, XP, money and
  **achievements** (`SendAddAchievementRequest`, :37606) to the local
  player. Some script rewards count facts on the way
  (`prologue_ma_done`, `prologue_sts_done` before `q005_done`, :37349-37386;
  gig rewards are recognised by the record name `sts_…`, :37546).
  [2.5-verified offline]
- Open-world completion state: an NCPD hustle's is its point-of-interest
  mappin's phase (`gamedataMappinPhase`: CompletedPhase 0, DefaultPhase 1,
  DiscoveredPhase 2, UndiscoveredPhase 3; final.reds:1327) plus its
  community and containers, all modelled by Codeware's `OpenWorldSystem`
  (`CW src/App/World/OpenWorldSystem.cpp`), which resets one; the mappin
  phase has no script setter (`Raw::MappinSystem::SetPoiMappinPhase`,
  `CW src/Red/MappinSystem.hpp`). A gig is a quest phase: its journal
  entries and facts can be set by script, its graph position cannot (no
  script or Codeware call moves a phase; Codeware's `StopPhase` /
  `RestartPhase` vfuncs are commented out, `CW src/Red/QuestsSystem.hpp`).
  [C] [I]
- District and fixer progress shown in the UI:
  `JournalManager.GetMetaQuestData(gamedataMetaQuest)` →
  `JournalMetaQuestScriptedData{hidden, percent, text}` (:196722, :319408);
  what feeds it is native. [2.5-verified offline for the API; the source
  of the percentage is unknown]
- Vehicles are unlocked by `VehicleSystem.EnablePlayerVehicle(vehicle,
enable)` (:189919), called by quest nodes, not by reward records: a
  guest paid a reward record gets no car from it. [I]
- Phantom Liberty: `IsEP1()` (:223600), `JournalManager.IsEp1Entry(entry)`
  (:196693). The point of no return is a save type
  (`inkSaveType.PointOfNoReturn`, :8239) and a reward screen
  (`UI_PointOfNoReturnRewardScreen`, :315025). [2.5-verified offline]
- To check in game (spikes C1a, C1b of the carry-over design): a hustle
  marked done from outside stays done across a save and load; a gig whose
  end facts are set finishes through its own graph. [2.5-untested]

### 13.45 Gameplay restrictions: how quests hold V, and how a guest wears the host's (2026-11-19, scripts, Codeware and RTTI read)

For `docs/architecture/story.md`, "Restrictions"
(`scripts/tandem/Restrictions.reds`). Nothing here was run; F is
`final.reds` (2.50).

- **The status effect API.** `StatusEffectSystem.ApplyStatusEffect(objID,
statusEffectID, opt instigatorID: TweakDBID, opt instigatorEntityID,
opt applyCount, opt direction, opt isSavable, opt proxyEntityID)` and
  `RemoveStatusEffect(objID, statusEffectID, opt removeCount: Uint32)`
  (F:51168, :51180); `GetAppliedEffects(objID, out array)`,
  `HasStatusEffectWithTag`, the static `ObjectHasStatusEffect(target, id)`
  and `ObjectHasStatusEffectWithTag(target, tag)` (:51214). **No remove by
  source or instigator**: `StatusEffectHelper.RemoveStatusEffectsByInstigatorID`
  (:169066) matches the instigator, then removes the whole record. One
  `StatusEffect` per record per target: `GetStackCount`, `GetMaxStacks`,
  `GetInstigatorStaticDataID`, `GetInstigatorEntityID` (:83695), the
  instigator being the last application's. The game's own counted uses
  apply one stack and take one off (`RemoveStatusEffect(…,
t"GameplayRestriction.NoWeapons", 1u)`, :282682/:282700, :548508…:548809).
  Events: `ApplyStatusEffectEvent { isNewApplication, isAppliedOnSpawn,
instigatorEntityID, proxyEntityID }` (:203824) and
  `RemoveStatusEffect { isFinalRemoval }` (:203958), both with
  `staticData` and `stackCount` (:179275). [2.5-verified offline for the
  signatures; maxStacks of the restriction records is TweakDB data, not
  read]
- **Codeware's view of the sources**: `StatusEffect.sourcesData:
array<gameSourceData{name: CName, savable}>`, `applicationSource`,
  `instigatorRecordID`, `maxStacks` (CW
  `scripts/Base/Addons/StatusEffect.reds`). Quests name a source when they
  add restrictions (below), so this is the one place that tells a quest's
  from another's. **Not readable from script**: `gameSourceData` is a class
  (`ISerializable`, 0x40 bytes, `name` at +0x30) held by value, without
  `ref`; `for x in` over the array is refused, and
  `NameToString(effect.sourcesData[i].name)` compiles but overwrites the
  VM's native frame: the host crashed 2 s into every car ride (2026-11-21,
  access violation at `NameToString`'s native, +0xd38a7c, a saved register
  holding data; script frames read off the frozen thread's stack:
  `TandemRestrictions.HostTick`). Read it in C++ through RTTI if it is
  ever needed. [broken-on-2.5]
- **What a gameplay restriction is**: a status effect with the gameplay
  tag `GameplayRestriction`. `PlayerPuppet.OnStatusEffectApplied` (F:43899)
  hands those to `PlayerGameplayRestrictions.OnGameplayRestrictionAdded`
  (:43931, :179041; removal :44490, :179082), and pushes the record's
  `ActionRestriction` into the state machine's blackboard on a **new**
  application only (:43934, `AddGameplayRestriction` :39826) while every
  removal event pops it (:44492, :39842): taking one stack off a record
  another source also holds can drop its action restriction early.
  `RemoveAllGameplayRestrictions` removes the tag (:179037), on every fast
  travel (:50417). There is no restriction value in
  `gamedataStatusEffectType` (:4332); records cast to
  `GameplayRestrictionStatusEffect_Record` (:249721). [2.5-untested]
- **Records the scripts name** (`t"GameplayRestriction.*"`): BlockAllHubMenu,
  BlockAllMenu, NoWeapons, NoMovement, FistFight, BlockFastTravel,
  NoRadialMenus, NoJump, NoCameraControl, InfiniteAmmo, FastForward
  (+CrouchLock, HintActive), Tier2Locomotion (Slow, Fast), BodyCarrying*,
  VehicleNoInteraction, VehicleCombatNoInterruptions, PhoneCall*,
  NoScanning, ForceCrouch, FocusModeLocomotion, FirearmsNoUnequipNoSwitch,
  Firearms, DriverCombat*, Braindance, SecurityLocker,
  NoWorldInteractions, BlockDeviceInteractions. `GameplayRestriction.NoCombat`
  is named by no vanilla script (quests and tandem use it; it exists in
  TweakDB). [2.5-untested]
- **How the game answers them** (tags the player state machine reads):
  `NoCombat` (unless `FastForward`), or `VehicleScene`, is
  `IsNoCombatActionsForced` (F:246203), one of the reasons of
  `IsEmptyHandsForced` (:246315); the upper body state machine's
  `ForceEmptyHands` state then sends `EquipmentManipulationAction.UnequipAll`
  (:540443) and sets `PlayerStateMachine.UpperBody` = 5 (:540449), and the
  quick slots, the wheel and the hints are blocked (:239164, :420961,
  :246807). So **holstering the game's way is a NoCombat-tagged effect**: a
  one-off `EquipmentSystemWeaponManipulationRequest` (UnequipWeapon 21,
  UnequipAll 25, :11190) does not stop a redraw. Fists, Melee, Firearms,
  FirearmsNoUnequip/NoSwitch (:246185-:246202; on add, Fists and Melee
  request the weapon, :179052); NoWeapons (`EmptyHandsEvents`, :540826);
  NoJump (vault and climb, :246174; the jump hint by record, :246810);
  ForceCrouch (:246168); NoDriving (:545366); NoScanning (:43929). Forced
  walk is no tag: a state machine parameter (:244637); NoSprint, NoRun and
  VehicleNoSummoning are read by no script (their records' action
  restrictions or stats). [2.5-untested]
- **The other reasons hands are forced empty**: the state machine's
  `ForceEmptyHands` (the scene tier's `emptyHands`,
  `SceneTierAbstractEvents`, :529823-:529861; `questSetTier_NodeType
{ forceEmptyHands }`), `ForceEmptyHandsByZone` (a safe zone), and the
  quest fact `ForceEmptyHands` (:246315). `SceneAimForced` and
  `SceneSafeForced` (events `SceneForceWeaponAim` / `SceneForceWeaponSafe`,
  :44668, :44708) override them. None of these is a status effect: the host
  reads `PlayerStateMachine.UpperBody == 5` and reports it as NoCombat.
  [2.5-untested]
- **Quests**: `questGameplayRestrictions_NodeType { action
(AddRestriction, RemoveRestriction, RemoveAllRestrictions), source: CName,
restrictionIDs: array<TweakDBID> }`, `questCharacterManagerParameters_SetStatusEffect`,
  `questSetTier_NodeType`, `questEquipItemParams { unequipTypes }` (CW
  imports; the RTTI dump). Quest unequips go through
  `OnUnequipByContextRequest` (F:56330). The scene tier is the state
  machine's `SceneTier` parameter; `PlayerPuppet.GetSceneTier` reads
  `PlayerStateMachine.HighLevel` (:44340); tiers 2 to 5 are "safe"
  (`IsInSafeSceneTier`, :245890). `IsInLoreAnimationScene` (:53783) has no
  script reader; lore animations are the status effect tag `LoreAnim`.
  [2.5-untested]
- **Static zones are not status effects.** `SecuritySystemControllerPS`
  sets the player's security area type when it crosses a perimeter
  (:114622, :114668, `SetSecurityAreaTypeE3HACK` :46209); `SAFE` maps to
  `PublicZone`, not `SafeZone` (:46273), so security areas never holster.
  The no-weapons safe zone is the quest fact `CityAreaType` = 2 (:45927,
  :46262) → `OnEnterSafeZone`: `ForceEmptyHandsByZone` and
  `PlayerStateMachine.Zones` = 2 (:46231-:46240). Security areas are placed
  world devices (`SecurityArea` :502149, `SecurityAreaControllerPS`
  :117493, quest-driven transitions :117830-:117914), and the safe-zone
  fact is set by the world's own quest triggers: **a guest's world has
  them on its own** (its own phases set the fact; the quest freeze lets
  open-world phases run, `architecture/story.md`), so the host shares none
  of them. [2.5-untested: that a frozen guest's safe zones still apply]
- **Telling ours from the game's**: an invented instigator TweakDBID is
  only a hash and one instance keeps one instigator, so it proves nothing.
  Two ways: our own TweakXL clones of the records (only our instances are
  touched; tag checks still fire, record-ID checks such as the jump hint
  do not), or the vanilla records with a ledger: apply only when the V
  does not have the effect (else it is the guest's own), one stack, take
  one stack off; a later re-application by the guest's game shows as an
  `ApplyStatusEffectEvent` with `isNewApplication` false: one stack means
  it refreshed ours (it is the guest's now), two that it stacked (ours
  comes off, its stays). tandem uses the second (record IDs behave exactly
  as for the host). [2.5-untested]
- **The host's backstop**: `GameObject.IsQuest()` (`m_markAsQuest`,
  F:13517); `GodModeSystem.HasGodMode(id, gameGodModeType.Immortal |
Invulnerable)` (:87673, types :530; quests set it through
  `questCharacterManagerParameters_SetMortality { source }`);
  `GameInstance.GetSceneSystem(game).GetScriptInterface().IsEntityInScene(id)`
  (:193151). [2.5-verified offline]
- To check in game (scenarios `story-restrict-guest`, `story-restrict-host`):
  which records a real quest scene puts on the host's V (the host logs
  them); that NoCombat applied to a guest
  holsters it and blocks the wheel; that taking our stack off leaves a
  restriction the guest's own game holds; maxStacks of NoCombat and
  NoWeapons. [2.5-untested]

### 13.65 Interactions: where a choice goes, what marks a quest's object, and what casts the doer (2026-11-19, scripts, SDK and RTTI read)

For `docs/architecture/story.md`, "Quest interactions"
(`scripts/tandem/Interact.reds`, `plugin/src/engine/casts.cpp`). Nothing
here was run; F is `final.reds` (2.50).

- **A choice is an event on its object.** The interaction component sends
  `InteractionChoiceEvent { choice: InteractionChoice, actionType }`
  (F:209855; its base `InteractionBaseEvent { hotspot, activator,
layerData.tag }`, SDK `game/interactions/InteractionBaseEvent.hpp`) to
  the hotspot and its components. `InteractionChoice { caption,
captionParts, data: array<Variant>, choiceMetaData { tweakDBName, type } }`
  (F:107418): a device's choice carries its action in `data`. About forty
  script classes handle it, by kind of object, not by quest (F:22359
  `ScriptedPuppet`, :69278 `Inventory`, :104828 `InteractiveDevice`,
  :175135 `gameLootContainerBase`, :298060 `gameLootBag`…).
  [2.5-verified offline]
- **Devices: one funnel.** `InteractiveDevice.OnInteractionUsed` →
  `Device.ExecuteAction(choice, executor, layerTag)` (F:98567: returns on a
  `CheckFailed` choice, the skill or item check, then runs each
  `DeviceAction` of `choice.data` and stamps the layer) →
  `Device.ExecuteAction(action, executor) -> Bool` (F:98590:
  `RegisterAsRequester`, `SetExecutor`, `ProcessRPGAction`). A device's
  screen goes there too (`DeviceInkGameControllerBase.ExecuteAction` queues
  a `UIActionEvent`, F:211574; `Device.OnUIAction` calls the same
  `ExecuteAction`, F:99471). Both are `const final` script functions:
  `@wrapMethod` takes them (scripts:check, redscript 0.5.32). The device's
  own actions for a V: `ScriptableDeviceComponentPS.GetActions(out
actions, GenerateContext(requestType, Device.GetInteractionClearance(),
executor))` (F:30174, :32102, :98744). **`GetActionByName` is broken in
  2.50** (F:30076, :30091: an `if` where a `while` was meant, only the
  first action is compared): walk `GetActions` instead.
  `FinalizeGetActions(out actions)` (F:30290, `final`) is the one place a
  Direct action list passes before it becomes choices, and
  `SetInactiveWithReason(false, reason)` (F:28336) greys an action with its
  reason. [2.5-untested]
- **Loot is taken by the engine.** `Inventory.OnInteractionUsed` (F:69278)
  unwraps `LootChoiceActionWrapper { itemId, action }` (F:175357); when
  `IsHandledByCode` the engine itself moves the item and the script only
  plays its sound, so a script cannot stop a pick-up, only see it (before
  it calls the wrapped function the item is still in its source). A host's
  V takes an item the game's way with `TransactionSystem.TakeItem(newOwner,
itemObject)` (a world item: `gameItemDropObject.GetItemObject()`,
  F:294978) or `TransferItem(source, target, itemID, amount)` (F:52081,
  :52093). [2.5-untested]
- **The game's own mark of a quest's object.** `GameObject.IsQuest()`
  returns `m_markAsQuest` (F:13517), set by a quest through
  `SetAsQuestImportantEvent` (F:13521) and overridden by kind: a device's
  persistent state's `IsMarkedAsQuest()` (F:101483, :28690, saved with the
  device); an item's `Quest` tag (F:52918); a container marked or holding a
  quest item (F:175152); a puppet (F:22687), a vehicle (F:88972), a dropped
  item (F:295086), a loot bag (F:298190). This is the yellow highlight the
  scanner shows. A frozen guest's copies carry **its own** story's marks,
  not the host's: only the host's copy can say whether an object is its
  quest's. [2.5-verified offline for the code; that every objective object
  is marked: to see]
- **How quests see an object being used** (SDK): `questInteraction_ConditionType
{ objectRef, eventType: Entered | Exited | Executed }` (an interaction
  executed on that placed object) and `questDevice_ConditionType {
objectRef, deviceControllerClass, deviceConditionFunction,
functionParameters }` (a function of the device's persistent state, run
  by the quest); `questInventory_ConditionType` for items (§13.29). All are
  evaluated in the game whose quest waits, against that game's object and
  that game's V: only an interaction done in the host's game, on the host's
  copy, by the host's V meets them. [native: no script lever; SDK structs]
- **What casts the doer.** A device puts a V in a workspot from the
  entity's callback for the action's event, after its persistent state
  handled the action: `Device.EnterWorkspot(activator, freeCamera,
componentName, deviceData)` (F:99542, only for `activator.IsPlayer()`) →
  `WorkspotGameSystem.PlayInDeviceSimple` (native, F:53212); the personal
  link through `EnterWorkspotWithIK` and `InitiatePersonalLinkWorkspot` →
  `PlayInDevice(this, puppet, n"unlockedCamera",
n"personalLinkPlayerWorkspot")` (F:99110-:99160; the action is
  `TogglePersonalLink`, F:113751); a computer's engineering spot
  (`Terminal.OnActionEngineering`, F:254455), a turret taken apart
  (F:171757), a door forced by hand (F:133818), the data-driven device
  operations (`DeviceOperations.EnterWorkspot`, F:214837, from their
  `SWorkspotData`). Whether a V is in one: `WorkspotGameSystem.IsActorInWorkspot`
  (F:53275); in a lore animation: the `PlayerStateMachine` blackboard's
  `IsInLoreAnimationScene` (F:53783, no script reader); an entity in a
  scene: `SceneSystemInterface.IsEntityInScene` (F:193151). [2.5-verified
  offline]
- **Reading that from the RTTI.** A class's event callbacks are its
  `CClass::listeners` (+0x1B0, `{callbackName +0x18, eventTypeId +0x20}`),
  matched to the event class's `eventTypeId` (+0x2C0); a scripted
  function's compiled code is `CBaseFunction::bytecode` (a `CCompiledCode`
  at +0x78, its `DataBuffer` data and size at +0x08), `flags.isNative`
  (+0xA8) for natives (SDK `RTTITypes.hpp`, `Scripting/Functions.hpp`;
  generated for 2.x). In loaded bytecode a final or static call names its
  `CBaseFunction*`, a virtual call its short name (a 64-bit CName): an
  8-byte window equal to a target's address or name hash is a call to it.
  `engine/casts.cpp` follows those from the callbacks for an action (5
  levels, 400 functions) and looks for `PlayInDevice`, `PlayInDeviceSimple`,
  `EnterWorkspot`, `EnterWorkspotWithIK`. Its first use checks the shape
  (`WorkspotGameSystem.PlayInDevice` native, `Device.EnterWorkspot`
  scripted with bytecode) and answers "unknown" otherwise. [unverified: the
  in-memory operand layout of call opcodes and that script callbacks are in
  `listeners`; read `casts:` in the log]
- **Ids.** `EntityID.GetHash` is 32 bits in script (F:14696): the 64-bit
  static id of a placed entity travels through natives that take an
  `EntityID`. `EntityID.IsStatic` (F:14690) tells a placed entity (the same
  in every game) from a spawned one. `TDBID.ToNumber` (F:14797) gives an
  item record's 64 bits. [2.5-verified offline]
- To check in game (scenarios `interact-guest-pickup`,
  `interact-guest-hostonly`, `interact-apart-own-gig`): the log's `casts:`
  lines for an access point's `TogglePersonalLink` (casts), a door's
  `ToggleOpen` and a computer's screen actions (do not); that a quest
  objective's object is `IsQuest()` on the host; that the host's V doing a
  guest's action on a quest switch moves the quest; that a greyed prompt
  shows the reason. [2.5-untested]

### 13.153 A choice of our own on a puppet's interaction component (2026-11-22, read in final.reds (2.50); run on instance b)

For `docs/architecture/prompts.md` (`scripts/tandem/Prompts.reds`). F is
`final.reds (2.50)` (`tools/scripts-decompile.sh`).

- **The component.** `ScriptedPuppet` asks for a component named
  `Interaction` (`m_interactionComponent`, F:20342) and never requires
  it. `InteractionComponent` (F:66048): `SetSingleChoice(choice, opt
layer)`, `SetChoices`, `ResetChoices(opt layer, opt deactivate)`,
  `GetActiveInputLayers(out)`, `GetActivatorsForLayer(layer, out)` (each
  `gameinteractionsActiveLayerData { activator, linkedLayersName,
layerName }`, F:216602). A layer is switched by
  `InteractionSetEnableEvent { enable, layer, linkedLayers }` queued on
  the entity (`ScriptedPuppet.EnableInteraction`, F:22893). Layers are
  the component's data: none can be added from script.
  [2.5-verified offline]
- **An NPC's layers** (F:20776 `ToggleInteractionLayers`, once, attaching;
  F:22580): `GenericTalk` (on for a crowd NPC whose record says
  `CanHaveGenericTalk`, or `ForceCanHaveGenericTalk`; off under a `Busy`
  status effect, F:72373), `ReturnTalk`, `Loot`, `Grapple`,
  `TakedownLayer`, `AerialTakedown`, `BossTakedownLayer`,
  `MassiveTargetTakedownLayer`, `RemoteTakedownLayer`,
  `NewPerkFinisherLayer`, `OnePunchAttackLayer`, `QualityRange_*`.
  [2.5-verified offline]
- **What vanilla does on those layers.** Activation
  (`OnInteractionActivated`, F:22556, an `InteractionActivationEvent {
eventType: EIET_activate | EIET_deactivate }`): for the named layers,
  `ScriptedPuppetPS.DetermineInteractionState` (F:25238) builds choices
  from the record's `objectActions` and `PushChoicesToInteractionComponent`
  (F:25526) **resets the layer's choices first**; deactivation resets
  them too. A press (`OnInteractionUsed`, F:22615): the actions in
  `choice.data` (none in a choice of ours), a takedown **for any choice on
  `Grapple`, `TakedownLayer`, `BossTakedownLayer` or
  `MassiveTargetTakedownLayer`** (`ProcessSyncedAnimationPuppetActions`,
  F:22826), then `ResetChoices()` on every layer but `Loot`. A second
  handler of the same event, `ScriptedPuppet.OnInteraction` (F:22780),
  acts on the names `Loot` and `Revive` (the cut multiplayer's: a
  `ResurrectEvent` to a `PlayerPuppet` hotspot, nothing on an NPC).
  [2.5-verified offline]
- **A choice.** `InteractionChoice { caption: String, captionParts, data,
choiceMetaData { tweakDBName, tweakDBID, type } }` (F:118045). The key
  and the default caption come from `Interactions.<tweakDBName>`
  (`InteractionBase_Record`: `Name`, `Action`, `Caption`, F:19035); the
  cut multiplayer's pickup sets only `tweakDBName = "PickUp"` and reads
  `InteractionChoiceEvent.actionType == BUTTON_PRESSED` (F:515492).
  Vanilla puts a reason in `caption` to show it (`SetInactiveReasonAsCaption`,
  F:28013): that `caption` replaces the record's text is read from that,
  not seen. Records the scripts name: `Interactions.Open`, `On`, `Off`,
  `Arm`, `Disarm`, `None`, `NewPerkFinisherBluntHold`. No input action
  with `Hold` in its name appears in the scripts: a hold choice's action
  is to read from the game's `inputContexts.xml`. [2.5-untested]
- **What the UI is told.** The engine writes the prompt on screen into
  `UIInteractions.InteractionChoiceHub` (`InteractionChoiceHubData { id,
active, title, choices: [InteractionChoiceData { inputAction,
isHoldAction, localizedName, … }] }`, F:232377, F:395204):
  `Prompts.reds` reads it to know its choice shows.
  `InteractionManager.IsInteractionLookAtTarget(activator, hotspot)`
  (F:193622). [2.5-untested]
- **A lead.** `PlayerPuppet.OnIncapacitated` (F:49362) turns on a layer
  named `Revive` when V is dead, and `ScriptedPuppet.OnResurrected`
  (F:22158) turns `Grapple`, `TakedownLayer`, `AerialTakedown` and
  `NewPerkFinisherLayer` back on: a player template may carry an
  interaction component of its own with a `Revive` layer. [unverified]
- **Measured on a running game** (instance b, 2026-11-22, a stand-in
  body `Character.TandemStandInWa`, Panam's template): its descriptor
  links `Grapple`, `TakedownLayer`, `Loot` (+ `QualityRange_*`),
  `AerialTakedown`, `GenericTalk`, `ReturnTalk`, `BossTakedownLayer`,
  `MassiveTargetTakedownLayer` (each a `DeviceVisualizerDefinition`,
  `LookAt`, not dynamic), `NewPerkFinisherLayer`, `RemoteTakedownLayer`
  (`Proximity`); `GenericTalk` and `ReturnTalk` are off by default. Panam's
  record's `objectActions` start with `GenericInteraction.PickUpBody`,
  `.Talk` (UI `Interactions.GenericTalk`, caption `LocKey#312`, "Talk"),
  `.ReturnTalk`, then the takedowns. [2.5-verified]
  - The game's prompt is built from the choices a layer holds **as it
    activates** (vanilla's `OnInteractionActivated` pushes the record's
    choices then): a choice set from a later tick shows only in place of
    one already up, never brings a prompt up. Set in the same event (a
    wrap after vanilla's handler), it shows. [2.5-verified]
  - `SetSingleChoice(choice, layer)` never showed a choice; `SetChoices([choice],
layer)` and `InteractionSetChoicesEvent` do. [2.5-verified]
  - The prompt shows `GetLocalizedText(localizedName)`, and the hub's
    `localizedName` is the **record's caption** followed by the choice's
    `caption` (`'LocKey#273 Trade'` read "Open"). A record caption
    `"LocKey#Tandem-Trade-Prompt"` (a Codeware localization key of
    `Text.reds`) shows our text ("[F] Trade"). [2.5-verified]
  - `GenericTalk` deactivates ~1.2 s after it activates and activates again
    a second later, our V still and looking at the body (the crowd talk's
    own logic, not ours); `ReturnTalk` stays active. [2.5-verified]
- **A dump that crashed the game**: reading `HotSpotLayerDefinition`'s
  Codeware fields (`group`, `priorityMultiplier`, `areaFilterDefinition`,
  `gameLogicFilterDefinition` and the prereqs' class names) in a loop over
  a descriptor's layers: an access violation in the script VM
  (`Cyberpunk2077.exe+0x4753d00`). `tag`, `enabled` and the linked layers'
  `tag` and `visualizerDefinition` read safely. [2.5-verified]
- **Unknown until run**: which layers the `Interaction` component of V's
  own `player_*_tpp_cutscene.ent` has (`Prompts.reds` falls back to
  `GenericTalk` when it has no `ReturnTalk`). [unverified]

### 13.92 Contests: what the game's own data says of a fist fight, a race, a shooting competition (2026-11-20, read in final.reds, not run)

- **Fist fights** (Beat on the Brat and every other brawl): the quest puts
  the status effect `GameplayRestriction.FistFight` on V **and on each
  opponent**. Vanilla reads it on both: an NPC wearing it is aggressive
  whatever its preset (`ScriptedPuppet.IsAggressive`, `:21118`), is
  defeated, never killed, by a hit from or on a wearer
  (`NPCPuppet` `:128701`, `HitReactionComponent.CanDieCondition`
  `:185843-185847`), cannot be taken down by a wearer (`:514051`); a
  wearer's melee stimulus is ignored by bystanders (`:146182`); V's weapon
  wheel is fists only (`:239572`); devices read the tag `FistFight` on V
  (`:172198`, `:278038`). [2.5-untested]
- **Shooting competitions**: a status effect tagged
  `ShootingRangeCompetition` on V (`:246200`, `:542732`: the weapon states
  read it; the competition's scope is `Items.Lexington_Shooting_
Competition_scope`, `:71882`). [2.5-untested]
- **Races**: `GameInstance.GetRacingSystem(game).IsRaceInProgress()`
  (`RacingSystem`, `:189724`; also `IsAIVehicleRegistered(vehicle)`), read
  by the vehicle states (`:246236`, `:287898`); the race's HUD is a
  `VehicleRaceQuestEvent` on V's car (`vehicleRaceUI`: PreRaceSetup 0,
  CountdownStart 1, RaceStart 2, RaceEnd 3, Disable 4; `:94639`); the quest
  fact `sq024_current_race_checkpoint_fact_add` forwards checkpoints
  (`:94632`). [2.5-untested]
- **The race HUD, for co-op races** (2026-11-21, read in final.reds, not
  run): the car's `VehicleRaceQuestEvent` becomes a
  `ForwardVehicleRaceUIEvent` (mode, `maxPosition`, `maxCheckpoints`) queued
  to the UI system (`:97148`), heard by `hudCarRaceController`
  (`OnForwardVehicleRaceUIEvent`, `:415728`): every race's HUD, so its
  modes and counts are every race's. The checkpoint count it shows is that
  same `sq024_…_fact_add` fact (`:415807`, read as `fact/maxCheckpoints`);
  the place is the native's blackboard `UI_ActiveVehicleData.PositionInRace`
  (`:415830`, out of `maxPosition`: the race's NPCs and V), which the clock
  tick writes back as the fact `sq024_current_race_player_position`
  (`:415839`). The clock tick is the car's own (`ToggleRaceClock`,
  `:94649`), from the quest's RaceStart. So the vanilla HUD takes one place
  and one count, no extra racers; driving it on another game would write
  that game's `sq024` facts. The NPC racers are cars the racing system
  registered (`IsAIVehicleRegistered`), driven by `AIVehicleRacingCommand`
  along a spline (`:294569`). A car's engine can be held by the quest's own
  `VehicleQuestToggleEngineEvent` (`toggle`, `lockState` Lock 1 / Unlock 2;
  `:94690`). [2.5-untested]
- **A race's route and rewards, for joiners** (2026-11-21, read in final.reds
  and the game's `tools/redmod/tweaks`, not run): the racing system shows no
  checkpoint list to scripts (`RacingSystem` has only the two calls above;
  its native layout is opaque, `unk48[0x130-0x48]`); the AI racers follow a
  `worldRaceSplineNode` named by the quest (`questvehicleRacingParams.
splineRef`). Script-side, a quest's map pins are journal entries
  (`JournalQuestMapPinBase`, children of an objective), placed by
  `MappinSystem.GetQuestMappinPosition(pinHash)`, the quest menu's own read
  of an objective's pins (`:360688`); `GetQuestMappinPositionsByObjective`
  gives an objective's (`:358454`). Whether a race's objective holds a pin
  per checkpoint is not known. The checkpoint holograms are LCD screens
  (`RaceCheckpoint_Record`, `RaceCheckpointSelector`, `:324769`). The Beast
  in Me's races pay `QuestRewards.sq024_finish_<race>_race` (XP: 486, the
  big race 728) and `sq024_win_<race>_race` (eddies: the badlands 3860),
  `<race>` badlands, big, city, santo_domingo
  (`quest_rewards/side_quests.tweak`); mq056's races (racing map pins
  `Mappins.mq056_*_poi`, `mappins_racing.tweak`) have no reward record.
  [2.5-untested]
- So a contest is told with no list of quests: the restriction (or tag) on
  our V, or the racing system's word. Arcade machines are minigames, a
  cyberpsycho is a fight like any other, rides are workspots or vehicle
  scenes: none is a contest here.

### 13.96 Spectating: what makes a scripted moment, a V the NPCs do not see (2026-11-21, read in final.reds, not run)

- **A V the NPCs do not see**: `PlayerPuppet.SetInvisible(isInvisible)`
  toggles its `senseVisibleObject` component off (`VisibleObjectComponent`,
  `:43505`; `IsInvisible()` reads it back, `:43509`). Vanilla's own use:
  the grace period after a spawn turns it off together with
  `GodModeSystem.AddGodMode(..., Invulnerable, n"GracePeriodAfterSpawn")`
  (`GracePeriodAfterSpawn`, `:39489-39500`), and optical camo
  (`:44037`). Every `ScriptedPuppet` has the same component
  (`GetVisibleObjectComponent()`, `:20573`), so a puppet's can be toggled
  too. [2.5-untested]
- **Holding V still**: `GameplayRestriction.NoMovement` (`:43866`, applied
  by the locomotion states, `:283592`, `:520797`) and
  `GameplayRestriction.NoCameraControl` (`:12857`, `:154482`), the records
  the game's own scripted modes use. [2.5-untested]
- **What a scripted moment is made of, as a game can read it of its own
  V**: its scene tier (`PlayerPuppet.GetSceneTier`, §13.16), status effects
  tagged `GameplayRestriction` (§13.45: movement rules a quest puts on,
  apart from weapons-only zones, and body or camera holds: NoMovement,
  NoCameraControl, ForceCrouch, the `CameraAnimation` tag), a vehicle its
  scene drives (the player state machine's vehicle state Scene = 7, tier 3+
  or the `VehicleScene` tag: §13.27), and another world (§13.26). None
  names a quest: the list is the same for the base game and Phantom
  Liberty. [2.5-untested]
- **The audio listener is the player's, not the active camera's**: a
  camera takes it only when activated with
  `CameraComponent.Activate(blendTime, shouldOverrideAudioListeners =
true)` (`:106188`, `Deactivate` the same, `:106190`); vanilla's camera
  takeover does (`TakeControlOverCamera`, `:102040`, `:102051`), and a
  braindance moves it by `AudioSystem.SetBDCameraListenerOverride`
  (`:45364`, `:149636`). A camera of ours activated with `false` leaves the
  listener on our V. [2.5-untested]
- **A puppet's head in first person**: the game draws V's own first person
  from its FPP head (no face, hair or headgear; the head groups `FPP`,
  `FPP_hairs`, §5) and dresses a TPP puppet with `Items.PlayerMaTppHead` /
  `Items.PlayerWaTppHead` (`:347449`). On tandem's puppet the head is its
  own mesh components (§13.4: morph target skinned meshes), each hidden
  with `IVisualComponent.TemporaryHide` (§13.26): which components by name
  (the meshes' `h0_`, `hh_`, `he_`… prefixes) is unmeasured
  (`tools/game.sh cmd story spectate parts` lists them). [unverified]

### 13.99 One journal change moves others: the engine's rules (2026-11-21, decompiled, not run)

What `JournalManager.ChangeEntryStateByHash(hash, state, notify)`
(`0x142bde814`) does: it finds the entry (vtable `+0x220`) and calls the
manager's `ChangeEntryState` slot `+0x268` (`0x1406c1a48` on 2.50), which
dispatches on the new state (`gameJournalEntryState`: Undefined 0,
Inactive 1, Active 2, Succeeded 3, Failed 4). Read with
`tools/re/decompile`; the class tests are the RTTI class globals
`0x143bc89c8` `gameJournalRootFolderEntry`, `0x143bc8a28`
`gameJournalQuestDescription`, `0x143bc8ac8` `gameJournalPhoneChoiceGroup`,
`0x143bc8ac0` `gameJournalPhoneChoiceEntry`, `0x143bc8b18`
`gameJournalMetaQuestObjective` (named from their registration functions).

- **The setter of one entry** (`0x140654c0c`) changes nothing when the
  state is the same, and **refuses Failed on an Inactive entry**; an
  ancestor that is a quest description is never changed for a child. It
  then calls the journal listeners (script callbacks included) at once.
- **Inactive** (and Undefined): that entry only.
- **Active** (`0x1406543ec`): the entry and **every ancestor up to (not
  including) the root folder that is not Active, whatever its state**
  (a Succeeded quest goes back to Active), top down. Then
  (`0x140d42168`): a phone choice group's children not Active go Active;
  a meta quest objective's group (slot `+0x240`) others go Inactive.
- **Succeeded, Failed** (`0x140d4201c`): the entry, then every **Active**
  descendant, recursively, to the same state; a phone choice chosen
  (Succeeded) fails the other choices of its group.

So a journal cannot be put back by replaying each entry's old state once:
the order matters (an Active child reactivates its finished parent; a
finished parent ends its Active children), a Failed entry hidden as
Inactive cannot come back directly (Active first), and entries outside
the replayed list move too (a choice group's children, folders). What
tandem's mirror does about it: `docs/architecture/story.md`, "Saves".
Seen in game (2026-11-21, `story-gigs-apart`, before the fix): 176 own
gig entries given back in reverse order, 174 found again; 91 of 1150
entries not as before after the leave, 19 of 1318 the next session.
[2.5-untested]

### 13.111 Quest prefabs: how a phase loads an interior, and how another game can (2026-11-21, decompiled; the handler's place read live)

Read with `tools/re` on the 2.50 exe (`decompile`, `xref.py`, `asm.py`,
RTTI); one live read of a running 2.50 game confirmed the system's field.
Used by `plugin/src/engine/prefabs.cpp` (`docs/architecture/story.md`,
"Quest prefabs").

- **What a quest prefab is.** World sectors are tagged with the prefab
  they belong to (`worldStreamingSectorDescriptor.questPrefabNodeRef`,
  `worldNodeEditorData.questPrefabRefHash`); a phase lists the prefabs it
  needs (`questPhaseNodeDefinition.phaseInstancePrefabs`,
  `questQuestPhaseResource.phasePrefabs`: `questQuestPrefabEntry
{prefabNodeRef}`). The mode is `worldQuestPrefabLoadingMode` {Disable 0,
  ForceLoad 1}. [2.5-rtti]
- **The handler.** `questQuestsSystem` (0x300) creates a
  `questQuestPrefabsHandler` (0xF0) in its init (`0x140697564`) and holds
  it at **+0x100** (a handle; seen live: the object there is of that
  class). Its fields: +0x30 a `SharedSpinLock` (every phase change holds
  it); +0x38 the requests (id → prefabs, pending); +0x60 a sorted map
  GlobalNodeRef → count of phases asking (keys `DynArray<u64>` +0x60/+0x6C,
  counts +0x70); +0x88 the set not loaded yet (its listener's); **+0xB0
  `DynArray<{u64 ref, u8 mode}>`**, 16 bytes each (the layout of
  `questQuestPrefabsEntry`, what CDPR's cancelled multiplayer replicated as
  `questQuestsSystemReplicatedState.replicatedQuestPrefabs`): every prefab
  ever set, its mode now; +0xC8 the world streaming system;
  +0xD0 the next request id. [2.5-untested]
- **A phase's prefabs.** A phase instance's creation (`0x1402b13f0`)
  resolves its prefabs' NodeRefs and asks the handler (`0x140699f78`,
  request id kept at phase +0x84); each prefab's count goes up
  (`0x1406996c0`) and **0→1 calls `SetMode(handler, ref, ForceLoad)`**. The
  phase's end (`0x1402b1d30`) releases the request (`0x140698e24`): each
  count goes down (`0x140698f24`) and **1→0 calls `SetMode(ref,
Disable)`**. Both under the +0x30 lock. [2.5-untested]
- **`SetMode`** (hash **660547284**, `0x140699498`): looks the ref up in
  +0xB0; the same mode: nothing; else sets it (adds the pair when new) and
  queues `{ref, mode}` for the streaming system (`0x140699520`: under its
  SRW lock at +0x2E8, into its array at +0x2D8). No other effect, no
  check. The MP leftover that applied a replicated state (`0x142c9f548`)
  called it for every pair it was given: the engine's own "load another
  game's prefabs". [2.5-untested]
- **The streaming side** (`worldRuntimeSystemWorldStreaming`, 0x3E0; its
  update `0x14069b1fc` → `0x14069b450`): each queued pair finds the
  prefab's sectors (a map at +0x128 built from the sector descriptors); an
  unknown ref only tells the listeners "unknown" (state 2) and loads
  nothing; a new mode marks the sectors to stream in (ForceLoad) or out
  (Disable) and keeps the mode (+0x2A8). Listeners
  (`worldIQuestPrefabStateListener` vfunc +0xD8, `(listener, ref, state)`)
  hear 0 loaded, 1 not loaded, 2 unknown. The phone's holocall prefabs
  (`questPhonePrefabsListener`) and photo mode queue to the streaming
  system directly, not through the handler, so +0xB0 holds the quests'
  only. [2.5-untested]
- **Persistence.** Nothing above was seen being saved: a loaded save's
  phases start again and ask again. A guest never saves in a session, and
  its leave turns off what it turned on. [unverified]
- **What this makes possible.** The host reads +0xB0 (mode ForceLoad) under
  the lock once a second; a guest calls `SetMode` for the host's, under the
  same lock, as a phase does; off again only for those it turned on and no
  phase of its own counts (+0x60). One its own phase releases later goes
  Disable by the engine; the guest's next check puts it back while the
  host still has it. GlobalNodeRefs are hashes of the world data: the same
  in every game of the same version and DLC (Phantom Liberty is required).
  [2.5-untested]
- **What is inside.** Devices placed in a prefab's sectors have static ids
  (the same in every game) and follow the host's word like any placed
  device once streamed in (`devices.md`); its NPCs come from the host's
  quest spawners (`questSpawnManager` nodes the guest never runs), so the
  guest sees the host's as proxies; a population spawner set to spawn on
  start in such a sector would spawn the guest's own copy (a twin,
  `regions.md` §3). Not yet seen in game. [unverified]

### 13.112 The story's gates: the Watson lockdown and Dogtown's gate (2026-11-21, scripts and data read offline, not run)

- **Watson lockdown.** Act 1 (the data's `prologue` quests, q000 to q005)
  and Playing for Time (`act_01/q101_resurrection`) keep V in Watson; the
  journal names it ("Take part in more fights after the Watson lockdown is
  lifted", `mq025_psycho_brawl/psycho_brawl/barricade`). Vanilla reads
  `q005_done` as "past the prologue" (`RPGManager`'s minor activity XP,
  `final.reds:37349`) and `q101_done` (`HubMenuUtility.CreateMenuData`,
  `:332681`). tandem takes **`q101_done >= 1` as the lockdown lifted**.
  [inferred: when exactly the barricades go, to see in game]
- **What the barrier is.** Players report road barricades and blocked
  bridges (physical, placed world nodes) that a glitch can get past, not a
  kill zone; `gameWorldBoundarySystem` has no script functions and the
  boundary is per player (`coop-quests-world.md` §6.3). The game's archives
  were not read here (no unpacker): which nodes, and whether they are
  prefab variants or quest prefabs, is open. [unverified]
- **Dogtown's gate.** Fact **`ep1_side_content >= 1`**, the pause condition
  of `ep1\openworld\combat_zone_gate\combat_zone_gate.questphase` (NCO,
  §7, on 2.31); the gate's entities in the resource list
  (`ep1\openworld\combat_zone_gate\entities\dogtown_gate_border_scanner.ent`,
  `loc_q303_combat_zone_gate_*` meshes). [2.31-only]
- **Where a V is.** `PreventionSystem.GetCurrentDistrict()`
  (`final.reds:138920`): `District.IsDogTown()` (`:193970`), and the
  district record's `Type()` up its `ParentDistrict()` chain (`:51044`)
  for `gamedataDistrict.Watson` (128). [2.5-untested]

### 13.131 The campaign's quest ids, as the 2.50 scripts name them (2026-11-21, read in final.reds, not run)

The scripts name no quest resource, but their facts, records and sound
events carry the quests' ids (`final.reds` line numbers), enough to name
the audit's moments (`architecture/scripted-moments.md`). The phase and
scene resource paths behind them are read in game (`tools/game.sh cmd quest
trace on`, `cmd story moments`). [2.5-untested]

- **q000, the prologue**: the lifepaths (`q000_corpo_scrollbar_office`,
  `_mirror`, `_disconnect`, `_after_meeting`: the Corpo office's scroll
  bars, `:379891`) and the VR tutorial (`q000_vr_*`; its V is a replacer,
  `Character.q000_vr_replacer`, `:39349`, `:39778`, which Elsewhere.reds
  counts as "playing as someone else", §13.26).
- **q001, The Rescue and The Ripperdoc**: the morning in V's room
  (`q001_in_v_room`, `q001_get_dressed`, `q001_took_vroom_jacket`), T-Bug's
  hack before the mission (sound `q001_sc_00a_before_mission_tbug_hack`,
  `:166154`), Viktor's visit (`q001_ripperdoc_done`, read by the
  cyberware capacity tutorial, `:42352`).
- **q003, The Pickup**: Royce (`Character.q003_royce_boss`, `:285074`), the
  plant's weapon containers (`q003_*_container`).
- **q005, The Heist**: Johnny's chip (`q005_johnny_chip_acquired`,
  `:362336`), Saburo's dog tag, `q005_done`.
- **q101, Playing for Time**: `q101_done` opens Act 2 (§13.112).
- **q112, Play It Safe**: Oda's fight (`q112_oda_*`, his `Oda.Masked`
  effect, `:294473`).
- **Bosses by quest** (`:285067-285083`): `q110_animals_boss`,
  `q113_boss_smasher`, `q114_main_boss_sasquatch`, `q116_boss_smasher`;
  `q104_turret_broken` (`:384488`), `q108_active` (`:379493`),
  `q115_thruster_start` (a sound, `:556838`). Which quests these are is not
  in the scripts.
- **q201 to q204, the endings' epilogues**: the achievement art each
  `q20N_done` picks, `:388490`: q201 the Devil, q202 the Star, q203 the Sun,
  q204 Temperance.
- **Phantom Liberty, q301 to q307**: Songbird's Blackwall holocall
  (`q301_holocall_blackwall`, `:381148`), the Chimera (`q302_sc_02_chimera_*`
  rumble cases, `:43549`), server cores (`q303_server_core*`), Farida
  (`q304_04d_farida_done`), Cynosure's shutdown and MaxTac
  (`q305_*_shutdown_*`, `q305_maxtac`, `:24066`), and q307's hub locked but
  for the journal (`q307_hub_disabled_except_journal`, `:332662`: the
  endings' sequence).
- **Side quests**: a braindance (`sq030_braindance_active`, `:380271`), the
  street races (`sq024_*`).

### 13.132 Whether the game lets V fast travel now (2026-11-21, read in final.reds, not run)

What `revive.md`'s "No far respawn out of a locked quest area" reads
(`TandemRevive.TravelLock`). [2.5-untested]

- `FastTravelSystem.IsFastTravelEnabled()` (`:50501`): no entry in its
  persistent `m_fastTravelLocks` (`FastTravelSystemLock`: `lockReason`, a
  CName, and an optional `linkedStatusEffectID`). Locks come and go through
  `EnableFastTravelRequest` (`OnEnableFastTravelRequest`, `:50653`; the
  static `AddFastTravelLock` / `RemoveFastTravelLock`, `:50764`), which
  quests send with their own reasons. The scripts' own reasons pass with
  the moment: `InCombat` (`:242893`), `InVehicle` (`:90614`),
  `PreventionSystem` (removed at `:138840`), `DEBUG` (non-final builds).
- A `BlockFastTravel` status-effect tag on V keeps fast travel points from
  registering (`RegisterFastTravelPoint`, `:50507`); the replacers (Johnny,
  the VR tutorial, mq304's assassin, Kurt) apply
  `GameplayRestriction.BlockFastTravel` (`:39769` on).
- `IsFastTravelEnabledOnMap()` (`:50394`) is **not** that test: its
  `m_isFastTravelEnabledOnMap` is set by `FastTravelMenuToggledEvent` from
  `OnEnterFastTravelMenu` / `OnExitFastTravelMenu` (`:50131`): true only
  while the fast travel menu is open at a terminal.

### 13.160 The quest's own fade, and the game's choice widget (2026-11-27, read in the 2.50 exe and final.reds; not run)

For `docs/architecture/parity.md` rows 1 and 3. F is `final.reds (2.50)`.

- **The quest fade** is a node of the render fx manager:
  `questSetFadeInOut_NodeType` (parent `questIRenderFxManagerNodeType`,
  size 0x48; `fadeColor: Color` at 56, `duration: Float` at 60, `fadeIn:
Bool` at 64; vtable 0x143179a18), held by a
  `questRenderFxManagerNodeDefinition` (`type` handle at 72). Its vtable
  holds only the type getter and the destructor (0x14120ce68); the only
  code referencing its class is its RTTI registration (0x1416a0c60,
  0x142410bd0). What applies the fade is the definition's runtime node
  instance, not in RTTI: still to find (from the definition's
  instance-creating slot), before a guest can fade the game's way
  instead of `Curtain.reds`. [2.5-verified offline]
- **The choice widget** (`dialogWidgetGameController`, F:405469) draws
  `m_data`, its copy of `UIInteractions.DialogChoiceHubs`, and ends each
  redraw (`OnInteractionsChanged`) in `m_root.SetVisible(m_AreDialogsOpen)`,
  set by `InteractionUIBase.OnDialogsData` (F:277754) from the board. A
  hub put in `m_data` by script while the board holds none is drawn and
  then hidden by the same redraw. An option's tag is the "[TAG] text"
  prefix of its `localizedName`, split by
  `DialogHubLogicController.UpdateDialogHubData` (F:405765), unless its
  caption parts carry tags (`GetCaptionTagsFromArray`, F:400197).
  [2.5-verified offline]
- **Input**: `ChoiceScrollUp`, `ChoiceScrollDown` and `ChoiceApply` are
  in the `InteractionActions` context, which `UIExploration` includes
  (`r6/config/inputContexts.xml`); `Choice1_Hold` is a 0.3 s hold of
  `Choice1` (`<hold action="Choice1_Hold" timeout="0.3" />`).
  [2.5-verified offline]

### 13.164 A scene's camera is a baked animation; the cut multiplayer ran scenes on every peer (2026-11-28, live RTTI of 2.50; not run)

For `docs/architecture/parity.md` row 1 (spectating): can a guest run a
scene of its own whose camera plays the host's camera track?

- **A scene's camera is a recorded animation, not a stream.** Its camera
  actor plays `scnCameraRid` (`animations: array:scnCameraAnimationRid`,
  each a `handle:animIAnimationBuffer` and `scnCameraAnimationLOD`, a
  trajectory and 3×7 float tracks, baked) through `scnPlayRidAnimEvent`
  (`animResRefId: scnRidAnimationSRRefId` into the scene's `.rid`
  resource, `animOriginMarker`, `actorPlacement`); the FPP events
  (`scnPlayFPPControlAnimEvent`) only blend, limit or smooth the player's
  own camera (`cameraBlendInDuration`, `yawLimitLeft`…,
  `enableWorldSpaceSmoothing`). No scene event or node takes a camera
  transform at run time, and a scene resource of ours cannot hold the
  host's track: it is known only as it plays. So "a scene of the guest's
  own playing the host's camera track" has no lever: the guest's watch
  camera (a camera entity placed every frame from `scene::CameraTrack`)
  is the only way to show a track that is streamed. [2.5-verified]
  (RTTI)
- **What the engine was built for instead**: every peer runs the same
  scene. `scnPlayerActorDef` acquires its performer by
  `findNetworkPlayerParams: scnFindNetworkPlayerParams` (`networkId`), so
  a scene can cast a network player; `scnISceneSystem` still exports
  `GetPeerIdDialogChoiceFactName` (dialogue choices per peer); and the
  quest system replicated its state (`questQuestsSystemReplicatedState`,
  `replicatedQuestPrefabs`). The cut multiplayer's path to a shared
  cutscene is each game playing the scene from replicated quest state,
  with its baked camera, the players cast by network id: the camera then
  needs no stream at all. [2.5-verified] (RTTI; never called)
- **Why it is not built**: that path runs the host's scene, and so its
  quest outputs, on the guest. Its other actors are found in the quest's
  context (`findActorInContextParams`), in the world
  (`findActorInWorldParams`, community entries) or spawned by the scene
  (`spawnDespawnParams`); on a guest those are its own frozen quest's
  (story sync) or the host's proxies, not the cast, so a scene started
  there finds nobody or spawns a second cast. Making it work means binding
  each actor to the host's proxy and suppressing every fact, reward and
  choice the guest's copy would emit: a scene engine of our own around
  the game's. Until that is designed (parity.md row 1, "Next"), the watch
  camera stays, registered as what it is. [unverified]
