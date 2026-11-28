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
  remembers `SetIsAttitudeChanged`. A camera's Friendly Mode is
  `ForceIgnoreTargets` (154015: `Repeat` true, `Attitude` left at
  `AIA_Friendly`, 0) turned into `SetDeviceAttitude` by
  `OnForceIgnoreTargets` (153837); a turret's is `SetDeviceAttitude` itself
  (172228). `SetDeviceFriendly` (152479) works on the **local** player only:
  its agent friendly to `GetLocalPlayerMainGameObject()`'s, its group that
  player's (`SetAttitudeGroup` when the action is completed, a duration of
  0; `SetAttitudeGroupUnsavable` during a timed hack), its senses off that
  player (`SetDetectionOverwrite`), the group it had cached
  (`CacheInitialAttitude`). A hostile `SetDeviceAttitude` reaches only
  `Device.OnSetDeviceAttitude` (101641), which undoes nothing (the flag
  stays set); the game's own undo is a re-enabled security system
  (`SecuritySystemEnabled`: the state clears the flag, 153633; the entity
  takes the system's group back, 151809). A turned device judges a
  sighting by its own attitude (`SetAsIntrestingTarget`, 151254); the
  security system does not ask who reported (`ProcessInput`, 114902). The
  camera's warning `LocKey#53158` (`SurveillanceCamera.OnCurrentTargetAppears`, 166161) is shown when its target is the local player and it is not
  friendly to the player _(inferred: the "intruder detected" line)_.
  A destroyed device is
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
   camera stops filling the meter); a puppet kept out of a turned camera's
   senses by `SetAttitudeTowards` and `SetDetectionOverwrite`; the undo
   (`TandemUnbefriend`) leaving it hostile again (scenarios
   `devices-camera-friendly`, `turret-friendly`).
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
- A followed target is not a locked one: `SensorDevice.OnCurrentTargetAppears`
  (151273) of a turret connected to a security system returns before
  `StartLockingTarget` when that system is `SAFE` (nothing) or `ALERTED`
  (`TARGETLOCK`, no lock); only `COMBAT`, follow mode or no security system
  lock (the last after `m_timeToActionAfterSpot`). `ShootStart` (171502)
  needs the sensor state `TARGETLOCK`. arasaka-park's sentry
  17d93f9464117cb6 followed a forced puppet 6 s without a shot, and never
  locked a forced V (2026-11-21, logs; the security state itself not read)
  _(inferred)_; hence `TandemTake`'s own lock, not run in game.
  [2.5-untested]
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
  6. A copy's laser and light follow the host's words (scenario
     `turret-guest`): `m_laserMesh.Toggle` and `DetermineLightScanRefs`
     take on a copy whose own senses saw nobody.
  7. A turret in a player's hands (scenario `turret-hands`): the camera's
     forward is the turret's while held; `m_animFeature.Shoot` is true
     while its trigger is held; the copy's `TandemAim` on a place turns it
     there.
- A turret's look: its laser is the `laserMesh` component
  (`SecurityTurret.m_laserMesh`, toggled on in `OnCurrentTargetAppears`,
  171416, off in `OnAllValidTargetsDisappears`, 171449); the scan light is
  `SensorDevice.DetermineLightScanRefs(colour)` (152924, protected final:
  green to a local player it is friendly to, else the colour asked) over
  `m_lightColors` (`LedColors_SensorDevice`: off, red, green, blue, yellow,
  white). Red at a target (151277), blue while it scans the local player
  (151492), yellow when it lost it or all targets (151377, 151530).
  [2.5-untested]
