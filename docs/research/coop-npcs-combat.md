# One set of enemies — NPC proxies, hits, damage, deaths, loot

Research for milestone 5 of [`ARCHITECTURE.md`](../ARCHITECTURE.md): the
host's game runs NPC AI; guests show proxies with the AI off; a guest's hit
goes to the host, which applies it; an NPC attack on a guest's puppet becomes
a damage event for that guest.

**Sources**

- `final.reds:LINE` — decompiled vanilla scripts of game 2.50
  (`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`).
- RTTI — live dump of game 2.50 (`tools/re/run tools/re/rtti.py class NAME`).
  `@ 0x14…` is the native handler address (image base 0x140000000);
  `+0x…` is a property offset.
- Codeware — `~/.cache/tandem/ref/codeware/scripts` (MIT).

**Tags**: `[S]` read in the scripts, `[R]` read in the RTTI dump, `[C]`
Codeware API, `[I]` inferred by me from those (not stated anywhere).

**Caveats**

- Everything here is a script or RTTI fact. **Nothing was run in co-op or in
  game.** Each section ends with what must be tested; the list is repeated at
  the end.
- The decompiler mis-nests long `if … else if` chains (for example
  `DamageSystem.CheckForQuickExit`, `NPCPuppet.OnPotentialDeath`): trust the
  calls and their order, not the exact branch structure.
- This extends `engine-knowledge.md` §8 (how earlier projects did combat);
  it does not repeat it.

## Contents

