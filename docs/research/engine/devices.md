# Devices and world objects

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.30 Devices and world objects (2026-11-19, 2.50 scripts and RTTI dump read, not run)

What tandem's device sync (`docs/architecture/devices.md`,
`scripts/tandem/Devices.reds`) relies on. Read offline on 2026-11-19 from
the 2.50 scripts (`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`, line
numbers below) and the live RTTI dump taken earlier (`.cache/re/rtti.json`);
**nothing here has run in game yet**.

Tags, as in `engine-knowledge.md`: [2.5-untested] = read from the 2.50
game's own scripts, not yet run in game; [2.5-static] = read from the 2.50
RTTI dump (types, fields, sizes); _(inferred)_ = our reading, to confirm.

#### Identity

- `gamePersistentID` is `{entityHash: Uint64 @0, componentName: CName @8}`,
  16 bytes; `entEntityID` is `{hash: Uint64}`. [2.5-static]
- A device's state object is addressed by
  `CreatePersistentID(entityID, n"controller")` (97178; devices and
  vehicles both use the component name `controller`, 90512, 154251, 154667, 217233) plus the state's class name:
  `GamePersistencySystem.GetConstAccessToPSObject(id, className)`,
  `QueuePSEvent(id, className, evt)`, `QueuePSDeviceEvent(action)`
  (27524–27530). [2.5-untested]
- A placed entity's id is a function of the world file, not of the run:
  - scripts turn a node reference into an id with no lookup:
    `Cast<EntityID>(ResolveNodeRefWithEntityID(nodeRef, GetMyEntityID()))`
    (124862), and `CreatePersistentID(Cast<EntityID>(globalRef), …)` (292053);
  - states that are saved hold ids of other placed entities:
    `LiftControllerPS.floorIDs: array:entEntityID`, `floorPSIDs:
array:gamePersistentID`, `DoorControllerPS.openingTokens: [EntityID]`,
    all `persistent`: a save would not load on another machine otherwise.
    [2.5-static]
  - `PersistentID.IsStatic` / `IsDynamic` (26207–26209) tell placed from
    spawned.
  - _(inferred)_ so `(entityHash, class name)` names the same device in
    every game. **To verify in game**: `tools/game.sh cmd device near door`
    on two machines at the same spot prints the same id (the first thing the
    `devices-door` scenario shows: the mock player's log carries the id the
    game sent).
- Spawned devices (dynamic ids) are not the same in two games: left out
  (`TandemDevices.Follows`).

#### Where a device's state lives

- `gamePersistentState` → `gameComponentPS` → `gameDeviceComponentPS`
  (native; `markAsQuest`, `isScanned`, `exposeQuickHacks`…) →
  `SharedGameplayPS` (`m_deviceState: EDeviceStatus`, 28928) →
  `ScriptableDeviceComponentPS` (29183) → one `…ControllerPS` per kind.
  `persistent` fields are what a save stores. [2.5-untested]
- `EDeviceStatus` {DISABLED −2, UNPOWERED −1, OFF 0, ON 1, INVALID 2}
  (10473); `EDeviceDurabilityState` {NOMINAL 0, MALFUNCTIONING 1, BROKEN 2,
  DESTROYED 3} (10487); `ESecuritySystemState` {UNINITIALIZED 0, SAFE 1,
  ALERTED 2, COMBAT 3} (10407).