- A turret in the player's hands: `TakeOverControlSystem` (scriptable
  system by that name) `GetControlledObject()` (154115); the trigger
  `ControlledDeviceInputAction(pressed)` (171464) runs `ShootStart` /
  `ShootStop`; a controlled turret's shots are `AIWeapon.Fire` with the
  player as owner (171601). The camera is the device's while held
  (`OnTCSTakeOverControlActivate`, 152744) _(inferred: the active camera's
  forward is the turret's aim)_. [2.5-untested]
- Leaving and taking a device's control from script: the static
  `TakeOverControlSystem.ReleaseControl(game)` (154288) queues
  `RequestReleaseControl` (false when nothing is controlled), whose handler
  gives the player back its body (`ToggleToMainPlayerObject`, 154331); a
  quest's forced release goes this way
  (`SensorDeviceControllerPS.QuestReleaseCurrentObject`, 153789).
  `TakeOverControlSystem.RequestTakeControl(device, ToggleTakeOverControl)`
  (154180) takes one, as a device's own `Device.OnToggleTakeOverControl`
  does (102002) and the chain's `MoveToSpecificEntity` with a bare
  `ToggleTakeOverControl` (`SetProperties(true, false)`, 154681).
  [2.5-untested]

### 13.169 Physics props: what a script reaches of a prop's body, and which props have no entity (2026-11-28, read in final.reds (2.50), the RTTI dump and the node types; not run)

- **Two kinds of physics prop.** A placed _entity_ (`worldEntityNode`, an
  `.ent`) with a `PhysicalMeshComponent` (`final.reds (2.50):209439`,
  `extends MeshComponent`) has a static `EntityID`, the same in every game;
  a `worldDynamicMeshNode` ("a mesh with simulated rigid-body physics, not
  destructible", WolvenKit's object spawner; RTTI `worldDynamicMeshNode <
worldMeshNode`, instance `worldDynamicMeshNodeInstance`, 0x1b0 bytes) is a
  sector node with no entity: no `EntityID`, no script object, no targeting
  component (`parity.md` row 4: "not readable"). Measured since: most
  street clutter is a third kind, `worldInstancedDestructibleMeshNode`
  (§13.171). [2.5-untested]
- **A body by script**: `PhysicalMeshComponent.CreatePhysicalBodyInterface(opt
bodyIndex)` (`:209441`; `ColliderComponent` has one too, `:202652`)
  returns `PhysicalBodyInterface` (`:209478`), whose script declaration has
  `IsSimulated`, `IsKinematic`, `GetTransform`, `SetTransform(Transform)`,
  `SetIsKinematic`, `ToggleKinematic`, `AddLinearImpulse(impulse,
originInCOM, opt offset)`, `SetIsQueryable`. The game's own devices wake a
  mesh that way: `SetIsKinematic(false)` then `AddLinearImpulse`
  (`ActivatedDeviceTrap.ActivatePhysicalMeshes`, `:485463`; a traffic
  light's fallen mesh, `:280246`). [2.5-untested]
- **More natives than the scripts declare**: the RTTI of
  `entPhysicalBodyInterface` also has `GetLinearVelocity`,
  `SetLinearVelocity`, `GetAngularVelocity`, `SetAngularVelocity`,
  `GetMass`, `SetMass`, `SetIsSleeping`, `SetDisplacement`,
  `SetSimulationFilter` (`docs/research/data/rtti-natives-2.50.tsv`,
  handlers `0x142998948` for `SetLinearVelocity`). The plugin calls one by
  name (`Red::CallVirtual`, `TandemObjects.SetBodyVelocity`). [2.5-untested]
- **The engine's notice of a touch**: `PhysicalCollisionEvent`
  (`:313453`: `myComponent`, `otherEntity`, `otherComponent`,
  `worldPosition`, `worldNormal`, `deltaVelocity`, `impulse`) is sent to
  the entity whose body collided; `ActivatedDeviceTrap.OnPhysicalCollisionEvent`
  (`:485518`) is the one handler in the scripts. Whether every placed
  physics entity gets it, or only one whose mesh asks for collision
  events, is not known. [unverified]
- A component's place: `IPlacedComponent.GetLocalToWorld()` and
  `GetInitialPosition()` (`:19992`). [2.5-untested]

### 13.171 How many of the city's physics props are world nodes (2026-11-28, live RTTI read of 2.50 on instance c in Kabuki, V at (-915, 1885, 36); Ghidra; RHT's world inspector, MIT)

- **Counted live** (`tools/re` scans by vtable, read-only; the whole
  streamed set around V, not a radius): physics clutter is
  overwhelmingly **`worldInstancedDestructibleMeshNode`**, not
  entities and not `worldDynamicMeshNode`. Attached around V: 416 such
  node instances holding 1,306 bodies, of which 181 nodes (818 bodies)
  are `simulationType` Dynamic and 203 (420 bodies) Kinematic with
  `turnDynamicOnImpulse` (they fall when hit): **1,238 bodies a touch
  moves**. Loaded in sectors: 4,855 such nodes, 12,026 instances
  (`instanceTransforms`, node +0xB8), 3,945 `isDestructible`. Against
  that: `worldDynamicMeshNode` 23 nodes (15 `isDebris`), no live
  instance; `worldPhysicalDestructionNode` 201 nodes, no live instance;
  `entPhysicalMeshComponent` 1,963 on 436 entities, of which only 4 are
  Dynamic and 144 leave it to their mesh (13 of those vehicles); 1,408
  are Kinematic, on devices (100 entities), cars (39), containers (49),
  items (17) and plain entities (75). So at most about 150 entity
  bodies against 1,238 node bodies: **about 9 in 10 of what a V or a
  car knocks over in a street has no entity**. Which meshes they are was
  not read (their paths are hashes, node +0x90). [2.5-verified]
- **Their name**: a node instance carries its node at +0x60 (a handle,
  read on all 20 sampled) and its global node id at +0x80, the same in
  every game (sector data; RHT's `WorldNodeInstance` layout); one body
  is that id and its index in `instanceTransforms`. RHT reaches the
  instances by hooking `WorldNodeInstance_Initialize`, `_Attach`,
  `_Detach` (address-library hashes) into a registry. [2.5-verified for
  +0x60; unverified for the rest]
- **Their bodies**: the instance's visibility override (vtable slot
  0xF8, `0x14299d388`, decompiled) walks an array of per-instance
  objects at +0x150 (count +0x15C) and a bitset at +0x1D8, one bit per
  instance (hidden or broken), and the render proxy at +0x190. What the
  per-instance objects are (physics actors or proxies), how to read a
  body's pose and speed or hold it kinematic, is not known: no script
  surface, no RTTI function on the node, its instance or
  `worldRuntimeSystemDestruction`. [unverified]

### 13.167 An explosive's exploded look without its blast (2026-11-28, read in final.reds, not run)

- Streamed in exploded, an explosive device shows its wreck through its
  own resolve: `ExplosiveDevice.ResolveGameplayState` (129093), when its
  state `DoExplosiveResolveGameplayLogic()` (136869: `m_doExplosiveEngineerLogic`
  and `m_provideExplodeAction`) and `IsExploded()`, calls
  `ToggleVisibility(false)` (129270): `m_mesh` and `m_collider` off, its
  `componentON_i` on and `componentOFF_i` off, the exploded appearance
  when `m_switchAppearanceOnExplosion`, `SetGameplayRoleToNone`, and the
  state `ForceDisableDevice`. No attack, no pulse, no VFX.
  [2.5-untested]
- A live explosion (`StartExplosionPipeline`, 129147, then `Explode`, 129189) also stops what a whole one runs: `KillAllFxInstances`,
  `EffectsOnStartStop(true)`, its `m_loopAudioEvent`, a `ChangeLightEvent`
  of strength 0; `HandleDeath` (129130) turns its `m_distractionCollider`
  off and `RemoveEnvironmentalHazardStimuli` (129546). The mesh swap comes
  last (`SwapMeshDelayedEvent` after `GetTimeToMeshSwap()`: the same
  `ToggleVisibility(false)`). [2.5-untested]
- `SetExplodedState` (136876) only writes `m_exploded` and sends the state's
  changed event: an entity standing whole stays whole until its resolve
  runs again (its next stream-in). [2.5-untested]
- Kin: `SensorDevice` (cameras, turrets, 134230), `C4` (495920) and
  `ExplosiveTriggerDevice` (placed traps, 496916) extend `ExplosiveDevice`,
  their states `ExplosiveDeviceControllerPS`. A door's kind is
  `DoorControllerPS.GetDoorType()` (189977): `EDoorType.AUTOMATIC` opens
  for an authorized V near it (`IsSomeoneAuthorizedNearby`, 201966).
  [2.5-untested]
- Used: `ExplosiveDevice.TandemShowExploded` (`Devices.reds`, a record of
  an exploded barrel applied while it stands in our world).
