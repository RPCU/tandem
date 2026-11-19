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
  `scripts/Base/Addons/StatusEffect.reds`). The array must be indexed
  (`for x in` over it is refused: the element type is a class without
  `ref`). Quests name a source when they add restrictions (below), so this
  is the one place that tells a quest's from another's. [2.5-untested]
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
  which records a real quest scene puts on the host's V and with which
  `sourcesData` (the host logs both); that NoCombat applied to a guest
  holsters it and blocks the wheel; that taking our stack off leaves a
  restriction the guest's own game holds; maxStacks of NoCombat and
  NoWeapons. [2.5-untested]