- What tandem reads (all public, const):

  <!-- prettier-ignore -->
  | Class | Read with | Saved fields behind it |
  | --- | --- | --- |
  | `ScriptableDeviceComponentPS` | `GetDeviceState()`, `GetDurabilityState()` (29507), `WasQuickHacked()` (30743), `IsQuickHacksExposed()` (28555), `IsGlitching()` (29665) | `m_deviceState`, `m_durabilityState`, … |
  | `DoorControllerPS` (134529) | `IsOpen()`, `IsLocked()`, `IsSealed()` (134587–134607) | `isOpened @728`, `isLocked @729`, `isSealed @730` |
  | `LiftControllerPS` (137644) | `GetActiveFloor()` (138433), `GetTargetFloor()` (137796), `IsMoving()` (138445) | `activeFloor @784`, `movementState @792` |
  | `ExplosiveDeviceControllerPS` | `IsExploded()` (121176) | `m_exploded` |
  | `SensorDeviceControllerPS` | `IsAttitudeChanged()` (153366): cameras (`SurveillanceCameraControllerPS`, 166500) and turrets (`SecurityTurretControllerPS`, 172039) | `m_isAttitudeChanged` |
  | `AccessPointControllerPS` | `IsNetworkBreached()` (119102) | `m_isBreached` |
  | `MediaDeviceControllerPS` | `GetActiveStationIndex()` (259375): radios (`RadioControllerPS`, 477468), TVs (`TVControllerPS`, 259624) | `m_activeStation` |
  | `SecuritySystemControllerPS` | `GetSecurityState()` (114109) | `m_securitySystemState` |

  Offsets from the RTTI dump. [2.5-static]

- `m_targetFloor` is not saved and is −1 once the lift arrived
  (`ChangeActiveFloor`, 137808); it starts at 0 before any ride, so it only
  means something while `IsMoving()`.
- A state object exists and handles events while its entity is streamed out
  (`LiftControllerPS.ProcessUnstreamedLiftMovement`, 138357, is the game's
  own handling of that case: "no owner entity → just change the floor").
  Whether `GetConstAccessToPSObject` returns a state for a device the save
  has never seen is not known. [2.5-untested]

#### How a device changes

- Everything done to a device is a `DeviceAction` event handled by its
  state in `On<ActionClass>(evt) -> EntityNotificationType` (26111;
  `SendThisEventToEntity` makes the entity play it). The handlers end in
  `UseNotifier(evt)` → **`Notify(notifier, action)`** (33765, 33834): one
  place almost every action passes through, whoever sent it (player,
  quickhack, quest, another device), streamed in or not. tandem wraps it to
  look at the device at once. [2.5-untested]
- Not through `Notify`: an explosive sets `SetExplodedState(true)` on its
  state from the entity when it goes off (`ExplosiveDevice.Explode`, 120621
  → 121187): wrapped too. Other entity-side writes (a lift's
  `ChangeActiveFloor` on arrival, `SetMovementState`) are caught by looking
  at the devices around V every few frames.
- **The setters quests use** are public by name:
  `ScriptableDeviceComponentPS.GetQuestActionByName(name)` (32250) gives a
  ready action for `ForceEnabled`, `ForceDisabled`, `ForcePower`,
  `ForceUnpower`, `ForceON`, `ForceOFF`, `QuestStartGlitch`,
  `QuestStopGlitch`…; doors add `ForceOpen`, `ForceClose`, `ForceLock`,
  `ForceUnlock`, `ForceSeal`, `ForceUnseal` (135033); explosives
  `ForceDetonate` (121327); lifts `GoToFloor`, `ForceGoToFloor`,
  `ForceTeleportToFloor` (138117); media devices `SetChannel` (259208).
  Queued with `QueuePSDeviceEvent`, they run the game's own path: the door
  animates and sounds, the barrel explodes. This is the same entry as
  `questDeviceManager_NodeType` (`coop-quests-world.md` §7.2). [2.5-untested]
- Side effects of the door setters (135528–135716): `ForceOpen` also
  unlocks and unseals; `ForceClose` and `ForceLock` unseal; `ForceSeal`
  closes. Hence the order in `TandemDevices.ApplyDoor`.
- Lifts: `QuestGoToFloor` moves the cabin when it is not already moving and
  not there (`LiftDevice.OnQuestGoToFloor`, 198140);
  `QuestForceTeleportToFloor` puts it there, and with no entity
  `ProcessUnstreamedLiftMovement` just changes the floor. The action's
  floor is `SetProperties(floor)` on `QuestGoToFloor` /
  `QuestForceTeleportToFloor` (199809–199826).
- Explosives: `ForceDetonate`'s state handler does nothing
  (`OnQuestForceDetonate`, 121262); the entity's sets health to 0 and runs
  `StartExplosionPipeline(this)` (120788): attack, physics pulse, effects,
  then `SetExplodedState(true)`. With no entity nothing happens, so an
  unloaded barrel is marked with `SetExplodedState(true)` directly; on
  attach `ResolveGameplayState` hides an exploded one (120519).