1. [The multiplayer scaffolding left in the engine](#1-the-multiplayer-scaffolding-left-in-the-engine)
2. [Turning an NPC's brain off](#2-turning-an-npcs-brain-off)
3. [NPC state worth replicating](#3-npc-state-worth-replicating)
4. [The hit pipeline](#4-the-hit-pipeline)
5. [Health and death](#5-health-and-death)
6. [Damage on a guest's V](#6-damage-on-a-guests-v)
7. [Spawning and identity](#7-spawning-and-identity)
8. [Loot](#8-loot)
9. [Stealth, combat state, takedowns, quickhacks](#9-stealth-combat-state-takedowns-quickhacks)
10. [Summary](#10-summary)
11. [Open questions for an in-game test](#11-open-questions-for-an-in-game-test)

---

## 1. The multiplayer scaffolding left in the engine

The cancelled multiplayer ("CPO") left its replication **data model** in the
RTTI and its **branches** in the scripts. There is no evidence that the
transport behind it works in the retail build (the session classes have no
script-visible functions). Its value for tandem is twofold: it tells us what
CDPR considered the minimal state of a replicated NPC, and some of its
natives are ordinary local setters we can call.

### 1.1 Global predicates

<!-- prettier-ignore -->
| Function | Where | Note |
| --- | --- | --- |
| `IsMultiplayer()`, `IsClient()`, `IsServer()`, `IsHost()` (globals) | `final.reds:107959-107975`, `97967` | native, no handler in the dump `[R]` |
| `worldRuntimeInfo.IsMultiplayer/IsClient/IsServer()` | `final.reds:109101-109105` | `@ 0x140ee4808 / 0x1410790c8 / 0x1411fab68` |
| `entEntity.IsReplicated()` | `final.reds:11248` | `@ 0x1412265d4` |
| `entEntity.IsControlledByAnyPeer()` | `final.reds:11256` | `@ 0x14108c64c` |
| `entEntity.IsControlledByLocalPeer()` | `final.reds:11258` | `@ 0x140ff79f0` |
| `entEntity.IsControlledByAnotherClient()` | `final.reds:11260` | `@ 0x14039ca54` |
| `gameObject.IsPlayerControlled()` | RTTI | `@ 0x140e38910` |
| `gamedataStatusEffect_Record.Replicated()` | RTTI | per-record flag, `@ 0x140eb987c` |
| `gamedataItem_Record.ReplicateWhenNotActive()` | RTTI | `@ 0x1428c8cb0` |
| `entIComponent.isReplicable` | RTTI `+0x8c` | per-component flag |

**Do not flip `IsMultiplayer()`** (already in §8): about 120 script sites
branch on these. The ones that matter for combat, as a map of what the
vanilla client/server split was:

<!-- prettier-ignore -->
| Site | Line | What the MP branch does |
| --- | --- | --- |
| `DamageSystem.DealDamages` | 85771-85776 | `forReal = !IsClient()`: **a client computes the hit but does not drain the pool** |
| `GameObject.OnHitUI` | 12435-12441 | a client shows no damage numbers from its own pipeline |
| `DamageSystem.ProcessClientHit / ProcessClientKill` | 84546-84556 | numbers and kill feed come from `ServerHitData`/`ServerKillData`, only if `instigator.IsControlledByLocalPeer()` |
| `DamageSystem.ConvertHitDataToDamageInfo` | 84487-84544 | references a target only if `IsReplicated()` or static id |
| `ScriptedPuppet.SquadUpdate` | 21780-21794 | no squads in MP |
| `PlayerCombatController.ActivateCombat/OutOfCombat` | 242876-242925 | no health-regen modifier swap in MP |
| death menu `OnDisplayDeathMenu` | 347380-347394 | **no death menu in MP** |
| `PlayerPuppet.OnGameAttached` | 40101-40121 | input listeners only if local peer or host |
| `DEBUG_DamageInputReceiver` | 243341 | kill cheats only when not a client |
| time dilation helpers | 240310-240449 | blocked in MP (see §8 of engine-knowledge) |

`[I]` The vanilla design is exactly tandem's: the client runs the whole hit
pipeline for feel (reactions, VFX) with `forReal = false`, the server's pools
are the truth, and feedback (numbers, kill) comes back from the server.

### 1.2 Replicated entity state (`netIEntityState`, size 0x50)

`+0x38 persistentID: Uint64`, `+0x40 recordID: TweakDBID` — **an entity was
announced by persistent id + TweakDB record**, the same pair tandem plans.

<!-- prettier-ignore -->
| Class (size) | Fields |
| --- | --- |
| `gamePuppetReplicatedState` (0xe8) | `actionsBuffer: gameActionsReplicationBuffer` +0x50, `animEventsState` +0x68, `entityEventsState` +0x88, `initialOrientation: EulerAngles` +0xa8, `initialLocation: Vector3` +0xb4, `initialAppearance: CName` +0xc0, `health: Float` +0xc8, `armor: Float` +0xcc, `hasCPOMissionData` +0xd0, `CPOMissionVotedHistory` +0xd8 |
| `gameNpcPuppetReplicatedState` (0x100) | the above + `weaponStates: gameWeaponsReplicatedState` +0xe8 |
| `gamePlayerPuppetReplicatedState` (0xe8) | nothing added |
| `gameMuppetReplicatedState` (0x320) | the CPO test player ("muppet"): `Muppet.ReplicateInputStates`, `Muppet.UseLoadout` |
| `gameDeviceBaseReplicationProxy` (0x88) | `scriptState: gameDeviceReplicatedState`, `versionId`, `versionTimestamp: netTime`, initial transform |
| `gameweaponGrenadeReplicatedState` (0xa0) | `instigator`, `itemID`, `currentTransform`, `exploded`, `launched` |
| `netEntityReplicatedState`, `mpPersistentTestBoxState` | generic / test |

Scripts name the class to use: `NPCPuppet.GetReplicatedStateClass()` returns
`n"gameNpcPuppetReplicatedState"` (`final.reds:125366`), `PlayerPuppet`
returns `n"gamePlayerPuppetReplicatedState"` (`39370`).

### 1.3 Replicated component state (`netIComponentState`, size 0x20)

`+0x10 componentName: CName`, `+0x18 enabled: Bool`.

<!-- prettier-ignore -->
| Class | Fields |
| --- | --- |
| `gameAINetStateComponentReplicatedState` (0x80) | eight `gameNetAIState {value: Int32, prevValue: Int32, time: Float}`: `replHighLevelState` +0x20, `replUpperBodyState` +0x2c, `replStanceState` +0x38, `replHitReactionModeState` +0x44, `replBehaviorState` +0x50, `replPhaseState` +0x5c, `replDefenseMode` +0x68, `replLocomotionMode` +0x74 |
| `entAnimationControllerReplicatedState` (0xc0) | `animWrapperVarsState`, look-at requests (`entReplicatedLookAtAdd/Remove`), `animFeaturesState: entReplicatedAnimFeaturesState`, `inputSettersState` (Bool/Float/Int/Vector) |
| `moveMovePoliciesComponentReplicatedState` (0x60) | `movePolicies: moveReplicatedMovePoliciesState` — items `{key: Uint64, policies: handle:movePolicies}` |
| `entRagdollComponentReplicatedState` (0x38) | `transforms: array:Transform`, `isSleeping: Bool` |
| `gameStatusEffectComponentReplicatedState` (0x40) | `replicatedInfo: array {statusEffectRecordID: TweakDBID, stackCount: Uint32, source: CName}` |
| `gameWeakspotComponentReplicatedState` (0x30) | `array {weakSpotRecordID: Uint64, wsHealthValue: Float, LastDamageInstigator: whandle:gamePuppet}` |
| `gameAttachmentSlotsReplicatedState` (0x38) | `stateVersion`, `slots: array:gameAttachmentSlotReplicatedState` |
| `gamestateMachineComponentReplicatedState` (0x1a980) | the whole PSM `stateContext`; natives `MP.RequestItem`, `MP.RequestReload` |
| `gameScanningComponentReplicatedState`, `gameFPPCameraComponentReplicatedState`, `gameTransformAnimatorComponentReplicatedState`, `netDefaultComponentReplicatedState` | scanning, camera, animated transforms, default |

**The CPO wire model of an NPC** was therefore: record + persistent id +
initial transform + appearance; health and armor; eight AI state integers;
anim features / wrapper variables / anim inputs / look-ats; **move policies
(not positions)**; status effects as (record, stacks, source); weapon states;
weak spots; ragdoll body transforms after death; anim events and entity
events as time-stamped lists.

Replicated items carry `entReplicatedItem {entity: whandle +0x08, netTime
+0x18}`; `netTime` is `{milliSecs: Uint64}`; `netPeerID` is `{value: Uint8}`.

### 1.4 Replication helpers callable from scripts

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `gameObject.ReplicateAnimFeature(obj, inputName: CName, value: animAnimFeature)` | `0x140d36680` |
| `gameObject.ReplicateAnimEvent(obj, eventName: CName)` | `0x140e3eb80` |
| `gameObject.QueueReplicatedEvent(evt: redEvent)` | `0x14114abc8` |
| `gameObject.ReplicateInputFloat/Bool/Int/Vector(obj, inputName, value)` | `0x140e1ad78`, `0x142a1dc74`, `0x142a1dd80`, `0x142a1de8c` |
| script callbacks `gameObject.OnEventReplicated(evt)`, `OnAnimFeatureReplicated(inputName, value)`, `OnAnimEventReplicated(eventName)` | script |
| script globals `ApplyFeatureToReplicate`, `SetInput{Bool,Int,Float,Vector}ToReplicate`, `PushEventToReplicate`, `StartReplicatedEffectEvent`, `StopReplicatedEffectEvent`, `BreakReplicatedEffectLoopEvent` | script |
| `MPReplicateComboAttack(position, instigator)` | no handler |
| `gameWeakspotObject.SetReplicationInstigator / GetReplicationInstigator` | `0x142a88dbc`, `0x142a88d9c` |
| `gameAINetStateComponent.Get/SetReplicated*State` (16 natives) | §3.1 |

`[I]` The `…ToReplicate` script helpers are the **catalogue of what NPC
scripts consider animation-relevant**: every call site of
`ApplyFeatureToReplicate`/`SetInput…ToReplicate`/`PushEventToReplicate` is a
piece of visual state a proxy needs. Wrapping these few globals on the host
captures NPC animation changes at the source instead of polling. Needs a
count of call sites and a test (open question 3).

### 1.5 Replicated game systems

Sixteen classes derive from `gameIReplicatedGameSystem` (size 0x58): the
damage, god-mode, debug-cheats, journal, ping, time, mappin and quests
systems (interface + implementation each). Their shared states derive from
`gameIGameSystemReplicatedState`:

<!-- prettier-ignore -->
| Shared state | Fields |
| --- | --- |
| `gameDamageSystemSharedState` | `hitHistory: array:gamedamageServerHitData`, `killHistory: array:gamedamageServerKillData` |
| `gamedamageServerHitData` (0x70) | `id: Uint32` +0x40, `damageInfos: array:gameuiDamageInfo` +0x48, `instigator: whandle:gameObject` +0x58 |
| `gamedamageServerKillData` (0x78) | `id: Uint32`, `killInfo: gameuiKillInfo` |
| `gameGodModeSharedState` | `datas: array {entity: whandle, flags: Int32}` |
| `gameTimeSystemReplicatedState` | `paused: Bool`, `gameTime: GameTime` |
| `questQuestsSystemReplicatedState` | `replicatedQuestPrefabs: array:questQuestPrefabsEntry` |
| `gamePingSystemReplicatedState` | `replicatedPingEntries: array:gamePingEntry` |
| `gamemappinsMappinSystemReplicatedState` | mappins, quest mappin manager state |
| `gameJournalManagerSharedState`, `gameDebugCheatsSharedState` | — |

Shots were replicated as `gameReplicatedShotData {timeStamp: netTime,
attackId: TweakDBID, target: whandle:gameObject, targetLocalOffset: Vector3}`
and `gameReplicatedContinuousAttack {startTimeStamp, stopTimeStamp,
attackId}`: **a shot was (attack record, target, offset on the target)**, not
a ray. `[I]` Good shape for tandem's `Hit` message.

### 1.6 Sessions, players, spawning

<!-- prettier-ignore -->
| Class | What |
| --- | --- |
| `mpGameSession`, `mpServerGameSession`, `mpClientGameSession` (: `gameBaseGameSession`) | session objects, no script API |
| `mpPlayerManager` (: `gameIPlayerManager`) | `GetPlayerNicknameByGameObject` `@ 0x142bed1c0`; `PrespawnRequest(gamePlayerSpawnParams)`, `PrespawnRequestAccepted/Denied`, `SpawnRequest`, `SpawnRequestAccepted/Denied` (no handlers) |
| `mpPlayer`, `mpLocalPlayer`, `mpRemotePlayer`, `mpServerPlayer` (: `gamePlayer`) | player records |
| `netEntitySystem` (: `worldIRuntimeSystem`, 0x90) | the entity replication runtime system; no script API |
| `gameClientEntitySpawnSystem` (: `gameIClientEntitySpawnSystem`) | client-side spawner; no script API |
| `gameServerBlackboardUpdateProxy` | blackboard replication proxy |
| `gameaimAssistAimAssistRemotePlayer`, `gamemappinsRemotePlayerMappin` | remote-player aim assist config and mappin |
| `gamecarryReplicatedEntitySetAttachmentTo{Entity,Node,World}` | body-carry attachment replication |
| `gameinteractionsEnableClientSideInteractionEvent` | interaction made client-side |
| `cpPlayerSystem.FindPlayerControlledObjects(position, radius, includeLocalPlayers, includeRemotePlayers, out objs)` | `@ 0x142e687dc`: the player system knows "remote players" |
| `cpPlayerSystem.LocalPlayerControlExistingObject(entityID)` | `@ 0x142e689e8`: make the local player control an existing entity (used by `TakeOverControlSystem`) |
| `gamePlayerControlledComponent` (0x98), `AICTreeNodePlayerControlledDefinition` | a component and a **behaviour-tree node meaning "this puppet is driven by a player, not by AI"** |
| quest nodes `questMultiplayerAIDirector*`, `questMultiplayerHeist*`, `questMultiplayerTeleportPuppet*`, `questMultiplayerChoiceToken*`, `questMultiplayerJunctionDialog*` | CPO heist quest nodes |
| `Sample_Replicated_*`, `mpPersistentTestBox*`, `mpCMultiplayerDebugFunctions` | samples and tests |
| menus `gsmMenuState_Multiplayer*`, `gsmState_AutoJoinServer`, `FindServersMenuGameController`, `inkServerInfo` | server browser leftovers |
| device scripts `Door/SecurityTurret/WindowBlinders.ApplyReplicatedState`, `SecurityTurret.Replicate{IsShooting,Health,IsOn,IsDead}` | devices replicated a small script state (`final.reds:171242-171451`) |

`[I]` Leads worth an in-game probe, in order: (a) what `IsReplicated()` and
`IsControlledBy*()` return today for V, NPCs and our puppets; (b) whether an
NPC record whose behaviour tree reaches `AICTreeNodePlayerControlledDefinition`
or carries a `gamePlayerControlledComponent` idles with its AI off — that
would be the engine's own "proxy" switch; (c) whether `netEntitySystem`
exists as a live instance.

---

## 2. Turning an NPC's brain off

### 2.1 What an NPC's brain is made of

<!-- prettier-ignore -->
| Piece | Class | Access |
| --- | --- | --- |
| behaviour tree agent | `AIHumanComponent : AICAgent : gameComponent` (0x5b8); script `AIHumanComponent extends AIComponent` (`final.reds:159862`, `143623`) | `ScriptedPuppet.GetAIControllerComponent()` |
| state integers | `NPCStatesComponent extends AINetStateComponent` (`167844`) | §3.1 |
| senses | `senseComponent` (0x1a0), script `SenseComponent` (`155475`) | `gamePuppet.GetSenses()` `@ 0x140e3936c`, `GetSensesComponent()` |
| being seen | `senseVisibleObjectComponent` | `gamePuppet.GetVisibleObject()` `@ 0x142ae5810` |
| threats | `AITargetTrackerComponent`, script `TargetTrackingExtension` (`156462`, `156594`) | `gamePuppet.GetTargetTracker()` `@ 0x142ae57b4` |
| reactions to stimuli | `ReactionManagerComponent` (script, `143735`) | `GetStimReactionComponent()` |
| stimuli emitted | `StimBroadcasterComponent` (script, `149801`) | `GetStimBroadcasterComponent()` |
| movement | `movePoliciesComponent` (0x600), `moveComponent` | `AddPolicies/PopPolicies/GetTopPolicies` `@ 0x14101ce98 / 0x1408d53fc / 0x142c1814c` |
| attitude | `gameAttitudeAgent` | `gamePuppet.GetAttitude()` `@ 0x140199460` |
| hit reactions | `HitReactionComponent` (script, `183465`) | `m_hitReactionComponent` |
| squads | `SquadMemberComponent` + `AISquadHelper` | `GetSquadMemberComponent()` |
| crowd behaviour | `gameCrowdMemberComponent` (0x220), script `CrowdMemberBaseComponent` (empty, `217858`) | §2.5 |
| saved component state | `gameComponentsStateSaveComponent` (0x90, no properties, no functions) | — |
| persistent state | `ScriptedPuppetPS : gamePuppetPS` (`GetIsDead/SetIsDead`, `GetWasIncapacitated`, `IsDefeatMechanicActive`) | `GetPuppetPS()`, `GetPS()` |

`AICAgent` natives (all with handlers) — the complete script surface of the
brain (`final.reds:143624-143703`): `SetBehaviorArgument/GetBehaviorArgument`,
`GetSignals() -> gameBoolSignalTable`, `SendCommand(AICommand) -> Bool` `@
0x141527538`, `CancelCommand`, `CancelCommandById`, `StartExecutingCommand`,
`StopExecutingCommand(ById)`, `CancelOrInterruptCommand(commandName,
useInheritance, success)`, `GetEnqueuedOrExecutingCommand`,
`IsCommandExecuting/IsCommandWaiting`, `GetCommandState`,
`GetHighLevelState() -> gamedataNPCHighLevelState`, `GetAIRole/SetAIRole`,
`InvokeBehaviorCallback(cbName)` `@ 0x140d668e4`, `GetLOD() -> Int32`,
`GetCombatMode()`, `GetStoryTier() -> gameStoryTier`, `GetUpdateTickCount()`,
`ForceTickNextFrame()`, `EnableCollider()/DisableCollider()`,
`GetOrCreateBlackboard(def, cache)`, combat space size.

**There is no "disable AI" native or script function.** No `SetAIEnabled`,
`DisableAI`, `ToggleSensor` or similar exists in 2.50 scripts or in the
RTTI function list; the name pool has no such string either. The switches
that do exist:

<!-- prettier-ignore -->
| Switch | Effect | Source |
| --- | --- | --- |
| `entIComponent.Toggle(on: Bool)` `@ 0x1404c94f4`, `IsEnabled()` | generic component on/off; exists on every component, including `AIHumanComponent`, `senseComponent`, `movePoliciesComponent`, `entRagdollComponent` | RTTI |
| `SenseComponent.ToggleComponent(condition)` | `SetHearingEnabled(condition)` + `Toggle(condition)` | `final.reds:155650` |
| `SenseComponent.ToggleSenses(condition)` | `Toggle(condition)` only | `155655` |
| `SenseComponent.SetHearingEnabled(enabled)` | hearing only, `@ 0x14121f764` | `155496` |
| `SenseComponent.RemoveSenseMappin()`, `RequestRemovingSenseMappin()`, `RemoveHearingMappin()` | removes the detection-eye mappin | `155546-155550` |
| `senseComponent.enableBeingDetectable` (+0x140), `forceDisableUI` (+0x141), `hideUIElements` (+0x68), `enabledSenses` (+0x69) | properties | RTTI |
| `reactionPreset = ReactionPresets.NoReaction` (record) | no stim reactions; **not enough alone on 2.5** (engine-knowledge §2) | measured by NCO |
| `gameStoryTier.Cinematic` (`AICAgent.GetStoryTier`) | `ScriptedPuppet.IsOnAutonomousAI()` is "tier == Gameplay" (`21139`): scenes take NPCs off autonomous AI | `[I]` no setter found in scripts; the scene system sets it |
| status effect type `AndroidTurnOff` | `ToggleSenses(false)`, leaves squad, `IsActive` false (`21540-21546`); androids only in vanilla | `[S]` |
| `BaseStatusEffect.Unconscious` / `Defeated` | `IsActiveInternal()` false (`21067`) — but they also play the downed animation | `[S]` |
| an always-active `AIMoveToCommand`, or an engine movement controller | the tree yields movement (engine-knowledge §2-3, what tandem's player puppet uses) | `[2.5-verified]` there |

What the vanilla game does when an NPC dies is the reference list of "things
to switch off" (`ScriptedPuppet.OnIncapacitated`, `final.reds:21797-21834`):
`GetSensesComponent().ToggleComponent(false)`, `GetBumpComponent().Toggle(false)`,
`RevokeAllTickets()`, `EnableInteraction(n"Grapple"|n"TakedownLayer"|
n"AerialTakedown"|n"NewPerkFinisherLayer", false)`, `UpdateQuickHackableState(false)`,
`RemoveLink()` (device link / security system), squad leave
(`SquadUpdate(true, AISquadType.Combat)`).

### 2.2 Recommended recipe for a guest's proxy `[I]`

In order of preference; each step is independent and testable.

1. **Spawn the proxy from a record whose brain is harmless**, as tandem's
   player puppet does: same `entityTemplatePath` and appearance as the real
   NPC's record, `reactionPreset = "ReactionPresets.NoReaction"`, no
   `Invulnerable`/`Immortal` tags. A generated TweakDB record per NPC record
   (`Character.X` → `Tandem.Proxy.X`, cloned at runtime with TweakXL) keeps
   visuals, rig, hit shapes, stats and weapons identical. Whether an NPC
   record can be given an empty/idle AI archetype (`archetypeData`, the
   behaviour tree reference) without breaking attachment is **untested**.
2. On attach: `AIHumanComponent.Toggle(false)` `[I]` (untested: the agent
   may be assumed alive by other components), `GetSensesComponent()
.ToggleComponent(false)` + `RemoveSenseMappin()`, `GetBumpComponent()
.Toggle(false)`, target tracker `ClearThreats()`, leave squads.
3. Drive movement with tandem's engine movement controller (already proven
   on NPC-record bodies); it also keeps the tree off locomotion.
4. Drive state with the eight state integers (§3.1), anim features and
   status effects from the host.
5. Keep the proxy's health pool but never let local hits change it (§4.3);
   set it from the host (§5.2).

**Host side**: nothing is switched off. **Guest side**: the above.
**Suppressed on guests**: the proxy's senses (no detection meter of its
own), stimuli reactions, squad logic, security-system link, quickhack/
takedown interactions until they are forwarded (§9).

### 2.3 `NPCPuppet.SetDisableRagdoll`

At `final.reds:125961`, the function
`SetDisableRagdoll(disableRagdoll: Bool, opt force: Bool, opt leaveRagdollEnabled: Bool)` toggles `m_npcRagdollComponent` (an `entRagdollComponent`, `isEnabled` at
+0x8b). `CanEnableRagdollComponent()` (`125938`) refuses while the entity has
god mode Invulnerable or Immortal, a status effect tagged `DisableRagdoll`,
or is a boss/MaxTac not yet incapacitated. `NPCPuppet.OnGodModeChanged()`
(`125928`) re-toggles it. **A proxy made Immortal to protect its health
pool loses its ragdoll** — another reason to protect the pool at the damage
pipeline instead (§4.3).

### 2.4 Commands for driving a proxy (if its tree stays alive)

`AICAgent.SendCommand` accepts (RTTI, selected): `AIMoveToCommand`
(`movementTarget: AIPositionSpec` +0x68, `movementType` +0xc0,
`ignoreNavigation` +0xc4, `desiredDistanceFromTarget` +0xc8,
`finishWhenDestinationReached` +0xcc), `AITeleportCommand` (`position` +0x60,
`rotation` +0x70, `doNavTest` +0x74), `AIHoldPositionCommand` (`duration`),
`AIRotateToCommand`, `AIAimAtTargetCommand`, `AIShootCommand`
(`targetOverridePuppetRef: gameEntityReference`, `duration`, `once`),
`AIForceShootCommand`, `AIMeleeAttackCommand`, `AIThrowGrenadeCommand`,
`AIThrowGrenadeForcedCommand`, `AIUseCoverCommand` (`coverNodeRef: NodeRef`,
`forcedEntryAnimation`, `exposureMethods`), `AIMoveToCoverCommand`,
`AIStopCoverCommand`, `AIEquipCommand` (`slotId`, `itemId: TweakDBID`,
`failIfItemNotFound`, `durationOverride`), `AIUnequipCommand`,
`AISwitchToPrimaryWeaponCommand` (`unEquip`),
`AISwitchToSecondaryWeaponCommand`, `AIInjectCombatTargetCommand`,
`AIInjectCombatThreatCommand`, `AIInjectLookatTargetCommand`,
`AISetCombatPresetCommand`, `AIMountCommand/AIUnmountCommand`,
`AIUseWorkspotCommand`, `AIJoinCrowdCommand`, `AIAssignRoleCommand`,
`AIFollowTargetCommand`, `AIPatrolCommand`, `AIScanTargetCommand`.
`AICombatRelatedCommand` adds `immediately: Bool` (+0x60).

`[I]` A "command-driven proxy" (brain alive, host sends the same commands) is
a fallback for things an AI-off body cannot do alone (enter cover with the
right animation, equip a weapon, shoot VFX). It is not authoritative: use it
for looks only.

### 2.5 Crowd NPCs versus full AI puppets

- `gamePuppet.IsCrowd()` (`final.reds:19256`, native) and
  `ScriptedPuppet.IsCrowd()` (`20958`); `gamePuppet.HasCrowdStaticLOD()`.
- `gameCrowdMemberComponent` natives: `IsInCrowd()` `@ 0x140dbc228`,
  `ChangeMoveType(name)`, `ChangeFearStage(stage: gameFearStage,
playInitAniation)`, `OnCrowdReaction(stimType)`, `TryStopTrafficMovement()`,
  `AllowWorkspotsUsage(allow)`, `SetThreatLastKnownPosition`, property
  `forceCrowdToIgnore: Bool` (+0x90).
- Crowd members are spawned and recycled by the community/population systems
  (`gameCrowdEntityReuseEvent`), get **dynamic** entity ids, and are
  simplified AI (`AIHumanComponent.WasForcedToEnterCrowd()`).
- The damage system treats them specially: `DamageSystem.ProcessCrowdTarget`
  (`85015`); crowds are skipped by data tracking (`128567`) and loot
  (`125563`).
- `AIReactionSystem` (script `ReactionSystem`, `149543`) owns crowd fear:
  `RegisterFearReaction(npc, threat)`, `AddFearSource(pos, halfExtend)`,
  `TryAndJoinTraffic`, `TryRegisteringAggressiveNPC`, `MarkDeadBody(id,
isAggressive, isInScene)`, `MarkDespawnCandidate(id)`.

`[I]` First version: crowds are host-only like everything else (guests'
crowd density forced to 0, §7.4) and proxied by `NetId` at a lower rate.
Crowd fear on a guest then comes for free from the host's AI.

---

## 3. NPC state worth replicating

### 3.1 The eight state integers (the engine's own replicated set)

`NPCStatesComponent` (script) **extends `AINetStateComponent`**, and its
getters and setters are thin wrappers over the native replicated slots
(`final.reds:167900-167980`):

<!-- prettier-ignore -->
| State | Native pair (`gameAINetStateComponent`) | Handlers (get / set) | Enum |
| --- | --- | --- | --- |
| high level | `Get/SetReplicatedHighLevelState(Int32) -> Bool` | `0x140f788c4` / `0x140dbda00` | `gamedataNPCHighLevelState` |
| upper body | `Get/SetReplicatedUpperBodyState` | `0x140d6cc68` / `0x140dbda80` | `gamedataNPCUpperBodyState` |
| stance | `Get/SetReplicatedStanceState` | `0x140fe0fcc` / `0x140dbd880` | `gamedataNPCStanceState` |
| hit reaction | `Get/SetReplicatedHitReactionModeState` | `0x141158be0` / `0x140dbd900` | `EHitReactionMode` |
| behaviour | `Get/SetReplicatedBehaviorState` | `0x142a2a208` / `0x142a2a274` | `gamedataNPCBehaviorState` |
| phase | `Get/SetReplicatedPhaseState` | `0x142a2a250` / `0x142a2a374` | `ENPCPhaseState` |
| defense mode | `Get/SetReplicatedDefenseMode` | `0x142a2a22c` / `0x142a2a2f4` | `gamedataDefenseMode` |
| locomotion | `Get/SetReplicatedLocomotionMode` | `0x14111dd20` / `0x140dbd980` | `gamedataLocomotionMode` |

Enum values (RTTI, 2.50):

- `gamedataNPCHighLevelState`: Alerted 0, Any 1, Combat 2, Dead 3, Fear 4,
  Relaxed 5, Stealth 6, Unconscious 7, Wounded 8.
- `gamedataNPCUpperBodyState`: Aim 0, Any 1, Attack 2, ChargedAttack 3,
  Defend 4, Equip 5, Normal 6, Parry 7, Reload 8, Shoot 9, Taunt 10.
- `gamedataNPCStanceState`: Any 0, Cover 1, Crouch 2, Stand 3, Swim 4,
  Vehicle 5, VehicleWindow 6.
- `gamedataDefenseMode`: DefendAll 0, DefendMelee 1, DefendRanged 2,
  NoDefend 3.
- `gamedataLocomotionMode`: Moving 0, Static 1.
- `gamedataNPCBehaviorState`: State1..State5 = 0..4.
- `ENPCPhaseState`: Phase1..Phase5 = 0..4.
- `EHitReactionMode`: Regular 0, ForceImpact 1, ForceStagger 2,
  ForceKnockdown 3, Fragile 4, Weak 5, Tough 6, Bulky 7, Unstoppable 8,
  UnstoppableTwitchMin 9, UnstoppableTwitchNone 10, StaggerMin 11,
  BulkyStaggerMin 12.
- `gamedataNPCType`: Android 0, Any 1, Cerberus 2, Chimera 3, Device 4,
  Drone 5, Human 6, Mech 7, Spiderbot 8.
- `gamedataNPCRarity`: Boss 0, Elite 1, MaxTac 2, Normal 3, Officer 4, Rare
  5, Trash 6, Weak 7.

How a change is applied locally (`final.reds:126331-126346`, `168055-168188`):

1. `NPCPuppet.ChangeHighLevelState(obj, newState)` (static; siblings
   `ChangeUpperBodyState`, `ChangeStanceState`, `ChangeHitReactionModeState`,
   `ChangeDefenseModeState`, `ChangeLocomotionMode`, `126348-126416`) builds
   an `NPCStateChangeSignal` (`168910`) and sets it on the puppet's signal
   table: `signalTable.GetOrCreateSignal(n"NPCStateChangeSignal")`,
   `Set(id, false)`, `SetWithData(id, signal)`, `Set(id, true)`.
2. `NPCStatesComponent.OnNPCStateChangeSignalReceived(signal)` calls the
   private `Change…State`, which stores the value through the native setter
   and then runs the **side effects**:
   - high level (`UpdateHighLevelState`, `168168`): `AnimFeature_NPCState
{state}` applied under `m_highLevelAnimFeatureName`; blackboard
     `PuppetState.HighLevel`; anim wrapper weight by state name
     (`GetAnimWrapperNameBasedOnHighLevelState`); `AnimationSystem
.EnterCombatMode/ExitCombatMode(entityId)` (`106801`); `OnCombat()`
     switches the sense preset (`SenseComponent.RequestMainPresetChange`),
     plays `start_combat` VO; `OnDead()`; a `gameHighLevelStateDataEvent` is
     queued on the NPC **and on the player** (`168082-168088`);
     `AIComponent.InvokeBehaviorCallback(n"OnHighLevelChanged")`.
   - upper body / stance / defense / locomotion: one anim feature each
     (`UpdateUpperBodyState` `168454`, `UpdateStanceState`,
     `UpdateDefenseMode` `168564`, `UpdateLocomotionMode` `168540`).

**Host side**: wrap the eight private `NPCStatesComponent.Change…` functions
(or poll the eight getters at snapshot time) and send changed values.
**Guest side**: call the same static `NPCPuppet.Change…State(proxy, value)`
so the proxy gets the anim features, wrappers, blackboard values and combat
mode exactly as a real NPC — provided the signal is still delivered when the
tree is off; otherwise call `SetReplicated…State` and apply the anim feature
by hand `[I]` (open question 4). **Suppressed on guests**: `OnCombat()`'s
sense-preset change and squad/VO side effects are harmless; `OnDead()` must
not run before the host's death message (§5).

The `PuppetState` blackboard (`final.reds:149518-149536`) mirrors these and
adds: `WeakSpots`, `ReactionBehavior`, `ForceRagdollOnDeath`,
`InExclusiveAction`, `SlotAnimationInProgress`, `WorkspotAnimationInProgress`,
`InAirAnimation`, `InPendingBehavior`, `HasCalledReinforcements`,
`IsBodyDisposed`, `DetectionPercentage: Float`. Read on the host with
`gamePuppet.GetBlackboard()` `@ 0x140d95e58` or
`GetHighLevelStateFromBlackboard()`.

### 3.2 Attitude

- `gameAttitudeAgent` (script `AttitudeAgent`, `final.reds:108489-108502`):
  `GetAttitudeGroup() -> CName`, `SetAttitudeGroup(group)`,
  `SetAttitudeGroupUnsavable(group)`, `GetAttitudeTowards(other) ->
EAIAttitude`, `SetAttitudeTowards(agent, attitude)`,
  `SetAttitudeTowardsAgentGroup(targetAgent, ownerAgent, attitude)`,
  `IsDangerous(other)`.
- `AttitudeSystem` (`174690-174703`): `GetAttitudeGroup(entityID)`,
  `SetAttitudeRelation(firstGroup, secondGroup, attitude)`,
  `SetAttitudeGroupRelationPersistent`, `SetAttitudeRelationFromTweak`,
  `GetAttitudeRelation`.
- `EAIAttitude`: `AIA_Friendly` 0, `AIA_Neutral` 1, `AIA_Hostile` 2.
- Helpers: `GameObject.GetAttitudeTowards(first, second)` (`11750`),
  `GameObject.GetAttitudeBetween` (`11774`), `IsFriendlyTowardsPlayer`
  (`11778`), `IsHostileTowardsPlayer` (global).
- Event: `AttitudeChangedEvent` → `GameObject.OnAttitudeChanged` (`12402`).

Attitude decides three things on a guest: whether weapons can target the
proxy (`TSF_EnemyNPC` needs `Att_Hostile`, engine-knowledge §2), whether a
hit is dropped as friendly fire (`DamageSystem.CheckForQuickExit`,
`final.reds:86453-86472`: attitude Friendly ⇒ `DealNoDamage` +
`FriendlyFireIgnored`), and nameplate colour.

**Host**: send the NPC's attitude group and its attitude towards each
player's puppet when it changes. **Guest**: `SetAttitudeGroup` on the proxy,
`SetAttitudeTowards(localV, …)`. **Host, for guests' puppets**: each guest
puppet must be in V's attitude group or enemies will ignore it (open
question 9).

### 3.3 Target tracking

`TargetTrackerComponent` natives (`final.reds:156463-156588`):
`GetTopHostileThreat(onlyVisible, out threat: TrackedLocation) -> Bool`,
`GetHostileThreats(onlyVisible)`, `GetThreats`, `ThreatFromEntity`,
`HasHostileThreat`, `AddThreat(…)`, `AddThreatOnPosition(pos, ttl)`,
`RemoveThreat(id)`, `ClearThreats()`, `MapThreat(et) -> Uint32`,
`SetThreatPersistence`, `SetThreatBaseMul`, `SetThreatAccuracy`,
`CollectTrackers(onlyHostiles, onlyInContact) -> [Entity]`,
`RegisterListener(et, AIScriptsTargetTrackingListener)`,
`PushSquadSync/PullSquadSync(AISquadType)`. Script side:
`TargetTrackingExtension.OnHit(puppet, evt)` (`23604`) adds the instigator as
a threat on every hit.

**Host**: for each NPC send the top hostile threat as a `PlayerId`/`NetId`
(the aim target). **Guest**: use it only for look-at/aim animation
(`AIInjectLookatTargetCommand` or a look-at event); do not add threats.
**Host, important**: a guest's forwarded hit must reach
`TargetTrackingExtension.OnHit` with the guest's puppet as instigator so the
NPC turns on the right player — it does if the hit goes through the pipeline
(§4.4).

### 3.4 Weapons and whether drawn

- `GameObject.GetActiveWeapon(obj) -> WeaponObject` (`final.reds:11862`),
  `ScriptedPuppet.GetWeaponRight/GetWeaponLeft(obj)` (`21349`, `21355`).
- `TransactionSystem.GetItemInSlot(obj, slotID) -> ItemObject` (`52211`)
  with `t"AttachmentSlots.WeaponRight"` / `WeaponLeft` (`22030-22037`).
- Loadout in the persistent state: `ScriptedPuppetPS.GetLeftHandLoadout()`,
  `GetRightHandLoadout()`, `GetCustomWeaponLoadout()`,
  `GetGenericMeleeLoadout()`, `GetGenericRangedLoadout()` (`24873-24889`).
- Upper body state `Equip` (5), `Aim` (0), `Shoot` (9), `Reload` (8),
  `Attack` (2) tells what the hands do.
- The engine's replicated form: `gameAttachmentSlotsReplicatedState`
  (per-slot state) and `gameWeaponsReplicatedState`.

**Host**: send (slot, ItemID TweakDBID) for `WeaponRight`/`WeaponLeft` on
change. **Guest**: put the same item in the proxy's slot
(`TransactionSystem.GiveItem` + `AddItemToSlot`, from redscript — see the
2.5 crash note in engine-knowledge §2) or `AIEquipCommand` if the tree is
alive. A spawned proxy of the same record already has the same inventory
(`gamePuppet.InitializeBaseInventory()` `@ 0x140888478`) `[I]`.

### 3.5 Cover

Stance `Cover` (1) is in the stance integer. Cover selection lives in
`CoverManager` (`GameInstance.GetCoverManager`, `final.reds:14172`;
`AICoverHelper.GetCoverManager(puppet)` `159176`) and in the cover blackboard
of `AIHumanComponent` (`coverBlackboard` +0x20). Covers are world nodes
(`NodeRef`), so `AIUseCoverCommand.coverNodeRef` is a stable name across
games `[I]`. First version: replicate position + stance + anim features and
ignore cover objects.

### 3.6 Status effects

Natives of `gameStatusEffectSystem` (RTTI; script `StatusEffectSystem`,
`final.reds:51168`):

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `ApplyStatusEffect(objID: EntityID, statusEffectID: TweakDBID, instigatorID: TweakDBID, instigatorEntityID: EntityID, applyCount: Uint32, direction: Vector4, isSavable: Bool, proxyEntityID: EntityID) -> Bool` | `0x140b04fe8` |
| `RemoveStatusEffect(objID, statusEffectID, removeCount: Uint32) -> Bool` | `0x140b04d40` |
| `RemoveAllStatusEffectOfType(objID, statusEffectType)` | `0x14063c688` |
| `GetAppliedEffects(objID, out [StatusEffect])` | `0x140e39a18` |
| `GetAppliedEffectsWithID / OfType / OfTypeName / WithTag` | — |
| `HasStatusEffect(objID, id)`, `HasStatusEffectOfType`, `HasStatusEffectWithTag(objID, tag)` | `0x1401ad058`, `0x140cd3284`, `0x140cf9ff4` |
| `RegisterListener(objID, gameIStatusEffectListener)` | `0x140e6f578` |
| `SetStatusEffectRemainingDuration(objID, id, duration) -> Bool` | `0x140c2b2a4` |

Script helpers: `StatusEffectHelper.ApplyStatusEffect(target, id, opt delay)`
and overloads with instigator (`final.reds:168953-169008`),
`RemoveStatusEffect(target, id, opt removeCount)` (`169054`),
`RemoveStatusEffectsWithTag` (`169113`), `GetAppliedEffects(target)`
(`169206`). Events on the entity: `ApplyStatusEffectEvent` →
`OnStatusEffectApplied` (`GameObject` `12246`, `ScriptedPuppet` `21498`,
`NPCPuppet` `126459`), `RemoveStatusEffect` → `OnStatusEffectRemoved`
(`12286`, `21595`, `127044`).

The CPO wire form was `(statusEffectRecordID, stackCount, source: CName)`,
filtered by the record flag `gamedataStatusEffect_Record.Replicated()`.

**Host**: wrap `NPCPuppet.OnStatusEffectApplied/Removed` (or register a
status-effect listener) and send (NetId, record, stacks, instigator).
**Guest**: `ApplyStatusEffect`/`RemoveStatusEffect` on the proxy for effects
that are **visual or gate interactions** (burning, EMP, blind, knockdown,
`Defeated`, `Unconscious`, `Grappled`, `AIQuickHackStatusEffect.BeingHacked`,
cloaked). **Suppressed on guests**: a status effect's own damage ticks — DoT
effects apply attacks through the pipeline (`hitFlag.DamageOverTime`), which
§4.3 already neutralises for proxies; effects applied locally by a guest's
own hit (`DamageSystem.ProcessStatusEffects`, `85883`) must not be applied
locally but arrive from the host (open question 6: does `Replicated()` mark
the visual subset?).

### 3.7 Animation-driving features

Beyond §3.1: anim features (`AnimationControllerComponent.ApplyFeature`),
anim wrapper weights (`SetAnimWrapperWeightOnOwnerAndItems`), anim inputs
and anim events — exactly the four lists of
`entAnimationControllerReplicatedState`. Tandem already mirrors anim
features for player puppets (pose-streaming.md); the NPC case can use the
`…ToReplicate` helpers as the host-side tap (§1.4). Hit reactions are
animation too: see §4.5.

---

## 4. The hit pipeline

### 4.1 From a trigger pull to a `gameHitEvent`

1. **Attack object**. Scripts build an `AttackInitContext {record:
Attack_Record, instigator: wref<GameObject>, source: wref<GameObject>,
weapon: wref<WeaponObject>}` (`final.reds:177624`; RTTI
   `gameAttackInitContext` size 0x40: record +0x00, instigator +0x10, source
   +0x20, weapon +0x30) and call `IAttack.Create(context) -> IAttack`
   (`52747`, static native). Subclasses: `Attack_GameEffect` (`177631`:
   `PrepareAttack(instigator) -> EffectInstance`, `StartAttackContinous()`,
   `StopAttack()`, `SetAttackPosition(pos)`, static `SpawnExplosionAttack(
record, weapon, instigator, source, position, opt duration)`),
   `Attack_Continuous`, `Attack_Projectile` (`513298`). `IAttack.StartAttack()
-> Bool` (`@ 0x14236ceec`) runs it. Weapons do this natively for bullets;
   about 50 script sites do it for melee, explosions, devices, vehicles.
2. **Game effect**. A game-effect attack runs a `gameEffectInstance`
   (`GameInstance.GetGameEffectSystem(game).CreateEffectStatic(effectName,
effectTag, instigator, opt weapon)` `@ 0x1410ae958`, `CreateEffect(
gameEffectRef, instigator, weapon)` `@ 0x140c0e9c4`, then `Run()` `@
0x1410f2e3c`; shared data via `GetSharedData()` + `EffectData.SetVector/
SetEntity/…`). Its object filters and executors are native classes
   (`gameEffectObjectFilter_NoInstigator`, `_NoPlayer`, `_NoPuppet`,
   `_NotAlive`, `_OnlyNearest`, `_NoInstigatorIfPlayerControlled`, …;
   `gameEffectExecutor_HitReaction`, `_DamageProjection`, `_SendStimuli`,
   scripted ones via `gameEffectExecutor_Scripted`).
3. **Projectiles**: `gameprojectileComponent` (0x920), events
   `gameprojectileShootEvent`, `gameprojectileHitEvent`,
   `gameprojectileBroadPhaseHitEvent`; `gameProjectileSystem`.
4. **The event**. The effect (or the projectile script) sends a
   `gameHitEvent` (RTTI `gameeventsHitEvent`, size 0xf0) **to the target
   entity**:

   | Field                                                      | Offset |
   | ---------------------------------------------------------- | ------ |
   | `attackData: AttackData`                                   | +0x40  |
   | `target: wref<GameObject>`                                 | +0x50  |
   | `hitPosition: Vector4`                                     | +0x60  |
   | `hitDirection: Vector4`                                    | +0x70  |
   | `hitComponent: wref<IPlacedComponent>`                     | +0x80  |
   | `hitColliderTag: CName`                                    | +0x90  |
   | `hitRepresentationResult: gameQueryResult` (`hitShapes[]`) | +0xa0  |
   | `attackPentration: Float`                                  | +0xc8  |
   | `hasPiercedTechSurface: Bool`                              | +0xcc  |
   | `attackComputed: gameAttackComputed`                       | +0xd0  |
   | `projectionPipeline: Bool` (script field)                  | +0x00  |

   Subclasses: `gameProjectedHitEvent` (damage preview while aiming),
   `gameVehicleHitEvent`, `gameRagdollHitEvent` (`impactForce`, `speedDelta`,
   `heightDelta`), `gameTargetHitEvent`/`gameTargetDamageEvent` (sent to the
   **instigator**). A miss is a `gameMissEvent`.

   `AttackData` (`final.reds:52303`; RTTI `gamedamageAttackData` 0xd0):
   native `attackType: gamedataAttackType` +0x40, `instigator` +0x48,
   `source` +0x58, `weapon` +0x68, `attackDefinition: IAttack` +0x78,
   `attackPosition` +0x90, `weaponCharge` +0xa0, `numRicochetBounces`,
   `numAttackSpread`, `attackTime`, `triggerMode`; script `flags:
[SHitFlag]`, `statusEffects: [SHitStatusEffect]`, `hitType`,
   `vehicleImpactForce`, `minimumHealthPercent`, `additionalCritChance`,
   `randRoll`, `hitReactionMin/Max`. Accessors are script functions
   (`SetInstigator` `52417`, `SetSource`, `SetWeapon`, `SetAttackDefinition`,
   `SetAttackType` `52325`, `AddFlag(flag, source: CName)`, `RemoveFlag`,
   `HasFlag`, `PreAttack()` `52406`).

   `gameAttackComputed` natives (`87605-87620`): `GetTotalAttackValue(
statPoolType) -> Float`, `GetAttackValue(damageType)`, `SetAttackValue(
value, opt damageType)`, `AddAttackValue`, `MultAttackValue`,
   `GetAttackValues() -> [Float]` (indexed by `gamedataDamageType`),
   `SetAttackValues([Float])`, `GetOriginalAttackValues()`. Scripts can
   create one: `new gameAttackComputed()` (`552779`).

5. **Entry into the damage system** — all script, all wrappable:

   ```
   GameObject.OnHit(evt: gameHitEvent)            final.reds:12315  (cb)
     └ ProcessDamagePipeline(evt)                  12406
         └ DamageSystem.QueueHitEvent(evt, this)   84282  native @ 0x14098c6ec
   ScriptedPuppet.OnHit  23591 → super; NPCPuppet.OnHit  128282 → super
   GameObject.OnVehicleHit(gameVehicleHitEvent)    12386 → QueueHitEvent
   GameObject.OnHitProjection(gameProjectedHitEvent) 12398 → StartProjectionPipeline
   GameObject.OnMiss(gameMissEvent)                12320 → QueueMissEvent
   ```

### 4.2 Inside `DamageSystem` (`final.reds:84277-87603`)

`gameDamageSystem : gameIDamageSystem : gameIReplicatedGameSystem` (0x218).
Natives: `QueueHitEvent(evt, receiver)` `@ 0x14098c6ec`,
`QueueMissEvent(evt)` `@ 0x14098d1fc`, `StartProjectionPipeline(evt)` `@
0x142b38eb0`, `RegisterListener/UnregisterListener` `@ 0x140ca55bc /
0x140ca5464`, `RegisterSyncListener/UnregisterSyncListener`,
`ProcessSyncStageCallbacks(stage, hitEvent, pipelineType)` `@ 0x14098cea8`,
`ProcessSyncStageMissCallbacks`, static `GetDamageModFromCurve`.

Everything else is **script** (so `@wrapMethod(DamageSystem)` works on each
step). `QueueHitEvent` ends up calling, on a job thread (engine-knowledge §8):

```
ProcessPipeline(hitEvent, cache)                      84290
  sync callbacks PreProcess
  PreProcess(hitEvent, cache) -> Bool                 84558
    ConvertDPSToHitDamage, CalculateDamageVariants, CacheLocalVars,
    ModifyHitFlagsForPlayer,
    CheckForQuickExit -> return false                 86416  (no attackData/target/source,
                                                      own vehicle, self damage, friendly fire)
    InvulnerabilityCheck  -> flag DealNoDamage        84846
    ImmortalityCheck      -> flag ImmortalTarget      84876
    DeathCheck            -> DealNoDamage if dead     84886
    ModifyHitData                                     84603  (reduction, localized damage,
                                                      instant kill, dodge, evasion, mitigation, …)
  sync callbacks Process
  Process(hitEvent, cache)                            84914  (returns at once if DealNoDamage)
    ProcessPlayerFixedPercentageOverride, device/grenade explosion rules,
    CalculateSourceModifiers / TargetModifiers / SourceVsTarget / GlobalModifiers,
    ProcessCrowdTarget, ProcessVehicleTarget, ProcessVehicleHit, ProcessRagdollHit,
    ProcessTurretAttack, ProcessDeviceTarget, ProcessQuickHackModifiers,
    ProcessOneShotProtection,
    if !projectionPipeline: DealDamages(hitEvent)     85771
        forReal = !GameInstance.GetRuntimeInfo(…).IsClient()
        StatPoolsManager.ApplyDamage(hitEvent, forReal, out resourcesLost)   218915
        SendDamageEvents(hitEvent, resourcesLost)     85778
            gameTargetDamageEvent  → instigator
            gameDamageReceivedEvent{hitEvent,totalDamageReceived} → target (if > 0)
  sync callbacks ProcessHitReaction
  ProcessHitReaction(hitEvent)                        84938
    hitEvent.target.ReactToHitProcess(hitEvent)       12410  → OnHitUI, OnHitAnimation,
                                                      OnHitSounds, OnHitVFX
  sync callbacks PostProcess
  PostProcess(hitEvent)                               85801
    ProcessStatusEffects, ProcessReturnedDamage, DamageManager.PostProcess,
    poise damage (RequestChangingStatPoolValue Poise) or
    SendDamageRequestToPreventionSystem (police heat for player hits)
```

`StatPoolsManager.ApplyDamage` (`218915-219154`): clamps per-hit damage on
bosses/MaxTac (`MaxPercentDamageTakenPerHit`), handles protection-layer hit
shapes, then per damage type `ApplyDamageSingle`: overshield, armor, and
health. If the damage exceeds current health it sets
`hitFlag.WasKillingBlow` (or leaves 1 HP for finisher grace /
`CannotKillPlayer`); **`DrainStatPool(hitEvent, Health, value)` runs only
`if forReal`** (`219136-219145`). The values lost are returned either way.

`hitFlag` (RTTI, selected): IgnoreImmortalityModes 1, FriendlyFire 2,
DisableSounds 3, DisableVFX 4, CannotReturnDamage 5, WasBlocked 9,
WasDeflected 10, WasDodged 11, WasEvaded 12, WasMitigated 13, Kill 14,
DontShowDamageFloater 15, **DealNoDamage 16**, **CannotModifyDamage 17**,
Headshot 18, CriticalHit 19, FinisherTriggered 20, DamageNullified 21,
Nonlethal 22, WasKillingBlow 23, ProcessDefeated 24, Defeated 25,
SilentKillModifier 26, **DeterministicDamage 27**, WeakspotHit 28,
StealthHit 29, DealtDamage 31, ImmortalTarget 32, CanDamageSelf 33,
SuccessfulAttack 34, DisableNPCHitReaction 36, VehicleDamage 37,
RagdollImpact 40, IgnoreDifficulty 41, QuickHack 42, DamageOverTime 44,
TargetWasAlreadyDeadNoStatPool 49; project flags from 100000
(FriendlyFireIgnored 100007, ForceNoCrit 100008, ForceDismember 100010,
BreachHit 100019, …).

`gamedataAttackType`: ChargedWhipAttack 0, Direct 1, Effect 2, Explosion 3,
ForceKill 4, GuardBreak 5, Hack 6, Melee 7, PressureWave 8, QuickMelee 9,
Ranged 10, Reflect 11, StrongMelee 12, Thrown 13, WhipAttack 14.

**Listeners** (`final.reds:84217-84275`, `87544-87580`):

```
native class ScriptedDamageSystemListener extends IDamageSystemListener {
  protected func OnHitTriggered(hitEvent: gameHitEvent);
  protected func OnMissTriggered(missEvent: gameMissEvent);
  protected func OnHitReceived(hitEvent: gameHitEvent);
  protected func OnPipelineProcessed(hitEvent: gameHitEvent);
}
DamageSystem.RegisterListener(listener, registereeID: EntityID,
    callbackType: gameDamageCallbackType, opt pipelineType: DMGPipelineType)
DamageSystem.RegisterSyncListener(listener, registereeID, callbackType,
    stage: gameDamagePipelineStage, opt pipelineType)
```

- `gameDamageCallbackType`: HitTriggered 0 (registeree is the instigator),
  HitReceived 1 (registeree is the target), PipelineProcessed 2,
  MissTriggered 3.
- `gameDamagePipelineStage`: PreProcess 0, Process 1, ProcessHitReaction 2,
  PostProcess 3.
- `gameDamageListenerPipelineType` (script `DMGPipelineType`): Damage 1,
  ProjectedDamage 2, All 0xFFFFFFFF.
- Listeners are **per entity id**. Vanilla use: `GenericHitPrereq`
  (`83994-84035`).

### 4.3 Guest: a local hit on a proxy must not be applied

Three interception points, from earliest to latest:

<!-- prettier-ignore -->
| Point | How | Keeps | Loses |
| --- | --- | --- | --- |
| `NPCPuppet.OnHit` (wrap, don't chain) | drop the event for proxies | nothing local | hit reaction, blood, numbers, computed damage |
| add `hitFlag.DealNoDamage` in a `PreProcess` sync listener or an `OnHit` wrap | `Process` returns at once | hit reaction/VFX (`ReactToHitProcess` still runs for non-players) | the computed damage (modifiers never run) |
| **wrap `DamageSystem.DealDamages`** | for a proxy target call `StatPoolsManager.ApplyDamage(hitEvent, false, lost)` then `SendDamageEvents` | **the full computed hit** (armor, crit, headshot, weak spot, range, perks), reactions, VFX, numbers | nothing; the pool is untouched |

The third is what the engine's own client did (`forReal = !IsClient()`), and
it yields exactly what the host needs. `[I]` Recommended:

- Wrap `DamageSystem.DealDamages(hitEvent)` (private final, script). If
  `hitEvent.target` is a proxy: `forReal = false`. If additionally
  `hitEvent.attackData.GetInstigator()` is the local V (or something V owns:
  its vehicle, turret, grenade — `IsPlayerControlled()`, quickhack source),
  **enqueue** a `Hit` for the net thread: target `NetId`/static hash, damage
  per `gamedataDamageType` from `attackComputed.GetAttackValues()` after
  `ApplyDamage` (post-mitigation, which is what `resourcesLost` holds),
  `attackData.GetAttackType()`, the flags that matter (Headshot,
  CriticalHit, WeakspotHit, StealthHit, Nonlethal, QuickHack,
  DamageOverTime, ForceDismember), hit shape name
  (`hitRepresentationResult.hitShapes[0]`), hit position relative to the
  target, direction, attack record id, weapon item id.
- Hits on a proxy from anything else (another proxy, a device, the
  environment) are computed for looks and **not** forwarded: the host has
  its own copy of that event.
- The hook runs on a job thread: enqueue only (AGENTS.md threading rule).

Also to neutralise on guests for proxy targets, since they change state
outside `DealDamages`: `DamageSystem.PostProcess` → `ProcessStatusEffects`
(applies on-hit status effects locally, `85883`), poise drain (`85841`),
`SendDamageRequestToPreventionSystem` (police heat is the host's, `85856`);
`ProcessInstantKill` (`84767`) and `ProcessOneShotProtection` (`85069`) in
the pre/process stages; weak-spot destruction (`WeakspotObject`, `182179`,
has its own health pool `WeakspotHealth`); `ScriptedPuppet
.PuppetDamagePipelineFinalized` (`23601`) → `TargetTrackingExtension.OnHit`
(harmless with the tracker cleared); `RPGManager.AwardExperienceFromDamage`
(XP is per player and local — keep).

What V's own game legitimately keeps: damage numbers (`OnHitUI` →
`DisplayHitUI`, `12435-12451`), hit markers, XP, weapon perks procs on V.
With latency the numbers are predictions; the host's result corrects the
health bar.

### 4.4 Host: applying a hit "from" a guest

The instigator must be **the guest's puppet on the host** so that threat,
aggro, police heat, kill credit and "killed by" all point at the right
player. Options:

1. **Through the pipeline** (keeps reactions, stagger, dismemberment, death
   animation direction, target tracking). Pattern used by vanilla scripts
   (`final.reds:253452-253469`, `552776-552800`, `127798-127820`):

   ```
   let evt = new gameHitEvent();
   evt.attackData = new AttackData();
   evt.target = npc;
   ctx.record = TweakDBInterface.GetAttackRecord(attackRecordId);
   ctx.instigator = guestPuppet; ctx.source = guestPuppet; ctx.weapon = weaponOrNull;
   evt.attackData.SetAttackDefinition(IAttack.Create(ctx));
   evt.attackData.SetInstigator(guestPuppet);
   evt.attackData.SetSource(guestPuppet);
   evt.attackData.SetAttackType(type);
   evt.attackData.AddFlag(hitFlag.DeterministicDamage, n"tandem");
   evt.attackData.AddFlag(hitFlag.CannotModifyDamage, n"tandem");
   evt.hitPosition = …; evt.hitDirection = …;
   ArrayPush(evt.hitRepresentationResult.hitShapes, shape);   // optional
   GameInstance.GetDamageSystem(game).QueueHitEvent(evt, npc);
   ```

   The damage the guest computed has to survive the host's own modifiers.
   `[I]` Two ways: (a) set `evt.attackComputed = new gameAttackComputed()`
   with `SetAttackValues(values)` and the two flags above (vanilla's
   `DeterministicDamage`/`CannotModifyDamage` skip crit and several
   modifiers — `86769-86774`, `86704` — but **not all**: armor, level
   difference and difficulty scaling have their own checks, e.g.
   `IgnoreDifficulty` at `87539`); or (b) register a sync listener / wrap
   `DealDamages` on the host and, for events tagged as forwarded (flag source
   `n"tandem"`), overwrite `attackComputed.SetAttackValues(values)` just
   before `StatPoolsManager.ApplyDamage`. (b) is exact and still runs
   reactions. Whether `QueueHitEvent` recomputes `attackComputed` from the
   attack record when one is already set is **unknown** (open question 1).

   `CheckForQuickExit` requires `attackData.GetSource()` to be defined
   (`86427`) and drops friendly fire: the guest puppet must not be Friendly
   to the NPC, or add `hitFlag.FriendlyFire`.

2. **Straight to the pool** (no reaction, simplest, what CAMP did):
   `StatPoolsSystem.RequestChangingStatPoolValue(Cast<StatsObjectID>(npc
.GetEntityID()), gamedataStatPoolType.Health, -damage, guestPuppet,
false, false)` — the instigator parameter still credits the kill
   (`NPCDeathListener` → `gamePotentialDeathEvent.instigator =
npc.GetLastHitInstigator()` — which comes from hit history, **not** from
   the pool request, `203292-203296`; so kill credit needs the pipeline or
   `NPCPuppet.SetMyKiller(guestPuppet)` `129587`).

3. **Kill outright**: `NPCPuppet.Kill(opt instigator, opt skipNPCDeathAnim,
opt disableNPCRagdoll)` (`125843`).

Host bounds every forwarded hit (range, rate, plausible damage) as in §8.

**Host side**: receive `Hit`, validate, apply as above on the game thread,
broadcast the result (§5). **Guest side**: §4.3. **Suppressed**: nothing on
the host; the guest puppet's own god modes must not block its being an
instigator.

### 4.5 Hit reactions (stagger, knockdown) on proxies

`HitReactionComponent` (script, `final.reds:183465`) decides the reaction
from accumulated damage vs thresholds and the hit-reaction mode, then sets an
anim feature and may ragdoll. On a guest it runs for locally computed hits
(prediction) and is missing for hits by other players. `[I]` Replicate the
host's reaction as an event: wrap the function that applies the reaction
anim feature on the host (to be located in `HitReactionComponent`:
`OnHitReactionCumulativeDamageUpdate` `184485`, `UpdateDefeated`), send
(NetId, reaction type, direction, zone), apply the same anim feature on the
proxy. The engine's own executor is `gameEffectExecutor_HitReaction`. Open
question 5.

---

## 5. Health and death

### 5.1 `gameStatPoolsSystem` (RTTI; script `StatPoolsSystem`, `final.reds:83540`)

<!-- prettier-ignore -->
| Function | Handler |
| --- | --- |
| `GetStatPoolValue(objID: StatsObjectID, statPoolType, opt perc: Bool = true) -> Float` | `0x1404ca364` |
| `GetStatPoolMaxPointValue(objID, type) -> Float` | `0x1409b2094` |
| `HasStatPoolValueReachedMin(objID, type) -> Bool` | `0x140c49d20` |
| `HasStatPoolValueReachedCustomLimit(objID, type) -> Bool` | `0x142ac97f8` |
| `RequestSettingStatPoolValue(objID, type, newValue: Float, instigator: wref<GameObject>, opt perc: Bool, opt ignoreCustomLimit: Bool)` | `0x1405f0bfc` |
| `RequestSettingStatPoolValueIgnoreChangeMode(objID, type, newValue, instigator, opt perc)` | `0x140567674` |
| `RequestSettingStatPoolValueCustomLimit(objID, type, newValue, instigator, opt perc)` | `0x1410f3678` |
| `RequestChangingStatPoolValue(objID, type, diff: Float, instigator, forceChunkTransfering: Bool, opt perc: Bool, opt ignoreCustomLimit: Bool)` | `0x140ced80c` |
| `RequestSettingStatPoolMinValue / MaxValue(objID, type, instigator)` | `0x14184adc0`, `0x142ac9eb0` |
| `RequestSettingModifier(objID, type, modType: gameStatPoolModificationTypes, modifier: StatPoolModifier)`, `RequestSettingModifierWithRecord`, `RequestResetingModifier`, `GetModifier` | regen/decay |
| `RequestRegisteringListener(objID, type, listener: IStatPoolsListener)` / `RequestUnregisteringListener` | `0x140ce72bc`, `0x1402f033c` |
| `RequestAddingStatPool(objID, recordID, opt force)`, `RequestRemovingStatPool`, `IsStatPoolAdded`, `ToPercentage`, `ToPoints` | — |

Note the default of `perc` differs by caller: `GetStatPoolValue(id, Health,
false)` returns points, and `Revive` uses `RequestSettingStatPoolValue(…,
percAmount, null, true)` (`final.reds:45327`). Always pass `perc` explicitly.

`gamedataStatPoolType` (RTTI): Health **17**, Armor 3, Poise 27, Overshield
24, Memory 22, Stamina 35, Oxygen 25, WeakspotHealth 41, QuickHackUpload 31,
QuickHackDuration 30, CallReinforcementProgress 9, LeftArmHealth 20,
LeftLegHealth 21, RightLegHealth 33, HitShapeArmor 18, Fear 14, CPO_Armor 45.

Listeners (`final.reds:95252-95258`):
`ScriptStatPoolsListener.OnStatPoolValueChanged(oldValue, newValue,
percToPoints)`, plus callbacks seen in subclasses: `OnStatPoolAdded()`,
`OnStatPoolMinValueReached(value)`, `OnStatPoolCustomLimitReached(value)`,
`OnStatPoolValueReached(old, new, percToPoints)`;
`CustomValueStatPoolsListener.SetValue(valuePerc)` `@ 0x14117f4d4` for a
threshold.

### 5.2 Health of a proxy

**Host**: one `ScriptStatPoolsListener` per replicated NPC on Health (and
Poise/WeakspotHealth if shown), or read `GetStatPoolValue(id, Health, true)`
in the snapshot; send percent (levels and difficulty may differ per game —
CAMP used per-mille). **Guest**: `RequestSettingStatPoolValue(proxyId,
Health, percent, null, true)`; SET, never subtract. A guest must also stop
the proxy's own regeneration: `GetModifier(…, Regeneration, mod);
mod.enabled = false; RequestSettingModifier(…)` (the pattern of
`ScriptedPuppet.HandleDeath`, `final.reds:19579-19597`).

**Trap**: setting health to 0 on a proxy triggers the whole local death flow
(§5.3). That is useful (it is how to "force" a death) but it must only
happen on the host's death message, so health updates below the defeat limit
must be clamped on guests until the death/defeat message arrives.

### 5.3 The death flow of an NPC (vanilla)

```
Health pool reaches the custom limit (0.1 % if the defeat mechanic is active, else 0)
  NPCDeathListener.OnStatPoolCustomLimitReached           final.reds:203273
    npc.MarkForDeath()   if BaseStatusEffect.ForceKill or WorkspotStatus.Death
    npc.MarkForDefeat()  otherwise
    queue gamePotentialDeathEvent{instigator = npc.GetLastHitInstigator()}
  NPCPuppet.OnPotentialDeath(evt)                          128687
    SetMyKiller(evt.instigator); squad VO
    if IsDefeatMechanicActive():
        IsAboutToDie()            → Kill()
        nonlethal hit             → BaseStatusEffect.Unconscious
        always                    → BaseStatusEffect.Defeated,
                                    BaseStatusEffect.InvulnerableAfterDefeated
        fist fight                → BaseStatusEffect.DefeatedWithRecover
  status effect Defeated → DefeatedEvent → ScriptedPuppet.OnDefeated   21679
    HandleDefeated(): FindAndRewardKiller(gameKillType.Defeat), OnIncapacitated(),
                      SquadUpdate(leave), m_hitReactionComponent.UpdateDefeated()   19617

Health pool reaches 0 (min)
  NPCDeathListener.OnStatPoolMinValueReached               203284  (drones: MarkForDeath)
  engine sends gameDeathEvent{instigator}  [I: native, from the stat pool system]
  NPCPuppet.OnDeath(evt)                                   128622
    super → ScriptedPuppet.OnDeath → HandleDeathByTask(instigator)   21683, 11479
        → (PostPhysics task) HandleDeath(instigator)       19579
            disable Health regeneration
            FindAndRewardKiller(gameKillType.Normal, instigator)   → XP, bounty, KillRewardEvent
            OnDied(): remove Defeated, GetPuppetPS().SetIsDead(true), OnIncapacitated()   21687
    m_npcRagdollComponent.Toggle(true)
    TriggerEvent(n"RequestDeathAnimation")
    ClearDefeatAndImmortality()                            128598
    unregister death and poise listeners
    AISquadHelper.NotifySquadOnIncapacitated; AIComponent.InvokeBehaviorCallback(n"OnDeath")

NPCPuppet.OnIncapacitated()                                ≈128880-128937
    ReactionSystem.MarkDeadBody(id, isAggressive, isInScene)
    ragdoll: Toggle(true) / HandleRagdollOnDeath(…)
    super.OnIncapacitated() (21797: senses off, interactions off, loot interaction, links)
    ProcessLoot()                                          (native, §8)
    SetSenseObjectType(gamedataSenseObjectType.Deadbody)
```

Predicates: `ScriptedPuppet.IsDead()` = `HasStatPoolValueReachedMin(Health)`
(`21094`); `IsDeadNoStatPool()` = `GetPuppetPS().GetIsDead()` (`21101`);
`ScriptedPuppet.IsDefeated(obj)` (`20986`), `IsUnconscious`, `IsAlive(obj)`
(`20979`), `IsActive()` = not dead, defeated, unconscious, turned off or
incapacitated (`21067`); `NPCPuppet.IsAboutToDie/IsAboutToBeDefeated/
IsAboutToDieOrDefeated()` (`125872-125886`).

Forcing functions:

<!-- prettier-ignore -->
| Function | Line | Effect |
| --- | --- | --- |
| `NPCPuppet.Kill(opt instigator, opt skipNPCDeathAnim, opt disableNPCRagdoll)` | 125843 | returns if stat `IsInvulnerable > 0`; `MarkForDeath()`, `SetIsDefeatMechanicActive(false)`, then `ScriptedPuppet.Kill` |
| `ScriptedPuppet.Kill(…)` | 21286 | `RequestSettingStatPoolValueIgnoreChangeMode(Health, 0.0, instigator)` |
| `ScriptedPuppet.SoftKill(…)` | 21306 | health to 0 (or 1 with a second heart) via `RequestSettingStatPoolValue` |
| `NPCPuppet.MarkForDeath()` / `MarkForDefeat()` | 125868, 125876 | set `m_shouldDie` / `m_shouldBeDefeated` only |
| `NPCPuppet.SetIsDefeatMechanicActive(active, opt isInitialisation)` | 125895 | custom Health limit 0.1 or 0.0 |
| `ScriptedPuppet.SendDefeatedEvent(obj)` | 21645 | queues `DefeatedEvent` |
| `StatusEffectHelper.ApplyStatusEffect(npc, t"BaseStatusEffect.Defeated" / "Unconscious" / "ForceKill")` | — | the vanilla ways to defeat / knock out / force-kill |
| `ScriptedPuppet.SetSkipDeathAnimation(value)` | 23165 | no death animation |
| `gamePuppet.HideIrreversibly()` | 19392 | native `@ 0x142ae5918`: corpse removed from view |
| `ResurrectEvent` → `OnResurrect` | 21836 | un-defeat (used for `DefeatedWithRecover`) |

`gameeventsDeathEvent` (0x50): `instigator: whandle:gameObject` +0x40.
`gamePotentialDeathEvent`: `instigator` (`final.reds:203299`). Scripts create
`new gameDeathEvent()` only for devices and vehicles (`98536`, `492130`,
`509625`), never for puppets.

**Host**: after the fact, send one reliable `NpcDied` / `NpcDefeated`
message: NetId, kind (dead / defeated / unconscious), killer `PlayerId` or
`NetId`, last hit (type, direction, zone, flags `ForceDismember`…),
`skipDeathAnim`, ragdoll or animated death. Tap: wrap `NPCPuppet.OnDeath`
and `ScriptedPuppet.OnDefeated` (chain them), read `m_myKiller`.

**Guest**, on that message:

- dead: `proxy.Kill(killerPuppetOrNull, skipDeathAnim, disableRagdoll)`
  `[I]` — it runs the local `OnDeath` chain (ragdoll, death animation,
  senses off, interactions off, loot interaction). Requires the proxy not to
  be Invulnerable.
- defeated / unconscious: `SetIsDefeatMechanicActive(true)`, health to the
  host's percent, apply `BaseStatusEffect.Defeated` (+ `Unconscious`).

**Suppressed on guests** inside that local chain: `FindAndRewardKiller`
(`19598`, `19618`) — XP/street cred/bounty must go to the real killer only:
wrap `ScriptedPuppet.RewardKiller(killer, killType, isAnyDamageNonlethal)`
(`21851`) and let it through only when the host's message names the local
player; `TrySetPreventionCodeRedReinforcement` (`128656`, police);
`SendDataTrackingEvent` (`128562`, achievements/telemetry — let through only
for the local killer); `ProcessLoot()` (§8); `ReactionSystem.MarkDeadBody`
(crowd reactions belong to the host).

### 5.4 Ragdoll

- Component: `entRagdollComponent` (0x220), `isEnabled` +0x8b; no functions
  beyond `Toggle/IsEnabled`.
- Requests are **events on the entity**: `entRagdollActivationRequestEvent`
  (script `RagdollActivationRequestEvent`, `final.reds:188115`) with `data:
entragdollActivationRequestData {filterDataOverride: CName, type:
entragdollActivationRequestType (Default 0, Animated 1, Forced 2),
activationNoGroundThreshold: Float, activateOnCollision, applyPowerPose,
applyMomentum, calculateEarlyPositionGroundHeight: Bool}`.
- Builders: `CreateForceRagdollEvent(debugSourceName: CName)` (`188095`) =
  `CreateRagdollActivationRequestEvent(Forced, n"None", true, true, name)`
  (`188099`); `CreateRagdollApplyImpulseEvent(worldPos, impulseVal,
influenceRadius)` (`188128`); `CreateDisableRagdollEvent(name)` (`216196`).
  Usage: `npc.QueueEvent(CreateForceRagdollEvent(n"…"))`.
- Notifications: `RagdollNotifyEnabledEvent` → `OnRagdollEnabledEvent`
  (`127397`), `RagdollNotifyDisabledEvent` (`128004`), `RagdollImpactEvent`
  (`127474`), `entRagdollPutToSleepEvent`,
  `entRagdollNotifyVelocityTresholdEvent`, `UncontrolledMovementStartEvent
{ragdollOnCollision, ragdollNoGroundThreshold}` (`128951`).
- `gamePuppet.CanRagdoll()` `@ 0x141072638`; `ScriptedPuppet.CanRagdoll(obj)`
  (`20776`).
- Ragdolls deal damage: `NPCPuppet.OnStartRagdollDamageEvent` (`127798`)
  queues a `gameRagdollHitEvent` (`Attacks.RagdollImpact`), and a ragdolling
  body trips others (`127550`, `127570`). On guests these are covered by
  §4.3 (no damage to proxies) but **a proxy's ragdoll can still hurt the
  local V** `[I]` — check.
- The engine's replicated form was the body transforms + `isSleeping`
  (`entRagdollComponentReplicatedState`): ragdolls are not deterministic.

**Host**: send "ragdoll started" with the impulse, later the resting root
transform. **Guest**: `QueueEvent(CreateForceRagdollEvent(n"tandem"))` +
impulse event; when the host reports rest, correct the corpse's root if it
is far (corpses matter for looting and body carrying).

### 5.5 Dismemberment

- `DismembermentComponent.RequestDismemberment(obj: GameObject, bodyPart:
gameDismBodyPart, woundType: gameDismWoundType, opt hitPosition: Vector4,
opt isCritical: Bool, opt debrisPath: String, opt debrisStrength: Float)`
  (static, `final.reds:188532`): queues a `DismembermentEvent` (+ audio
  event) on the entity. Private natives behind it: `DoDismemberment(bodyPart,
woundType, opt strength, isCritical, opt debrisPath, opt debrisStrength)`,
  `ReportExplosion(hitPosition, strength)`, `SpawnGutsFromLastHit(path,
strength)` `@ 0x142b33df4`; `GetDismemberedLimbCount()`.
- Also reachable by event: `RequestDismembermentEvent {bodyPart,
dismembermentType, hitPosition, isCritical}` → `ScriptedPuppet
.OnRequestDismemberment` (`21481`).
- `gameDismBodyPart` (bit masks): HEAD 1, LEFT_ARM 14, RIGHT_ARM 112,
  LEFT_LEG 896, RIGHT_LEG 7168, BODY 8192. `gameDismWoundType`: CLEAN 1,
  COARSE 2, HOLE 64.
- Decided in `HitReactionComponent` (about 40 call sites, `185415-186414`).
- Side effect to keep local-only: `RequestDismemberment` applies a perk buff
  to the **local player** whenever any NPC is dismembered (`188569-188576`).

**Host**: wrap `RequestDismemberment` (static script function), send
(NetId, bodyPart, woundType, hitPosition, isCritical). **Guest**: call it on
the proxy; wrap it so the perk buff is only applied when the local player is
the killer. Guests must not dismember proxies from local hits
(`hitFlag.ForceDismember` path runs inside the reaction code: prediction
would be irreversible) — open question 5.

### 5.6 Corpse disposal

Blackboard `PuppetState.IsBodyDisposed`; `gamePuppet.HideIrreversibly()`;
body carry (`gamecarryReplicatedEntitySetAttachmentTo*`); disposal devices
(`TakedownAndDisposeBody` `final.reds:210940`, container scripts
`210097-210220` call `m_npcBody.ProcessLoot()` and `DropLootBag()`); despawn
candidates `ReactionSystem.MarkDespawnCandidate(id)`. **Host** owns corpse
lifetime: send `NpcRemoved(NetId)`; **guest** deletes the proxy
(`DynamicEntitySystem.DeleteEntity`). Carrying a body is a mount: milestone 8.

---

## 6. Damage on a guest's V

### 6.1 How NPC attacks reach a player (single-player)

The player is a `GameObject` like any other: `GameObject.OnHit` →
`QueueHitEvent` → the pipeline of §4.2 with player-specific steps:
`ModifyHitFlagsForPlayer` (`final.reds:86408`),
`ProcessPlayerIncomingDamageMultiplier` (`84831`),
`ProcessPlayerFixedPercentageOverride` (`85107`),
`ProcessGrenadeExplosionDamageToPlayer` (`85250`), `ProcessLevelDifference`
(`87465`), `ScalePlayerDamage` (`87494`, difficulty), block/deflect
(`87114-87292`), `ProcessOneShotProtection` (`85069`). `PlayerPuppet` does
not override `OnHit`; it overrides the feedback (`OnHitVFX` `107873`,
`OnHitSounds` `107895`, hit indicators) and `OnDamageReceived`.
`ReactToHitProcess` returns early for a player target that is Invulnerable
or when `DealNoDamage` is set (`12416`).

God modes (`gameGodModeSystem : gameIReplicatedGameSystem`, script
`GodModeSystem` `87673`):

| Function                                                                | Handler       |
| ----------------------------------------------------------------------- | ------------- |
| `AddGodMode(entID, gmType: gameGodModeType, sourceInfo: CName) -> Bool` | `0x1409a4048` |
| `RemoveGodMode(entID, gmType, sourceInfo) -> Bool`                      | `0x1409a3de8` |
| `HasGodMode(entID, gmType) -> Bool`                                     | `0x1409909a4` |
| `GetGodModeCount(entID, gmType) -> Uint32`                              | `0x142b1da80` |
| `GetGodModeSources(entID, gmType) -> [CName]`                           | `0x142b1db3c` |
| `ClearGodMode(entID, sourceInfo)`                                       | `0x1409a42cc` |
| `EnableOverride(entID, gmType, sourceInfo) -> Bool`                     | `0x142b1d988` |
| `DisableOverride(entID, sourceInfo) -> Bool`                            | `0x142b1d8cc` |

`gameGodModeType`: **Invulnerable 0, Immortal 1, Mortal 2** (RTTI values).
Global `GetImmortality(gameObject, out type) -> Bool` (`final.reds:87656`).
In the pipeline: Invulnerable ⇒ `DealNoDamage` (also stat
`gamedataStatType.IsInvulnerable > 0`, `84846-84860`); Immortal ⇒ flag
`ImmortalTarget`, damage lands but health stops above 0;
`hitFlag.IgnoreImmortalityModes` bypasses both. The second heart is an
**override**: `EnableOverride(player, Immortal, n"SecondHeart")`
(`243476-243490`).

### 6.2 Host: an NPC hits a guest's puppet

The guest's puppet on the host is an NPC-record entity. The host's NPCs
shoot it through the normal pipeline. `[I]` Recommended:

- On the host, wrap `DamageSystem.DealDamages`: if `hitEvent.target` is a
  **guest's puppet**, run `StatPoolsManager.ApplyDamage(hitEvent, false,
lost)` (compute, do not drain: the guest owns that health) and enqueue a
  `PlayerDamage` message for that guest: instigator (`NetId`/static
  hash/`PlayerId`), attack record, attack type, damage per type **before
  player-side mitigation** (the puppet has NPC stats, not that V's armor and
  perks — see below), flags, direction, position.
- Keep the puppet alive regardless: no death flow may run on a player's
  puppet from the host's pools (`Immortal` god mode on puppets, or the
  `forReal = false` branch above, which never drains).

The damage number problem: the host's pipeline computes with the **puppet's**
stats (an NPC record's armor/health), not the guest V's. Two choices `[I]`:
send the raw attack (record + attacker level/weapon stats) and let the
guest's pipeline compute everything against the real V (accurate, but needs
the attacker's stats on the guest — the proxy of the same record has them);
or send `attackComputed.GetOriginalAttackValues()` (pre-modifier) and let
the guest apply its own mitigation. Both mean **the guest re-runs the
pipeline**, below.

### 6.3 Guest: applying a damage event to the local V

Build the same `gameHitEvent` as §4.4 with `target = localV`, `instigator =
source = the proxy of the attacking NPC` (it exists on the guest; if it does
not, use V itself with `hitFlag.CanDamageSelf`, else `CheckForQuickExit`
drops it — `source` must be defined), the host's attack record, type,
flags, direction (drives the hit indicator and the camera kick) and
`QueueHitEvent(evt, localV)`. The guest's pipeline then applies V's armor,
perks, cyberware (`ProcessCyberwareModifiers` `86878`), block/deflect, god
modes, second heart, and plays V's hit feedback. On the game thread only
(the call is `QueueHitEvent`; the pipeline itself runs where the engine
schedules it).

Simpler fallback: `RequestChangingStatPoolValue(V, Health, -damage,
instigatorProxy, false, false)` — no indicator, no mitigation, no feedback.

**Suppressed on the guest**: damage to V from the **proxies' own attacks**
(a proxy with AI off does not attack; but a proxy's ragdoll, a proxy
vehicle, explosions the guest simulates locally for looks). Rule: on a
guest, a hit on V whose instigator is a proxy and which did not come from
the host's `PlayerDamage` is dropped (`DealNoDamage`), by a sync listener at
`PreProcess` registered for V with `HitReceived`, or in the `DealDamages`
wrap. Environmental damage (falls, drowning, own grenades, vehicle crashes
V drives) stays local: V's owner simulates V.

### 6.4 Death, second heart, and the co-op "downed" state

Vanilla flow when V's health hits 0:

1. `PlayerPuppet.OnDeath(evt)` (`final.reds:45314`): closes the radial
   wheel, applies `GameplayRestriction.BlockAllMenu`, telemetry,
   `AutoSaveSystem.InvalidateAutoSaveRequests()`.
2. The player state machine's high-level/vitals transitions
   (`HighLevelTransition`, `StartDeathEffects` `283766`): temporary unequip,
   blackboard `PlayerStateMachine.Vitals = 1` (`gamePSMVitals`: Alive 0,
   Dead 1, Resurrecting 2), death camera and animation
   (`AnimFeature_PlayerVitals`, `EvaluateSettingCustomDeathAnimation`),
   sounds `global_death_enter`, `ui_death`.
3. `SetIsResurrectionAllowedBasedOnState` (`283786`): if stat
   `ForcePreventResurrect == 0` and `HasSecondHeart` (stat
   `gamedataStatType.HasSecondHeart > 0`) and not `DeathLand` and not
   `BaseStatusEffect.ForceKill` → permanent bool `isResurrectionAllowed =
true` (then `ResurrectDecisions/ResurrectEvents`, `528747`, `529013`,
   queue a `ResurrectEvent`, `529042`). Otherwise: unless quest fact
   `block_death_menu == 1`, blackboard `PlayerStateMachine.DisplayDeathMenu =
true`, and health forced to 0.
4. `OnDisplayDeathMenu(value)` (`347380`): **returns if `IsMultiplayer()`**;
   else after `player.deathMenu.delayToDisplay` (3 s) spawns
   `MenuScenario_DeathMenu` (`334551`, `338659`), whose controller calls
   `GetSystemRequestsHandler().LoadLastCheckpoint(true)` (`335229`,
   `347642`; native `272617`).

To replace this with "downed, revive":

<!-- prettier-ignore -->
| Must be blocked | How |
| --- | --- |
| V actually dying (state machine `Dead`, death camera) | `GodModeSystem.AddGodMode(V, gameGodModeType.Immortal, n"tandem")` while in a session with ≥ 2 players (§8: the only layer that also survives scripted kills, falls, drowning) |
| `hitFlag.IgnoreImmortalityModes` attacks and `Kill()`/`ForceKill` on V | wrap `PlayerPuppet.OnDeath` as a last resort: if it fires anyway, revive (`RequestSettingStatPoolValue(V, Health, pct, null, true)`, the body of private `PlayerPuppet.Revive(percAmount)` `45327`) |
| the death menu and checkpoint reload | quest fact `block_death_menu = 1` (`283760`) — a vanilla switch; plus a wrap of `OnDisplayDeathMenu` |
| the second heart firing during a downed state | it is a god-mode override (`n"SecondHeart"`); decide whether a second heart means "self-revive once" in co-op |
| `GameplayRestriction.BlockAllMenu` on death | not applied if `OnDeath` never runs (see §8: downed deliberately without it) |
| autosave invalidation | same |

Entering downed: a Health `CustomValueStatPoolsListener` at ≈1 % on V →
apply `BaseStatusEffect.Defeated` (works on the player per §8) + restrictions
(`NoMovement`, `NoJump`, weapons), add `Invulnerable` with its own source
during the bleed-out, send `PlayerDowned`. Others see it on the puppet
(status effect or anim feature). Revive is a request to the owner
(ARCHITECTURE: "revives are requests to the owner"); the owner removes the
effects and sets health.

Enemies should stop shooting a downed player: on the host, set the downed
puppet's attitude to Neutral/Friendly for enemies or mark it
(`SetSenseObjectType(gamedataSenseObjectType.Deadbody)` as vanilla does for
corpses, `128935`) `[I]`.

---

## 7. Spawning and identity

### 7.1 Entity ids

`entEntityID` is `{hash: Uint64}` (RTTI). Natives (no handlers listed; they
are inlined intrinsics): `IsDefined(id)`, `IsDynamic(id)`, `IsStatic(id)`,
`GetHash(id) -> Uint32`, `ToDebugString`, `ToDebugStringDecimal`,
`FromHash(hash: Uint64) -> EntityID`, `ToHash(id) -> Uint64` (script:
`final.reds:14685-14696`; `FromHash/ToHash` are in the RTTI — and used by
CAMP as `EntityID.ToHash` — but not declared in the decompiled vanilla
struct, so declare them in tandem's scripts or go through Codeware).

- **Static** ids are the hash of the entity's node path in the world
  (placed NPCs, community entries, quest prefab entities): identical in
  every game (verified in practice by CAMP on 2.31, engine-knowledge §8;
  re-verify on 2.50, open question 7).
- **Dynamic** ids come from `gameDynamicEntityIDSystem` (0x140, no script
  API): a per-game counter. Never comparable across games → tandem's
  `NetId`.
- `gamePersistentID {entityHash: Uint64, componentName: CName}`: `IsDefined`,
  `IsDynamic`, `IsStatic`, `IsEntity`, `IsComponent`, `GetComponentName`,
  `ExtractEntityID(id) -> EntityID`, `ForEntity(id)`, `ForComponent(id,
component)`, `ToHash`; script `CreatePersistentID(entityID, componentName)`
  and `Cast(EntityID) -> PersistentID` (`final.reds:26204-26224`).
  `gameComponent.GetPersistentID()` `@ 0x140ddadc4`.
- `entEntity.GetEntityID()` `@ 0x140bf5c18`; `GameInstance.FindEntityByID(
game, id) -> Entity`.
- NodeRef → entity: `ResolveNodeRef(id: NodeRef, context: GlobalNodeRef) ->
GlobalNodeRef`, `ResolveNodeRefWithEntityID(id, context: EntityID)`;
  `gameEntityReference {reference: NodeRef, names: [CName],
dynamicEntityUniqueName: CName, slotName, sceneActorContextName, type}`
  with `CreateEntityReference(nodeRefString, communityEntryNames)`,
  `GetGameObjectFromEntityReference(reference, game, out target) -> Bool`,
  `GetFixedEntityIdsFromEntityReference(ref, game, out ids)`. An entity
  reference names community members as (community NodeRef, entry name) —
  the stable name of a community NPC.
- `gamePuppetBase.GetRecordID() -> TweakDBID` `@ 0x140c7fac8`;
  `GetAppearanceName()`; `gamePuppet.GetResolvedGenderName()`,
  `GetBodyType()`.

### 7.2 Who spawns NPCs

<!-- prettier-ignore -->
| Source | Engine class / node | Ids | Script control |
| --- | --- | --- | --- |
| placed spawners | `worldPopulationSpawnerNode {objectRecordId: TweakDBID +0x38, appearanceName: CName +0x40, spawnOnStart +0x48, alwaysSpawned +0x49, spawnInView +0x4a, prefetchAppearance, isVehicle}` | static | Codeware `WorldStateSystem.ActivatePopulationSpawner / DeactivatePopulationSpawner / ResetPopulationSpawner(NodeRef)`, `GetPopulationSpawner(NodeRef) -> PopulationSpawnerWrapper` (`IsActive`, `GetRecordID`, `GetAppearanceName`, `GetTransform`, `GetSpawnedEntityIDs`, …) `[C]` |
| communities (encounters, gangs, vendors, scripted crowds) | `worldCommunityRegistryNode`, `gameCommunitySystem`, `communitySpawnEntry`, phases and time periods | static per entry slot `[I]` | Codeware `GetCommunity(NodeRef) -> CommunityWrapper` (`GetEntries()`, `GetActiveEntityIDs()`), `CommunityEntryWrapper` (`IsActive`, `GetName`, `GetPhases`, `GetSpawnedEntityIDs`, `GetSpawningEntityIDs`, `GetReservedEntityIDs`), `ActivateCommunity / DeactivateCommunity(NodeRef, opt entry)`, `ResetCommunity`, `SetCommunityPhase(NodeRef, entry, phase)` `[C]` |
| quest spawn nodes | `questSpawnManagerNodeDefinition` with `questSpawnSet_NodeType`, `questSpawner_NodeType`, `questCommunityTemplate_NodeType` | as the community/spawner they activate | follow the host's quest (milestone 6) |
| ambient crowd | `gameCommunitySystem` + `gamePopulationSystem` (0x20d0, no script API); priorities `gamePopulationEntityPriority`: Quest 0, Community 1, Crowd 2 | dynamic | `CommunitySystem.ChangeDensityModifier(Float)` `@ 0x142bcd748`, `ResetDensityModifier()`, `EnableDynamicCrowdNullArea(areaLocalBBox: Box, areaLocalToWorld: WorldTransform, savable: Bool, duration: Float) -> Uint64` `@ 0x142bcd848`, `DisableCrowdNullArea(areaId)` (`final.reds:315504-315516`); quest nodes `questCrowdManagerNodeType_ControlCrowd`, `_EnableNullArea` |
| police | `gamePreventionSpawnSystem` (script `PreventionSpawnSystem`, `189163`) | dynamic | `RequestUnitSpawn(recordID, spawnTransform) -> Uint32` `@ 0x140fdfe80`, `RequestChaseVehicle`, `RequestRoadBlockadeSpawn`, `RequestAVSpawn*`, `RequestDespawn(entityID)`, `RequestDespawnAll(aggressive)`, `CancelAllSpawnRequests()`, **`TogglePreventionActive(isActive)`** `@ 0x141197534`, `TogglePreventionCrowdSpawns(toggle)`, `GetNumberOfSpawnedPreventionUnits()`, `IsEntityRegistered(id)`, `MarkAsDead(id)`, `RegisterEntityDeathCallback(scriptable, functionName, entityID)`; result struct `gameSpawnRequestResult {requestID, success, spawnedObjects}` |
| dynamic events (gang chases) | `gameDynamicSpawnSystem` (0x150): `GetNumberOfSpawnedUnits()`, `IsEntityRegistered(id)`; quest nodes `questDynamicVehicleSpawn/Despawn/DespawnAll_NodeType` | dynamic | script callbacks `SpawnRequestFinished`, `SpawnCallback`, `ChangeAttitude` |
| companions | `gameCompanionSystem`: `SpawnSubcharacter(recordID, offset, offsetDir)`, `SpawnSubcharacterOnPosition(recordID, pos)`, `DespawnSubcharacter`, `DespawnAll`, `GetSpawnedEntities` | dynamic | — |
| stubs (far, not streamed) | `gameEntityStubSystem`, `gameEntityStubComponent(PS)` (`GetSpawnerID()` `@ 0x142a257e0`), `gameEntityStubClass`: Other 0, Puppet 1, Vehicle 2 | — | none |
| mods | Codeware `DynamicEntitySystem` (below); dev-only `exEntitySpawner.Spawn/SpawnRecord/Despawn` (no handlers) | dynamic | — |

Spawner events: `gameEntitySpawnerEvent {spawnedEntityId: EntityID +0x40,
eventType: gameEntitySpawnerEventType (Spawn 2, Despawn 3, Death 4)}`;
`GameInstance.GetEntitySpawnerEventsBroadcaster()
.RegisterSpawnerEventPSListener(spawnerOrCommunityId: EntityID,
communityEntryName: CName, psListenerPersistentId, psListenerClassName) ->
Uint32` — only persistent-state listeners.

Codeware entity lifecycle callbacks (`CallbackSystem`): `Entity/Assemble`,
`Entity/Initialize`, `Entity/Reassemble`, `Entity/Attach`, `Entity/Attached`,
`Entity/AfterAttach`, `Entity/Detach`, `Entity/Uninitialize`,
`Entity/Extract` `[C]` — the uniform tap for "an NPC appeared/disappeared",
whatever spawned it (tandem's `Body.reds` already uses the assemble stage).

### 7.3 Spawning proxies by record

Codeware `DynamicEntitySystem` (`GameInstance.GetDynamicEntitySystem()`)
`[C]`: `CreateEntity(spec: DynamicEntitySpec) -> EntityID`,
`DeleteEntity(id)`, `EnableEntity/DisableEntity(id)`, `IsSpawned/IsSpawning`,
`GetEntity(id)`, tags (`AssignTag`, `GetTagged(tag)`, `DeleteTagged`),
`RegisterListener(tag, target, function)`. `DynamicEntitySpec`: `recordID:
TweakDBID` **or** `templatePath: ResRef`, `appearanceName: CName`,
`position: Vector4`, `orientation: Quaternion`, `persistState`,
`persistSpawn`, `alwaysSpawned`, `spawnInView`, `active`, `tags`. This is
what `scripts/tandem/System.reds` uses for player puppets; the id it returns
is a dynamic id local to that game, mapped to the host's `NetId` in the
proxy table.

The announcement an implementer needs from the host is the engine's own
triple (§1.2): **record `TweakDBID`, appearance `CName`, transform**, plus
tandem's `NetId`; then inventory/weapons (§3.4), health, state integers,
attitude, status effects.

### 7.4 Suppressing a guest's own spawning

Per ARCHITECTURE ("guests' own spawning is suppressed") `[I]`:

<!-- prettier-ignore -->
| Source | Guest-side switch | Risk |
| --- | --- | --- |
| ambient crowd | `ChangeDensityModifier(0.0)` (restore with `ResetDensityModifier()`); or a large `EnableDynamicCrowdNullArea` around V | does a 0 modifier fully stop spawns? does it also stop traffic? test |
| police | `PreventionSpawnSystem.TogglePreventionActive(false)`, `CancelAllSpawnRequests()`, `RequestDespawnAll(true)` | the script `PreventionSystem` must also be kept at heat 0 (milestone 4) |
| dynamic events | no switch found; they are started by quests/`gameAIDirectorSystem` | open |
| communities, placed spawners (static ids) | **two candidate designs**, below | the hard part |

For **static** NPCs the id is shared, so a guest does not need a separately
spawned proxy: the guest's own copy of the entity _is_ the proxy, provided
its AI is off and its state is driven by the host. Candidate designs:

- **A. Adopt the local copy**: let the guest's game spawn community and
  placed NPCs as usual, and on `Entity/Attach` for a puppet with a static id
  turn it into a proxy (§2.2 steps 2-5). Pro: no spawn suppression, phases
  and workspots follow the guest's own (host-followed) quest state, identity
  is free. Con: the brain must be switched off on an entity whose record we
  did not choose (step 1 of §2.2 is unavailable), and a guest may have
  entities the host does not (different streaming position, different
  community phase) → proxies with no owner must be hidden until the host
  announces them.
- **B. Suppress and re-spawn**: `DeactivateCommunity`/`DeactivatePopulation
Spawner` for everything streamed in on a guest and spawn proxies by record
  from the host's announcements. Pro: one proxy path for everything. Con:
  needs the list of community/spawner NodeRefs per sector, and quests check
  spawner state (`questSpawnerReady_ConditionType`,
  `questCharacterSpawned_ConditionType`) — breaks "followed, never run on
  their own" less if quest phases are also suppressed on guests.

`[I]` Recommendation: **A for static ids, proxies by `NetId` for dynamic
ids**, with crowd/police/dynamic spawning suppressed on guests. It matches
the identity section of ARCHITECTURE as written. Its feasibility hinges on
open question 2 (switching a vanilla-record NPC's brain off at attach).

---

## 8. Loot

### 8.1 What happens at death

- On attach, an NPC that was already incapacitated regenerates its loot:
  `GenerateLoot()`, `EvaluateLootQualityByTask()` (`final.reds:125562-125566`,
  skipped for crowds).
- On incapacitation: `ScriptedPuppet.OnIncapacitated` →
  `EnableLootInteractionWithDelay(this)` (`21806`, `22251`: a
  `LootPickupDelayEvent` after `items.dropSettings
.delayBeforeEnablingLootPickUp`, default 2 s, → `UpdateLootInteraction()`
  `23356`); `NPCPuppet.OnIncapacitated` → **`ProcessLoot()`** (`128931`).
- Weapons: `ScriptedPuppet.DropHeldItems()` (`22027`): if the record's
  `DropsWeaponOnDeath()` and the held item's record has a `DropObject()`,
  `DropWeapons()`.
- Natives of `gamePuppet` (RTTI): `GenerateLoot()` `@ 0x14088450c`,
  `GenerateLootWithStats(lootModifiers)`, `DropWeapons()` `@ 0x140884948`,
  `DropAmmo()`, `CacheLootForDropping()`, `DropLootBag()` `@ 0x142ae5498`,
  `ProcessLoot()` `@ 0x140bff794`, `IsLooted() -> Bool`,
  `WasLootGenerated() -> Bool`, `InitializeBaseInventory()`,
  static `HasLootableItems(self)`.
- `gameLootManager` (script `LootManager`, `202205`): `GenerateLoot(lootID:
TweakDBID, out itemList: [ItemModParams], opt gameNode: GameObject)` `@
0x142b016c0`, `SpawnItemDrop(obj, itemID, position, rotation) -> Bool`,
  `SpawnItemDropOfManyItems(obj, itemInstructions: [DropInstruction],
dropItemName, position, rotation)`, `SpawnThrowableItemDrop`,
  `GetDefaultDropItemName()`, `PurgeLoot()`. `DropInstruction.Create(itemID,
quantity)`.
- Loot objects: `gameItemDropObject : gameLootObject` (`isEmpty`, `isIconic`,
  `hasQuestItems`, `spawnedItemID`, `GetItemObject()`), `gameLootBag`,
  `gameLootContainerBase` (`lootTables: [TweakDBID]`, `useAreaLoot`,
  `contentAssignment`, `containerType`, `wasLootInitalized`, `IsEmpty()`),
  `gameLootSlot`.
- Looting a corpse: interaction → `ScriptedPuppet.OnInteractionUsed`
  (`22359`) → `LootAllItems` = `TransactionSystem.TransferAllItems(this,
choiceEvent.activator)` (`22557`); then `OnInventoryEmptyEvent` (`23451`),
  `OnItemRemovedEvent` (`23466`).
- Loot is **rolled locally and randomly** (`GenerateLoot`), scaled by the
  local player's level and perks (`EvaluateLootQuality`, `23368-23381`).

### 8.2 "Each player loots their own copy"

**Decided and built 2026-11-18** (../architecture/loot.md, Loot;
`scripts/tandem/Loot.reds`): per-player loot as below; dropped weapons
per player; quest and important items (Quest tag, keycards, iconics) go
to every player's inventory whoever loots them (`SharedItem`, user's rule
2026-11-18); a removed proxy corpse leaves a loot bag; what a V drops on
purpose is a shared world item the host arbitrates (`WorldItem`,
`WorldItemTaken`). Engine facts in engine-knowledge §13.19.

This falls out of the proxy design with almost no messages `[I]`:

- A proxy on a guest is a real puppet with its own inventory. When the guest
  runs the death flow from the host's message (§5.3), its own
  `ProcessLoot()` rolls a corpse inventory for that guest; the host's corpse
  has its own roll. Each player loots their local corpse; nothing is sent.
- Dropped weapons (`DropWeapons()`) spawn a local `gameItemDropObject` in
  each game: also per player.
- Therefore **do not suppress `ProcessLoot()` on guests** (correction to the
  generic rule in §5.3: loot is the exception); do suppress nothing else.

What still needs a rule:

<!-- prettier-ignore -->
| Case | Rule |
| --- | --- |
| quest items on a corpse (`hasQuestItems`) | the host's pickup advances the quest; a guest's copy must not (quests follow the host). Either strip quest-tagged items from guests' proxies or forward the pickup to the host |
| iconic weapons, unique drops | policy decision: per-player copies duplicate uniques (most co-op games allow it) |
| corpse disappears on the host (despawn, disposal) while a guest loots | the proxy is removed by `NpcRemoved`; keep it while its loot UI is open or drop a loot bag (`DropLootBag()`) before deleting |
| a guest joins after the kill | the proxy is announced already dead; `GenerateLoot()` runs at attach for an incapacitated NPC (`125562`) — the vanilla path for loading a save |
| world containers | not NPCs; static ids, each game has its own contents — out of scope here |
| shared money/XP | per player, local (authority table) |

---

## 9. Stealth, combat state, takedowns, quickhacks

### 9.1 The player's combat state is driven by NPC AI — which a guest has none of

`PlayerCombatController` (script, `final.reds:242633`) sets blackboard
`PlayerStateMachine.Combat` (`gamePSMCombat`: Default 0, InCombat 1,
OutOfCombat 2, Stealth 3): `ActivateCombat()` (`242876`), `ActivateStealth()`
(`242905`), `ActivateOutOfCombat()` (`242915`). Its inputs are flags: crouch,
and **`m_isTracked`**, set by `OnStartedBeingTrackedAsHostile(evt)` /
`OnStoppedBeingTrackedAsHostile()` (`242808-242816`), which come from the
native AI events `StartedBeingTrackedAsHostile` /
`StoppedBeingTrackedAsHostile` (`315706`, `315709`) delivered to
`PlayerPuppet.OnStartedBeingTrackedAsHostile` (`46292`) when an NPC's target
tracker takes the player as a hostile threat.

Entering combat also: swaps the health regeneration modifier
(`BaseStatPools.PlayerBaseInCombatHealthRegen`, skipped in MP),
`FastTravelSystem.AddFastTravelLock(n"InCombat")`, combat music
(`NotifyGameTone(n"EnterCombat")`, `HandleCombatMix`), chatter.
`PlayerPuppet.IsInCombat()` (`45377`); NPC side
`NPCPuppet.IsInCombat(npc)` (`130014`), `IsInCombatWithTarget` (`130026`).

**Consequence**: on a guest, proxies have no tracker, so V **never enters
combat** (no combat music, fast travel and saving allowed mid-fight,
out-of-combat regen, no stealth state either). **Host**: for each player
puppet, detect "tracked as hostile" (the puppet is an NPC, so use the NPCs'
trackers: `TargetTrackerComponent.CollectTrackers(true, false)` on the
puppet, or wrap the threat add on NPCs) and send `TrackedAsHostile(PlayerId,
on/off)`. **Guest**: queue the two events on the local V (`new
StartedBeingTrackedAsHostile()` is `importonly native` — constructing it
from script must be tested; otherwise call
`PlayerPuppet.m_combatController.OnStartedBeingTrackedAsHostile(null)` /
`OnStoppedBeingTrackedAsHostile()` through a wrapped accessor). Open
question 8.

### 9.2 Detection

- Per NPC: `SenseComponent.GetDetection(entityID) -> Float`
  (`final.reds:155506`), `AddDetection(target: SenseComponent, detection)`,
  `ResetDetection`, `SetDetectionFactor`, `SetDetectionMultiplier(entityID,
multiplier)`, `SetDetectionOverwrite(entityID)`,
  `SetDetectionAttitudeOverwrite(group)`, `IsAgentVisible(object)`,
  `GetTimeSinceLastEntityVisible(entityID)`; presets
  `SenseComponent.RequestMainPresetChange(obj, presetName)` (`155683`),
  `UsePreset(TweakDBID)`; blackboard `PuppetState.DetectionPercentage`.
- UI: sense mappins (`CreateSenseMappin`, `RemoveSenseMappin`,
  `HasSenseMappin`, `gamemappinsStealthMappin`), `forceDisableUI`,
  `hideUIElements`; the HUD's detection meter listens to the mappin.
- `senseComponent` only knows "the player" (`playerInPerception:
whandle:PlayerPuppet` +0xa0, `shouldStartDetectingPlayerCached`,
  `SetIsPlayerInterestingFromSecuritySystemPOV`): **the detection meter
  logic is hard-wired to the local player object**. On the host, NPCs detect
  a guest's puppet as an NPC target (through `SetForcedSensesTracing(
targetObjectType, attitudeToTarget)`, `SetMainTrackedObject`, attitude),
  without the player's gradual detection meter `[I]`.

**Host**: send each guest the detection value its puppet has on the NPCs
that see it (`GetDetection(puppetId)`), if it is filled for non-player
targets — open question 10. **Guest**: show it by writing
`PuppetState.DetectionPercentage` on the proxy and creating the sense mappin,
or draw tandem's own meter. First version: no stealth meter for guests,
state it.

### 9.3 Security systems

`SecuritySystemControllerPS` (`final.reds:113935`) is a device persistent
state with static ids: areas, agents, `SecuritySystemInput`/
`SecuritySystemOutput` events (`ScriptedPuppetPS.OnSecuritySystemOutput`
`24800`), `NotifySecuritySystemCombat` (`168440`), reprimands, turrets,
cameras. NPCs link to it (`GetSecuritySystem()` `24789`, `RemoveLink()`).
World-device sync is not in milestone 5 (§8: "no world-object sync at
all"). **Host** owns it; **guests** must not let proxies feed their local
copy (step 2 of §2.2: remove the link), and local cameras/turrets on a guest
reacting to V is an open problem for the devices milestone.

`AIReactionSystem`/stimuli: `gameStimuliSystem.BroadcastStimuli(effect)`,
`StimBroadcasterComponent.TriggerSingleBroadcast(obj, stimType, radius)`,
`AddActiveStimuli`. A guest's gunshots must be heard by the host's NPCs:
**host** re-broadcasts stimuli from the guest's puppet when it receives the
guest's fire/footstep/explosion events `[I]` (a puppet that visibly fires on
the host may already broadcast through its weapon — test).

### 9.4 Takedowns and grapples

- Player side is the locomotion state machine: `LocomotionTakedownDecisions`
  (`final.reds:530388`), `TakedownBeginEvents` (`533265`),
  `TakedownLeapToPrey*`, `TakedownSlideToPrey*`, `TakedownGrapplePrey*`
  (`535229`), `GrappleMount*` (`536060`), `GrappleStand*`;
  `gamePSMTakedown`: Default 0, EnteringGrapple 1, Grapple 2, Leap 3,
  Takedown 4. Helpers `TakedownUtils` (`285396`):
  `TakedownActionNameToEnum`, `SetTakedownAction`, `CleanUpGrappleState`,
  `ExitWorkspot`.
- The pair animation is a **synced workspot** on both entities:
  `WorkspotGameSystem.SendJumpToAnimEnt(owner, syncedAnimName, true)`,
  `SendForwardSignal(owner)`, `SendJumpCommandEnt(owner, entryId, instant)`
  (`285012-285028`); names `grapple_sync_kill`, `grapple_sync_shove`, boss
  variants `smasher_takedown_phase…`. NPC side: workspot tags `Grappled` →
  `BaseStatusEffect.Grappled` (`22567`), `ScriptedPuppet.IsBeingGrappled`
  (`21171`), events `StartTakedownEvent {slideTime, target, actionName}`
  (`252771`), `EndTakedownEvent` (`298433`), AI task `AITakedownHandler`
  (`457783`).
- The result is a game effect: `TakedownGameEffectHelper.FillTakedownData(
executionOwner, activator, target, effectName, effectTag, opt
statusEffect)` (`252966`) = `GetGameEffectSystem().CreateEffectStatic(
effectName, effectTag, activator)` + shared data (position, target entity)
  - `Run()`; tags `kill`, `setUnconscious`, `setUnconsciousTakedownNetrunner`,
    `setUnconsciousTakedownMassiveTarget`, `setUnconsciousAerialTakedown`
    (`285280-285332`); `DefeatTarget` (`285348`) also sends the NPC the action
    signal `takedown_defeat`.
- Interactions offered by the NPC: layers `Grapple`, `TakedownLayer`,
  `AerialTakedown`, `NewPerkFinisherLayer`, `BossTakedownLayer`,
  `MassiveTargetTakedownLayer` (`21807-21815`), built in
  `ScriptedPuppet.OnInteractionActivated` (`22262`).

**Guest** (the actor's game decides, like a shot): play the takedown locally
on the proxy (the synced workspot runs on two local entities), wrap
`FillTakedownData` so the kill/unconscious effect is not applied to the
proxy but sent as `Takedown(target, effectTag, anim)`. **Host**: validate
(distance, target alive and not in combat with someone else), play the same
synced animation between the guest's puppet and the real NPC if the puppet
can enter that workspot (it is an NPC-record entity with a player rig —
untested), else skip the animation and apply the effect: `Kill` or
`Unconscious`+`Defeated` with the guest's puppet as instigator. Meanwhile
the host's NPC must be frozen for the duration (`BaseStatusEffect.Grappled`
on it at grapple start) or its AI will turn and shoot. **Suppressed on
guests**: the takedown layers on proxies until this exists (first version:
`EnableInteraction(layer, false)` for proxies, as vanilla does for dead
NPCs).

### 9.5 Quickhacks on NPCs

Flow (`final.reds`): the quickhack panel lists `PuppetAction`s (`205378`,
`extends ScriptableDeviceAction`; `ScriptedPuppet.GetQuickHackActions`,
`TranslateChoicesIntoQuickSlotCommands` `23934`, gate `IsQuickHackAble()`
`22905`) → choosing one queues it (`ProcessRPGAction` `26605`,
`PutActionInQuickhackQueue` `26672`, RAM cost) → upload is a stat pool
(`gamedataStatPoolType.QuickHackUpload`, `HasActiveQuickHackUpload()`
`24188`) reported by `UploadProgramProgressEvent` (`120392`) →
`ScriptedPuppet.OnUploadProgressStateChanged` (`24388`): `STARTED` applies
`AIQuickHackStatusEffect.BeingHacked` (+ `BeingHackedWithTraceableHack`);
`COMPLETED` removes them and calls `OnQuickSlotCommandUsed(cmd)` (`24180`) →
`ExecuteAction(action as PuppetAction)` → `CompleteAction(game)` (`26639`):
rewards, XP (`RPGManager.AwardExperienceFromQuickhack`), then
`ProcessStatusEffects(actionEffects, game)` and `ProcessEffectors` apply the
record's `CompletionEffects` to the target. Damage hacks deal damage through
status-effect attacks → the normal pipeline with `hitFlag.QuickHack`
(`ProcessQuickHackModifiers` `85310`). NPC-on-player hacks use the same
upload machinery (`AIQuickHackAction` `250919`). UI: `HUDManager :
gameNativeHudManager` (0x5d0) owns scanner/quickhack panel targeting.

**Guest**: keep the panel, queue, RAM cost and upload local (they are V's);
at `CompleteAction` for a proxy target, do not apply `CompletionEffects`
locally: send `Quickhack(target, objectActionId = GetObjectActionID())`.
**Host**: apply the action's completion effects to the real NPC with the
guest's puppet as instigator (`ApplyStatusEffect(… instigatorEntityID =
puppet)`), which then replicate back as status effects (§3.6); the damage
they cause is the host's (so the guest's own hit forwarding must carry **0**
for `QuickHack`-flagged hits, the double-count bug of §8). **Suppressed on
guests**: traces and network effects on the local security system; `IsQuick
HackAble()` needs the proxy "active, not crowd, aggressive or connected"
(engine-knowledge §2) — attitude must be replicated first.

---

## 10. Summary

<!-- prettier-ignore -->
| Topic | Host side | Guest side | Suppress on guests |
| --- | --- | --- | --- |
| NPC brain | unchanged | harmless record or `Toggle(false)` on AI/senses/bump; engine movement controller | senses, reactions, squads, security link, takedown/hack interactions |
| state integers | wrap/poll the 8 `NPCStatesComponent` values | `NPCPuppet.Change…State(proxy, v)` (or native setters + anim feature) | `OnDead()` before the host's death message |
| attitude | send group + attitude to each player | `SetAttitudeGroup`, `SetAttitudeTowards(V, …)` | — |
| weapons | send `WeaponRight/Left` items on change | same item in the proxy's slot | — |
| status effects | wrap `OnStatusEffectApplied/Removed` | apply/remove on the proxy | on-hit effects from local hits; DoT damage |
| guest hits an NPC | build `gameHitEvent` with instigator = guest's puppet, exact damage; or pool change + `SetMyKiller` | wrap `DamageSystem.DealDamages`: `ApplyDamage(evt, false, …)`, enqueue `Hit` if V is the instigator | pool drain, poise, on-hit effects, police heat, instant kill |
| health | listener or snapshot, percent | `RequestSettingStatPoolValue(…, pct, null, true)`; regen modifier off | reaching the defeat/death limit before the host says so |
| death / defeat | wrap `NPCPuppet.OnDeath`, `OnDefeated`; send kind, killer, last hit | `proxy.Kill(…)` / apply `Defeated`(+`Unconscious`) | `RewardKiller`, data tracking, prevention, `MarkDeadBody` unless local killer |
| ragdoll | send start + impulse, then rest transform | `QueueEvent(CreateForceRagdollEvent(…))`, correct at rest | ragdoll damage to V from proxies |
| dismemberment | wrap `DismembermentComponent.RequestDismemberment` | call it on the proxy | local prediction of dismemberment; perk buff unless local killer |
| NPC hits a guest | wrap `DealDamages` for guest puppets: compute, don't drain, send `PlayerDamage` | re-run the hit on V with the proxy as instigator (`QueueHitEvent`) | hits on V by proxies that did not come from the host |
| V's death | — | `Immortal` god mode, `block_death_menu`, downed state, revive requests | death menu, checkpoint reload, `BlockAllMenu` |
| identity | static hash or assign `NetId` | static: adopt local copy; dynamic: spawn proxy by (record, appearance) | crowd density 0, prevention off |
| loot | own roll | own roll on the proxy's corpse (`ProcessLoot` runs locally) | quest items on guests' corpses |
| combat state of V | send `TrackedAsHostile(PlayerId)` | feed `PlayerCombatController` | — |
| takedown | apply effect (kill/unconscious) from the guest's puppet | play locally, forward `FillTakedownData` | effect on the proxy; layers until implemented |
| quickhack | apply completion effects from the guest's puppet | panel/RAM/upload local, forward at `CompleteAction` | local completion effects; quickhack damage in forwarded hits |

Engine-provided shapes to copy into the protocol (§1): NPC announcement =
(persistent id, record, appearance, initial transform, health, armor); AI
state = 8 × Int32; status effect = (record, stacks, source); shot = (attack
record, target, local offset, time); hit feedback = (id, damage infos,
instigator); kill feedback = (id, kill info).

## 11. Open questions for an in-game test

Each is a small probe; none needs a second player unless stated.

1. **Forwarded damage fidelity**: does `QueueHitEvent` recompute
   `attackComputed` from the attack record when the event already carries
   one with `SetAttackValues`? Which host-side modifiers still change a hit
   flagged `DeterministicDamage` + `CannotModifyDamage` (armor, level
   difference, difficulty)? Decides between §4.4 option 1a and 1b.
2. **Brain off on a vanilla record**: `AIHumanComponent.Toggle(false)` on a
   live community NPC — does it stop the tree, crash, or get re-enabled?
   Do `senseComponent.Toggle(false)` + cleared tracker suffice to make it
   inert? Does the engine movement controller attach to it? Decides design A
   vs B in §7.4.
3. **`…ToReplicate` taps**: do wrapped `ApplyFeatureToReplicate` /
   `SetInput…ToReplicate` / `PushEventToReplicate` fire for NPC combat
   animation on the host, and how often? What do `gameObject
.ReplicateAnimFeature` etc. do in single player (no-op, or local apply)?
4. **State signals on an AI-off proxy**: does `NPCPuppet.ChangeHighLevelState
(proxy, Combat)` still reach `NPCStatesComponent
.OnNPCStateChangeSignalReceived`, or must the native setters and anim
   features be driven by hand?
5. **Hit reactions and dismemberment**: which function applies the stagger/
   knockdown anim feature (to wrap on the host and replay on guests)? Can a
   guest's local pipeline be kept from dismembering a proxy while still
   playing a flinch?
6. **Status effects**: is `StatusEffect_Record.Replicated()` set on a useful
   subset (visual ones)? Does applying `Defeated`/`Unconscious` on a proxy
   with `forReal` damage off behave?
7. **Static ids on 2.50**: log `EntityID.ToHash` + `IsStatic` for the same
   community NPCs in two games (two saves at the same spot are enough) —
   identical? Are community members static per entry slot, and stable across
   community phases and respawns?
8. **Player combat state**: can a script construct and queue
   `StartedBeingTrackedAsHostile` on V, or must `PlayerCombatController` be
   driven directly? Does combat music follow?
9. **Guest puppets as targets on the host**: which attitude group makes
   enemies attack a puppet exactly when they would attack V
   (`SetAttitudeGroup` to V's group), and do they produce
   `gameHitEvent`s on it through the normal pipeline (so the `DealDamages`
   wrap sees them)? Does the puppet being an NPC record change NPC accuracy
   or cover behaviour?
10. **Detection of a puppet**: is `SenseComponent.GetDetection(puppetId)`
    filled gradually for a non-player target, or is detection instant?
11. **`IsReplicated()` / `IsControlledByLocalPeer()` today**: values for V,
    a vanilla NPC, a tandem puppet. Is there a live `netEntitySystem`? Does
    a record using `AICTreeNodePlayerControlledDefinition` /
    `gamePlayerControlledComponent` exist in TweakDB (a ready-made
    "player-controlled puppet" archetype)?
12. **Crowd suppression**: `ChangeDensityModifier(0.0)` — are crowds and
    traffic fully gone, immediately, and does anything (quests, vendors)
    depend on them? `TogglePreventionActive(false)` side effects.
13. **Proxy death**: `proxy.Kill(killer, …)` on an AI-off proxy — full death
    animation/ragdoll? Does `ProcessLoot()` give a lootable corpse? Does a
    late-announced dead proxy (spawned then killed with
    `skipNPCDeathAnim = true`) look right?
14. **Applying damage to V with a proxy as instigator**: hit indicator
    direction, block/deflect and second heart all behave? Is `source`
    required to be the same entity as the instigator?
15. **Ragdoll damage**: can a proxy's ragdoll or a proxy vehicle hurt the
    local V, and is it caught by the "instigator is a proxy" rule (§6.3)?
16. **Takedown on the host**: can a guest's puppet (NPC record, player rig)
    enter the synced takedown workspot with a real NPC?
