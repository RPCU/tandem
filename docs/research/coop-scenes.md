# Scenes, dialogues, braindances and other story moments in co-op

What a future implementer needs so that cutscenes, staged scenes, dialogue
choices, braindances and similar moments do not look wrong to the other
players. It extends [`engine-knowledge.md`](engine-knowledge.md) §7 (what the
campaign fork and NCO did) with what the game itself exposes.

**Sources**

- `final.reds:LINE` — decompiled vanilla scripts of 2.50
  (`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`).
- **RTTI** — the live dump of game 2.50 (`tools/re/run tools/re/rtti.py`).
  `@ 0x14…` is a native function's handler address in the 2.50 exe; `+0x..`
  is a property offset. Both are for **2.50 only**.

**Caveat.** Everything here is read from scripts and RTTI. **Nothing was
tested in co-op or even in game.** A name being present does not prove the
feature behind it still works (see the CDPR multiplayer leftovers in §3.4).
Statements marked _(inferred)_ are deductions, not something read in a
source. Co-op rules are **proposals**, consistent with `ARCHITECTURE.md`:
the host's game runs the story, guests follow.

## Contents

1. [Scene tiers](#1-scene-tiers)
2. [The scene system](#2-the-scene-system)
3. [Dialogue choices](#3-dialogue-choices)
4. [Subtitles, voice, fades, videos](#4-subtitles-voice-fades-videos)
5. [Braindance](#5-braindance)
6. [Other "elsewhere" states](#6-other-elsewhere-states)
7. [Locks and restrictions](#7-locks-and-restrictions)
8. [Summary table](#8-summary-table)
9. [Open questions](#9-open-questions-need-an-in-game-test)

---

## 1. Scene tiers

### 1.1 The enums

| `GameplayTier` (`final.reds:3104`) | value | `gamePSMHighLevel` (`:6684`) |
| ---------------------------------- | ----- | ---------------------------- |
| `Undefined`                        | 0     | `Default`                    |
| `Tier1_FullGameplay`               | 1     | `SceneTier1`                 |
| `Tier2_StagedGameplay`             | 2     | `SceneTier2`                 |
| `Tier3_LimitedGameplay`            | 3     | `SceneTier3`                 |
| `Tier4_FPPCinematic`               | 4     | `SceneTier4`                 |
| `Tier5_Cinematic`                  | 5     | `SceneTier5`                 |
| —                                  | 6     | `Swimming`                   |

RTTI adds `GameplayTier.Any = -1` (used by conditions) and a twin enum
`audioGameplayTier` with the same 0–5 values. `Tier2WalkType` =
`Undefined 0, Slow 1, Normal 2, Fast 3` (`:3113`, RTTI).

NPCs have their own, coarser notion: `gameStoryTier { Gameplay = 0,
Cinematic = 1 }`, read with `AICAgent.GetStoryTier()` (@ 0x141648d90) and
announced by `gameStoryTierChangedEvent { newTier @+0x50 }` (an `AIAIEvent`).
`gamedataStatusEffect_Record.RemoveOnStoryTier()` and the AI task
`RemoveStatusEffectsOnStoryTier` strip status effects from an NPC entering a
cinematic.

### 1.2 Who sets the tier

- **Quest/scene graphs**, natively. Node types (RTTI, all
  `questISceneManagerNodeType` inside a `questSceneManagerNodeDefinition`):

  <!-- prettier-ignore -->
  | Node | Fields |
  | --- | --- |
  | `questSetTier_NodeType` | `tier`, `usePlayerWorkspot`, `useEnterAnim`, `useExitAnim`, `forceEmptyHands`, `motionConstrainedTierDataParams` |
  | `questSetTier2Params_NodeType` | `playerWalkType`, `usePlayerWorkspot`, enter/exit anim flags |
  | `questSetTier3Params_NodeType` | yaw/pitch limits and speed multipliers, `objectRef`, `slotName`, `offsetPos`, `rotationTime`, `rotateHeadOnly`, workspot flags |
  | `questSetTier4Params_NodeType` | `objectRef: NodeRef`, `adjustTime`, workspot flags |
  | `questUseWorkspotPlayerParams` | `tier: questUseWorkspotTier {Tier3 0, Tier4 1}`, `cameraSettings: gameTier3CameraSettings`, `emptyHands`, … — a quest "use workspot" on the player also raises the tier |

- The native **`gameTierSystem`** (RTTI, size 0xf0, **no script functions**)
  holds the state. It is saved: `gameTierSaveData.globalTiers:
array<gameGlobalTierSaveData { subtype: gameGlobalTierSubtype {Quest 0,
Supervisor 1}, data: handle<gameSceneTierData> }>`. So there can be two
  requesters (quest, "supervisor") and the tier survives a save/load.
- `gamePlayerTierComponent` exists on the player (RTTI, size 0x90) but
  exposes **no properties and no functions**: nothing to call there.
- Conditions graphs use: `questSceneTier_ConditionType { tier, isInverted }`,
  prereq `gameTierPrereq { tier }`.

### 1.3 How the tier reaches scripts

1. Native code puts a `SceneTier` object (RTTI `gameSceneTier :
senseStimuliData`) in the player state machine as the **permanent
   scriptable parameter `n"SceneTier"`** (`final.reds:245829-245843`).
   `SceneTier.GetTier()`, `GetTierData() -> SceneTierData`,
   `GetForceEmptyHands()`, `SetTierData()` (`:247719`).
2. `SceneTierData` (`:248000`; RTTI `gameSceneTierData`): `tier @+0x40`,
   `emptyHands @+0x44`, `userDebugInfo: String @+0x48` (RTTI only — may name
   the scene or node that asked, _inferred_; worth reading in game).
   Subclasses: `SceneTier1Data`; `SceneTier2Data { walkType }`;
   `SceneTierDataMotionConstrained { params: MotionConstrainedTierDataParams
{ splineRef, adjustingSpeed, adjustingDuration, travellingSpeed,
travellingDuration, notificationBackwardIndex } }` → `SceneTier3Data {
cameraSettings: Tier3CameraSettings }`, `SceneTier4Data`, `SceneTier5Data`
   (`:314864`, `:315021-315054`).
3. The `HighLevel` state machine reacts: `SceneTierInitialDecisions` enters
   when the requested tier is > 1 (`:529676`); `SceneTierII…V` states
   (`:530094`, `:532338-532494`) each write
   **`PlayerStateMachine.HighLevel`** = 2…5 and start an on-demand locomotion
   state machine (`LocomotionTier3/4/5`, `:283680-283699`). Exploration
   writes 1 (`:283772`), swimming 6 (`:527266`).
4. A second blackboard field, **`PlayerStateMachine.SceneTier`**
   (`:53707`), is **never written by scripts** — only listened to (HUD,
   `PlayerPuppet`, vehicle state machine). It is set natively _(inferred
   from the absence of any script writer)_.

### 1.4 Reading it and being notified

<!-- prettier-ignore -->
| How | Notes |
| --- | --- |
| `PlayerPuppet.GetSceneTier(player) -> Int32` (static, `:44340`) | reads `HighLevel`; 0 if no player |
| `player.GetPlayerStateMachineBlackboard().GetInt(GetAllBlackboardDefs().PlayerStateMachine.HighLevel)` | 1–6 |
| `RegisterListenerInt(PlayerStateMachine.SceneTier, obj, n"OnSceneTierChange")` | what `PlayerPuppet` does (`:40009`), callback `OnSceneTierChange(Int32)` (`:43746`) |
| wrap `PlayerPuppet.OnSceneTierChange(newState: Int32)` | what the campaign fork did (§7 of engine-knowledge) |
| blackboard getter from the plugin | the PSM blackboard is _local instanced_ per player entity (`:278077`) |
| `PlayerStateMachine.SceneAimForced`, `SceneSafeForced`, `SceneWeaponLoweringSpeedOverride`, `ScenePreventFirstEquip` (`:53777-53780`) | scene weapon overrides |
| `PlayerStateMachine.IsInLoreAnimationScene`, `IsInWorkspot`, `Vehicle == 7 (Scene)`, `IsPlayerInsideElevator`, `Takedown`, `Vitals` (§6) | related flags |

Two fields, two meanings: `HighLevel` is what the script state machine has
entered; `SceneTier` is what native code requested. They can differ for a
frame or more (the state machine re-syncs `HighLevel` in `OnUpdate`,
`:529866-529875`) and while swimming (`HighLevel` 6).

### 1.5 What each tier restricts (from scripts)

<!-- prettier-ignore -->
| Tier | Movement | Weapons | Camera | UI / other |
| --- | --- | --- | --- | --- |
| 1 | free | free | free | everything |
| 2 | walk only, speed capped by `walkType` through status effects `GameplayRestriction.Tier2Locomotion[Slow\|Fast]` and a `MaxSpeed` stat modifier (`:283640`, `:530120`) | `ForceSafeState`; `emptyHands` from the tier data → `ForceEmptyHands` (`:529835`); "safe tier" = `HighLevel` 2–5 (`:245890`) | free (special `SpaceShuttleInterior` params, `:247051`) | quick-slot wheels disabled from `SceneTier >= 2` (`:527993`); pocket radio blocked (`:43750`); stamina bar hidden unless needed (`:322872`) |
| 3 | none: `LocomotionTier3`, usually in a workspot | as tier 2 | limited: `SetCameraParamsWithOverridesEvent`, `paramsName n"Tier3Scene"`, yaw/pitch limits from `Tier3CameraSettings` (`:247063`) | inventory/hub blocked for `HighLevel` 3–5 (`:278075`); phone blocked 3–5 (`:241033`); vehicle state machine enters `Scene` (`:551688`); oxygen bar hidden (`:322357`) |
| 4 | none: `LocomotionTier4` (animation-driven, first person) | as tier 2 | `SetCameraParamsEvent n"Tier4Scene"` (`:247003`): the scene animates the head, small look-around | as tier 3; stamina bar always hidden (`:322900`) |
| 5 | none: `LocomotionTier5` | as tier 2 | scene camera, not V's head | as tier 4; fast-forward unavailable (`:519821`, `:519880`); breathing effects removed (`:532487`) |

Other tier consumers: crosshair hidden outside tier 1 (`:419816`), mappins
have `VisibleInTier[]` / `ShowInTier3` / `NameplateVisibleInTier[]` flags in
TweakDB (RTTI `gamedataMappinUIRuntimeProfile_Record`), save lock reasons
`gameSaveLockReason.Scene = 2` and `Tier = 10` (`:3658`), police dispatch
aborts with `Abort_SystemLockedBySceneTier` (`:140647`).

The scene can also drive V's weapon: events `SceneForceWeaponAim`,
`SceneForceWeaponSafe { weaponLoweringSpeedOverride }`,
`SceneFirstEquipState` on the player (`:44668`, `:44708`, `:250023`,
`:514299`) — only honoured when `HighLevel` is 2–5.

### 1.6 Co-op notes

- **Detect** (host): listen to `PlayerStateMachine.SceneTier` and
  `HighLevel`; send both, plus `emptyHands`, the tier-2 walk type and, for
  3–5, V's position. Cheap, reliable channel, only on change.
- **Sync**: `{ player, requestedTier, highLevel, emptyHands, walkType }`.
  It is state of _a V_, owned by that player's game (authority table, row
  "a V: transform, stance…"). On the host it doubles as the story signal.
- **Guest**: a guest's game runs no quest, so its own tier stays 1 unless we
  restrict it. There is **no script or RTTI setter** for a player's tier;
  the practical lever is §7's status effects (`NoMovement`, `NoCombat`,
  `NoWeapons`…), which is also what the campaign fork used.
- **Proposed rule**

  <!-- prettier-ignore -->
  | Host tier | Guests |
  | --- | --- |
  | 1 | nothing |
  | 2 | free; weapons lowered near the scene (`ForceSafeState`-like), subtitles and choices mirrored; no tether |
  | 3 | free to move; `NoCombat` near the host; host's puppet shown in its workspot pose; choices mirrored |
  | 4 | brought next to the host if far; `NoCombat` + invulnerable; kept out of the actors' way; choices, subtitles, fades mirrored |
  | 5 | spectator: guest camera follows the host's active camera (§2.4), own V hidden or parked, input reduced to "skip vote"; fades and videos mirrored |

---

## 2. The scene system

### 2.1 What scripts can ask

`GameInstance.GetSceneSystem(game) -> SceneSystem` (`final.reds:14216`; RTTI
`scnSceneSystem : scnISceneSystem`, size 0x10d0). Two natives on the system
(`:193089`):

- `GetScriptInterface() -> SceneSystemInterface` (@ 0x140c10014)
- `GetPeerIdDialogChoiceFactName() -> String` (@ 0x142a339f0) — see §3.4

`SceneSystemInterface` (`:193095`; RTTI `scnScriptInterface`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `IsEntityInScene(entity: EntityID) -> Bool` | 0x140378688 |
| `IsEntityInDialogue(entity: EntityID) -> Bool` | 0x1403772b4 |
| `AreEntitiesInDialogue(first: EntityID, second: EntityID) -> Bool` | 0x142cde53c |
| `IsFastForwardingAllowed(ffMode: scnFastForwardMode) -> Bool` | 0x140bf7a8c |
| `IsFastForwardingActive(ffMode) -> Bool` | 0x140dbd7c8 |
| `FastForwardingActivate(ffMode)` / `FastForwardingDeactivate()` | 0x1413557f8 / 0x141355864 |
| `SetSceneSystemCameraControlEnabled(enabled: Bool, opt blendTime: Float)` | 0x14184281c |
| `GetSceneSystemCameraControlEnabled() -> Bool` | 0x14134f9ec |
| `GetSceneSystemCameraLastCameraPosition() -> WorldPosition` | 0x141906ac4 |
| `GetSceneSystemCameraLastCameraOrientation() -> Quaternion` | 0x141916fec |
| rewindable-section functions | see §5 |

`scnFastForwardMode { Default 0, GameplayReview 1 }`.

**There is no script or RTTI function that lists running scenes, names the
current scene, or lists its actors.** What exists:

- per entity: `IsEntityInScene` / `IsEntityInDialogue` (used by NPC
  reactions, e.g. `:146292`, `:146798`); prereq state
  `NPCInScenePrereqState.OnSceneInspectionStateChanged(isEntityInScene)`;
- `gamestateMachineGameScriptInterface.IsSceneAnimationActive() -> Bool`
  (@ 0x140d5e320) for the player;
- `vehicleBaseObject.IsPerformingSceneAnimation()` (@ 0x14191e24c) and
  `IsInQuestOrScenePhysicsState()` (@ 0x1421bc800) for vehicles; events
  `gameQuestOrSceneSetVehiclePhysicsActive`, `vehicleCameraSceneEnableEvent`;
- `InteractionManager.AreSceneInteractionsBlocked() -> Bool`
  (@ 0x142a41d94), `SetBlockAllInteractions(Bool)` (@ 0x142a41f7c)
  (`:199584`).

So "which scene" must come from the quest side: the host's
`QuestPhaseInstance::ExecuteNode` hook (engine-knowledge §7) sees
`questSceneNodeDefinition { sceneFile: raRef<scnSceneResource> @+0x48,
sceneLocation: scnWorldMarker { type {Tag 0, NodeRef 1}, tag, nodeRef }
@+0x50, interruptionOperations, syncToMusic, notAllowedToBeFrozen }`. A
scene instance is identified natively by `scnSceneInstanceId { sceneId:
scnSceneId { resPathHash: Uint64 }, ownerId { hash }, internalId: Uint8,
hash }` (RTTI) — the resource path hash is the stable name of a scene across
games.

### 2.2 What a scene resource declares (RTTI `scnSceneResource`)

`entryPoints`, `exitPoints`, `actors: array<scnActorDef>`, `playerActors:
array<scnPlayerActorDef>`, `props: array<scnPropDef>`, `workspots`,
`workspotInstances: array<scnWorkspotInstance { dataId, playAtActorLocation,
localTransform, originMarker, workspotInstanceId }>`, `voInfo`,
`effectDefinitions`, `interruptionScenarios`, `sceneGraph`, `screenplayStore`
(lines and choice texts as loc ids), `sceneCategoryTag`.

- `scnSceneCategoryTag`: `voiceset 0, mainQuests 1, sideQuests 2,
minorQuests 3, otherQuests 4, dialoguesQuests 5, streetOpenWorld 6,
vendorsOpenWorld 7, dancefloorsOpenWorld 8, cityOpenWorld 9,
chatsOpenWorld 10, otherOpenWorld 11, holocalls 12, other 13` — lets a
  filter separate story scenes from ambient chatter.
- **How a scene gets its actors**: `scnEntityAcquisitionPlan` =
  `findInContext 1, findInWorld 2, spawnDespawn 3, findInEntity 4,
spawnSet 5, community 6, spawner 7, findInNode 8, findNetworkPlayer 9`.
  `scnActorDef` carries `actorId`, `voicetagId`, the plan and its
  parameters (`findActorInWorldParams`, `spawnDespawnParams {
dynamicEntityUniqueName, spawnMarker…, specRecordId, appearance,
spawnOnStart, alwaysSpawned, keepAlive, findInWorld … }`, community,
  spawner), anim sets, `actorName`, `specCharacterRecordId`,
  `specAppearance`. Props: same idea (`scnPropDef`).
  Consequence for identity (`ARCHITECTURE.md`): actors found in the world or
  in a community are placed entities (same hash everywhere); actors with
  `spawnDespawn` are **spawned by the scene** and need a `NetId` from the
  host _(inferred)_.
- `scnPlayerActorDef` has `specTemplate`, `specCharacterRecordId`,
  `playerName` and — notably — `acquisitionPlan` plus
  `findNetworkPlayerParams { networkId: Uint32 }`: scenes were designed to
  be able to cast a _specific network player_ (§3.4).

### 2.3 Scene timeline events worth knowing (RTTI, `scnSceneEvent` subclasses)

`scnSceneEvent { id, type, startTime: Uint32, duration: Uint32 }`. Seen:
`scnDialogLineEvent { screenplayLineId, voParams, visualStyle }`,
`scnPlaySkAnimEvent { animName, rootMotionData, playerData }`,
`scnChangeIdleAnimEvent`, `scnLookAtEvent`, `scneventsPlayerLookAtEvent`,
`scneventsCameraEvent`, `scneventsCameraPlacementEvent`,
`scneventsCameraParamsEvent`, `scneventsPlayRidCameraAnimEvent`,
`scnAudioEvent { performer, audioEventName, emitterName }`,
`scnPlayVideoEvent { videoPath, isPhoneCall }`. Graph nodes: `scnSectionNode
{ events, sectionDuration, actorBehaviors }`, `scnRewindableSectionNode`
(§5), `scnChoiceNode` (§3), `scnCutControlNode`, `scnInterruptManagerNode`.
These are resource data, not runtime objects a script can reach.

### 2.4 Camera

- V's head camera is `FPPCameraComponent` (`final.reds:174901`; RTTI
  `gameFPPCameraComponent`): `pitchMin/Max @+0x3e0`, `yawMaxLeft/Right
@+0x3e8`, `headingLocked @+0x3f6`, `sensitivityMultX/Y @+0x498`;
  `ResetPitch()` (@ 0x1405badcc), `ResetCurrentGameplayInput()`
  (@ 0x142182220), `SceneDisableBlendingToStaticPosition()` (@ 0x141953c00).
  Any `CameraComponent` has `Activate(opt blendTime, opt
shouldOverrideAudioListeners)` / `Deactivate(…)` (`:106180`).
- Tier 3/4 keep V's FPP camera and constrain it with
  `SetCameraParamsEvent { paramsName }` /
  `SetCameraParamsWithOverridesEvent` queued on the player (`:247003`,
  `:247063`); tier 5 uses a scene camera (`scneventsCamera*`).
- "Scene system camera control": `SetSceneSystemCameraControlEnabled(true)`
  gives the camera to the scene; `false` frees it (braindance editor, §5).
  `GetSceneSystemCameraLastCameraPosition/Orientation` return where the
  scene camera last was.
- **What the screen shows right now**, whatever owns it:
  `GameInstance.GetCameraSystem(game)` (RTTI `gameCameraSystem`):
  `GetActiveCameraWorldTransform(out transform: Transform) -> Bool`
  (@ 0x141212054), `GetActiveCameraFOV() -> Float` (@ 0x1418c6220),
  `GetActiveCameraData() -> entCameraData`,
  `GetActiveCameraForward/Right/Up`, `PrepareBlendCamera()` /
  `AbandonBlendCamera()` (also quest node `questPrepareBlendCamera_NodeType`).
  This is the host-side sampler for a spectator camera.
- V's third-person body: quest node
  `questEntityManagerEnablePlayerTPPRepresentation_NodeType { enable }`
  (cutscenes that show V from outside switch this on); see
  `third-person-overhaul.md`.
- Quest-driven looks: `questPlayerLookAt_NodeType { objectRef, slotName,
offsetPos, duration, adjustPitch/Yaw, endOn… }`,
  `questEnablePlayerGameplayLookAt_NodeType { enable }`,
  `questCameraClippingPlane_NodeType { preset }`.
- Free camera: `PlayerSystem.IsInFreeCamera()`, `SetFreeCameraTransform(
newTransform: Transform)` (`:49995`; @ 0x14279090c, @ 0x142e68a4c) — a
  candidate for the guest's spectator camera _(inferred; unverified in a
  retail build)_.

### 2.5 Workspots (sitting, leaning, using, synced animations)

`GameInstance.GetWorkspotSystem(game) -> WorkspotGameSystem` (`final.reds:53211`;
RTTI `gameWorkspotGameSystem`). Useful subset:

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `IsActorInWorkspot(actor: GameObject) -> Bool` | 0x1409ef4ac |
| `GetExtendedInfo(actor) -> ExtendedWorkspotInfo { isActive, entering, exiting, playingSyncAnim, inReaction, inMotion }` | 0x142a850a0 |
| `IsWorkspotEnabled(actor) -> Bool` | 0x140ac6fa4 |
| `GetDeviceUser(deviceID: EntityID) -> GameObject` | 0x142a84f44 |
| `PlayInDevice(device, actor, opt flavour, opt actorDataCompName, opt deviceDataCompName, opt syncSlotName, opt slideTime, opt slideBehaviour, opt callbackEventRecipient)` | (RTTI) |
| `PlayInDeviceSimple(device, actor, allowCameraMov, …)`, `StopInDevice(actor, opt posDelta, opt orientDelta)` | 0x142a869ac (Stop) |
| `PlayNpcInWorkspot(npc, master, referenceEntity, actorDataCompName, syncSlotName, slideTime, syncOffsetSourceCompName)` | 0x142a85978 |
| `StopNpcInWorkspot(actor)` | 0x14191a774 |
| `MountToVehicle(parent, child, slideTime, animDelay, workspotResourceContainer, slotName, opt syncronizedObjects, opt entrySlotName, opt animVariables) -> Bool` | (RTTI) |
| `UnmountFromVehicle(parent, child, opt instant, …)`, `SwitchSeatVehicle(…)`, `IsInVehicleWorkspot(vehicle, child, slotName)` | 0x1413a3984, –, 0x142a85378 |
| `SendJumpToAnimEnt(actor, animName, instant)`, `SendJumpToTagCommandEnt(actor, entryTag, instant, opt deviceId)`, `SendJumpCommandEnt(actor, entryId, instant)` | 0x142a864cc, 0x142a8662c, 0x142a86370 |
| `SendFastExitSignal(actor, …)`, `SendSlowExitSignal(actor, animationName, dontWait…)` | 0x142a860d8, 0x142a8685c |
| `SendDynamicSyncBindSignal(actor, master)`, `SendDynamicSyncUnbindSignal(actor)` | 0x142a84d84, 0x142a84eb4 |
| `ResetPlaybackToStart(actor)`, `HardResetPlaybackToStart(actor)` | 0x14157d3a4, 0x142a852e8 |
| `GetWorkspotInstance(actorID) -> workWorkspotInstance { tree, resource, animName @+0x110, idleAnim @+0x118, entryId @+0x120 }` | registered without a handler in the dump |

Events on the actor: `WorkspotStartedEvent { nodeId: GlobalNodeID, tags:
[CName] }`, `WorkspotFinishedEvent { nodeId, tags }` (`:250074`, `:250282`;
RTTI `workWorkspotStartedEvent`/`…FinishedEvent`). The player keeps the
tags: `PlayerPuppet.GetPlayerCurrentWorkspotTags()`,
`PlayerContainsWorkspotTag(tag)` (`:44865`); tags seen in scripts:
`FinisherWorkspot`, `DisableCameraControl`, `Grab`. Blackboard
`PlayerStateMachine.IsInWorkspot` (Int, 0/1) is written by the `Workspot`
locomotion state (`:549017`, `:549035`). Quest side:
`questUseWorkspotNodeDefinition` (`questUseWorkspotParams { workspotNode:
NodeRef, forceEntryAnimName }`, functions `UseWorkspot 0, JumpWorkspot 1,
StopWorkspot 2, IdleOnlyMode 3`), conditions
`questCharacterWorkspot_ConditionType { puppetRef, isPlayer, spotRef,
animationName, waitForAnimEnd }` and `questCharacterSceneSpot_ConditionType`.
Status effects `WorkspotStatus.SyncAnimation`, `.Death`, `.Braindance` mark
NPCs in special workspots (`:127269`, `:20785`, `:291920`).

**Sync idea**: `nodeId` (a `GlobalNodeID`, the same in every game) + the
current `animName`/`entryId` describe a workspot pose compactly; a guest
could put the host's puppet in the same spot with
`AIUseWorkspotCommand`/`PlayNpcInWorkspot` + `SendJumpToAnimEnt`
_(inferred; whether the player rig's puppet accepts NPC workspot commands
is the open question already noted in engine-knowledge §7)_.

### 2.6 Voicesets and look-ats

- `scnVoicesetComponent` (RTTI, size 0x2f8): only
  `combatVoSettingsName @+0x220` and `IsGenericTalkInteractionEnabled() ->
Bool` (@ 0x142ce38bc) are exposed. Quest nodes:
  `questPlayVoiceset_NodeType`, `questChangeVoicesetState_NodeType`; entity
  event `entChangeVoicesetStateEvent`. Barks are requested from scripts with
  `GameObject.PlayVoiceOver(instigator, voName: CName, debugSource…)`
  (`:12047`, native 2-arg form `:107978`).
- Look-ats in scenes are timeline events (`scnLookAtEvent`,
  `scneventsPlayerLookAtEvent`), not script calls. On a guest they come for
  free only if the pose itself is streamed (see `pose-streaming.md`).

### 2.7 Fast-forward (skipping dialogue)

The player can hold a key to fast-forward a scene. It is a **time
dilation on the whole game** named `n"SceneSystemFastForward"`
(`final.reds:520893`) plus status effect `GameplayRestriction.FastForward`
(tag `FastForward`) (`:521798-521830`). Allowed when: not in braindance, not
in combat, tier < 5, not falling, not in an elevator, and
`IsFastForwardingAllowed` (`:519817-519830`). In co-op this is a "slowed
and stopped time" case (`../architecture/time.md`, open problems): it changes the
host's clock rate. Detect with `IsFastForwardingActive(Default)` or the
`FastForward` tag; proposal: fast-forward needs every player's consent (or
is host-only and the guests' mirrored subtitles/animations simply speed
up), never a local time dilation on a guest.

### 2.8 Co-op notes

- **Detect** (host): quest node hook for scene start (resource hash,
  location); per-entity `IsEntityInScene` to mark which proxies are
  scene-driven; tier and workspot signals for V; camera system for the shot.
- **Sync**: `SceneStarted { sceneHash, marker }`, `SceneEnded`; scene-driven
  NPCs are ordinary host-owned NPC proxies, with a flag "in scene" so the
  guest never lets local AI or reactions touch them; spawned actors get a
  `NetId`.
- **Guest**: do **not** play its own copy of a story scene (two copies of
  one scene = two sets of actors, the failure the architecture forbids).
  Follow the host's actors as proxies. The unanswered part is fidelity:
  facial animation, lipsync and VO only exist where the scene actually runs
  (see §4 and the open questions).

---

## 3. Dialogue choices

### 3.1 Data the UI receives

Blackboard `UIInteractions` (`final.reds:195335`, auto-created, global):

<!-- prettier-ignore -->
| Field | Type | Meaning |
| --- | --- | --- |
| `DialogChoiceHubs` | Variant `DialogChoiceHubs` | all dialogue hubs on screen |
| `ActiveChoiceHubID` | Int | focused hub, `-1` none |
| `SelectedIndex` | Int | highlighted option in the active hub |
| `LastAttemptedChoice` | Variant `InteractionAttemptedChoice` | **the choice the player just confirmed** |
| `InteractionChoiceHub` | Variant `InteractionChoiceHubData` | world interactions (doors, loot…) — not dialogue |
| `VisualizersInfo` | Variant `{ activeVisId, visIds }` | which visualizers exist |
| `IsQuestNotificationUp`, `HasScrollableInteraction`, `LookAtTargetVisualizerID`, `NameplateOwnerID`, … | | |

Structures (scripts + RTTI offsets):

```
DialogChoiceHubs      { choiceHubs: [ListChoiceHubData] }            :243020
ListChoiceHubData     { id: Int32, activityState: EVisualizerActivityState,
                        flags: EVisualizerDefinitionFlags,
                        isPhoneLockActive: Bool, title: String,
                        choices: [ListChoiceData],
                        timeProvider: wref<IVisualizerTimeProvider>,
                        hubPriority: Uint8 (RTTI only, +0x50) }      :389618
ListChoiceData        { localizedName: String, type: ChoiceTypeWrapper,
                        inputActionName: CName,
                        captionParts: InteractionChoiceCaption,
                        timeProvider: wref<IVisualizerTimeProvider> } :389610
InteractionAttemptedChoice { visualizerType: EVisualizerType,
                        choice: InteractionChoice, choiceIdx: Int32,
                        isSuccess: Bool }                            :199455
InteractionChoice     { caption: String, captionParts, data: [Variant],
                        choiceMetaData { tweakDBName, tweakDBID, type },
                        doNotTurnOffPreventionSystem: Bool }         :107418
IVisualizerTimeProvider.GetCurrentProgress() -> Float  @ 0x1418f39e8
                       .GetDuration() -> Float         @ 0x141915940 :391705
```

Enums: `EVisualizerType { Device 0, Dialog 1, Loot 2, Invalid 3 }`;
`EVisualizerActivityState { Visible 0, Available 1, Active 2 }`;
`EVisualizerDefinitionFlags` (RTTI bit values) `Fading 1,
HeadlineSelection 2, QuestImportant 8, CPO_Mode 16`;
`gameinteractionsChoiceType` (flags wrapped in `ChoiceTypeWrapper`)
`QuestImportant 0, AlreadyRead 1, Inactive 2, CheckSuccess 3, CheckFailed 4,
InnerDialog 5, PossessedDialog 6, TimedDialog 7, Blueline 8, Pay 9,
Selected 10, Illegal 11, Glowline 12` (`:679`). Caption parts
(`InteractionChoiceCaptionPart` subclasses: `StringPart`, `IconPart`,
`BluelinePart`, script parts) carry lifepath/attribute icons and tags.

Consumers to copy from: `InteractionUIBase` registers the three listeners
(`:278122-278190`), `dialogWidgetGameController` draws the hubs
(`:399949`), `DialogueChoiceHubPrereq` answers "is a choice hub on screen"
(`:314419`).

### 3.2 Where a choice comes from (resource side, RTTI)

`scnChoiceNode`: `options: array<scnChoiceNodeOption>`, `mode:
scnChoiceNodeNsOperationMode { attachToActor 0, attachToProp 1,
attachToGameObject 2, attachToScreen 3, attachToWorld 4 }`, `timedParams:
scnChoiceNodeNsTimedParams { action: {appear 0, disappear 1,
disappearFading 2}, timeLimitedFinish, duration }`, `reminderParams`,
`lookAtParams`, `choiceGroup`, `choicePriority`, `hubPriority`,
`interruptCapability`, `choiceFlags {IsFocusClue 1, …}`,
`doNotTurnOffPreventionSystem`, **`cpoHoldInputActionSection`**.
`scnChoiceNodeOption`: `screenplayOptionId`, `caption: CName`, `blueline`,
`isFixedAsRead`, `isSingleChoice`, `type`, `timedParams`, conditions
(`questCondition`, `triggerCondition`, `bluelineCondition`, …),
`gameplayAction: TweakDBID`, `iconTagIds`.

A quest graph waits on a choice with `questChoiceSection_ConditionType {
sceneFile, choiceSectionId: scnNodeId, choiceSectionName, optionName, mode:
{Highlight 0, Selection 1} }`. So a choice is nameable across games as
**(scene resource hash, choice node id, option index or `optionName`)**.

### 3.3 How a choice is selected

- **No native "select option N" is exposed.** The interaction manager has
  three script functions only (`IsInteractionLookAtTarget`,
  `SetBlockAllInteractions`, `AreSceneInteractionsBlocked`).
  `ActiveChoiceHubID` and `SelectedIndex` are _outputs_ of the native
  dialog visualizer (`gameinteractionsvisDialogVisualizerLogic`): the UI
  only listens to them. Whether writing them back changes the native
  selection is untested (open question).
- Selection therefore goes through **input**. The campaign fork injected
  raw key events (engine-knowledge §7, "Dialog choice injection": the
  `RawInputData` stride it used is broken on 2.5; Codeware's
  `InkSystem_ProcessInputEvents` 673454961 is the 2.5-verified entry).
  Each option also names its `inputActionName` (`ListChoiceData`), and the
  hub UI registers `n"ChoiceApply"` (`:403290`); `Choice1` is used for
  single-key prompts (`:248590`).
- **Result, readable on the host**: `UIInteractions.LastAttemptedChoice`
  changes; `PreventionSystem.OnPlayerChoice` already consumes it
  (`:139656`, `:141776`): `visualizerType == Dialog`, `isSuccess`,
  `choiceIdx`, `choice.caption`, `choice.choiceMetaData`. That is the
  cleanest "the host picked option N" signal.
- World interactions (not dialogue) arrive as `InteractionChoiceEvent {
hotspot, activator, layerData, choice, actionType }` on the hotspot's
  owner (`:209855`; RTTI `gameinteractionsChoiceEvent`), and components can
  publish choices with `InteractionComponent.SetChoices(choices, layer)` /
  `SetSingleChoice` / `ResetChoices(layer, deactivate)` (@ 0x142b4101c,
  0x142b41120, 0x140fed83c).

### 3.4 CDPR's own multiplayer hooks for choices (leftovers)

The 2.50 exe still carries pieces of the cancelled multiplayer:

- `SceneSystem.GetPeerIdDialogChoiceFactName() -> String` and the prereq
  `SelectedForMultiplayerChoiceDialog` (`final.reds:319146`): fulfilled when
  `GetFactStr(thatFactName) == entity.GetControllingPeerID()`, and always
  true when `!GetRuntimeInfo(game).IsMultiplayer()`. In CDPR's design
  **one fact held the peer id of the player allowed to choose**.
- Quest nodes `questMultiplayerChoiceTokenNodeDefinition { params {
timeout: Uint32, compatibleDeviceName: CName } }`,
  `questMultiplayerJunctionDialogNodeDefinition`,
  `questMultiplayerTeleportPuppetNodeDefinition { destinationRef,
destinationOffset, spawnPointTag, teleportAllPlayers,
areaNodeTriggerRef }`; condition
  `questNewPlayerPuppetAttached_ConditionType`.
- Event `MultiplayerGiveChoiceTokenEvent` (`:244531`): gives one player a
  "choice token" (`CPOMissionDataTransferred.isChoiceToken`), re-rolled
  with the native `RandomizePlayer(player)` (@ 0x142cab7a8) after
  `timeout`.
- Scene resources: `scnEntityAcquisitionPlan.findNetworkPlayer = 9`,
  `scnPlayerActorDef.findNetworkPlayerParams.networkId`,
  `scnChoiceNode.cpoHoldInputActionSection`, flag
  `EVisualizerDefinitionFlags.CPO_Mode`.
- `PlayerSystem.FindPlayerControlledObjects(position, radius,
includeLocalPlayers, includeRemotePlayers, out objs) -> Uint32`
  (@ 0x142e687dc), `IsCPOControlSchemeForced()`.

All of this is gated on `IsMultiplayer()`/`IsServer()`/`IsClient()`
natives that return single-player answers in retail. **Treat it as a
design reference** ("one player holds the choice token, with a timeout"),
not as something to call. [unverified]

### 3.5 Timed choices

A hub or an option has a `timeProvider`; the UI reads
`GetCurrentProgress()` (0→1) and `GetDuration()` (seconds) every frame.
Options of type `TimedDialog`. On expiry the node acts per
`scnChoiceNodeNsTimedAction`. The provider is a native object
(`IVisualizerTimeProvider` is abstract, import-only): a guest cannot
construct one, so a mirrored hub needs its own timer drawn from
`{ duration, hostTimeStartedUs }` _(inferred)_.

### 3.6 Phone and holocall choices

Calls are scenes of category `holocalls`; their reply options appear as
dialogue hubs too, with `ListChoiceHubData.isPhoneLockActive`. Phone state:
blackboard `UI_ComDevice.PhoneCallInformation` (Variant
`PhoneCallInformation`, `:241130`), phases and the answer/reject inputs are
in engine-knowledge §7. `PhoneSystem.IsBlockedByTier()` = `HighLevel` 3–5
(`:241033`). Text-message replies are journal entries
(`gameJournalPhoneChoiceEntry`, `JournalManager.GetMessagesAndChoices`),
synced by the journal mirror, not by the choice path.

### 3.7 Co-op notes

- **Detect** (host): listener on `DialogChoiceHubs` (hub list changed),
  `ActiveChoiceHubID`, `SelectedIndex` (what the host is hovering),
  `LastAttemptedChoice` (what was picked).
- **Sync** host → guests: per hub `{ id, title, flags, timed {duration,
startedAt}, options [{ localizedName, type flags, caption parts reduced
to icon/tag ids }] }`, the hover, and `ChoiceMade { hubId, index }`. Text
  is already localized **in the host's language**; to show it in each
  guest's language the option must be named by scene hash + node id +
  option id and resolved locally from the scene's `screenplayStore`
  _(inferred; needs the resource loaded on the guest)_.
- **Guest → host**: `ChoiceVote { hubId, index }`.
- **Guest display**: write a `DialogChoiceHubs` variant into the guest's own
  `UIInteractions` blackboard (the campaign fork did this with a stand-in
  hub id 77000) or draw a tandem widget. Lifepath/attribute-gated options
  (`Blueline`, `CheckSuccess/Failed`) are computed from **the host's V**:
  show them as the host sees them.
- **Proposed rule**: the host's game owns the choice. Guests see the same
  options and the host's hover, may vote (the vote is advice shown next to
  each option); the host confirms. Timed choices are never delayed by a
  vote. A later option: "token" mode in the spirit of §3.4, where the host
  hands the decision to one guest and applies that guest's pick through the
  input path.

---

## 4. Subtitles, voice, fades, videos

### 4.1 Subtitles

Blackboard `UIGameData` (`final.reds:217714`, auto-created, global):
`ShowDialogLine` (Variant `[scnDialogLineData]`), `HideDialogLine` (Variant
`[CRUID]`), `HideDialogLineByData` (Variant `[scnDialogLineData]`),
`ShowSceneComment` (String), `HideSceneComment` (Bool),
`ShowSubtitlesBackground` (Bool).

```
scnDialogLineData { id: CRUID @+0x00, text: String @+0x08,
                    type: scnDialogLineType @+0x28,
                    speaker: wref<GameObject> @+0x30,
                    speakerName: String @+0x40, duration: Float @+0x60,
                    isPersistent: Bool @+0x64 }                 :268028
  static GetDisplayText(self) -> scnDialogDisplayString
         { text, translation, preTranslatedText, postTranslatedText,
           language }
  static HasKiroshiTag(self) -> Bool, HasMothertongueTag(self) -> Bool
```

`scnDialogLineType`: `None 0, Regular 1, Holocall 2, SceneComment 3,
OverHead 4, Radio 5, GlobalTV 6, Invisible 7, OverHeadAlwaysVisible 8,
OwnerlessRegular 9, AlwaysCinematicNoSpeaker 10, GlobalTVAlwaysVisible 11,
Narrator 12` (`:7395`).

`BaseSubtitlesGameController` (`:268101`) listens to the three fields
(`OnShowDialogLine`, `OnHideDialogLine`, `OnHideDialogLineByData`,
`:268284-268340`) and on creation asks
`GameInstance.GetSubtitleHandlerSystem(game).RegisterSubtitleController(
this) -> gamePendingSubtitles { pendingSubtitles: [scnDialogLineData] }`
(@ 0x141400880) for lines already on screen (`:268440`). A line counts as
"main dialogue" when it has a speaker and is not Radio/OverHead/GlobalTV/
Invisible, **or** is `OwnerlessRegular` (`:268608-268624`). Overhead chatter
uses `ChattersGameController` with the same data.

- **Detect** (host): listeners on `ShowDialogLine` / `HideDialogLine` /
  `HideDialogLineByData`, or wrap `SpawnDialogLine` (campaign fork).
- **Sync**: `{ id, text, type, speaker (EntityID → NetId/hash),
speakerName, duration, isPersistent }`, and the hide by id.
- **Guest**: set the same variant on its `UIGameData` blackboard; the
  vanilla widget draws it. If the speaker cannot be resolved to a local
  entity, send type `OwnerlessRegular` so the line still shows as main
  dialogue _(inferred from `:268608`)_. Text arrives in the host's
  language (same remark as §3.7).

### 4.2 Voice-over and scene audio

VO is played by the scene (`scnDialogLineEvent.voParams`, `scnAudioEvent`)
through the audio system; **no script-level "line X is playing on entity Y"
hook was found** beyond the subtitle data. A guest that does not run the
scene hears nothing. Options, all unproven: (a) replay only the scene's
audio/VO-bearing nodes on the guest (the campaign fork's QuestMirror
mirrored `questAudio*` and `questScene*` nodes — unproven, and replaying a
whole scene node contradicts §2.8); (b) trigger the VO on the proxy by
voiceset/line id if a native for it is found; (c) voice chat makes the
host's audio moot — not a real answer. This is the biggest open item.

### 4.3 Fades to black

- There is **no `gameuiFadeInOut` class and no "screen is black" flag** in
  2.50's RTTI or scripts. A fade is a quest node:
  `questSetFadeInOut_NodeType { fadeColor: Color @+0x38, duration: Float
@+0x3c, fadeIn: Bool @+0x40 }`, a `questIRenderFxManagerNodeType` (inside
  `questRenderFxManagerNodeDefinition`). The loading screen has
  `inkLoadingFadeInOutCallback` (native). Scene side:
  `SceneCustomData_ColorFadeParams`.
- **Detect** (host): only the quest-node hook sees it (class
  `questRenderFxManagerNodeDefinition` was among the 31 classes the
  campaign fork mirrored).
- **Guest**: executing that one node on the guest is the obvious route (it
  is self-contained: colour, duration, direction) but "a node run outside
  its phase" is exactly what `../architecture/story.md` lists as unproven. Fallback:
  a tandem full-screen ink layer driven by `{ color, duration, fadeIn,
hostTime }`.
- Loading screens: `questOverrideLoadingScreen_NodeType`,
  `questGlitchLoadingScreen_NodeType`, `questSetSaveDataLoadingScreen_…`,
  `questWaitForAnyKeyLoadingScreen_NodeType`; fast travel publishes
  `FastTRavelSystem.FastTravelLoadingScreenStarted/Finished` on a
  blackboard (`:50907`); save lock reason `LoadingScreen = 7`.

### 4.4 Videos

`questHUDVideo_NodeType { video: raRef<Bink>, position, size, audioEvent,
skippable, looped, playOnHud, fullScreen, useFullscreenVideoState, … }`
(UI manager node); runtime event `gameuiHUDVideoStartEvent { videoPathHash:
Uint64, fullScreen, skippable, isLooped, … }` / `gameuiHUDVideoStopEvent`
handled by `gameuiHUDVideoPlayerController`; in scenes `scnPlayVideoEvent {
videoPath, isPhoneCall }`. A full-screen bink (intro, endings, some
transitions) is self-contained: `{ pathHash, flags, hostTime }` is enough
for a guest to play the same file _(inferred)_.

### 4.5 HUD visibility

Quest node `questSetHUDEntryVisibility_NodeType` → event
`questHUDEntryVisibilityEvent { dataEntries:
array<questHUDEntryVisibilityData> }`. Per-tier visibility is in the
widgets (§1.5). A spectating guest should apply the host's tier to its HUD
rules _(proposal)_.

---

## 5. Braindance

### 5.1 What it is to the engine

A braindance is a **"rewindable section" of a scene**
(`scnRewindableSectionNode { events, sectionDuration, actorBehaviors,
playSpeedModifiers }`). When one starts, the player receives
`scnRewindableSectionEvent { active: Bool }` and
`PlayerPuppet.OnRewindableSectionEvent` (`final.reds:45353`) adds the
on-demand state machine `n"BraindanceControls"`, disables camera bobbing,
queues `SetIsInBraindance { newState }` to `BraindanceSystem`, and calls
`AudioSystem.SetBDCameraListenerOverride(true)`.

### 5.2 State you can read

<!-- prettier-ignore -->
| What | Where |
| --- | --- |
| in a braindance? | `SceneSystemInterface.IsRewindableSectionActive()` (@ 0x14037ede0) — the ground truth; `PlayerNotInBraindancePrereq` uses it (`:319756`) |
| same, script copy | `BraindanceSystem.GetIsInBraindance()` (`:247774`, persistent, also locks saving); PSM `scriptInterface.IsPlayerInBraindance()` (@ 0x14037ed7c) |
| progress 0–1, time, duration | `GetRewindableSectionProgress()` (@ 0x14037f0b8), `…TimeInSec()` (@ 0x142cde6b0), `…DurationInSec()` (@ 0x1418c5718) |
| direction, speed | `Get/SetRewindableSectionPlayDirection` (`scnPlayDirection { Forward 0, Backward 1 }`), `Get/SetRewindableSectionPlaySpeed` (`scnPlaySpeed { Pause 0, Slow 1, Normal 2, Fast 3, VeryFast 4 }`) |
| paused, resetting | `IsRewindableSectionPaused()`, `IsRewindableSectionResetting()` |
| restart / jump | `ResetRewindableSection(resetSpeed, postDir, postSpeed)`, `JumpRewindableSection(resetSpeed, jumpTime, postDir, postSpeed) -> Bool`; quest node `questRewindableSectionTimeJump_NodeType { sceneFile, jumpTargetTime, jumpSpeed, … }` |
| playback (first person, on rails) vs editor (free camera) | `GetSceneSystemCameraControlEnabled()`: true = playback; `IsInEditorMode = !that` (`:520744`) |

Blackboard `Braindance` (`BraindanceBlackboardDef`, `:48378`, global):
`IsActive` (Bool), `IsFPP` (Bool), `Progress` (Float), `SectionTime`
(Float), `PlaybackSpeed` (Variant `scnPlaySpeed`), `PlaybackDirection`
(Variant `scnPlayDirection`), `activeBraindanceVisionMode` /
`lastBraindanceVisionMode` (Int `braindanceVisionMode { Default 0, Audio 1,
Thermal 2 }`), `Clue` (Variant), `EnableExit` (Bool). HUD listeners:
`HUDManager.OnBraindanceToggle` (`:49471`), `BraindanceGameController`
(`:444087`).

Facts: `braindanceVisionMode` (written on every layer change, `:520148`),
`braindaneAudioLayerAvailable`, `braindaneThermalLayerAvailable` (sic,
`:520191`), per-quest facts such as `sq030_braindance_active` (`:380271`).
Quest nodes: `questAddBraindanceClue_NodeType { clueName, startTime,
endTime, layer: gameuiEBraindanceLayer }`,
`questDiscoverBraindanceClue_NodeType`, `questEnableBraindanceFinish_NodeType`.
Player events from quests: `EnableBraindanceActions` /
`DisableBraindanceActions { actionMask: SBraindanceInputMask { pauseAction,
playForwardAction, playBackwardAction, restartAction, switchLayerAction,
cameraToggleAction } }`, `ForceBraindanceCameraToggle { editorState }`,
`PauseBraindance` (`:44739-44772`).

### 5.3 What it does to the player

- Restrictions: `GameplayRestriction.NoMovement` + a "no hub" restriction
  on entry (`:530352`), `GameplayRestriction.Braindance` in editor mode
  (`:520785`); UI contexts `UIGameContext.BraindancePlayback` /
  `BraindanceEditor` (`:520641`); footsteps muted; glitch effects
  `transition_glitch_loop[_braindance]`; quick slots disabled (`:527995`);
  fast-forward off; saving locked.
- Editor camera: on-demand state machine `n"LocomotionBraindance"`
  (`:520462`) with states `BraindanceFly` / `BraindanceFastFly`
  (`:515724`, `:515848`), a collision filter
  (`EnableBraindanceCollisionFilter`, `:515414`) and
  `LocomotionBraindanceParameters.SetUpperMovementLimit/
SetLowerMovementLimit`. **V's own entity is what flies**: in editor mode
  the player's transform moves through the recording _(inferred from the
  locomotion state machine being on the player)_.
- NPCs: status effects tagged `Braindance` (and `Sleep`) make NPCs ignore
  stimuli (`:144843`, `:146202`); `WorkspotStatus.Braindance`;
  `gameBraindanceDissolveComponent` does **not exist in 2.50's RTTI** (only
  `BraindanceModule`/`BraindanceInstance` HUD classes) — the dissolve look
  is done elsewhere.
- Mappins: `VisibleInBraindance`, `NameplateVisibleInBraindance` flags.

### 5.4 Co-op notes

- **Time**: the rewindable section has **its own clock** (pause, 4 speeds,
  reverse, jump). It is "another timeline" in `../architecture/time.md`'s terms:
  it must not touch the world clock that guests follow.
- **Detect** (host): `scnRewindableSectionEvent` / `Braindance.IsActive`.
- **Sync**: `BraindanceState { active, sceneHash, timeSec, durationSec,
direction, speed, paused, editorMode, layer }` at a few Hz + on change,
  plus the host's camera (§2.4).
- **Guest — three candidates**
  1. _Spectate_: guest is held (`NoMovement`, invulnerable), sees the
     host's camera and HUD state. Simple; passive for the guest.
  2. _Own copy, slaved clock_: the guest's game plays the same rewindable
     scene locally and its time is driven from the host with
     `JumpRewindableSection`/`SetRewindableSectionPlaySpeed`/`…Direction`,
     while the guest flies its own editor camera and can scan clues. This
     is the one place where "play its own copy" is defensible: a recording
     is deterministic content, not shared world state. Requires starting a
     scene on a guest outside its quest — the unproven part.
  3. _Stay outside_: guests remain in the real world next to the host's
     body (the host V sits in a chair in a workspot) and only see a
     "host is in a braindance" marker.
- **Proposed rule**: start with 1 (it needs nothing unproven), keep the
  guest's V parked beside the host's body; investigate 2 for milestone 6
  because braindance investigation is gameplay worth sharing. Clues found
  are quest facts → host-owned.

---

## 6. Other "elsewhere" states

### 6.1 Playing someone else: replacers (Johnny, Kurt, VR tutorial, …)

- The player system swaps the **controlled object**:
  `PlayerSystem.GetLocalPlayerMainGameObject()` (@ 0x1402f6f08) stays V,
  `GetLocalPlayerControlledGameObject()` (@ 0x140c5ba44) becomes the
  replacer; `LocalPlayerControlExistingObject(entityID)` (@ 0x142e689e8)
  does the swap (`final.reds:49978-49994`). Quest node:
  `questReplacer_NodeType { enable: Bool, objectRef: gameEntityReference,
audioOverrideAppearanceName: CName }`.
- Tests: `PlayerPuppet.IsReplacer()` = record ≠
  `Character.Player_Puppet_Base`; `IsJohnnyReplacer()` = record ==
  `Character.johnny_replacer`; `IsVRReplacer()` =
  `Character.q000_vr_replacer` (`:39344-39364`). Other replacer records in
  scripts: `Character.kurt_replacer`,
  `Character.mq304_assassin_replacer_male/_female` (`:39762-39810`).
- Notifications: callback `PlayerSystem.OnLocalPlayerChanged(
controlledObject)` sets blackboard `UI_PlayerStats.isReplacer` (Bool)
  (`:50036-50052`, listened to e.g. at `:209987`);
  `OnLocalPlayerPossesionChanged(gamedataPlayerPossesion { Default 0,
Johnny 1 })`; fact **`isPlayerPossessedByJohnny`**
  (`GetPossessedByJohnnyFactName()`, `:50012`);
  `RegisterPlayerPuppetAttachedCallback(obj, func) -> Uint32` /
  `…DetachedCallback` (@ 0x140a4d2d0 / 0x140a4d0e0).
- What the game applies itself
  (`EvaluateApplyingReplacerGameplayRestrictions`, `:39762`): for every
  replacer `GameplayRestriction.BlockAllHubMenu` + `BlockFastTravel`; for
  Johnny also god mode `Invulnerable` with source
  `n"JohnnyReplacerSequence"` (and infinite ammo, removed on return).
  Subtitles have a Johnny mode (`m_isPlayerJohnny`), choices have type
  `PossessedDialog`.
- **Co-op**: the plugin samples "local V" every frame — it must sample the
  **controlled** object, and the appearance/rig it announces changes
  (Johnny is a different entity template). Detect with `isReplacer` +
  record id; sync `{ player, replacerRecord (TweakDBID) }`; guests respawn
  that player's puppet from the replacer record. Johnny flashbacks are
  "another timeline": the host is in 2013/2023 Night City with its own
  cast; proposed rule: guests are brought along as spectators (tier-5 rule
  of §1.6) or, later, cast as generic bystanders; they never stay in 2077
  while the host's world streams the flashback level.

### 6.2 Relic malfunctions

No dedicated system found in scripts: they are quest-driven (FX nodes,
status effects, forced tier/workspot). They show up through the generic
signals: tier, `GameplayRestriction.*`, fades. Proposed: nothing special;
the host's V plays its animation, guests see it. [inferred]

### 6.3 Cyberspace

Status-effect tags `Cyberspace` (blocks quick slots, `:528001`) and
`CyberspacePresence` (`:43947`, `:44502`); districts
`Coastview_q110Cyberspace`, `CorpoPlaza_q201Cyberspace`,
`LittleChina_Q101Cyberspace` (`:4758-4808`) — cyberspace is a **separate
place in the world**, reached by teleport _(inferred)_. Detect: the tags,
or the district. Proposed: treat as a host teleport → regroup guests.

### 6.4 Intro, lifepath prologues, endings

Tier-5 scenes, full-screen videos (§4.4), replacers and — for endings —
`questCheckpointNodeDefinition { pointOfNoReturn, endGameSave, … }`,
`questShowPointOfNoReturnPromptEvent`, blackboard
`UI_PointOfNoReturnRewardScreen`. Nothing new to detect; they stack the
mechanisms above. Guests starting from a quest-free world (engine-knowledge
§7) skip the prologue by construction.

### 6.5 Vehicle scenes (forced passenger, chases)

- The vehicle state machine enters state `Scene` when `SceneTier` is 3–5
  (`SceneDecisions`, `final.reds:551665-551690`); `SceneEvents.OnEnter`
  sets `PlayerStateMachine.Vehicle = 7` (`gamePSMVehicle.Scene`), calls
  `SetVehicleCameraSceneMode(true)` and mounts V into the seat's workspot
  if the scene has not done it (`:552330-552344`). `gamePSMVehicle`:
  `Default 0, Driving 1, Combat 2, Passenger 3, Transition 4, Turret 5,
DriverCombat 6, Scene 7`.
- Status tags: `VehicleScene` (forces no-combat, `:246203`),
  `VehicleCombat`, `VehicleCombatBlockExit`, `VehicleBlockExit`,
  `VehicleOnlyForward`, `VehicleFPP`, `NoDriving`, `BlockMountVehicle`,
  `MetroRide`, `DelamainTaxi`, `AllowFastForwardInVehicle`.
- Quest nodes: `questEntityManagerMountPuppet_NodeType`,
  `questToggleCombatForPlayer_NodeType`,
  `questToggleSwitchSeatsForPlayer_NodeType`,
  `questRequestVehicleCameraPerspective_NodeType`,
  `questSetVehicleCamera_NodeType`, `questEnablePlayerVehicle_NodeType`;
  conditions `questCharacterMount_ConditionType`,
  `questCharacterMountedTogether_ConditionType`.
- Natives: `vehicleBaseObject.IsPerformingSceneAnimation()`,
  `IsInQuestOrScenePhysicsState()`; `VehicleCameraManagerFT.
FT_IsSceneModeActive()` / `FT_RequestSceneMode(active)`.
- **Co-op**: the scene car is an AI/quest vehicle → host-owned (authority
  table). Guests get mounted into free seats (the campaign fork's seat
  order is in engine-knowledge §7) with `VehicleScene`-like restrictions;
  if no seat is free, tether/regroup at the destination. A car V drives
  itself is still the driver's.

### 6.6 Elevators

`LiftDevice` (`final.reds:197796`) writes
`PlayerStateMachine.IsPlayerInsideElevator` (Bool) and `CurrentElevator`
(Variant) (`:198525-198537`); `IsPlayerInsideMovingElevator` exists too.
Fast-forward is blocked inside (`:519827`); save lock
`PlayerOnMovingPlatform = 13`. Quest events `QuestGoToFloor`,
`QuestForceTeleportToFloor`, `QuestStopElevator`, … on the device. Many
story elevators are **loading tunnels** (the level changes around the
cabin) _(inferred)_. Proposed: the lift is a device → host-owned state;
riding guests ride; guests left outside are regrouped when the host's
elevator arrives (treated like a fast travel).

### 6.7 Sleeping, sitting, waiting

Workspots (§2.5) plus skipped time: UI `MenuScenario_TimeSkip`,
`TimeSkipPopupData`, events `TimeSkipOpenedEvent` / `TimeSkipClosedEvent` /
`TimeSkipFinishEvent`, native `TimeSkipFinishedEvent`
(`gameTimeSkipFinishedEvent`), quest condition
`questTimeSkipped_ConditionType` (`questTimeSkipMode { PreSkip 0, PostSkip
1 }`), status tag `NoTimeSkip`, NPC tag `Sleep`. Time of day is host-owned;
a skip is a host decision applied to everyone ("skipped time" in
`../architecture/time.md`). Proposed: a guest's sleep/wait request becomes a
request to the host.

### 6.8 Takedowns, finishers, synced animations

`PlayerStateMachine.Takedown` (`gamePSMTakedown { Default 0,
EnteringGrapple 1, Grapple 2, Leap 3, Takedown 4 }`, script also writes 5 at
`:210132`), `TakedownTargetID`, `FinisherTarget` (EntityID);
`PlayerPuppet.GetIsInWorkspotFinisher()` (`:39390`); workspot tag
`FinisherWorkspot`; camera params `WorkspotLocked` (`:247040`); NPC status
`WorkspotStatus.SyncAnimation`; `SyncAnimDeathTask`. They are two-actor
workspots: `PlayNpcInWorkspot(npc, master, …, syncSlotName, …)` and
`SendDynamicSyncBindSignal(actor, master)`.
**Co-op**: a guest's takedown targets a host-owned NPC: the guest plays its
half locally at once (principle 2), sends `{ target, kind }`; the host
plays the NPC half and decides the outcome. A replication struct
`gameReplAnimTransformSyncAnimRequest` exists in RTTI (CDPR leftover,
unverified).

### 6.9 Death and "load last checkpoint"

`PlayerStateMachine.Vitals` (`gamePSMVitals { Alive 0, Dead 1,
Resurrecting 2 }`), `DisplayDeathMenu` (Bool; set at `final.reds:283812`
unless the death menu is blocked; the death screen listens at `:347222`);
reload = `GetSystemRequestsHandler().LoadLastCheckpoint(true)`
(`:347642`). Second Heart resurrection is decided in
`SetIsResurrectionAllowedBasedOnState` (`:283786`); status
`BaseStatusEffect.ForceKill` marks quest kills.
**Co-op**: a V's death is that player's (authority table); a guest must
never load a checkpoint (that would reload a single-player world): block
`DisplayDeathMenu` and replace with the downed/revive flow (milestone 8).
A **host** reload restarts the world → guests are held behind a fade and
re-snapshotted.

---

## 7. Locks and restrictions

### 7.1 Mechanism

Restrictions are **status effects** on the player whose records are
`GameplayRestrictionStatusEffect_Record` (type name in the CName pool) and
whose **gameplay tags** are what code tests. All carry the tag
`GameplayRestriction` (`RemoveStatusEffectsWithTag(target,
n"GameplayRestriction")`, `final.reds:179037`).

- Apply/remove: `StatusEffectHelper.ApplyStatusEffect(target, id, opt
delay)` (`:168953`), `RemoveStatusEffect`, `RemoveStatusEffectsWithTag`
  (`:169113`).
- Query: `StatusEffectSystem.ObjectHasStatusEffect(target, id) -> Bool`
  (`:51214`), `ObjectHasStatusEffectWithTag(target, tag) -> Bool`
  (`:51220`) (both native static).
- Notify: the player's status-effect applied/removed handlers call
  `PlayerGameplayRestrictions.OnGameplayRestrictionAdded/Removed(player,
record/evt, gameplayTags)` (`:179040`, `:179082`) — a single place to
  wrap; blackboard `PlayerStateMachine.ActionRestriction` (Variant) has a
  listener pattern at `:105088`.
- Quests: `questGameplayRestrictions_NodeType { action:
questGameplayRestrictionAction { AddRestriction 0, RemoveRestriction 1,
RemoveAllRestrictions 2 }, source: CName, restrictionIDs:
array<TweakDBID> }` — **the story's own lever**, with a `source` so
  several requesters do not cancel each other.

### 7.2 Records named in scripts (`t"GameplayRestriction.…"`)

`NoMovement`, `NoCameraControl`, `NoJump`, `ForceCrouch`, `NoWeapons`,
`Firearms`, `FirearmsNoUnequipNoSwitch`, `FistFight`, `InfiniteAmmo`,
`NoScanning`, `NoRadialMenus`, `NoWorldInteractions`,
`BlockDeviceInteractions`, `BlockAllHubMenu`, `BlockAllMenu`,
`BlockFastTravel`, `Braindance`, `FastForward`, `FastForwardCrouchLock`,
`FastForwardHintActive`, `Tier2Locomotion`, `Tier2LocomotionSlow`,
`Tier2LocomotionFast`, `FocusModeLocomotion`, `PhoneCallRestriction`,
`PhoneCallDeviceActionRestrictions`, `VehicleNoInteraction`,
`VehicleCombatNoInterruptions`, `DriverCombatFirearms`,
`DriverCombatBikeWeapons`, `SecurityLocker`, `BodyCarrying*`. More exist
only in TweakDB (quests pass ids, scripts never name them): the tag list
below is the fuller picture.

### 7.3 Tags scripts test (`ObjectHasStatusEffectWithTag(player, n"…")`)

<!-- prettier-ignore -->
| Area | Tags |
| --- | --- |
| combat | `NoCombat`, `NoWeapons`, `Fists`, `Melee`, `Firearms`, `FirearmsNoUnequip`, `FirearmsNoSwitch`, `FirearmsNoUnequipNoSwitch`, `OneHandedFirearms`, `NoArmsCW`, `NoQuickMelee`, `NoQuickHacks`, `ForceAim`, `FistFight`, `InfiniteAmmo`, `TrainingGuns`, `TrainingMelee` |
| movement | `NoMovement`, `NoJump`, `NoSlide`, `NoLadder`, `ForceCrouch`, `ForceStand`, `NoEncumbrance` |
| vehicle | `VehicleScene`, `VehicleCombat`, `VehicleCombatBlockExit`, `VehicleCombatNoInterruptions`, `VehicleBlockExit`, `VehicleOnlyForward`, `VehicleFPP`, `VehicleBlockRadioInput`, `VehicleSummoning`, `NoDriving`, `BlockMountVehicle`, `BlockTrafficInteractions`, `DriverCombatFirearms`, `DriverCombatBikeWeapons`, `MetroRide`, `DelamainTaxi` |
| UI/menus | `LockInHubMenu`, `BlockAllHubMenu`, `BlockAllMenu`, `BlockFastTravel`, `NoRadialMenus`, `NoWeaponWheel`, `NoCrafting`, `NoScanning`, `NoZooming`, `NoTimeSkip`, `NoTimeDisplay`, `NoWorldInteractions`, `DeviceControlZoomLock` |
| phone | `PhoneCall`, `PhoneNoCalling`, `PhoneNoTexting`, `PhoneInterrupted`, `NoPhone` |
| story | `Braindance`, `Cyberspace`, `FastForward`, `FastForwardHintActive`, `AllowFastForwardInVehicle`, `LoreAnim`, `SpaceShuttleInterior`, `Sleep`, `AllowTracingInTier2…5`, `PreventLowHealthOverlay`, `KeepState` |

`IsNoCombatActionsForced` = `NoCombat && !FastForward || VehicleScene`
(`:246203`).

### 7.4 Weapons forced away

Three independent sources, OR-ed in the upper-body state machine
(`final.reds:246318-246324`): state-context flags `ForceEmptyHands` (from
the tier's `emptyHands`, `:529835`) and `ForceEmptyHandsByZone` (PSM event
id `n"ForceEmptyHandsByZone"`, `:46236`); the **quest fact
`ForceEmptyHands`**; and likewise `ForceSafeState` /
`ForceSafeStateByZone` / fact `ForceSafeState` (`:245443`). `QuestForceShoot`
(PSM blackboard Bool) is the opposite. Device event `QuestRemoveWeapon`
exists for lockers.

### 7.5 Safe zones

`PlayerStateMachine.Zones` (Int `gamePSMZones { Default 0, Public 1, Safe 2,
Restricted 3, Dangerous 4 }`, `:6714`; `IsInSafeZone`, `:246698`) and
`SecurityZoneData` (Variant). Zones come from security areas the player
overlaps, i.e. they are per player and position-based: each game computes
its own V's zone. The _security state_ of an area (alarmed or not) is world
state → host.

### 7.6 Co-op notes

- **Detect**: wrap `OnGameplayRestrictionAdded/Removed` on the host, or poll
  the tags that matter.
- **Sync**: not a blind copy. A restriction is on the **host's V**; for
  each one decide whether it is about V (keep local: `BodyCarrying*`,
  `PhoneCall`) or about the story moment (mirror: `NoCombat`, `NoWeapons`,
  `BlockFastTravel`, `VehicleScene`, `NoTimeSkip`, `FistFight`). The quest
  facts `ForceEmptyHands`/`ForceSafeState` arrive through the fact mirror
  and act on guests with no extra work (they are read live by the guest's
  own state machine) — check they are in the mirrored fact list.
- **Guest**: apply with a tandem source name and ref-count per reason, and
  remove leftovers on load (both lessons from the campaign fork,
  engine-knowledge §7).
- **Proposed rule**: "story restrictions follow the host inside the scene's
  area; personal restrictions stay personal". Never mirror
  `RemoveAllRestrictions` blindly: it would wipe the guest's own.

---

## 8. Summary table

<!-- prettier-ignore -->
| Moment | Detect on the host | Send | Guest behaviour (proposal) |
| --- | --- | --- | --- |
| tier 2 walk-and-talk | PSM `SceneTier`/`HighLevel` | tier, walk type, empty hands | free; weapons lowered nearby; subtitles + choices |
| tier 3 seated/limited | same + `IsInWorkspot`, `WorkspotStartedEvent` | tier, workspot node + anim | free, `NoCombat` nearby; host puppet posed |
| tier 4 FPP cinematic | same + `IsSceneAnimationActive` | tier, V pose/transform | brought close, protected, kept out of the way |
| tier 5 cutscene | same + camera system | tier, camera transform + FOV | spectator of the host's camera, own V parked |
| scene start/end, actors | quest node hook (`questSceneNodeDefinition.sceneFile`), `IsEntityInScene` | scene hash, marker, actor ids | no local copy; actors are host proxies flagged "in scene" |
| subtitles | `UIGameData.ShowDialogLine` / `HideDialogLine*` | line data | write the same blackboard |
| VO / lipsync | nothing found | ? | open |
| dialogue hub | `UIInteractions.DialogChoiceHubs`, `ActiveChoiceHubID`, `SelectedIndex` | hubs, options, timer, hover | mirrored hub; vote |
| choice made | `UIInteractions.LastAttemptedChoice` | hub id, index | close hub; outcome arrives as facts/scene |
| fast-forward | `IsFastForwardingActive`, tag `FastForward` | on/off | consent or host-only; never local dilation |
| fade | quest node hook (`questSetFadeInOut_NodeType`) | colour, duration, direction | same fade (node or own overlay) |
| video | `gameuiHUDVideoStartEvent` / node | path hash, flags | play the same bink |
| braindance | `scnRewindableSectionEvent`, `IsRewindableSectionActive`, `Braindance` blackboard | time, speed, direction, mode, layer, camera | spectate first; own slaved copy later |
| Johnny / replacer | `UI_PlayerStats.isReplacer`, record id, fact `isPlayerPossessedByJohnny` | replacer record | swap that player's puppet; follow as spectators in flashbacks |
| cyberspace | tags `Cyberspace`, district | host teleport | regroup |
| vehicle scene | PSM `Vehicle == 7`, tag `VehicleScene` | vehicle id, seats | mounted in free seats, no combat |
| elevator | PSM `IsPlayerInsideElevator`, `CurrentElevator` | device state | ride, or regroup on arrival |
| sleep / time skip | `TimeSkip*` events, `TimeSkipFinishedEvent` | new time | applied; guest requests go to the host |
| takedown / finisher | PSM `Takedown`, `FinisherTarget`, workspot tags | actor, target, kind | own half local; NPC half by the host |
| death / checkpoint | PSM `Vitals`, `DisplayDeathMenu` | downed; host reload | guest never reloads; host reload = re-snapshot |
| restrictions | `OnGameplayRestrictionAdded/Removed`, tags, facts `ForceEmptyHands`/`ForceSafeState` | story restrictions only | applied with a tandem source, ref-counted |

Authority-table rows this implies (to add when implemented): _scene
playback and camera — host_; _dialogue choice — host (guests vote)_;
_braindance clock — host_; _a V's tier and restrictions — that player's
game, story restrictions pushed by the host_.

## 9. Open questions (need an in-game test)

1. Does `PlayerStateMachine.SceneTier` change strictly before `HighLevel`,
   and what does `SceneTierData.userDebugInfo` contain during a real scene
   (scene name?).
2. Can a guest's tier be raised at all without a quest (inject the
   `n"SceneTier"` permanent parameter, or call into `gameTierSystem`)? Or
   are status-effect restrictions the only lever?
3. Does a story scene's NPC animation (body, face, lipsync) reach a guest
   through NPC pose/animation sync alone, or must the scene run locally?
   What does a host-driven proxy look like during a tier-4 scene?
4. VO: is there any native that plays a scene line's VO on an entity
   outside a running scene? If not, is running the scene's audio-only part
   on a guest feasible?
5. Does executing a lone `questSetFadeInOut_NodeType` (or HUD video node)
   on a guest work outside its phase (the ../architecture/story.md open problem, on
   the smallest possible node)?
6. `UIInteractions.LastAttemptedChoice`: confirm it fires for every
   dialogue choice (including timed-out and phone choices) and that
   `choiceIdx` matches the index in `ListChoiceHubData.choices`.
7. Does writing `SelectedIndex`/`ActiveChoiceHubID` move the native
   selection, or is input injection the only way to apply a choice?
   Validate Codeware's `InkSystem_ProcessInputEvents` path on 2.50.
8. Does a stand-in `DialogChoiceHubs` variant written by script render
   correctly (caption parts, icons, `timeProvider = null`) and stay up, or
   does the native visualizer overwrite it every frame?
9. Do the CDPR leftovers do anything in retail:
   `GetPeerIdDialogChoiceFactName()` (what string?), `findNetworkPlayer`
   actors, `FindPlayerControlledObjects(includeRemotePlayers)`?
10. Braindance on a guest: can a rewindable scene be started outside its
    quest, and do `JumpRewindableSection` / `SetRewindableSectionPlaySpeed`
    slave it smoothly to a host clock? In editor mode, is it really V's
    entity that moves (what does the plugin's "local V" sample report)?
11. Replacers: what do `GetLocalPlayerMainGameObject` and
    `…ControlledGameObject` return during a Johnny sequence, and which one
    does the plugin's current sampling follow?
12. Spectator camera: is `SetFreeCameraTransform` usable in retail, or
    must a `CameraComponent` be spawned and `Activate`d? Does following a
    remote camera stream the right sectors when the guest's V is far
    (probably not: the V must be moved too)?
13. `IsEntityInScene` on the host for community/crowd NPCs in ambient
    scenes (`scnSceneCategoryTag` street/chats): cheap enough to poll for
    every proxy, or event-driven only?
14. Workspot mirroring: does the remote-player puppet accept
    `PlayNpcInWorkspot`/`AIUseWorkspotCommand` and `SendJumpToAnimEnt`
    with the host V's `nodeId` + `animName`?
15. Fast-forward: effect of the host's `SceneSystemFastForward` time
    dilation on the clock guests follow.
16. Which `GameplayRestriction.*` records exist in TweakDB 2.50 beyond the
    ones scripts name (dump with TweakXL), and which ones quests apply
    during the main scenes.