- An explosion's hits carry the device as **source** and whoever set it off
  as instigator (`DoAttack`, 120666: `PrepareGameEffectAttack(game,
instigator or this, this, …)`, flag source `ExplosiveDevice`).
- Cameras and turrets: `SetDeviceAttitude` (172588, public fields
  `Attitude`, `Repeat`; factory `ActionSetDeviceAttitude()`, 33755) is what
  the friendly quickhack sends; `SensorDevice.OnSetDeviceAttitude` (152428)
  does the work on the entity (`SetDeviceFriendly`) and the state only
  remembers `SetIsAttitudeChanged`. A destroyed device is
  `ForceDisableDevice()` from `Device.OnDeath` (98530): its status goes
  DISABLED.
- Detection is written around the local player: `OnOnDetectedEvent` (151512)
  calls `NotifyAboutSpottingPlayer` only for `evt.target.IsPlayer()`;
  `IsAttitudeFromContextHostile` (153828) and `SetDeviceNeutral` use
  `GetLocalPlayerMainGameObject()`. A camera in the host's game does not
  treat a guest's puppet as a player. Hence each game's own copy senses its
  own V (architecture file).
- Security system: `SetSecuritySystemState{state}` (209767) is a plain
  event its state handles (`OnSetSecuritySystemState`, 116157 →
  `QuestChangeSecuritySystemState`, 116276, with the local player as the
  culprit). With a culprit of our choosing:
  `ReportPotentialSituation(ActionSecurityBreachNotification(position, who,
ESecurityNotificationType.COMBAT | ALARM))` (114892, 30730).
- Breach: a won minigame ends in `SetIsBreached(true)` (119083, also from
  the plain event `BreachAccessPointEvent`, 119812) and
  `RefreshSlaves` (119138): each device of the network gets
  `SetExposeQuickHacks` (199912 → `OnSetExposeQuickHacks`, 33073) and the
  minigame's programs as actions with the local player as executor
  (`ProcessMinigameNetworkActions`, 119626); loot and money
  (`ProcessLoot`, `RewardMoney`) go to the local player, once
  (`m_moneyAwarded`).
- Distraction: `QuickHackDistraction` (118491) sets `m_isGlitching` and
  re-queues itself after its duration to stop
  (`OnQuickHackDistraction`, 31235); the entity starts the effect and the
  stimulus NPCs come to look at (`BasicDistractionDevice`, 120176).
  `QuestStartGlitch` / `QuestStopGlitch` play the look only.
- The engine's own leftovers of device replication: `DoorReplicatedState
{isOpen, wasImmediateChange}`, `SecurityTurretReplicatedState {isOn,
isShooting, isDead, health}`, `WindowBlindersReplicatedState {isOpen,
isTilted}` (`: gameDeviceReplicatedState`). Nothing drives them, but they
  are CDPR's answer to "what does a host send for a door": the state and
  whether to play the change — tandem's `DeviceState` and its `how`.
  [2.5-static]

#### Not devices

- Glass, covers and other breakable scenery are physics destruction
  (`gameDestructionPersistencySystem`: no script functions, no properties),
  not `ScriptableDeviceComponentPS`: no state to sync, only the moment
  they break (§13.90). [2.5-static]
- Loot containers are `gameLootContainerBase` (`coop-quests-world.md`
  §7.3): per player, nothing sent.
- Vehicles have a `ScriptableDeviceComponentPS` too (`VehicleComponentPS`):
  left to the vehicle sync.

#### To verify in game (nothing below has run)

1. The same id on two machines (Identity, above).
2. `GetConstAccessToPSObject` on a streamed-out, and on a never-visited,
   device; `QueuePSDeviceEvent` of a quest action on a streamed-out door
   (state changes, the door is open when walked up to).
3. `ForceOpen` on V's flat door, a shop shutter, a lift door (`IsLiftDoor`).
4. `QuestGoToFloor` with V aboard on the host and a puppet standing in the
   cabin; the puppet's placement flag "in a moving elevator" during the ride.
5. `ForceDetonate` damage on the host's NPCs, and that the wrap in
   `Combat.reds` keeps a device's hit off a guest's puppet.
6. `SetDeviceAttitude` with no executor (the friendly look, and that the
   camera stops filling the meter).
7. `Device.OnGameAttached` / `OnDetach` wrap cost while driving through the
   city (hundreds of attaches a minute), and the 32-a-frame look.

### 13.90 The world's objects: a ping's beams, a turret's fire, physics destruction (2026-11-20, read in final.reds and the RTTI dump, not run)

Tags as in §13.30. Line numbers: the 2.50 `final.reds`.

#### The Ping quickhack's network beams

- The quickhack on a device is the action `PingDevice` (118580); its state
  passes it to an attached slave when the device itself is not in the
  world (`OnActionPing`, 32755), else to the entity:
  **`Device.OnActionPing`** (101250) → `PulseNetwork(true)` (101258).
  On an NPC, `PuppetDeviceLinkPS.PingDevicesNetwork` (112687) queues the
  same `PingDevice` to the NPC's access points: one entry for both.
  [2.5-untested]
- `PulseNetwork` queues a `StartPingingNetworkRequest` (source: the device,
  `EPingType.SPACE`, `GetSpacePingDuration()`, 111440) to `NetworkSystem`
  (109142, a scriptable system), which runs the game effect
  `pingNetworkEffect` from the source (`AddActivePing`, 110543); its
  executor `EffectExecutor_PingNetwork` (492950) registers a link (a beam)
  from the source to each target in range. Nothing persistent but
  `SetRevealedInNetworkPing` at the pulse's end (`revealNetworkAtEnd`).
  [2.5-untested]
- A quest fact `pingingNetworkDisabled` turns `PulseNetwork` off (101260).
- `PulseNetwork` also runs from the scanner looking at a device
  (`ShouldPulseNetwork`, 100279) and from the vision pulse (100325): those
  are not the quickhack and are not shared.
- To verify in game: the beams on the second game after `TandemPulseNetwork`
  on its copy; that `OnActionPing` runs once per ping (not once per device
  of the network).

#### Physics destruction (glass, covers, breakable scenery)

- Two kinds of breakable things [2.5-static]: a component of an entity
  (`entPhysicalDestructionComponent`, 832 bytes: `destructionParams`,
  `destructionLevelData`, `isEnabled`, `systemsToNotifyFlags`; script
  functions `IsFractured()` and `TriggerDestruction(velocity)`, 290562) and
  world nodes with no entity (`worldPhysicalDestructionNode`,
  `worldInstancedDestructibleMeshNode`, `worldBakedDestructionNode`).
  `gameDestructionPersistencySystem` has no script function and no
  property.
- An entity hears of its own component breaking:
  `PhysicalDestructionEvent {componentName, levelOfDestruction}` (290568),
  handled by `GameObject.OnPhysicalDestructionEvent` (13897, a sound
  distraction stim) and overridden without a call to it in
  `DestructibleMasterDevice` (279959), `DisplayGlass` (478292),
  `BillboardDevice` (486393), `BaseDestructibleDevice` (489055),
  `DestructibleRoadSign` (489246), `DestructibleMasterLight` (489517),
  `RetractableAd` (490972), `ElectricLight` (491659), `Window` (496144),
  `RoadBlock` (506454). [2.5-untested]
- World geometry: the native `gameDestructibleSpotsSystem` calls its one
  script function `OnNotifyAboutFracture(localPlayerObj, fracturePos)`
  (315523; vanilla: a crowd stim when V is in a vehicle within 10 m).
  Which nodes notify is their `systemsToNotifyFlags` _(inferred)_.
  [2.5-untested]
- A physics impulse with no attack: `CombatGadgetHelper.SpawnPhysicalImpulse`
  (208535): the game effect `physicalImpulseSphere` with `radius` and
  `position` in its shared data, run from a source object; grenades
  (302899) and explosive devices (`DoPhysicsPulse`, 120641) use it beside
  their attack. Other levers seen, not used: the events
  `enteventsTriggerDestructionEvent {velocity}`, `ToggleImpulseDestruction`,
  `PhysicalImpulseEvent {worldPosition, worldImpulse, radius}` (to an
  entity), the effect executors `gameEffectExecutor_PhysicalFractureField`
  and `_TriggerDestruction` (in effect resources, not reachable by name
  from script), the quest node `questEntityManagerSetDestructionState`.
  [2.5-static]
- To verify in game (`tools/game.sh cmd glass …`):
  1. `glass part` on a `DisplayGlass`, a `Window`, a road sign:
     `TriggerDestruction(20)` breaks it, with its sound; no hit event.
  2. `glass break` looking at a shop front's pane and at a bus shelter:
     whether the impulse (1 m) shatters world glass; if not, the radius or
     another lever.
  3. Shooting those: `glass` counts a break told for each (the two hooks
     fire), and how many notices one pane gives.
  4. With a second game: the echo count rises on the receiver and nothing
     comes back (`objects: … echo(es)`).
  5. That an NPC or V beside a replayed break takes no damage and is not
     staggered.
  6. A waited break (scenario `glass-latecomer`): the impulse 120 m from V
     right after a teleport shatters a pane (its physics streamed in), and
     `FindEntityByID` finds a placed entity's part within 60 m.

#### A turret's fire

- `SecurityTurret` (171182, a `SensorDevice`) holds its weapon in
  `AttachmentSlots.WeaponRight` (`GiveWeaponToTurret`, 171366:
  `Items.w_special_flak` unless its state names another). A locked target
  (`OnTargetLocked`, 171425) starts `ShootStart` (171475), which sets the
  anim feature `SecurityTurretData.Shoot` and runs `ShootAttachedWeapon`
  (171530): `AIWeapon.Fire(this, weapon, simTime, 1.0, trigger, the
target's position, the target, …)`, the anim event `Shoot`, the effect
  `GetVfxNameOnShoot()`, then the next shot by a delayed event
  (`TurretShootingIntervalEvent`) or after a burst's pause
  (`TurretBurstShootingDelayEvent` → `ShootStart`). `ShootStop` (171492)
  ends it. A player-controlled turret fires with the player as owner
  (171608). The `SniperNest` (155212) fires as its player only.
  [2.5-untested]
- Targets (`SensorDevice`, 150628): a sensed object is recognized
  (`RecognizeTarget`, 151700, private) and is interesting when the
  security system says so (`ShouldReactToTarget`, 114173: its attitude
  group's relation to the target's, blacklists, areas) or, with no system,
  when the device is hostile to it; `RecognizeTarget(x, true)` (a quest's
  forced target) is interesting whatever they say. The followed target is
  `GetCurrentlyFollowedTarget()` (151041); `OnCurrentTargetAppears`
  (151273, overridden at 171407) starts the lock; `LoseTarget(x, true)`
  (151760) lets one go. Sensing treats the local player apart
  (`OnDetectionRiseEvent`, 151470; `OnSecuritySystemSupport`, 151817).
  [2.5-untested]
- Aim is two look-ats (`CreateLookAt`, 152183, private: the vertical and
  horizontal presets of its state) on an entity's `Chest` slot or on a
  place (a metre is added to a place, 152207), with the anim feature's
  `state = 0` (1: its idle sweep). [2.5-untested]
- A puppet of a remote V has its own attitude group, a copy of the
  player's with no relation records (§13.17): a security system reads it
  friendly by group, so the host's turret does not take a puppet on its own
  _(inferred)_; hence the forced target.
- To verify in game (`tools/game.sh cmd turret …`, scenarios `turret-host`,
  `turret-guest`):
  1. `turret aim` and `turret shot` on a copy: it turns, one tracer leaves
     its barrels, nothing takes damage.
  2. As a guest in sight of a hostile turret: the copy locks V and stays
     silent until the host's shots; V's health then falls by our own
     game's hits; no shot before the host's word.
  3. As a host: `turret engage` (and a real guest's `Seen`): the turret
     turns to the puppet and fires; the puppet's player is hurt by its own
     copy's shots only; our turret lets go on `Lost`.
  4. The wraps of the private `ShootStart` and `CreateLookAt` take (scc
     compiles them; that the game calls the wrapped ones is to see).
  5. A friendly-turned turret: no copy locks a V; its shots at NPCs are
     drawn on the guests.
