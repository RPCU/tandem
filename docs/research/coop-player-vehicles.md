# Co-op players and vehicles: what a remote V and a shared car need

Facts for milestones 2 (player presence), 3 (vehicles), 4 (regroup) and 8
(revive, pings): everything a player's game can read about its own V and
its vehicle, and what another game can call to show it on a puppet.
Authority is the table of [`../ARCHITECTURE.md`](../ARCHITECTURE.md): a V
is owned by its player's game, a driven vehicle by the driver's game.

This file extends [`engine-knowledge.md`](engine-knowledge.md) §4–6, §12–13
and [`pose-streaming.md`](pose-streaming.md); it does not repeat them.

## Sources and caveats

- **`final.reds:LINE`** — decompiled vanilla scripts,
  `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` (559 904 lines).
- **RTTI** — the live dump of game 2.50, `.cache/re/rtti.json`, queried with
  `tools/re/run tools/re/rtti.py class|find|func|enum`. `[N]` below marks a
  native function with a handler address (callable through
  `ExecuteFunction`, or directly through its handler, engine-knowledge §13.1).
- **Nothing here was tested in co-op.** These are names, signatures and enum
  values read from scripts and RTTI. Every "apply on the puppet" line is a
  proposal until tried in game. `(inferred)` marks what the sources do not
  state.
- **The script reference is older than the game.** Where they differ the
  RTTI is right for 2.50. Seen so far:
  - `PlayerStateMachineDef` in the scripts has one `Weapon` field
    (final.reds:53697); 2.50 has `RightWeapon`, `LeftWeapon`,
    `EquipmentRight`, `EquipmentLeft`, `WeaponTriggerMode`,
    `WeaponLeftTriggerMode`, `ShootState`, `IsDualWielding` and no `Weapon`
    in the layout (the script callback `PlayerPuppet.OnWeaponStateChange`
    still exists in 2.50's RTTI).
  - `gamedataMappinVariant.CPO_RemotePlayerVariant` is 146 in the scripts
    (final.reds:1322), 148 in 2.50; `gamedataStatType.HasSecondHeart` 756 → 758. **Never hard-code an enum number taken from final.reds**: resolve
    by name at startup, or take it from the RTTI dump.
  - Line numbers of script bodies are still the best description of the
    logic; re-check a signature against RTTI before wrapping it.
- **The RTTI dump is of the player's modded game**: it contains Codeware and
  other mods' script classes (`CazanuLimbSurvival.*`…). Native classes and
  enums are the game's; a script class's function list may include mod
  additions.

---

## 1. The player state machine blackboard

### 1.1 Reading and listening

```
// PlayerPuppet, final.reds:40649
GetPlayerStateMachineBlackboard() -> IBlackboard
  = GameInstance.GetBlackboardSystem(game)
      .GetLocalInstanced(player.GetEntityID(),
                         GetAllBlackboardDefs().PlayerStateMachine)
```

- It is a **per-entity ("local instanced") blackboard**: keyed by the
  player's `EntityID` (`gameBlackboardSystem.GetLocalInstanced(entityID,
definition)` [N]; `Get(definition)` [N] is for global ones such as
  `UI_*`). Whether `GetLocalInstanced` answers anything for a puppet's
  entity id is unknown (open question 1).
- `gameIBlackboard` natives (RTTI, all [N]), one family per type — Bool,
  Int, Uint, Float, String, Name, Vector2, Vector4, Quat, EntityID, Variant:
  `GetInt(id) -> Int32`, `SetInt(id, value, opt forceFire)`,
  `SignalInt(id)`,
  `RegisterListenerInt(id, object: IScriptable, func: CName, opt
fireIfValueExist: Bool) -> CallbackHandle`,
  `RegisterDelayedListenerInt(…)` (same parameters),
  `UnregisterListenerInt(id, out handle)`,
  `UnregisterDelayedListener(id, handle)`, `FireCallbacks()`
  (final.reds:46967–46977).
- Callback shape: `protected cb func OnX(value: Int32) -> Bool` on the
  registered object (example: `PlayerPuppet.OnLocomotionStateChanged`,
  registered at final.reds:39881 with `fireIfValueExist = true`).
- **Thread**: the scripts do not say on which thread a listener runs.
  `engine-knowledge.md` §12 measured script callbacks on several threads.
  Treat a listener as "may be a job thread": enqueue only (AGENTS.md,
  plugin threading). The safe alternative is polling `GetInt` on the game
  thread's frame tick; all fields below are cheap reads.
- Writers: the state-machine scripts call
  `DefaultTransition.SetBlackboardIntVariable(scriptInterface, id, value)`
  (final.reds:244675); `SetDetailedState(scriptInterface, state)` writes
  `LocomotionDetailed` (final.reds:281168).

**Owner's game**: polls (or listens to) the fields of §1.2 and sends them.
**Other games**: nothing reads this blackboard for a puppet; the values
drive what tandem applies (pose stream selection, weapon effects, events).
Writing them into a puppet's own blackboard is only useful if vanilla
scripts are to treat the puppet as a player (open question 1).

### 1.2 Every field (2.50 layout, RTTI `PlayerStateMachineDef`)

Type column: I = Int32, B = Bool, F = Float, E = EntityID, V = Variant,
V4 = Vector4. "Enum" is what the Int carries; `(inferred)` = by name only,
no script cast seen.

| Field                                            | T   | Enum / meaning                                          | Seen at        |
| ------------------------------------------------ | --- | ------------------------------------------------------- | -------------- |
| `Locomotion`                                     | I   | `gamePSMLocomotionStates`                               | 40668          |
| `LocomotionDetailed`                             | I   | `gamePSMDetailedLocomotionStates`                       | 281168         |
| `HighLevel`                                      | I   | `gamePSMHighLevel`                                      | 283771, 529018 |
| `UpperBody`                                      | I   | `gamePSMUpperBodyStates`                                | 129664         |
| `TimeDilation`                                   | I   | `gamePSMTimeDilation`                                   | 524116+        |
| `RightWeapon`                                    | I   | `gamePSMRangedWeaponStates` (inferred; 2.50 only)       | —              |
| `LeftWeapon`                                     | I   | same (inferred; 2.50 only)                              | —              |
| `EquipmentRight`                                 | I   | equipment state of the right hand (inferred; 2.50 only) | —              |
| `EquipmentLeft`                                  | I   | same, left hand (inferred; 2.50 only)                   | —              |
| `WeaponTriggerMode`                              | I   | `gamedataTriggerMode` (inferred; 2.50 only)             | —              |
| `WeaponLeftTriggerMode`                          | I   | same (inferred; 2.50 only)                              | —              |
| `ShootState`                                     | I   | `gamePSMShootStates` (inferred; 2.50 only)              | —              |
| `LatestWeaponReloadTime`                         | F   | sim time of the last reload                             | —              |
| `Melee`                                          | I   | `gamePSMMelee`                                          | 311300         |
| `UI`                                             | I   | `gamePSMUIState`                                        | —              |
| `Crosshair`                                      | I   | `gamePSMCrosshairStates`                                | 268757         |
| `Reaction`                                       | I   | `gamePSMReaction`                                       | —              |
| `Zones`                                          | I   | `gamePSMZones`                                          | 311818         |
| `SecurityZoneData`                               | V   | security zone struct                                    | 39881+         |
| `Vision`                                         | I   | `gamePSMVision` (1 = scanner)                           | 40549          |
| `VisionDebug`                                    | I   | `gamePSMVisionDebug`                                    | —              |
| `SceneTier`                                      | I   | gameplay tier 1–5 (`GameplayTier`)                      | 40009          |
| `CombatGadget`                                   | I   | `gamePSMCombatGadget`                                   | 520019+        |
| `LastCombatGadgetUsed`                           | V   | item of the last grenade                                | —              |
| `Consumable`                                     | I   | consumable state (enum not found)                       | —              |
| `Vehicle`                                        | I   | `gamePSMVehicle`                                        | 40272          |
| `MountedToVehicleInDriverSeat`                   | B   | set by `VehicleComponent.OnMountingEvent`               | 90563+         |
| `MountedToCombatVehicle`                         | B   | vehicle allows combat                                   | 40453          |
| `MountedToVehicle`                               | B   |                                                         | 40457          |
| `InArmoredVehicle`                               | B   |                                                         | 40448          |
| `ZoomLevel`                                      | F   | current zoom factor                                     | 39870          |
| `MaxZoomLevel`                                   | I   |                                                         | —              |
| `ToggleFireMode`                                 | B   | pulse                                                   | 239948         |
| `SwitchWeapon`                                   | B   | pulse                                                   | —              |
| `IsDoorInteractionActive`                        | B   |                                                         | 104815         |
| `FinisherTarget`                                 | E   |                                                         | 552947         |
| `IsInteractingWithDevice`                        | B   |                                                         | 92642          |
| `IsInteractingViaPersonalLink`                   | B   | jack-in                                                 | 38978          |
| `IsForceOpeningDoor`                             | B   |                                                         | 134161         |
| `IsControllingDevice`                            | B   | camera/turret takeover                                  | 154573         |
| `IsControllingCamera`                            | B   |                                                         | 154342         |
| `IsUIZoomDevice`                                 | B   |                                                         | 43457          |
| `UIZoomDeviceID`                                 | E   |                                                         | 154569         |
| `UseUnarmed`                                     | B   |                                                         | —              |
| `Berserk`                                        | I   | (enum not found)                                        | —              |
| `ActiveCyberware`                                | I   | (enum not found)                                        | —              |
| `Whip`                                           | I   | `gamePSMWhip`                                           | —              |
| `DEBUG_SilencedWeapon`                           | B   |                                                         | —              |
| `LeftHandCyberware`                              | I   | `gamePSMLeftHandCyberware`                              | 129668         |
| `UseLeftHand`                                    | B   |                                                         | —              |
| `MeleeWeapon`                                    | I   | `gamePSMMeleeWeapon`                                    | 129674         |
| `Carrying`                                       | B   | carrying a body                                         | 526850         |
| `CarryingDisposal`                               | B   |                                                         | 92640          |
| `CurrentElevator`                                | V   |                                                         | 198539         |
| `IsPlayerInsideElevator`                         | B   |                                                         | 198537         |
| `IsPlayerInsideMovingElevator`                   | B   |                                                         | —              |
| `Combat`                                         | I   | `gamePSMCombat`                                         | 240954         |
| `Stamina`                                        | I   | `gamePSMStamina`                                        | 245137         |
| `Vitals`                                         | I   | `gamePSMVitals`                                         | 283774         |
| `Takedown`                                       | I   | `gamePSMTakedown`                                       | 48594          |
| `TakedownTargetID`                               | E   |                                                         | —              |
| `Fall`                                           | I   | `gamePSMFallStates`                                     | 310942         |
| `Landing`                                        | I   | `gamePSMLandingState`                                   | 528757         |
| `UsingCover`                                     | B   |                                                         | 515685         |
| `IsInMinigame`                                   | B   | breach protocol                                         | 481065         |
| `UploadingQuickHackIDs`                          | V   | quickhacks being uploaded                               | 38387          |
| `CostFreeActionID`                               | V   |                                                         | —              |
| `AmountOfCostFreeActions`                        | I   |                                                         | —              |
| `MeleeSpreadableQuickhackActionID`               | V   |                                                         | —              |
| `EntityIDTargetingPlayer`                        | E   |                                                         | —              |
| `EntityIDVehicleRemoteControlled`                | E   | remote-driven vehicle                                   | —              |
| `IsVehicleRemoteControlCameraTPP`                | B   |                                                         | —              |
| `Swimming`                                       | I   | `gamePSMSwimming`                                       | 240960         |
| `BodyCarrying`                                   | I   | `gamePSMBodyCarrying`                                   | 95998          |
| `BodyDisposalDetailed`                           | I   | `gamePSMDetailedBodyDisposal`                           | 92643          |
| `DisplayDeathMenu`                               | B   | triggers the death screen (§7)                          | 283812         |
| `OverrideQuickHackPanelDilation`                 | B   |                                                         | —              |
| `NanoWireLaunchMode`                             | I   | `gamePSMNanoWireLaunchMode`                             | —              |
| `IsMovingHorizontally`                           | B   |                                                         | 45415          |
| `IsMovingVertically`                             | B   |                                                         | 45411          |
| `IsOnGround`                                     | B   |                                                         | —              |
| `MinElevation`, `MaxElevation`                   | F   |                                                         | —              |
| `ActionRestriction`                              | V   |                                                         | 39850          |
| `MeleeLeap`                                      | B   |                                                         | 516840         |
| `IsInWorkspot`                                   | I   | `gamePSMWorkspotState`                                  | 311077         |
| `QuestForceShoot`                                | B   |                                                         | 551771         |
| `LadderCameraParams`                             | I   |                                                         | —              |
| `DodgeTimeStamp`                                 | F   |                                                         | —              |
| `CanOnePunch`                                    | B   |                                                         | 22352          |
| `HasSmartGunLockedOnTarget`                      | B   |                                                         | —              |
| `SceneAimForced`, `SceneSafeForced`              | B   |                                                         | 44672          |
| `SceneWeaponLoweringSpeedOverride`               | F   |                                                         | —              |
| `ScenePreventFirstEquip`                         | B   |                                                         | —              |
| `MountedPreventFirstEquip`                       | B   |                                                         | 40459          |
| `IgnoreBarbedWireStateEnterTime`                 | F   |                                                         | —              |
| `IsInLoreAnimationScene`                         | B   |                                                         | —              |
| `Felled`                                         | B   |                                                         | 44692          |
| `IsVehicleInTPP`                                 | B   |                                                         | —              |
| `IsDriverCombatInTPP`                            | B   |                                                         | 39881+         |
| `DriverCombatWeaponType`                         | I   | (inferred `gamedataDriverCombatType`)                   | 39881+         |
| `TrackedTarget`                                  | V   |                                                         | —              |
| `TPPAimPosition`                                 | V4  | world aim point in third person                         | —              |
| `BodySlamBump`                                   | I   |                                                         | 556410         |
| `IsInBodySlamState`                              | B   |                                                         | 556333         |
| `AimInTimeRemaining`, `AimInTime`                | F   |                                                         | —              |
| `CanThrowCarriedNPC`                             | B   |                                                         | —              |
| `IsWeaponFirstEquip`                             | B   |                                                         | 524721         |
| `IsDualWielding`                                 | B   | (2.50 only)                                             | —              |
| `IsVisualCustomizationPhotomodeVehicleSlotTaken` | B   |                                                         | —              |

`PlayerPuppet` itself listens (final.reds:39840–40030) to `ZoomLevel`,
`Locomotion`, `Combat`, `SecurityZoneData`, `LeftHandCyberware`, `Vision`,
`MeleeWeapon`, `UpperBody`, `Vehicle`, `Weapon`, `IsDriverCombatInTPP`,
`DriverCombatWeaponType`, `IsInBodySlamState`, `CombatGadget`, `SceneTier`,
`Takedown`: its `On…StateChange(newState: Int32)` callbacks are wrap points
that already exist (`@wrapMethod(PlayerPuppet)`), an alternative to
registering listeners of our own.

### 1.3 Enum values (RTTI, game 2.50)

```
gamePSMLocomotionStates: Any=-1 Default=0 Crouch=1 Sprint=2 Kereznikov=3
  Jump=5 Vault=6 Dodge=7 DodgeAir=8 Workspot=9 Slide=10 SlideFall=11
  CrouchSprint=12 CrouchDodge=13                          (no value 4)
gamePSMDetailedLocomotionStates: NotInBaseLocomotion=0 Stand=1 AimWalk=2
  Crouch=3 Sprint=4 Slide=5 SlideFall=6 Dodge=7 Climb=8 Vault=9 Ladder=10
  LadderSprint=11 LadderSlide=12 LadderJump=13 Fall=14 AirThrusters=15
  AirHover=16 SuperheroFall=17 Jump=18 DoubleJump=19 ChargeJump=20
  HoverJump=21 DodgeAir=22 RegularLand=23 HardLand=24 VeryHardLand=25
  DeathLand=26 SuperheroLand=27 SuperheroLandRecovery=28 Knockdown=29
  CrouchSprint=30 Felled=31
gamePSMHighLevel: Any=-1 Default=0 SceneTier1=1 SceneTier2=2 SceneTier3=3
  SceneTier4=4 SceneTier5=5 Swimming=6
gamePSMUpperBodyStates: Any=-1 Default=0 SwitchItems=1 SwitchCyberware=2
  Reload=3 TemporaryUnequip=4 ForceEmptyHands=5 Aim=6 DualWield=7
gamePSMRangedWeaponStates: Any=-1 Default=0 Charging=1 Reload=2
  QuickMelee=3 NoAmmo=4 Ready=5 Safe=6 Overheat=7 Shoot=8
gamePSMWeaponStates: Any=-1 Default=0 NoAmmo=1 Ready=2 Safe=3
gamePSMShootStates: Idle=0 RightWeapon=1 LeftWeapon=2 BothWeapons=3
  ValentinosFrenzyInit=4 ValentinosFrenzyRightOnly=5 ValentinosFrenzy=6
gamePSMMelee: Any=-1 Default=0 Attack=1 Block=2
gamePSMMeleeWeapon: NotReady=0 Equipping=1 Idle=2 Safe=3 PublicSafe=4
  Parried=5 Hold=6 ChargedHold=7 Block=8 Targeting=9 Deflect=10
  ComboAttack=11 FinalAttack=12 StrongAttack=13 SafeAttack=14
  BlockAttack=15 SprintAttack=16 CrouchAttack=17 JumpAttack=18
  ThrowAttack=19 DeflectAttack=20 EquipAttack=21 Default=22
gamePSMCombatGadget: Default=0 EquipRequest=1 Equipped=2 Charging=3
  Throwing=4 WaitForUnequip=5 QuickThrow=6
gamePSMLeftHandCyberware: Default=0 Safe=1 EquipRequest=2 Idle=3
  Equipped=4 Charge=5 Loop=6 Catch=7 QuickAction=8 ChargeAction=9
  CatchAction=10 StartUnequip=11 Unequip=12
gamePSMCrosshairStates: Default=0 Safe=1 Scanning=2 GrenadeCharging=3
  Aim=4 Reload=5 ReloadDriverCombatMountedWeapons=6 Sprint=7 HipFire=8
  LeftHandCyberware=9 QuickHack=10
gamePSMVision: Any=-1 Default=0 Focus=1
gamePSMVisionDebug: Default=0 VisionToggle=1
gamePSMZones: Any=-1 Default=0 Public=1 Safe=2 Restricted=3 Dangerous=4
gamePSMCombat: Any=-1 Default=0 InCombat=1 OutOfCombat=2 Stealth=3
gamePSMVehicle: Any=-1 Default=0 Driving=1 Combat=2 Passenger=3
  Transition=4 Turret=5 DriverCombat=6 Scene=7
gamePSMSwimming: Any=-1 Default=0 Surface=1 Diving=2 Climbing=3
gamePSMTakedown: Any=-1 Default=0 EnteringGrapple=1 Grapple=2 Leap=3
  Takedown=4
gamePSMBodyCarrying: Any=-1 Default=0 PickUp=1 Carry=2 Dispose=3 Drop=4
  Aim=5 Throw=6
gamePSMBodyCarryingLocomotion: Default=0 Jump=1 Crouch=2 Sprint=3 Fall=4
  Land=5 DropBody=6
gamePSMBodyCarryingStyle: Any=0 Default=1 Friendly=2 Strong=3
  WoundedSoldier=4
gamePSMDetailedBodyDisposal: Default=0 Dispose=1 Lethal=2 NonLethal=3
gamePSMFallStates: Default=0 RegularFall=1 SafeFall=2 FastFall=3
  VeryFastFall=4 DeathFall=5
gamePSMLandingState: Default=0 RegularLand=1 HardLand=2 VeryHardLand=3
  DeathLand=4 SuperheroLand=5 SuperheroLandRecovery=6
gamePSMTimeDilation: Any=-1 Default=0 Sandevistan=1 Photomode=2
gamePSMStamina: Rested=0 Fatigued=1 Exhausted=2
gamePSMVitals: Alive=0 Dead=1 Resurrecting=2
gamePSMReaction: Default=0 Stagger=1
gamePSMWhip: Default=0 Charging=1 Pulling=2
gamePSMNanoWireLaunchMode: Default=0 Primary=1 Secondary=2
gamePSMWorkspotState: Default=0 Workspot=1
gamePSMCover: Any=-1 Default=0 InCover=1 Peek=2 Lean=3 OutOfCover=4
gamePSMUIState: (unnamed)=0 WeaponSelect=1
```

`LocomotionDetailed` is the one field that names every move a pose stream
must cover (crouch, slide, dodge, air dash, vault, climb, ladder, jumps,
landings, knockdown): it is written by each locomotion state's `OnEnter`
(final.reds:289647 Crouch, 550798 AimWalk, 551880 CrouchSprint, 552137
Sprint, 553244 Slide, 553004 SlideFall, 553707 Dodge, 557994 DodgeAir,
556464 Vault, 529545 Climb, 530440 Ladder, 556643/556676/556718 ladder
sprint/slide/jump, 557425 Jump, 557518 DoubleJump, 557597 ChargeJump,
557751 HoverJump, 556842 AirThrusters, 557295 AirHover, 557353
SuperheroFall, 558035–558328 the six landings, 558466 Knockdown, 558801
Felled).

---

## 2. Other state of the player

### 2.1 Position, velocity, movement

| What                  | How                                                                                                                                                                              | Note                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| position, rotation    | `entEntity.GetWorldPosition()`, `GetWorldOrientation()`, `GetWorldYaw()`, `GetWorldForward()` [N]                                                                                | already sampled by the plugin                                                      |
| velocity              | `gamePuppet.GetVelocity() -> Vector4` [N] (final.reds:19380)                                                                                                                     | world space                                                                        |
| moving                | `PlayerPuppet.IsMoving()` = blackboard `IsMovingHorizontally \|\| IsMovingVertically` (final.reds:45411)                                                                         |                                                                                    |
| on ground             | blackboard `IsOnGround`; `animAnimFeature_PlayerMovement.inAir`                                                                                                                  |                                                                                    |
| facing vs. movement   | `animAnimFeature_PlayerMovement` {`facingDirection` Vector4 +0x70, `standingTerrainAngle` +0x80, `verticalSpeed` +0x84, `movementDirectionHorizontalAngle` +0x88, `inAir` +0x8C} | the anim feature the engine applies as `playerLocomotion` (engine-knowledge §12.1) |
| crouch (persistent)   | `GamePuppetPS.IsCrouch()` [N] (final.reds:24668)                                                                                                                                 | for the player use `Locomotion == Crouch`                                          |
| standing on a vehicle | `gamePuppet.CheckIsStandingOnVehicle(vehicleID)`, `TryGetVehicleBelow() -> EntityID` [N]                                                                                         | surfing a moving car: send position relative to that vehicle (inferred)            |

`moveComponent` (RTTI size 0x2C0) exposes **no script native**; there is no
"movement input" getter by name. Input is only visible through
`StateGameScriptInterface.GetActionValue(n"…")` inside state-machine
scripts (aim is `GetActionValue(n"CameraAim") > 0`, final.reds:281249).

### 2.2 Aim and look direction

- Is aiming: `UpperBody == gamePSMUpperBodyStates.Aim (6)`; the weapon gets
  `gameweaponeventsOwnerAimEvent { isAiming: Bool }`
  (`WeaponObject.OnWaponeventsOwnerAimEvent`); status effect
  `BaseStatusEffect.PlayerAiming`. `ZoomLevel` (Float) and `AimInTime` /
  `AimInTimeRemaining` give the ADS blend.
- Camera: `PlayerPuppet.GetFPPCameraComponent()` (final.reds:40590) →
  `gameFPPCameraComponent` (size 0x510). Natives: `ResetPitch()`,
  `ResetCurrentGameplayInput()`, `SceneDisableBlendingToStaticPosition()`;
  inherited `gameCameraComponent.GetFOV()`, `GetZoom()`, `SetFOV`,
  `SetZoom`, `Activate`, `Deactivate`; inherited
  `entIPlacedComponent.GetLocalOrientation() -> Quaternion`,
  `GetLocalToWorld() -> Matrix`, `GetLocalPosition()` [N]. Properties:
  `pitchMin` +0x3E0, `pitchMax` +0x3E4, `yawMaxLeft` +0x3E8, `yawMaxRight`
  +0x3EC, `headingLocked` +0x3F6. **There is no "GetPitch" native**: pitch
  comes from the camera's orientation.
- The simplest by-name source of the look direction:
  `gameCameraSystem.GetActiveCameraWorldTransform(out Transform) -> Bool`,
  `GetActiveCameraForward()`, `GetActiveCameraRight()`, `GetActiveCameraUp()`,
  `GetActiveCameraFOV()` [N]. It is the _active_ camera: wrong in photo
  mode, on a controlled device camera and in vehicle third person (use
  `TPPAimPosition` there) (inferred).
- Aim point: `gametargetingTargetingSystem` [N]:
  `GetCrosshairData(weaponID: EntityID, instigator, out position, out
forward)`, `GetDefaultCrosshairData(instigator, out position, out
forward)`, `GetLookAtObject(instigator, opt withLOS, opt
ignoreTransparent) -> GameObject`, `GetLookAtPosition(...) -> Vector4`,
  `GetLookAtComponent(...)`, `GetCurrentSpread(weaponID, instigator)`,
  `GetTrackedTargetObject(instigator)`.
- The anim feature the engine feeds the first-person camera node:
  `animAnimFeature_FPPCamera` (0x100: `deltaYaw`, `deltaPitch`, `pitchMin`,
  `pitchMax`, `fov`, vehicle weights…).

**Owner sends**: aim pitch and yaw relative to the body (two angles,
quantised), plus the aiming flag. **Others apply**: if the pose stream
carries the spine and arms, nothing more is needed for the body; the angles
remain useful for the fallback (no pose stream), for a look-at on the
puppet's head, and to orient cosmetic shots (§3.4).

### 2.3 Stat pools: health, stamina, the rest

`gameStatPoolsSystem` [N] (object id =
`Cast<StatsObjectID>(entity.GetEntityID())`):
`GetStatPoolValue(id, type, opt perc = true) -> Float`,
`GetStatPoolMaxPointValue`, `ToPoints`, `ToPercentage`,
`RequestSettingStatPoolValue(id, type, newValue, instigator, opt perc, opt
ignoreCustomLimit)`, `RequestChangingStatPoolValue(id, type, diff,
instigator, forceChunkTransfering, opt perc)`,
`RequestSettingStatPoolValueIgnoreChangeMode(...)`,
`HasStatPoolValueReachedMin`, `RequestRegisteringListener(id, type,
listener)` → `OnStatPoolValueChanged`, plus the modifier functions.

`gamedataStatPoolType` values that matter: `Health=17`, `Stamina=35`,
`Armor=3`, `Overshield=24`, `Oxygen=25`, `Memory=22` (RAM),
`SandevistanCharge=34`, `BerserkCharge=4`, `OpticalCamoCharges=23`,
`GrenadesCharges=15`, `HealingItemsCharges=16`,
`ProjectileLauncherCharges=29`, `CyberdeckOverclock=10`, `WeaponCharge=42`,
`WeaponOverheat=43`, `Poise=27`. (Leftovers of the cut multiplayer:
`CPOShockedTrigger=44`, `CPO_Armor=45`.)

**Owner sends**: health as a percentage (a byte in the state message is
enough for a health bar), max health once and on change. **Others apply**:
display only. The puppet's own Health pool must not decide anything: make
the puppet invulnerable locally (`gameGodModeSystem.AddGodMode(entID,
gameGodModeType.Invulnerable, reason)` [N]; `gameGodModeType`:
Invulnerable=0, Immortal=1, Mortal=2) and turn hits on it into damage
events for its owner (ARCHITECTURE, combat).

---

## 3. Weapons

### 3.1 Where the weapon is

- In hand: `TransactionSystem.GetItemInSlot(obj,
t"AttachmentSlots.WeaponRight") -> ItemObject` (cast to `WeaponObject`),
  and `t"AttachmentSlots.WeaponLeft"` (left hand: grenades, arm cyberware,
  dual wield) (final.reds:11868, 11874, 52211). `GetActiveItemInSlot(obj,
slot) -> ItemID`.
- Slot ids are TweakDBIDs; only six `AttachmentSlots.*` strings are in the
  CName pool (`Head`, `WeaponLeft`, `WeaponRight`, `VehicleTrunk`,
  `VehicleDoorFrontRight`, `VehicleGenericMappin`): build the others with
  `TDBID.Create` / a TweakDBID literal, and send them as numbers (§5 of
  engine-knowledge: no debug strings in release).
- `gameweaponObject.GetAttachedSlotID() -> TweakDBID` [N],
  `IsEquippedInLeftHand()`.
- Holstered weapons are not attached to the first-person V (nothing in the
  scripts places them on body slots for the player); the arms group names
  `holstered_default…` of engine-knowledge §5 are appearance groups, not slots.
- The engine tells scripts about slot changes:
  `TransactionSystem.RegisterAttachmentSlotListener(owner, callback:
AttachmentSlotsScriptCallback) -> AttachmentSlotsScriptListener` [N];
  the callback (properties `slotID`, `itemID` to filter) gets
  `OnItemEquipped(slot, item)`, `OnItemEquippedVisual`,
  `OnItemUnequipped`, `OnItemUnequippedComplete`, `OnAttachmentRefreshed`
  (final.reds:179660). Entity events `ItemAddedToSlot` /
  `ItemRemovedFromSlot` reach `PlayerPuppet.OnItemAddedToSlot`
  (final.reds:41064, 41142).

### 3.2 Equip, unequip, holster on the owner

The player never calls `AddItemToSlot` for weapons directly; requests go
to the `EquipmentSystem` and the PSM's equipment state machines do the
slot work.

- `EquipmentSystemWeaponManipulationRequest { requestType:
EquipmentManipulationAction; equipAnimType: gameEquipAnimationType;
removeItemFromEquipSlot: Bool }` (final.reds:178788), queued on the
  `EquipmentSystem` scriptable system.
  `EquipmentManipulationAction`: Undefined=0, RequestActiveMeleeware=1,
  RequestActiveWeapon=2, RequestSlotActiveWeapon=3, RequestLastUsedWeapon=4,
  RequestFirstMeleeWeapon=5, RequestLastUsedMeleeWeapon=6,
  RequestLastUsedOrFirstAvailableWeapon=7,
  RequestLastUsedOrFirstAvailableRangedWeapon=8,
  RequestLastUsedOrFirstAvailableMeleeWeapon=9,
  RequestLastUsedOrFirstAvailableOneHandedRangedWeapon=10,
  RequestLastUsedOrFirstAvailableDriverCombatRangedWeapon=11,
  RequestLastUsedOrFirstAvailableDriverCombatBikeWeapon=12,
  RequestHeavyWeapon=13, CycleWeaponWheelItem=14,
  CycleNextWeaponWheelItem=15, CyclePreviousWeaponWheelItem=16,
  DiscardEmptyWeapons=17, RequestConsumable=18, RequestGadget=19,
  RequestFists=20, RequestLeftHandCyberware=21, UnequipWeapon=22,
  UnequipConsumable=23, UnequipGadget=24, UnequipLeftHandCyberware=25,
  UnequipAll=26, ReequipWeapon=27, RequestWeaponSlot1..4=28..31,
  RequestNextThrowableWeapon=32.
- `DrawItemRequest { itemID; equipAnimationType; assignOnly }` (native,
  `gameDrawItemRequest`, final.reds:222556), `EquipRequest { itemID;
slotIndex; addToInventory; equipToCurrentActiveSlot }`,
  `UnequipRequest`, `GameplayEquipRequest` (final.reds:222545).
- Inside the PSM: `EquipmentManipulationRequest { requestType:
EquipmentManipulationRequestType (Undefined=0, Equip=1, Unequip=2,
DualEquip=3); requestSlot: EquipmentManipulationRequestSlot (Undefined=0,
Right=1, Left=2, Both=3); equipAnim }` (final.reds:179290), and the
  native `ItemEquipRequest { slotId; itemId; startingRenderingPlane }` /
  `ItemUnequipRequest { slotId; itemId; instant }` (final.reds:318093, 318156) passed as the state parameter `n"itemUnequipRequest"`
  (final.reds:516715).
- `gameEquipAnimationType`: Default=0, Instant=1, FirstEquip=2,
  HACK_ForceInstantEquip=3. `IsWeaponFirstEquip` (blackboard) says the
  flourish animation plays.
- Observable result: `UpperBody` goes `SwitchItems` (1) then back,
  `EquipmentRight` / `EquipmentLeft` change, the slot listener fires, the
  weapon receives `gameweaponeventsSetActiveWeaponEvent` /
  `RemoveActiveWeaponEvent` (`WeaponObject.OnSetActiveWeapon`).

**Owner sends** (reliable event `WeaponSlot`): slot (right/left), the
item's TweakDBID (numeric), a flag "drawn / holstered", and the attachment
TweakDBIDs that change the look (scope, muzzle: `AttachmentSlots.Scope`,
`AttachmentSlots.PowerModule`, final.reds:36308–36310). Trigger: the slot
listener, not a poll.

**Others apply** on the puppet (inferred, from how scripts dress NPCs and
preview puppets, engine-knowledge §13.4): `TransactionSystem.GiveItem(puppet, itemID, 1)`
then `AddItemToSlot(puppet, t"AttachmentSlots.WeaponRight", itemID)`;
remove with `RemoveItemFromSlot(puppet, slot, shouldDestroyEntity = true)`
[N]. Full signature of the add (final.reds:52154):

```
AddItemToSlot(obj, slotID: TweakDBID, itemID: ItemID, opt highPriority,
  opt itemObject: wref<ItemObject>, opt plane: ERenderingPlane,
  opt keepWorldTransform, opt ignoreRestrictions,
  opt garmentAppearanceName: CName, opt appearanceItem: ItemID,
  opt dontCacheEquippedItem) -> Bool
```

Use `ItemID.FromTDBID(tdbid)` for a fresh item; `ignoreRestrictions =
true` is the obvious thing to try if the slot refuses (engine-knowledge §12.1 saw
`AddItemToSlot` return true and attach nothing for clothes: check with
`GetItemInSlot` afterwards, and that V's puppet record declares
`WeaponRight` / `WeaponLeft` in its `attachmentSlots`). The AI way,
`AIEquipCommand { slotId; itemId: TweakDBID; failIfItemNotFound;
durationOverride }` / `AIUnequipCommand { slotId; durationOverride }`
(final.reds:294613), needs a running AI behaviour: not for a puppet whose
AI is off.

### 3.3 Weapon object: state and natives

`gameweaponObject` (script `WeaponObject`, size 0x540), natives [N]:
`GetCurrentAttack()`, `GetAttack(name)`, `GetAttacks()`,
`SetAttack(attackID: TweakDBID) -> Bool`, `GetCurrentTriggerMode()`,
`GetTriggerModes()`, `GetCurrentRangedAttack()`,
`OverrideRangedAttackPackage`, `DefaultRangedAttackPackage`,
`GetFxPackage()`, `GetFxPackageQuickMelee()`, `GetSharedData() ->
IBlackboard`, `GetTotalAmmoCount()`, `HasAmmoChangeRequest()`,
`HasPendingReload()`, `StartReload(opt durationOverride) -> Float`,
`StopReload(status: gameweaponReloadStatus)` (Standard=0, Interrupted=1),
`SetTriggerDown(Bool)`, `PrepareContinuousAttack(startPos, startDir)`,
`StartPreparedContinuousAttack()`, `StartContinuousAttack(startPos,
startDir)`, `StopContinuousAttack()`, `IsContinuousAttackStarted()`,
`HasScope()`, `GetScopeOffset()`, `GetIronSightOffset()`,
`GetMuzzleOffset()`, `GetMuzzleSlotWorldTransform() -> Transform`,
`IsSilenced()`, `IsTargetLocked()`, `UpdateTargetingSight(targetID,
targetPosition)`, `ShootStraight(Bool)`, `SetupBurstFireSound(n)`,
`SetWeaponEffects([WeaponVFXAction_Record])`, `RemoveWeaponEffects()`,
`IsControlledByPlayer()`, `SetVisible(Bool)`, `GetAttachedSlotID()`,
`AI_ShootAt`, `AI_ShootForwards`, `AI_ShootSelfOffScreen`,
`AI_SetAttackData(attack)`, `AI_PlayChargeStartedSound()`,
`AI_PlayMeleeAttackSound(isQuickMelee)`.

There is **no `IsReloading` and no `GetCurrentMuzzle`** native: reload is
`RightWeapon == Reload (2)` / `UpperBody == Reload (3)` or
`HasPendingReload()`; the muzzle is `GetMuzzleSlotWorldTransform()`.

Script side (in 2.50's RTTI): `IsRanged()`, `IsMelee()`, `IsHeavyWeapon()`,
`IsShotgun()`, `IsBlade()`, `IsBlunt()`, `IsMonowire()`,
`IsMantisBlades()`, `IsThrowable()`, `IsCharged()`, `IsMagazineEmpty()`,
`GetAIBlackboard()`, `GetStateMachineBlackboardID() -> BlackboardID_Int`
(which PSM field this weapon reports to: right or left),
`GetWeaponRecord()`, `GetCurrentDamageType()`, and the static
`WeaponObject.TriggerWeaponEffects(weapon, fxAction: gamedataFxAction, opt
fxBlackboard: worldEffectBlackboard)`.

`gamedataFxAction`: EnterCharge=0, EnterDischarge=1, EnterLowAmmo=2,
EnterNoAmmo=3, EnterOverheat=4, EnterReload=5, ExitCharge=6,
ExitDischarge=7, ExitLowAmmo=8, ExitNoAmmo=9, ExitOverheat=10,
ExitReload=11, ExitShoot=12, MeleeBlock=13, MeleeHit=14,
MuzzleBrakeShoot=15, Shoot=16, SilencedShoot=17.

Weapon events (RTTI `gameweaponevents*`): `ShootEvent` (0x1E0, no script
properties), `StopFiringEvent`, `ReloadStartEvent`, `ReloadFinishEvent`,
`ReloadInterruptedEvent`, `OwnerAimEvent {isAiming}`,
`SetActiveWeaponEvent`, `RemoveActiveWeaponEvent`,
`ChangeTriggerModeEvent`, `CycleTriggerModeEvent`, `SetAmmoCountEvent`,
`ConsumeMagazineAmmo`, `RequestAmmoChange`, `SetMaxChargeEvent`,
`PerfectChargeEvent`, `EnableSmartGunHandlerEvent`, `AIAttackAttemptEvent`.
A `StartFiringEvent` class does not exist in 2.50.

### 3.4 What happens when the player shoots

`ShootEvents.OnEnter` (final.reds:548101), the PSM state entered for each
shot (burst and full-auto re-enter it):

1. `weapon.SetAttack(attackRecord.GetID())`, `weapon.QueueEvent(new
WeaponPreFireEvent())`.
2. `scriptInterface.PushAnimationEvent(n"Shoot")` on the owner.
3. Blackboard weapon state = `Shoot` (8).
4. Stims from the owner's `StimBroadcasterComponent`
   (`gamedataStimType.Gunshot`, `SilencedGunshot`, `IllegalAction`), fact
   `player_fired_a_weapon`.
5. `WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.Shoot |
SilencedShoot | MuzzleBrakeShoot, effectBlackboard)` — muzzle flash,
   shells, sound; `effectBlackboard` carries `muzzleFxScale`,
   `shellsFxScale` from the weapon's stats.
6. `WeaponObject.SendAmmoUpdateEvent(owner, weapon)`.

The projectile or hit-scan itself is native (the weapon's attack). Scripts
on the projectile side receive `gameprojectileShootEvent` /
`gameprojectileShootTargetEvent` (`cb func OnShoot(eventData)`, e.g.
final.reds:175774): `owner`, `weapon`, `localToWorld` (Matrix),
`startPoint`, `startVelocity`, `weaponVelocity`, `params:
gameprojectileWeaponParams` { `targetPosition`, `charge`,
`trackedTargetComponent`, `smartGunAccuracy`, `smartGunIsProjectileGuided`,
`hitPlaneOffset`, `shootingOffset`, `ignoreWeaponOwnerCollision`,
`ignoreMountedVehicleCollision`, `range`, `ricochetData` }.

**Owner sends** (reliable or sequenced-unreliable event `Shot`, one per
shot, batched per frame for shotguns and full-auto): slot, attack
TweakDBID, origin and direction (or target position), charge. The hook is a
wrap of `ShootEvents.OnEnter` or, lower, a listener on the PSM
`ShootState` / `RightWeapon` field going to `Shoot`. Hits are separate
(ARCHITECTURE: hit events to the host).

### 3.5 How a puppet can fire (cosmetic)

How NPCs do it, `AIWeapon.Fire(weaponOwner, weapon, timeStamp,
tbhCoefficient, requestedTriggerMode, opt targetPosition, opt target, opt
rangedAttack, …)` (final.reds:173136) ends in one of three natives on the
weapon (final.reds:54056–54085):

```
AI_ShootAt(targetPositionProvider: IPositionProvider, targetObject,
  instigator: wref<GameObject>, ammoCost: Uint16,
  projectileParams: gameprojectileWeaponParams, projectilesPerShot: Uint8,
  charge: Float, opt maxSpread: Float, opt muzzleOffset: Vector4)
AI_ShootForwards(instigator, ammoCost: Uint16, projectileParams,
  projectilesPerShot: Uint8, charge: Float, opt overridePos: Vector4,
  opt overrideForward: Vector4, opt muzzleOffset: Vector4)
AI_ShootSelfOffScreen(targetObject: gamePuppet, ammoCost, projectileParams,
  projectilesPerShot, charge)
```

`AIWeapon.Fire` itself uses `AI_ShootForwards(owner, …, playerShotOrigin,
cameraForward)` when the owner is a `PlayerPuppet` (final.reds:173278), so
`overridePos` / `overrideForward` is the path for "shoot from here, that
way" — exactly what a mirrored shot needs. Around it: `AIWeapon
.OnStartShooting(weapon, n)`, `OnStopShooting(weapon, duration)` (kills
loop effects, `TriggerWeaponEffects(weapon, ExitShoot)`).

Two levels for the puppet:

- **Effects only**: `WeaponObject.TriggerWeaponEffects(puppetWeapon,
gamedataFxAction.Shoot)` + the pose stream's recoil. No projectile, no
  tracer. Cannot hurt anything: the safe first step.
- **Real shot, harmless**: `AI_ShootForwards(puppet, 0, params, n, charge,
origin, direction)` gives tracers, impacts and decals, but also **deals
  damage on this game** — the puppet's shots must then be made harmless
  (damage belongs to the shooter's game and the host). Candidates: a
  damage-system filter on instigator = puppet, or an attack record with
  zero damage via `weapon.SetAttack` (open question 4).

Vehicles: `vehicleBaseObject.NPCShoot(target: Vector4, projectiles:
Uint32)` [N], `EnableNPCCombat(Bool)`, `IsNPCShooting()`,
`GetActiveWeapons(out [WeaponObject])`, event
`vehicleMountedWeaponShootEvent { weaponID, slotID }` (§8.8).

### 3.6 Reload, charge, zoom, fire mode

- Reload: PSM weapon state `Reload` (2), `UpperBody.Reload` (3),
  `LatestWeaponReloadTime`; events `ReloadStartEvent` / `ReloadFinishEvent`
  / `ReloadInterruptedEvent` on the weapon; effects `EnterReload` /
  `ExitReload`. On a puppet: effects + sound; the hands are the pose
  stream's.
- Charge (tech weapons, bows of the mod list aside): weapon state
  `Charging` (1); stat pool `WeaponCharge` on the **weapon's** entity id;
  `UpdateWeaponChargeEvent`, effects `EnterCharge` / `ExitCharge` /
  `EnterDischarge`; NPC effect names `charging_tpp`, `charged`,
  `discharge_tpp` (final.reds:173112–173120). Overheat: `WeaponOverheat`
  pool, state `Overheat` (7).
- Zoom: `ZoomLevel`, `MaxZoomLevel`; scope presence `HasScope()`.
- Fire mode: `ToggleFireMode` pulse, `WeaponTriggerMode`;
  `GetCurrentTriggerMode() -> TriggerMode_Record`.
- Safe / lowered weapon: weapon state `Safe` (6), melee `Safe` /
  `PublicSafe`, blackboard `SceneSafeForced`.

### 3.7 Melee

State is entirely in `MeleeWeapon` (§1.3): equip, idle, hold, charged
hold, block, deflect, each attack kind. State classes: `MeleeIdleEvents`
(final.reds:547921) … `MeleeComboAttackEvents` (552482),
`MeleeFinalAttackEvents` (552532), `MeleeStrongAttackEvents` (552740),
`MeleeBlockEvents` (554334), `MeleeDeflectEvents` (554018),
`MeleeThrowAttackEvents` (554784), `MeleeLeapEvents` (555314),
`MeleeDashEvents` (555715), `MeleeBodySlamAttackEvents` (556093),
`MeleeCrouchAttackEvents` (556910), `MeleeJumpAttackEvents` (556940),
`MeleeSprintAttackEvents` (556975), `MeleeGroundSlamAttackEvents`
(557111); common `MeleeAttackGenericEvents` (288580). Blackboard extras:
`Melee` (Attack/Block), `MeleeLeap`, `IsInBodySlamState`, `BodySlamBump`,
`CanOnePunch`. Weapon side: `StartCurrentMeleeTrailEffect(attackSide)`,
`StopCurrentMeleeTrailEffect`, `PlayMeleeSound()`,
`AI_PlayMeleeAttackSound(isQuickMelee)` [N], effects `MeleeHit`,
`MeleeBlock`. Quick melee with a gun: weapon state `QuickMelee` (3),
`QuickMeleeEvents` (final.reds:546822). Thrown knives and axes:
`ProjectileLaunchHelper.SpawnProjectileFromRightHand` (final.reds:249358,
in `MeleeTransition`), §4.1.

**Owner sends**: `MeleeWeapon` in the state message (one byte); attacks
are animation, covered by the pose stream; hits are hit events. **Others
apply**: trail effect start/stop on state change, sounds.

---

## 4. Throwables, quickhacks, consumables, cyberware

### 4.1 Grenades and thrown weapons

- State: `CombatGadget` (§1.3). Flow: `CombatGadgetEquipEvents`
  (final.reds:521255, anim event `CombatGadgetEquip`, state 2) →
  `CombatGadgetChargeEvents` (522299, state 3) →
  `CombatGadgetChargedThrowEvents` (521697, anim event
  `CombatGadgetChargedThrow`, state 4) or `CombatGadgetQuickThrowEvents`
  (521556, `CombatGadgetQuickThrow`, state 6) → `…Throw(…)`
  (`CombatGadgetTransitions.Throw`, final.reds:517818) →
  `WaitForUnequip` (5). Anim feature `AnimFeature_CombatGadget {
isQuickthrow, isChargedThrow, isDetonated }`. The grenade item sits in
  `AttachmentSlots.WeaponLeft`.
- Launch (final.reds:220095, `ProjectileLaunchHelper`):
  `SpawnProjectileFromRightHand(owner, templateName, appearance, itemObj)`
  (position provider = slot `n"RightHand"`, orientation from the
  crosshair), `SpawnProjectileFromScreenCenter(...)`, both building a
  `gameprojectileSpawnerLaunchEvent { launchParams:
gameprojectileLaunchParams { launchMode, logicalPositionProvider,
logicalOrientationProvider, visualPositionProvider,
visualOrientationProvider, ownerVelocityProvider }, templateName: CName,
appearance: CName, owner, projectileParams }` queued **on the item
  object**, whose `gameprojectileSpawnComponent` (`projectileTemplates`,
  `spawnOffset`, `slotName`) spawns the projectile. Trajectory:
  `SetLinearLaunchTrajectory(projectileComponent, velocity)`,
  `SetCurvedLaunchTrajectory(...)` (final.reds:220222, 220249). Also
  `TransactionSystem.ThrowItem(owner, item,
gameprojectileSetUpAndLaunchEvent) -> Bool` [N] (final.reds:52260).
- The grenade is a `BaseGrenade` (final.reds:175462); it explodes by its
  own script on whichever game spawned it.

**Owner sends** (reliable `Throw`): item TweakDBID, origin, initial
velocity, charge/quick, a throw id. **Others apply**: spawn the same
projectile from the puppet's item with static position/orientation
providers (`IPositionProvider.CreateStaticPositionProvider`,
`IOrientationProvider.CreateStaticOrientationProvider` as used at
final.reds:220150) so the arc is the same; its explosion must be cosmetic
there (damage is decided by the thrower's game / the host), same problem
as §3.5.

### 4.2 Quickhacks cast by a player

- Player-side state: `Vision == Focus` (scanner up), `Crosshair ==
QuickHack (10)`, `UploadingQuickHackIDs` (Variant, list of action ids),
  stat pool `Memory` (RAM), `IsInMinigame` (breach). Overclock:
  `CyberdeckOverclock` pool, `OverclockListener` (final.reds:327631).
- The effect is on the **target**: upload progress is the target's
  `QuickHackUpload` stat pool, the result a status effect / device action
  on the target. Targets are NPCs and devices owned by the host: a guest's
  quickhack is a request to the host, like a hit. Not designed here; the
  milestone-5 research owns it.
- Nothing visible happens on the caster's body beyond the scanning pose:
  no event needed for the puppet (inferred from the absence of caster
  animation events in the quickhack scripts).

### 4.3 Consumables: inhaler, injector, food, drink

- States: `ConsumableStartupEvents` (final.reds:515422),
  `ConsumableUseEvents` (515899, pushes anim event `n"UseConsumable"`),
  `ConsumableCleanupEvents` (516533). Blackboard `Consumable` (Int).
  Item types: `gamedataItemType.Con_Inhaler=9`, `Con_Injector=10`,
  `Con_Edible=8`, `Con_LongLasting=11`; `gamedataConsumableBaseName`:
  Alcohol=0, BonesMcCoy70=1, CarryCapacityBooster=2, Drinkable=3,
  Edible=4, FirstAidWhiff=5, HealthBooster=6, MemoryBooster=7,
  OxyBooster=8, StaminaBooster=9. Charges: pool `HealingItemsCharges`.
  The item is shown in the left hand (`AttachmentSlots.WeaponLeft`,
  `RequestConsumable` / `UnequipConsumable` actions of §3.2).
- **Owner sends**: reliable `UseConsumable { item TweakDBID }`; health
  follows in the state message. **Others apply**: attach the item to the
  puppet's left slot for the duration; the arm is the pose stream's.

### 4.4 Arm cyberware

- Item types: `Cyb_MantisBlades=16`, `Cyb_NanoWires=17` (monowire),
  `Cyb_StrongArms=18` (gorilla arms), `Cyb_Launcher=15` (projectile launch
  system). Mantis blades, monowire and gorilla arms are **melee weapons**
  in `WeaponRight` (`WeaponObject.IsMantisBlades()`, `IsMonowire()`):
  `MeleeWeapon` covers them; `Whip` (`gamePSMWhip`) and
  `NanoWireLaunchMode` add the monowire's states. Their look on the body
  is an **arms appearance group** (`*_mantis`, `*_launcher`,
  `holstered_strong…`, engine-knowledge §5): equipping one changes the
  arms customization keys, so it must travel with the appearance sync.
- Projectile launcher: `LeftHandCyberware` (§1.3,
  `LeftHandCyberwareEquipEvents` final.reds:530890 … `ChargeAction`
  533778, `QuickAction` 533226, `Unequip` 537177), anim feature
  `AnimFeature_LeftHandCyberware { actionDuration, state, isQuickAction,
isChargeAction, isLoopAction, isCatchAction, isSafeAction,
chargeNormalized, hasCWPerk }`; launch through
  `ProjectileLaunchHelper.SpawnArmsLauncherProjectileWithRotation(owner,
templateName, itemObj, opt rotationRad)` or
  `SpawnProjectileFromScreenCenter` (final.reds:284247); round in
  `AttachmentSlots.ProjectileLauncherRound`; charges in
  `ProjectileLauncherCharges`.
- Status effects seen: `BaseStatusEffect.StrongArmsThermalActive`,
  `StrongArmsElecricActive` (sic), `StrongArmsChemicalActive`,
  `GorillaArmsOnePunchNPCMark`, `MantisBladesInvulnerableLeapRelicBuff`.

### 4.5 Legs and movement cyberware

Abilities are stat flags on the player (`gamedataStatType`, read with
`StatsSystem.GetStatValue(id, stat) > 0`): `HasDoubleJump`,
`HasChargeJump`, `HasAirHover`, `HasAirThrusters`, `HasSuperheroFall`,
`HasDodge`, `HasDodgeAir`, `HasKerenzikov`, `HasKerenzikovSlide`,
`HasSandevistan` (`…Tier1..3`), `HasBerserk`, `HasSecondHeart`,
`HasOpticalCamoRare/Epic/Legendary`. They need no sync: the resulting move
shows up in `LocomotionDetailed` (DoubleJump=19, ChargeJump=20,
HoverJump=21, DodgeAir=22, AirThrusters=15, AirHover=16,
SuperheroFall=17, SuperheroLand=27). Player effects started by the state
scripts: `n"dash"` on dodge (final.reds:553665), `n"landing_regular"`,
`n"landing_hard"`, `n"landing_very_hard"`, `n"landing_death"`
(final.reds:281021–281049, 289850) — these are effects of the
first-person player entity; whether the third-person body has them is
unknown.

### 4.6 Sandevistan, Kerenzikov, Berserk

- Sandevistan: `SandevistanEvents` (final.reds:524116): blackboard
  `TimeDilation = Sandevistan (1)`, global time dilation reason
  `n"sandevistan"` (ease-out curve `n"SandevistanEaseOut"`), status
  effects of type `gamedataStatusEffectType.Sandevistan` (51), charge pool
  `SandevistanCharge`. Two Sandevistan users: reason
  `n"sandevistanVersusSandevistan"` (final.reds:240501) — the game already
  has the concept of a character exempt from another's dilation.
- Kerenzikov: `KerenzikovEvents` (final.reds:527805): `Locomotion =
Kereznikov (3)` (spelled so), reason `n"kereznikov"`, curve
  `n"KerenzikovEaseOut"`, status `BaseStatusEffect.KerenzikovPlayerBuff`.
- Scanner slow-mo: `TimeDilationFocusModeEvents` (final.reds:529099).
- Berserk: `BaseStatusEffect.BerserkPlayerBuff`, status type `Berserk`
  (2), pool `BerserkCharge`, blackboard `Berserk`.
- Time functions: `TimeSystem.SetTimeDilation(reason, dilation, opt
duration, opt easeInCurve, opt easeOutCurve, opt listener)`,
  `UnsetTimeDilation(reason, opt easeOutCurve)`,
  `SetTimeDilationOnLocalPlayerZero(...)`,
  `SetIgnoreTimeDilationOnLocalPlayerZero(Bool)`,
  `IsTimeDilationActive(opt reason)`, `GetActiveTimeDilation(opt reason)`
  (final.reds:53855–53882); per entity
  `gameTimeDilatable.SetIndividualTimeDilation(reason, dilation, duration,
easeInCurve, easeOutCurve, ignoreGlobalDilation, useRealTime)`,
  `UnsetIndividualTimeDilation(easeOutCurve)`, `GetTimeDilationValue()`,
  `IsIgnoringGlobalTimeDilation()` [N] — the by-name tool for
  ARCHITECTURE's rule "the world never slows for one player": the owner's
  game keeps its local effect, others could speed the puppet up with an
  individual dilation > 1 (inferred; whether values above 1 are honoured is
  open question 9).

**Owner sends** (reliable `StatusEffects` delta): the applied effects that
have a visible side, by TweakDBID (see 4.8). **Others apply**: the matching
visual on the puppet, never the gameplay effect.

### 4.7 Optical camo

`OpticalCamoListener` (final.reds:269220) reacts to status effects tagged
`n"CamoActiveOnPlayer"`; effects `BaseStatusEffect.OpticalCamoCoolPerkShort`
and the `Cloaked` type (11); charges `OpticalCamoCharges`. Entity effects
used on NPCs: `n"cloak_on"`, `n"cloak_on_instant"`, `n"cloak_off"`,
`n"cloak_off_instant"` (final.reds:317732–318169) through
`GameObjectEffectHelper.StartEffectEvent(obj, name)`; NPC statuses
`BaseStatusEffect.Cloaked`, `SpecialCloaked_Hard`, `MaxtacCloaked`.
**Others apply** (inferred): `StartEffectEvent(puppet, n"cloak_on")` /
`n"cloak_off"` if the puppet's entity has those effects (NPC entities do;
V's cutscene template is unchecked), else hide the puppet's meshes.

### 4.8 Status effects, generally

`gameStatusEffectSystem` [N]: `ApplyStatusEffect(objID, statusEffectID,
opt instigatorID: TweakDBID, opt instigatorEntityID, opt applyCount, opt
direction, opt isSavable, opt proxyEntityID) -> Bool`,
`RemoveStatusEffect(objID, id, opt removeCount)`,
`RemoveAllStatusEffectOfType`, `GetAppliedEffects(objID, out
[StatusEffect])`, `GetAppliedEffectsWithTag`, `HasStatusEffect`,
`HasStatusEffectOfType`, `HasStatusEffectWithTag`,
`RegisterListener(objID, listener)` (`ScriptStatusEffectListener`:
`OnStatusEffectApplied(record)`, `OnStatusEffectRemoved(record)`),
`SetStatusEffectRemainingDuration`.

`gamedataStatusEffectType` values: Berserk=2, Bleeding=5, Blind=6,
Burning=10, Cloaked=11, Crippled=14, Defeated=17, DefeatedWithRecover=18,
EMP=19, Electrocuted=20, Exhausted=21, Grapple=23, Knockdown=28,
Poisoned=44, Regeneration=50, Sandevistan=51, Stagger=55,
StrongArmsActive=56, Stunned=57, Unconscious=60, UncontrolledMovement=61,
VehicleKnockdown=62, Wounded=64 (full list: `rtti.py enum
gamedataStatusEffectType`).

The owner's game registers one listener on its V and forwards
applied/removed ids (reliable). Whether applying a status effect to a
puppet plays its VFX without its gameplay (burning body, electrocution) is
open question 8.

---

## 5. Locomotion specials

For each: the signal the owner sends, and what else is needed besides the
pose.

| Move                       | Signal                                                                                                                                                                                                              | Besides the pose                                                                                                                                                                         |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| crouch                     | `LocomotionDetailed` Crouch=3, CrouchSprint=30                                                                                                                                                                      | collision capsule height on the puppet (inferred)                                                                                                                                        |
| sprint                     | Sprint=4                                                                                                                                                                                                            | —                                                                                                                                                                                        |
| slide                      | Slide=5, SlideFall=6                                                                                                                                                                                                | anim event `n"Slide"`; sound                                                                                                                                                             |
| dodge / dash               | Dodge=7; `DodgeTimeStamp`; status `DodgeBuff`, `PlayerJustDashed`                                                                                                                                                   | effect `n"dash"`                                                                                                                                                                         |
| air dash                   | DodgeAir=22; status `DodgeAirBuff`                                                                                                                                                                                  |                                                                                                                                                                                          |
| vault, climb               | Vault=9, Climb=8                                                                                                                                                                                                    | root moves through geometry: interpolate position without ground snapping                                                                                                                |
| ladder                     | Ladder=10, LadderSprint=11, LadderSlide=12, LadderJump=13; `LadderCameraParams`                                                                                                                                     | same                                                                                                                                                                                     |
| jump family                | Jump=18, DoubleJump=19, ChargeJump=20, HoverJump=21; `Fall`, `Landing`                                                                                                                                              | landing effects (§4.5)                                                                                                                                                                   |
| superhero landing          | SuperheroFall=17, SuperheroLand=27, SuperheroLandRecovery=28                                                                                                                                                        | shockwave is a gameplay attack: owner/host decides                                                                                                                                       |
| swimming                   | `HighLevel == Swimming (6)`; `Swimming` Surface=1, Diving=2, Climbing=3; pool `Oxygen`                                                                                                                              | water splashes                                                                                                                                                                           |
| carrying a body            | `Carrying`, `BodyCarrying` (PickUp=1 … Throw=6)                                                                                                                                                                     | the carried NPC is **mounted on the player**, slot `n"leftShoulder"`; state machine `n"CarriedObject"` (final.reds:92785); it is a host-owned NPC: needs a host event, not only the pose |
| takedown / grapple         | `Takedown` (EnteringGrapple=1, Grapple=2, Leap=3, Takedown=4), `TakedownTargetID`; NPC mounted in slot `n"grapple"` (final.reds:21182); status `Grappled`, `PlayerInFinisherWorkspot`, `FinisherActiveStatusEffect` | synchronized two-character animation: the NPC half must be played by its owner (host)                                                                                                    |
| finisher                   | `FinisherTarget` (EntityID)                                                                                                                                                                                         | same                                                                                                                                                                                     |
| workspot (sit, use device) | `IsInWorkspot == 1`, `Locomotion == Workspot (9)`, `IsInteractingWithDevice`                                                                                                                                        | see below                                                                                                                                                                                |
| knockdown / ragdoll        | Knockdown=29, Felled=31 + `Felled` bool; status `Knockdown`, `BikeKnockdown`, `VehicleKnockdown`, `UncontrolledMovement_Default`, `…_RagdollOffLedge`                                                               | a ragdolled V's bones are physics: the pose stream carries them if it samples after physics (open question 3)                                                                            |
| cover                      | `UsingCover`                                                                                                                                                                                                        | —                                                                                                                                                                                        |
| emotes                     | none exist for the player in the scripts                                                                                                                                                                            | photo-mode poses are not gameplay                                                                                                                                                        |

Workspots. `gameWorkspotGameSystem` [N]: `PlayInDevice(device, actor, opt
workspotStateFlavourName, opt actorDataCompName, opt deviceDataCompName,
opt syncSlotName, opt slideTime, …)`, `PlayInDeviceSimple(device, actor,
allowCameraMov, …)`, `StopInDevice(actor, opt posDelta, opt orientDelta)`,
`IsActorInWorkspot(actor) -> Bool`, `GetDeviceUser(deviceID)`,
`GetExtendedInfo(actor)`, `PlayNpcInWorkspot(npc, master, referenceEntity,
…)`, `StopNpcInWorkspot(actor)`, `SendJumpToAnimEnt(actor, animName,
instant)`, `SendFastExitSignal(...)`, `SendSlowExitSignal(...)`,
`MountToVehicle`, `UnmountFromVehicle`, `SwitchSeatVehicle` (§8.2).
Devices call `EnterWorkspot(target: Device, activator, opt freeCamera, opt
componentName)` (final.reds:214837). **Owner sends** the pose (a seated V
is just a pose) plus a reliable `Workspot { device EntityID, on/off }` so
the device shows as occupied; **others** should not run the workspot on
the puppet at first (the player's workspot animations are first-person
sets, like the rest of the gameplay graph).

Ragdoll classes for later: `entRagdollComponent`,
`entRagdollActivationRequestEvent`, `entRagdollApplyImpulseEvent`,
`entRagdollDisableEvent`, `entRagdollNotifyEnabledEvent` /
`…DisabledEvent`, `gamePuppet.CanRagdoll()` [N], script
`CreateDisableRagdollEvent(reason)`.

---

## 6. Proposed wire split

### 6.1 `PlayerState` (State channel, 30 Hz, must stay < 64 bytes)

Current message (protocol.hpp:101): `player`, `seq`, `timeUs`, `position`,
`orientation` (Quat), `velocity`, `stance` (u8), `flags` (u16). Proposed
(a change to an existing message: bumps `kProtocolVersion`):

| Field                       | Type          | Bytes    | Source                                                                                                                                                    |
| --------------------------- | ------------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `player`                    | PlayerId      | ≤4       | session                                                                                                                                                   |
| `seq`                       | u32           | 4        |                                                                                                                                                           |
| `timeUs`                    | u64           | 8        | host clock                                                                                                                                                |
| `position`                  | 3 × f32       | 12       | `GetWorldPosition`                                                                                                                                        |
| `velocity`                  | 3 × i16, cm/s | 6        | `GetVelocity` (±327 m/s)                                                                                                                                  |
| `yaw`                       | u16           | 2        | `GetWorldYaw`; a full quaternion only matters when mounted or ragdolled, where the vehicle or the pose gives it                                           |
| `aimPitch`, `aimYaw`        | 2 × i16       | 4        | camera forward relative to the body (§2.2)                                                                                                                |
| `locomotion`                | u8            | 1        | `LocomotionDetailed`                                                                                                                                      |
| `upperBody`                 | u8            | 1        | `UpperBody`                                                                                                                                               |
| `weaponRight`, `weaponLeft` | 2 × u8        | 2        | `RightWeapon`, `LeftWeapon`                                                                                                                               |
| `melee`                     | u8            | 1        | `MeleeWeapon`                                                                                                                                             |
| `leftHand`                  | u8            | 1        | high nibble `CombatGadget`, low nibble `LeftHandCyberware`                                                                                                |
| `vehicle`                   | u8            | 1        | `Vehicle` (gamePSMVehicle)                                                                                                                                |
| `packed`                    | u16           | 2        | `Combat` (2 bits), `Swimming` (2), `Takedown` (3), `BodyCarrying` (3), `Vitals` (2), `TimeDilation` (2), `Stamina` (2)                                    |
| `flags`                     | u16           | 2        | on ground, aiming, scanner (`Vision`), in workspot, carrying, dual wielding, cloaked, using cover, felled, safe weapon, in menu/paused, in scene tier ≥ 3 |
| `zoom`                      | u8            | 1        | `ZoomLevel` quantised                                                                                                                                     |
| `health`                    | u8            | 1        | Health percent (0–200 steps of 0.5 %)                                                                                                                     |
| **total**                   |               | **≤ 55** |                                                                                                                                                           |

The pose stream is its own message (pose-streaming.md), as is the vehicle
state (§8.9). When V is mounted, `position` is still sent (cheap) but the
receiver places the puppet by its seat.

### 6.2 Reliable events (Event channel)

| Event                      | Fields                                               | Trigger on the owner                                   |
| -------------------------- | ---------------------------------------------------- | ------------------------------------------------------ |
| `WeaponSlot`               | slot, item TDBID, part TDBIDs, drawn                 | attachment-slot listener (§3.1)                        |
| `Shot`                     | slot, attack TDBID, origin, direction, charge, count | wrap `ShootEvents.OnEnter` / `ShootState`              |
| `Reload`                   | slot, start/finish/interrupted                       | weapon state                                           |
| `Throw`                    | item TDBID, origin, velocity, kind                   | `CombatGadgetTransitions.Throw`, melee throw, launcher |
| `UseConsumable`            | item TDBID                                           | `ConsumableUseEvents.OnEnter`                          |
| `StatusEffects`            | added / removed TDBIDs                               | status effect listener                                 |
| `Appearance` / `Equipment` | (engine-knowledge §5, §13.4)                         | equipment change, incl. arm cyberware                  |
| `Mount` / `Unmount`        | vehicle id, seat CName hash, instant                 | `gamemountingMountingEvent` on the player (§8.2)       |
| `Workspot`                 | device id, on/off                                    | `IsInWorkspot`                                         |
| `CarryBody` / `Takedown`   | NPC id, phase                                        | `BodyCarrying`, `Takedown` (host-mediated)             |
| `Vitals`                   | alive / downed / dead, cause                         | §7                                                     |
| `Teleport`                 | position, yaw, reason                                | fast travel, regroup (§9)                              |
| `Ping`                     | position or entity, kind                             | §10.3                                                  |

`Shot` may go unreliable-sequenced if full-auto floods the reliable
channel: a lost muzzle flash is harmless, a late one is ugly.

---

## 7. Health, death, downed, revive

### 7.1 The vanilla flow

1. Health reaches zero: `HighLevelTransition`'s decisions test
   `StatPoolsSystem.HasStatPoolValueReachedMin(player, Health)`
   (`ToDeath`, e.g. final.reds:525674) and the PSM enters `DeathEvents`
   (final.reds:527754).
2. `StartDeathEffects` (final.reds:283766): custom death animation
   (`AnimFeature_PlayerDeathAnimation`, input `n"DeathAnimation"`),
   `ForceTemporaryUnequip`, `HighLevel = SceneTier1`, **`Vitals = Dead
(1)`**, `AnimFeature_PlayerVitals { state = 1, stateDuration =
TDB player.deathMenu.delayToDisplay (3.0) }`, sounds
   `global_death_enter`, `ui_death`.
3. `SetIsResurrectionAllowedBasedOnState` (final.reds:283784): if stat
   `ForcePreventResurrect == 0` and `HasSecondHeart` and the landing was
   not `DeathLand` and no `BaseStatusEffect.ForceKill` → state parameter
   `isResurrectionAllowed = true`, return. **Otherwise**: blackboard
   **`DisplayDeathMenu = true`** (unless `IsDeathMenuBlocked`) and Health
   forced to 0.
4. Second heart: `DeathDecisionsWithResurrection.ToResurrect`
   (final.reds:528472) after `stateDuration` (3.0 s) → `ResurrectEvents`
   (final.reds:529013): status `BaseStatusEffect.SecondHeart`, `Vitals =
Resurrecting (2)`, anim events `n"PlayerResurrect"` then
   `n"PlayerResurrected"`, a `ResurrectEvent` on the player →
   `PlayerPuppet.OnResurrected` (final.reds:45083).
   `PlayerPuppet.Revive(percAmount)` (final.reds:45327) just sets Health
   to a percentage.
5. Death screen: `gameuiInGameMenuGameController` listens to
   `DisplayDeathMenu` (final.reds:347222) → `OnDisplayDeathMenu(value)`
   (347380) → delays a `DeathMenuDelayEvent` (TDB
   `player.deathMenu.delayToDisplay` or `…delayToDisplayKillTrigger`) →
   `OnDeathScreenDelayEvent` (347396) → `SwitchMenu(n"death_menu")`
   (334556) → `DeathMenuGameController` (final.reds:335125; "load last
   save").
6. Entity side: `gameDeathEvent` → `PlayerPuppet.OnDeath`
   (final.reds:45314): closes the radial wheel, applies
   `GameplayRestriction.BlockAllMenu`, telemetry, invalidates autosave
   requests; `ScriptedPuppet.OnDied` (21687) sets
   `GetPuppetPS().SetIsDead(true)`.
7. Other inputs: `ScriptedPuppet.Kill(opt instigator, …)`
   (final.reds:21286) sets Health to 0 ignoring change mode;
   `BaseStatusEffect.ForceKill`; `EDeathType`: Ground=0, Air=1,
   Swimming=2; fall deaths through `Landing == DeathLand`.

### 7.2 Where to intercept for a "downed" state

In order of how little they disturb:

- **`OnDisplayDeathMenu` already returns early `if IsMultiplayer()`**
  (final.reds:347383). `IsMultiplayer()`, `IsClient()`, `IsServer()`,
  `IsHost()` are native globals **without a handler in 2.50** (RTTI),
  called 52 times in the scripts: remains of the cut multiplayer. They
  cannot be turned on by name; but the place tells where the game itself
  meant to suppress the death screen. Wrap
  `gameuiInGameMenuGameController.OnDisplayDeathMenu` and swallow it while
  a co-op session is live: V lies dead (Vitals = Dead, death animation
  played), no menu, nothing reloaded. This is the minimal "downed".
- **Never reach zero**: `gameGodModeSystem.AddGodMode(player,
gameGodModeType.Immortal, n"tandem")` [N] — Immortal takes damage but
  does not die (used for vehicles and bosses, final.reds:92044, 125203).
  The PSM never enters `DeathEvents`; tandem detects "health at the
  floor", applies its own downed state (a knockdown status such as
  `BaseStatusEffect.Knockdown`, input restrictions through
  `GameplayRestriction.*` status effects) and decides death itself. More
  control, more to build; avoids the death animation that has no "get up".
- **Reuse the second heart path**: force `isResurrectionAllowed` and delay
  `ToResurrect` until a revive arrives: `ResurrectEvents` is the only
  vanilla "stand back up" for a dead V. Needs wraps of
  `SetIsResurrectionAllowedBasedOnState` and
  `DeathDecisionsWithResurrection.ToResurrect` (state-machine script
  classes; wrapping their methods works like any script class) (inferred).
- Revive = `PlayerPuppet.Revive(percAmount)` semantics: set Health, then
  leave the downed state. In the authority table a revive is a **request
  to the owner**: the reviver's game sends `ReviveRequest { target }`,
  the owner's game applies it.
- Full wipe (everyone down): the host decides (load a checkpoint for all);
  out of scope here.

**Owner sends**: `Vitals` in the state message + reliable `Vitals` event
(downed, dead, revived). **Others apply**: the pose stream shows the body
on the ground; the puppet's `gamemappinsRemotePlayerMappin.vitals` (§10.2)
and nameplate show the state; interaction "revive" on the puppet.

Vehicles: a V downed while seated — NCO's sequence (instant unmount, force
the workspot out) is in engine-knowledge §6.

---

## 8. Vehicles

### 8.1 Classes

| Class (RTTI)                | Size  | Parent       | Script name                           |
| --------------------------- | ----- | ------------ | ------------------------------------- |
| `vehicleBaseObject`         | 0xBC0 | `gameObject` | `VehicleObject`                       |
| `vehicleWheeledBaseObject`  | 0xC10 | base         | `WheeledObject`                       |
| `vehicleCarBaseObject`      | 0xC60 | wheeled      | `CarObject`                           |
| `vehicleArmedCarBaseObject` | 0xD20 | car          | (weaponized cars)                     |
| `vehicleBikeBaseObject`     | 0xC10 | wheeled      | `BikeObject`                          |
| `vehicleAVBaseObject`       | 0xBC0 | base         | `AVObject`                            |
| `vehiclePreventionAVObject` | 0xBD0 | AV           |                                       |
| `vehicleTankBaseObject`     | 0xC20 | base         | `TankObject` (the Basilisk, "Panzer") |

`gamedataVehicleType`: Bike=0, Car=1, Panzer=2. Components and systems:
`VehicleComponent` (script, on every vehicle; `vehicle.GetVehicleComponent()`),
`VehicleComponentPS` (script persistent state; `vehicle.GetVehiclePS()`),
`vehicleController` (native component; `vehicle.GetAccessoryController()`)
and `vehicleControllerPS` (native; `GetVehicleControllerPS()`),
`vehicleChassisComponent` (0x190, **no script natives**),
`vehicleVehicleMountableComponent`, `vehicleCameraManager`
(`vehicle.GetCameraManager()`), `vehicleTPPCameraComponent`,
`gameVehicleSystem` (native since 2.5), `vehicleMoveSystem` (0x90, **no
script natives**), `gameNativeAutodriveSystem`.

Per kind, natives beyond the base:

- wheeled: `ToggleBrokenTire(tireID: Uint32, toggle: Bool) -> Bool`,
  `GetFlatTireIndex() -> Int32`;
- bike: `IsTiltControlEnabled()`, `EnableTiltControl(Bool)`,
  `GetCustomTargetTilt()`, `SetCustomTargetTilt(Float)`,
  `IsPerformingAWheelieOrEndo(minCOMOffset, minPitchAngle)`; blackboard
  `VehicleDef.BikeTilt`;
- car, AV, tank: none by name (AV thrusters through
  `VehicleQuestAVThrusterEvent { enable }` and PS `thrusterState`; script
  `AVObject.TurnOnThrusters()`).

### 8.2 Mounting

Data (RTTI):

```
gamemountingMountingSlotId { id: CName }
gamemountingMountingInfo   { childId: EntityID; parentId: EntityID;
                             slotId: MountingSlotId }
gamemountingMountingRequest (IScriptable) {
  lowLevelMountingInfo; preservePositionAfterMounting: Bool;
  mountData: MountEventData }
gamemountingUnmountingRequest { lowLevelMountingInfo; mountData;
                                delay: Float }
gameMountEventData { initialTransformLS: Transform;
  mountParentEntityId: EntityID; slotName: CName; entryAnimName: CName;
  isInstant: Bool; entrySlotName: CName;
  setEntityVisibleWhenMountFinish: Bool;
  removePitchRollRotationOnDismount: Bool; ignoreHLS: Bool;
  switchRenderPlane: Bool; isJustAttached: Bool; isCarrying: Bool;
  allowFailsafeTeleport: Bool; mountEventOptions: MountEventOptions }
gameMountEventOptions { entityID: EntityID; alive: Bool;
  occupiedByNonFriendly: Bool; silentUnmount: Bool }
gamemountingMountingRelationship { otherMountableType: gameMountingObjectType;
  otherMountableSubType: gameMountingObjectSubType;
  otherObject: wref<GameObject>;
  relationshipType: gameMountingRelationshipType; slotId }
gamemountingMountingEvent / UnmountingEvent { request; relationship }
```

Enums: `gameMountingObjectType` Invalid=-1, Object=0, Vehicle=1, Puppet=2,
Platform=3; `gameMountingObjectSubType` Invalid=-1, Car=0, Bike=1;
`gameMountingRelationshipType` Invalid=-1, Parent=0, Child=1;
`gameMountingSlotRole` Invalid=-1, Driver=0, Passenger=1, Passenger_FR=2,
Passenger_BL=3, Passenger_BR=4.

Facility (`GameInstance.GetMountingFacility(game)`,
`gamemountingIMountingFacility` [N]): `Mount(request)`,
`Unmount(request)`, `IsMountedToAnything(child: EntityID) -> Bool`,
`GetMountingInfoSingleWithObjects(opt child, opt parent, opt slotId) ->
MountingInfo`, `GetMountingInfoSingleWithIds(opt childId, opt parentId,
opt slotId)`, `GetMountingInfoMultipleWithObjects(…) -> [MountingInfo]`,
`GetMountingInfoMultipleWithIds(…)`; statics `InfoHasParent`,
`InfoHasChild`, `InfoIsComplete`, `SlotIsValid`,
`RelationshipGetOtherObject`.

Slot names (`vehicleEVehicleDoor` uses the same words):
`seat_front_left` (driver: `VehicleComponent.GetDriverSlotName()`,
final.reds:89325), `seat_front_right` (`GetFrontPassengerSlotName()`),
`seat_back_left`, `seat_back_right`; `trunk`, `trunk_body` (a body in the
trunk); on puppets `leftShoulder` (carried body), `grapple`. A bike's
passenger seat is `seat_back_left` (inferred from
`gameMountingSlotRole` and the seat helpers; check on a bike record:
`VehicleComponentPS.GetSeats() -> [VehicleSeat_Record]`).
`vehicle.HasOccupantSlot(slotName) -> Bool` [N] validates a seat;
`vehicle.GetSlotIdForMountedObject(obj) -> CName` [N] answers "which
seat"; `GetBoneNameFromSlot(slot)` [N].

The player's flow:

1. Interaction on a door → `gamevehicleVehicleMountableComponent
.MountEntityToSlot(parentID, childId, slot, mountType: MountType)`
   (final.reds:291016): fills a `MountingRequest`
   (`preservePositionAfterMounting = true`, options `alive`,
   `occupiedByNonFriendly`, `entityID` of the NPC to pull out; `MountType
.Hijack` → `vehicle.PreHijackPrepareDriverSlot()`) →
   `MountingFacility.Mount(request)`.
2. The engine sends `MountingEvent` to both ends:
   `PlayerPuppet.OnMountingEvent` (final.reds:40432) sets
   `MountedToVehicle`, `MountedToCombatVehicle`, `InArmoredVehicle`,
   `MountedPreventFirstEquip`, remembers `m_mountedVehicle`;
   `VehicleComponent.OnMountingEvent` (final.reds:90563) sets
   `MountedToVehicleInDriverSeat`, registers input, opens the door (PS
   event with `GetBoneNameFromSlot`), disables ragdoll on instant mounts…
3. The PSM gets its `Vehicle` state machine: `EnteringEvents.OnEnter`
   (final.reds:548360) plays the entry through the workspot system:

   ```
   WorkspotGameSystem.MountToVehicle(parent: vehicle, child: player,
     slideTime, animDelay, workspotResourceContainer: n"OccupantSlots",
     slotName, opt syncronizedObjects: [EntityID],
     opt entrySlotName: CName, opt animVariables: [CName]) -> Bool
   ```

   `entrySlotName` values seen: `n"stealing"`, `n"deadstealing"`,
   `n"enter_fast"` (combat entry). Then `DriveEvents` (548714) /
   `PassengerEvents` (548476) / `GunnerEvents` (548570); seat change
   `SwitchSeatsEvents` (549466) → `SwitchSeatVehicle(parent, child,
n"OccupantSlots", slotName, opt exitSlotName, …)`.

4. Leaving: `ExitingEvents` (final.reds:288499) and the variants
   `SpeedExitingEvents` (551280), `CoolExitingEvents` (551468),
   `CollisionExitingEvents` (550742), `DeathExitingEvents` (551214),
   `ImmediateExitWithForceEvents` (550633) → `WorkspotGameSystem
.UnmountFromVehicle(parent, child, opt instant, opt posDelta, opt
orientDelta, opt exitSlotName)` (final.reds:283196) and
   `MountingFacility.Unmount`. `vehicle.CanUnmount(isPlayer, mountedObject,
opt checkSpecificDirection: vehicleExitDirection) ->
vehicleUnmountPosition { direction, position: WorldPosition }` [N]
   picks the side (`vehicleExitDirection`: NoDirection=-1, Left=0,
   Right=1, Front=2, Back=3, Top=4).

How NPCs are seated by script — three by-name ways:

- **AI event** (what NCO used; needs the puppet's AI to handle it):
  `MountAIEvent { name = n"Mount"; data: MountEventData { slotName,
mountParentEntityId, isInstant, ignoreHLS = true } }` queued on the
  character (final.reds:485530, 495358); leave with an `AIEvent` named
  `n"ExitVehicle"` (final.reds:88693, 89927).
- **AI command**: `AIMountCommand { mountData }` inside an
  `AICommandEvent` (final.reds:322145), `AIUnmountCommand`.
- **Facility directly** (no AI): `VehicleComponent.MountEntityToSlot(
parentID, childId, slot)` (final.reds:92792) — builds the request with
  `preservePositionAfterMounting = true`, options `alive = false`,
  `occupiedByNonFriendly = false`, and calls `MountingFacility.Mount`. It
  is `protected` and used for trunk bodies; the same nine lines can be
  written in tandem's script with `isInstant = true`.

For a puppet whose AI is off, the facility call (third way) followed, if
the body does not sit, by `WorkspotGameSystem.MountToVehicle(vehicle,
puppet, 0.0, 0.0, n"OccupantSlots", slot)` is the candidate (inferred;
open question 5). Passenger animation sets:
`vehicle.GetAnimsetOverrideForPassenger(slotName) -> CName` [N],
`VehicleComponent.SetAnimsetOverrideForPassenger(passenger, value) ->
[CName]` (final.reds:90188).

Helpers (static, `VehicleComponent`, final.reds:89325–90508):
`IsMountedToVehicle(gi, owner | ownerID)`, `IsDriver(gi, owner)`,
`GetVehicle(gi, owner, out vehicle)`, `GetDriver(gi, vehicle,
vehicleID)`, `GetDriverMounted(gi, vehicleID)`, `GetAllPassengers(...)`,
`QueueEventToAllPassengers(...)`, `IsSlotAvailable(gi, vehicle, slot)`,
`IsSlotOccupied(gi, vehicleID, slot)`, `GetMountedSlotName(gi, owner, out
slot)`, `GetSeats(...)`, `IsDriverSlot(slot)`, `GetVehicleAllowsCombat`,
`OpenDoor(vehicle, slotID, opt delay)`; `PlayerPuppet.GetMountedVehicle()`
(final.reds:40497); `vehicle.IsPlayerMounted()`, `IsPlayerDriver()`,
`HasPassengers()` [N]; `VehicleComponentPS.GetNpcOccupiedSlots()`,
`IsSlotOccupiedByNPC(slot)`, `ToggleReserveSeatDuringUnmounting(active,
slotID)`; event `VehicleSeatReservationEvent` (final.reds:215184),
`VehicleStartedUnmountingEvent`.

**Owner (the mounting player's game) sends** `Mount { vehicle, seat hash,
instant }` / `Unmount { vehicle, seat, exit direction, instant }` from its
own `OnMountingEvent` / `OnUnmountingEvent` (job-thread rule: enqueue).
**Others apply**: mount the puppet into the same seat of their copy of the
vehicle, instantly; the entry/exit animation comes from the pose stream if
it is running, else is skipped. Exits name vehicle **and** seat
(engine-knowledge §6).

### 8.3 `vehicleBaseObject` natives (2.50, all [N] unless noted)

Identity and state: `GetRecord()`, `GetRecordID() -> TweakDBID`,
`GetBlackboard() -> IBlackboard`, `IsPlayerVehicle()`,
`IsPlayerActiveVehicle()`, `IsCrowdVehicle()`, `IsVehicleParked()`,
`HasTrafficSlot()`, `IsHackable()`, `SetIsHackable(Bool)`,
`GetDistanceToPlayerSquared()`, `GetTotalMass()`, `GetUIComponents()`,
`GetAIComponent() -> AIVehicleAgent`, `GetCustomizationComponent()`,
`GetAccessoryController() -> vehicleController`, `GetCameraManager()`.

Engine: `TurnVehicleOn(on)`, `TurnEngineOn(on)`,
`LockVehicleOnState(shouldLock)`, `IsVehicleTurnedOn()`,
`IsEngineTurnedOn()`, `IsVehicleOnStateLocked()`.

Motion and physics: `GetLinearVelocity() -> Vector4`, `IsInAir()`,
`IsFlippedOver()`, `IsLeaningOnOneWheel()`, `IsSkidding(threshold)`,
`AreFrontWheelsCentered()`, `IsAirControlEnabled()`,
`EnableAirControl(Bool)`, `ForceBrakesFor(seconds)`,
`ForceBrakesUntilStoppedOrFor(secondsToTimeout, callback)`,
`ActivateTemporaryLossOfControl()`, **`PhysicsWakeUp()`**,
`IsInTrafficPhysicsState()`, `IsInQuestOrScenePhysicsState()`,
`AddCollisionForce(Vector4)`, `GetCollisionForce()`, `ApplyAvgZOffset()`,
`ApplyPermanentStun()`; script-visible properties `isOnGround` (+0x25C),
`acceleration` (+0x264), `deceleration` (+0x268), `isReversing` (+0x2AB),
`burnout` (+0x2C4), `isVehicleOnStateLocked` (+0x6F2).

AI and traffic: `IsExecutingAnyCommand()`, `IsChasingTarget()`,
`IsPerformingPanicDriving()`, `CanStartPanicDriving()`,
`EnableHighPriorityPanicDriving()`, `IsPerformingSceneAnimation()`,
`IsAutoDriveModeEnabled()`, `CommandsFromDriverEnabled()`,
`GetCurrentSlotLocalPathLength/Progression/EstimatedTimeToArrival()`,
police strategy getters/setters.

Remote control (the netrunner quickhack and the Twin-Tone-style remote
driving): `IsVehicleRemoteControlled()`,
`SetVehicleRemoteControlled(enable, shouldUnseatPassengers,
shouldModifyInteractionState)`, `ToggleVehicleRemoteControlCamera()`,
`ActivateNetrunnerQuickhack(vehicleVehicleNetrunnerQuickhackType)`
(ForceBrakes=0, Explode=1, Accelerate=2, NONE=3),
`IsVehicleAccelerateQuickhackActive()`,
`IsVehicleForceBrakesQuickhackActive()`.

Accessories: `ToggleHorn(toggle, opt isPolice)`, `ToggleSiren(toggle)`,
`ToggleRadioReceiver(toggle)`, `SetRadioReceiverStation(index: Uint32)`,
`NextRadioReceiverStation()` (no handler), `SetRadioTier(tier,
overrideTier)`, `IsRadioReceiverActive()`,
`GetRadioReceiverStationName() -> CName`, `GetRadioReceiverTrackName()`,
`NotifyWindowChange(windowName, isOpened)`.

Damage: `DetachPart(partName: CName)`, `DetachAllParts()`,
`SetHasExploded()`, `SetDestructionGridPointValues(layer: Uint32, values:
[Float; 15], accumulate: Bool)`, `DestructionResetGrid()`,
`DestructionResetGlass()`, `ShouldDamageSystemIgnoreHit(component)`,
`TrySetHitCooldown()`.

Seats: `HasOccupantSlot`, `GetSlotIdForMountedObject`,
`GetBoneNameFromSlot`, `GetAnimsetOverrideForPassenger…`, `CanUnmount`,
`DetermineCoolExitImpulseLevel(...)`, `PreHijackPrepareDriverSlot()`,
`SendDelayedFinishedMountingEventToPS(isMounting, slotID, character,
delay)`.

Weapons: `IsArmedVehicle()`, `CanSwitchWeapons()`, `GetActiveWeapons(out
[WeaponObject])`, `EnableNPCCombat(Bool)`, `NPCShoot(target, projectiles)`,
`IsNPCShooting()`.

### 8.4 The vehicle blackboard (`VehicleDef`, `vehicle.GetBlackboard()`)

| Field                                                       | Type        | Meaning                                                    |
| ----------------------------------------------------------- | ----------- | ---------------------------------------------------------- |
| `SpeedValue`                                                | Float       | speed (sign = reverse)                                     |
| `GearValue`                                                 | Int         | current gear                                               |
| `RPMValue`, `RPMMax`                                        | Float       | engine speed                                               |
| `BikeTilt`                                                  | Float       | lean angle                                                 |
| `SuspensionTransversalForce`, `SuspensionLongitudinalForce` | Float       |                                                            |
| `MeanSlipRatio`                                             | Float       | tyre slip                                                  |
| `IsHandbraking`                                             | Int         |                                                            |
| `HandlingPenalty`, `DeclutchTimer`, `EngineTemp`            | Float       |                                                            |
| `IsAutopilotOn`                                             | Bool        |                                                            |
| `HeadLightMode`                                             | Int         | `vehicleELightMode`                                        |
| `VehicleState`                                              | Int         | `vehicleEState` (Default=1, On=2, Disabled=3, Destroyed=4) |
| `VehicleReady`                                              | Bool        |                                                            |
| `VehRadioState`, `VehRadioStationName`                      | Bool, CName |                                                            |
| `IsCrowd`, `IsUIActive`, `Collision`, `IsInWater`           | Bool        |                                                            |
| `DamageState`                                               | Int         | damage level 0–3                                           |
| `GameTime`, `RaceTimer`                                     | String      | UI                                                         |
| `IsTargetingFriendly`, `UseCarAlarmStim`                    | Bool        |                                                            |
| `Chase…`, `MinimapMappinDeleteAnim`                         |             | AI/UI                                                      |
| `VehicleCustomization…` (6 fields)                          |             | CrystalCoat state                                          |

There is **no steering angle, throttle or brake input** on the blackboard
or as a native: wheel steering and suspension travel on a puppet car must
come from the driver's inputs sampled another way (the wheel animation
inputs of the vehicle's animation graph, or `vehicle::PhysicsData` in the
SDK struct) or be derived from the motion (yaw rate and speed) on the
receiving side (inferred; open question 7).

### 8.5 Doors, windows, lights, horn, siren, radio

- Native PS `vehicleControllerPS` [N]: `GetState() / SetState(vehicleEState)`,
  `SetHeadLightMode(vehicleELightMode)`, `GetHeadLightMode()`,
  `CycleHeadLightMode(isPlayer)`, `SetAlarm(Bool)`, `IsAlarmOn()`,
  `SetDoorState(door: vehicleEVehicleDoor, state: vehicleVehicleDoorState,
opt immediate)`, `GetDoorState(door)`, `SetWindowState(door, state:
vehicleEVehicleWindowState)`, `GetWindowState(door)`,
  `SetDoorInteractionState(door, vehicleVehicleDoorInteractionState)`,
  `GetDoorInteractionState(door)`; properties `vehicleDoors` (6 ×
  `vehicleVehicleSlotsState`), `state`, `isAlarmOn`, `lightMode`,
  `lightTypeMask`.
- Script PS `VehicleComponentPS` wraps them (`SetDoorState`,
  `GetDoorState`, `SetWindowState`, `OpenAllVehDoors(forceScene)`,
  `CloseAllVehDoors`, `OpenAllVehWindows`, `LockAllVehDoors`…), and holds
  `sirenOn`, `sirenSoundOn`, `sirenLightsOn`, `crystalDomeState`,
  `thrusterState`, `anyDoorOpen`, `isDestroyed`, `isStolen`, `exploded`,
  `submerged`, `hornEnabled`, `npcOccupiedSlots`, `playerVehicle`.
- Enums: `vehicleEVehicleDoor` seat_front_left=0, seat_front_right=1,
  seat_back_left=2, seat_back_right=3, trunk=4, hood=5;
  `vehicleVehicleDoorState` Closed=0, Open=1, Detached=2;
  `vehicleEVehicleWindowState` Closed=0, Open=1;
  `vehicleVehicleDoorInteractionState` Available=0, Locked=1, Disabled=2,
  QuestLocked=3, Reserved=4; `vehicleELightMode` Off=0, On=1,
  HighBeams=2; `vehicleELightType` (bit mask) Head=1, Brake=2,
  LeftBlinker=4, RightBlinker=8, Blinkers=12, Reverse=16, Interior=32,
  Default=47, Utility=64.
- Lights by component: `vehicleController` [N] `ToggleLights(on, opt
lightType, opt inTime, opt lerpCurve, opt loop)` (final.reds:216245),
  `SetLightColor(type, color, inTime, forceOverrideEmissiveColor)`,
  `SetLightStrength`, `SetLightParameters`, `Reset…`,
  `AreDefaultLightsToggled()`.
- Events that already drive a vehicle from outside (quest events; queue on
  the vehicle entity): `VehicleQuestChangeDoorStateEvent { door:
EVehicleDoor; newState: EQuestVehicleDoorState; forceScene }`
  (final.reds:215456; states ForceOpen=0, ForceClose=1, OpenAll=2,
  CloseAll=3, ForceLock=4, ForceUnlock=5, LockAll=6, EnableInteraction=7,
  DisableInteraction=8, DisableAllInteractions=9, ResetInteractions=10,
  ResetVehicle=11, OpenAllRegular=12, QuestLock=13, QuestLockAll=14),
  `vehicleChangeWindowStateEvent { door; state: EQuestVehicleWindowState }`
  (ForceOpen=0, ForceClose=1, OpenAll=2, CloseAll=3),
  `VehicleExternalDoorRequestEvent { slotName; autoClose; autoCloseTime }`,
  `VehicleExternalWindowRequestEvent { slotName; shouldOpen; speed }`,
  `VehicleLightQuestToggleEvent { toggle; lightType }`,
  `VehicleLightQuestChangeColorEvent`, `VehicleQuestSirenEvent { lights;
sounds }`, `VehicleQuestHornEvent { honkTime }`,
  `VehicleQuestToggleEngineEvent { toggle; lockState:
VehicleQuestEngineLockState (DontToggleIfLocked=0, Lock=1, Unlock=2);
vehicleOnEngineOff }`, `VehicleQuestCrystalDomeEvent { toggle;
removeQuestControl }`, `VehicleQuestAVThrusterEvent { enable }`,
  `VehicleRadioEvent`, `VehicleRadioTierEvent`,
  `VehicleToggleBrokenTireEvent { tireIndex; toggle }`.
- Device actions the player's interactions produce (for wrapping on the
  owner): `VehicleDoorOpen`, `VehicleDoorClose`, `VehicleDoorInteraction`,
  `VehicleWindowOpen`, `VehicleWindowClose`, `VehicleOpenTrunk`,
  `VehicleCloseTrunk`, `VehicleOpenHood`, `VehicleCloseHood`,
  `VehicleDumpBody`, `VehicleTakeBody`, `ToggleVehicle`,
  `VehicleForceOccupantOut`, `VehicleDoorDetached`
  (final.reds:215202–215445, 512072).
- Radio availability is gated by the fact `unlock_car_hud_dpad`
  (engine-knowledge §6).

**Driver's game sends** (reliable `VehicleAccessories`, on change): door
and window states (6 × 2 bits each), light mode and light-type mask, siren
(lights, sound), horn on/off, engine on/off, radio on + station index,
crystal dome, thrusters, alarm. **Others apply** through the PS setters
and the `ToggleHorn` / `ToggleSiren` / `SetRadioReceiverStation` natives.
A passenger opening their own door is a request to the owner of the
vehicle (inferred from the authority table).

### 8.6 Damage, destruction, tyres

- Health: stat pool `Health` on the vehicle's id (engine-knowledge §6,
  2.31 numbers); script listener `VehicleHealthStatPoolListener` on
  `VehicleComponent` (`healthStatPoolListener`), `damageLevel` 0–3,
  `VehicleDef.DamageState`.
- Visual deformation: the **destruction grid**. Persistent form
  `vehicleDestructionPSData { gridValues: [Float; 30]; brokenGlass:
Uint32; brokenLights: Uint32; flatTire: Uint8; windshieldShattered;
windshieldPoints: [Vector3]; detachedParts: [CName] }` — the whole
  cosmetic damage state of a car in ≈ 150 bytes, ready to send. Runtime
  event `vehicleGridDestructionEvent { state: [Float; 16]; rawChange;
desiredChange; damageMultiplier; impactPoint; otherVehicle;
rammedOtherVehicle; otherVehicleRammed }` (script
  `VehicleGridDestructionEvent`, final.reds:217659),
  `vehicleGlassDestructionEvent { glassName }`,
  `gameeventsVehicleDestructionEvent { attackData; hitPosition;
hitDirection; hitComponent }`. Setters: `SetDestructionGridPointValues(
layer, values: [Float; 15], accumulate)` [N], `DestructionResetGrid()`,
  `DestructionResetGlass()`, `DetachPart(name)`, `DetachAllParts()`;
  quest events `VehicleQuestVisualDestructionEvent { accumulate;
frontLeft; frontRight; front; right; left; backLeft; backRight; … }`
  (final.reds:217448), `VehicleQuestWindowDestructionEvent { windowName;
window }` (`vehicleQuestWindowDestruction`: window_f=0, window_fl=1,
  window_fr=2, window_bl=3, window_br=4, window_b=5).
- Tyres: `ToggleBrokenTire(tireID, toggle)`, `GetFlatTireIndex()`,
  `VehicleToggleBrokenTireEvent`.
- Explosion: `VehicleComponent.ExplodeVehicle(instigator)`
  (final.reds:91927), `vehicle.SetHasExploded()`, PS `exploded`,
  `isDestroyed`; `VehicleComponent.RepairVehicle()` (93311). Status
  effects `VehicleBaseDamageOverTimeEffect`,
  `VehicleHighDamageOverTimeEffect`. God mode: vehicles are made
  `Immortal` while certain passengers sit in them (final.reds:92044).
- The grid layout mismatch (30 saved values, 16 in the event, 15 per
  `SetDestructionGridPointValues` layer → 2 layers) is consistent with
  "2 layers × 15 points (+1)"; verify before relying on indices
  (inferred).

**Owner (driver or host) sends** reliable `VehicleDamage`: health, damage
level, grid values on change (rate-limited), glass/lights masks, flat
tyres, detached parts, exploded. **Others apply** with the setters above;
their copy must not run its own collision damage (it is kinematic).

### 8.7 Summoning and the garage

`gameVehicleSystem` [N] (`GameInstance.GetVehicleSystem(game)`):
`SpawnActivePlayerVehicle(opt vehicleType: gamedataVehicleType) -> Bool`,
`SpawnPlayerVehicle(vehicleType, vehicleID: TweakDBID,
spawnOnlyOnValidRoad: Bool) -> Bool`,
`IsActivePlayerVehicleOnCooldown(type)`, `IsPlayerVehicleOnCooldown(type,
id)`, `ToggleSummonMode()`, `DespawnPlayerVehicle(vehicleID:
vehicleGarageVehicleID { recordID: TweakDBID; name: CName })`,
`UnregisterPlayerVehicle(id)`, `EnablePlayerVehicle(vehicle: String,
enable, opt despawnIfDisabling) -> Bool`, `EnableAllPlayerVehicles()`,
`GetPlayerVehicles(out [vehiclePlayerVehicle])`,
`GetPlayerUnlockedVehicles(out [...])`, `IsVehiclePlayerUnlocked(recordID)`,
`GetActivePlayerVehicle(type) -> vehiclePlayerVehicle`,
`TogglePlayerActiveVehicle(garageID, type, enable)`,
`TogglePlayerFavoriteVehicle(garageID)`,
`EnablePlayerVehicleCollision(Bool)`, plus chase tuning.
`vehiclePlayerVehicle { name: CName; recordID: TweakDBID; vehicleType;
destructionTimeStamp; isUnlocked; uiFavoriteIndex; … }`. Summon progress:
events `SummonStartedEvent { state: vehicleSummonState }`,
`SummonFinishedEvent { state }` (Idle=0, EnRoute=1, AlreadySummoned=2,
PathfindingFailed=3, Arrived=4); `vehicleESummonedVehicleType` Any=0,
Car=1, Motorcycle=2; `vehicleGarageState` NoVehiclesAvailable=0,
SummonAvailable=1, SummonDisabled=2. UI:
`VehiclesManagerPopupGameController`, `VehicleSummonWidgetGameController`.

Co-op reading: a summoned car is a spawned entity (identity = a `NetId`
from the host, ARCHITECTURE). A guest's summon is a request to the host
carrying the record TweakDBID (and its CrystalCoat/customisation), the
host spawns it (dynamic entity or `SpawnPlayerVehicle` on its own game)
and announces it; the guest's own `SpawnPlayerVehicle` must be suppressed
or its result adopted, or the car exists twice (the suspected NCO crash of
engine-knowledge §6: "the player's own summoned car replicated back").

### 8.8 Driver combat, weaponized vehicles, cameras

- PSM vehicle states: `Driving=1`, `Combat=2` (passenger leaning out),
  `Passenger=3`, `Transition=4`, `Turret=5`, `DriverCombat=6`, `Scene=7`.
  Driver combat classes: `DriverCombatEvents` (final.reds:288056),
  `DriverCombatFirearmsEvents` (549834), `DriverCombatMountedWeaponsEvents`
  (550240); blackboard `IsDriverCombatInTPP`, `DriverCombatWeaponType`,
  `TPPAimPosition`; status `BaseStatusEffect.DriverCombat`,
  `DriverCombatVehicleManeuvers`; `gamedataDriverCombatType`:
  CrystalDome=0, Disabled=1, Doors=2, MountedWeapons=3, Standard=4.
  Weapon requests `RequestLastUsedOrFirstAvailableDriverCombatRangedWeapon`
  (11), `…DriverCombatBikeWeapon` (12).
- Mounted weapons: item types `Wea_VehicleMissileLauncher=101`,
  `Wea_VehiclePowerWeapon=102`; `vehicle.GetActiveWeapons(out)`,
  `IsArmedVehicle()`, `weapon.IsVehiclePowerWeaponRear(vehicle)`; event
  `vehicleMountedWeaponShootEvent { weaponID: TweakDBID; slotID:
TweakDBID }` (script `VehicleMountedWeaponShootEvent`,
  final.reds:298664); pool `VehicleMissileLauncherCharges`;
  `vehicleToggleQuestWeaponEnabledEvent`. Puppet side: `NPCShoot(target:
Vector4, projectiles: Uint32)`, `EnableNPCCombat(true)`.
- The tank: `vehicleTankBaseObject`; mounting it toggles the player's hit
  shapes and targeting (`TogglePlayerHitShapesForPanzer`,
  `ToggleTargetingSystemForPanzer`, in `VehicleComponent.OnMountingEvent`).
- Cameras: `vehicleCameraManager` [N] `IsTPPActive()`,
  `GetActivePerspective()`, `GetPersistentPerspective()` →
  `vehicleCameraPerspective`: FPP=0, TPPClose=1, TPPMedium=2, TPPFar=3,
  DriverCombatClose=4, DriverCombatMedium=5, DriverCombatFar=6;
  `vehicleTPPCameraComponent` (0x680), `vehicleCinematicCameraComponent`,
  event `vehicleRequestTPPCameraSoftResetEvent`; blackboard
  `IsVehicleInTPP`; `vehicleControllerPS.SetAllowPassengerCameraSwitch`.
  The camera is local to each player and needs no sync; note that a
  driver in third person renders V's `player_*_tpp` body (engine-knowledge §12.1).

### 8.9 What the driver's game sends, what a puppet vehicle must turn off

**Driver's game sends** `VehicleState` (State channel, 30 Hz; extrapolated
500 ms per ARCHITECTURE): vehicle id, `timeUs`, position (3 × f32),
orientation (smallest-three quaternion, 6 bytes — roll and pitch matter),
linear velocity (3 × i16), angular velocity (3 × i16) for extrapolation,
then cosmetics: steering (i8), RPM (u8), gear (i8), flags (handbrake,
reversing, in air, burnout, brake lights, engine on), `BikeTilt` (i8).
About 44 bytes. Position and velocity from `GetWorldPosition`,
`GetWorldOrientation`, `GetLinearVelocity()`; angular velocity has no
native by name on the vehicle (SDK struct, or finite difference).

**A puppet vehicle on another game** must not simulate. By-name tools
found, weakest claims marked:

| Need                                             | By name                                                                                                                                                                                                                                                                                                                                                   | Status                                                                                                                      |
| ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| place the vehicle                                | `gameTeleportationFacility.Teleport(obj, position: Vector4, orientation: EulerAngles)` [N]                                                                                                                                                                                                                                                                | works for any `GameObject` by signature; per-frame use on a vehicle is untested and takes Euler angles (inferred)           |
|                                                  | `entEntity.SetWorldTransform(WorldTransform)`                                                                                                                                                                                                                                                                                                             | in 2.50's RTTI **without a handler**: a Codeware addition, not vanilla                                                      |
|                                                  | `vehicleTeleportEvent` (RTTI, 0x70, no script properties)                                                                                                                                                                                                                                                                                                 | exists; fields unknown                                                                                                      |
| stop physics                                     | `gameQuestOrSceneSetVehiclePhysicsActive` (redEvent, 0x48, one hidden 8-byte field) + `vehicle.IsInQuestOrScenePhysicsState()` [N]                                                                                                                                                                                                                        | the quest/scene way to freeze a vehicle's physics: the best by-name lead for "kinematic"; payload unknown (open question 6) |
|                                                  | `entPhysicalBodyInterface.SetIsKinematic(Bool)`, `ToggleKinematic`, `SetTransform(Transform)`, `IsKinematic()` [N], obtained from `entColliderComponent.CreatePhysicalBodyInterface(bodyIndex)` / `entPhysicalMeshComponent.CreatePhysicalBodyInterface`                                                                                                  | generic physics bodies; whether a vehicle's chassis is reachable this way is unknown                                        |
|                                                  | hash 1585713002 `SetKinematic`, 2933986803 `ForceMoveTo`, `vehicleMoveSystem` vtable +0x1F0 `SetSimpleMovement` (2599754135)                                                                                                                                                                                                                              | engine-knowledge §6, §13.5: not by name                                                                                     |
| wake / sleep                                     | `vehicle.PhysicsWakeUp()` [N]                                                                                                                                                                                                                                                                                                                             | by name                                                                                                                     |
| no AI driver                                     | no driver NPC is mounted on the copy; `GetAIComponent() -> AIVehicleAgent`; `IsExecutingAnyCommand()`; AI commands are `AIVehicleCommand` subclasses (`AIVehicleOnSplineCommand`, `…FollowCommand`, `…ToNodeCommand`, `…ChaseCommand`, `…DriveToPointAutonomousCommand`, `…JoinTrafficCommand`, `…PanicCommand`, `…RacingCommand`, `…DrivePatrolCommand`) | nothing to disable if nothing is issued; a traffic copy must first be taken out of traffic (host-owned anyway)              |
| not player-controlled                            | hash 4039776020 `SetIsPlayerControlled` (engine-knowledge §6)                                                                                                                                                                                                                                                                                             | not by name; needed when the local player sits in the driver-less copy as a passenger                                       |
| hold still when idle                             | `ForceBrakesFor(seconds)`, `ForceBrakesUntilStoppedOrFor`, `TurnEngineOn(false)`                                                                                                                                                                                                                                                                          | by name; a stop-gap, not a kinematic mode                                                                                   |
| engine sound and lights follow                   | `TurnVehicleOn(true)`, `TurnEngineOn(true)`; RPM/gear have **no setter by name**                                                                                                                                                                                                                                                                          | engine audio on a copy will not match without one (open question 7)                                                         |
| no autodrive                                     | `gameNativeAutodriveSystem.SetAutodriveEnabled(enabled, isDelamain)` [N]                                                                                                                                                                                                                                                                                  | by name                                                                                                                     |
| passengers                                       | §8.2                                                                                                                                                                                                                                                                                                                                                      |                                                                                                                             |
| no local damage                                  | `GodModeSystem.AddGodMode(vehicleID, Immortal \| Invulnerable, reason)`                                                                                                                                                                                                                                                                                   | by name                                                                                                                     |
| no "enter as driver" prompt while someone drives | `SetDoorInteractionState(seat_front_left, Reserved \| Locked)`                                                                                                                                                                                                                                                                                            | by name (inferred use)                                                                                                      |

`EnableHighPriorityPanelStreaming` does not exist in 2.50's RTTI (the
nearest name is `EnableHighPriorityPanicDriving`, an AI panic toggle).

---

## 9. Fast travel, teleport, regroup

- `gameTeleportationFacility` [N]
  (`GameInstance.GetTeleportationFacility(game)`):
  `Teleport(objectToTeleport: GameObject, position: Vector4, orientation:
EulerAngles)`, `TeleportToNode(objectToTeleport, nodeRef: NodeRef)`
  (final.reds:192802). No loading screen by itself: a far teleport just
  streams the world in around the player.
- Fast travel (final.reds:50235, script system `FastTravelSystem`): a
  `PerformFastTravelRequest { pointData: FastTravelPointData; player }`
  (final.reds:237972) → `PerformFastTravel(player, nodeData)` (50415):
  `PlayerGameplayRestrictions.RemoveAllGameplayRestrictions(player)`,
  writes `FastTRavelSystem.DestinationPoint` / reads `StartingPoint` (the
  blackboard definition is really spelled `FastTRavelSystem`),
  `player.GetFPPCameraComponent().ResetPitch()`, then
  **`TeleportToNode(player, nodeData.GetMarkerRef())`** and
  `SetFastTravelStarted()`; an autosave is requested after the loading
  screen (`m_requestAutoSafeAfterLoadingScreen`,
  `loadingScreenCallbackID`). `gameFastTravelPointData { mappinID;
pointRecord: TweakDBID; markerRef: NodeRef; requesterID; isEP1 }`.
  Locks: `AddFastTravelLock(reason, opt statusEffectID)`.
- Loading screens are ink controllers:
  `inkFastTravelLoadingScreenLogicController`,
  `inkFastTravelLoadingScreenData`, `inkSetNextLoadingScreenEvent`; quest
  node types `questOverrideLoadingScreen_NodeType`. No script native
  "show loading screen" was found.
- Related natives: `gameNativeAutodriveSystem` (autodrive is "travel while
  the world runs"), `StopAutoDriveOnTeleportRequest`; a leftover global
  `RepTeleportPlayer(player, pos: Vector3, rot: EulerAngles)` with no
  handler.

**Regroup** (milestone 4): the simplest by-name implementation is
`Teleport(localPlayer, hostPosition + offset, yaw)` on each guest when the
host fast-travels (wrap `FastTravelSystem.PerformFastTravel` on the host
to send `Teleport { markerRef or position }`), and on a tether break. A
guest who fast-travels alone asks the host (or everyone follows; a design
decision, see ARCHITECTURE "region limits"). Unmount first if seated;
teleporting a mounted player is undefined in the scripts (they call
`StopAutoDriveOnTeleportRequest` for vehicles). Puppets: the owner's next
`PlayerState` after a `Teleport` event must snap, not interpolate (send a
teleport sequence number).

---

## 10. HUD for co-op

### 10.1 Leftovers of the game's own multiplayer (surprise)

The cut multiplayer ("CPO") left named pieces in 2.50's RTTI and scripts:

- `gamemappinsMappinSystem.RegisterRemotePlayerMappin(data: MappinData,
playerObject: wref<GameObject>) -> NewMappinID` [N];
  `gamemappinsRemotePlayerMappin { vitals: Int32; hasMissionData: Bool }`;
  world controller `RemotePlayerMappinController` (final.reds:397549; it
  shows the marker only when `vitals == 1`, i.e. **a downed-teammate
  marker**), minimap `gameuiMinimapRemotePlayerMappinController`; mappin
  record `t"Mappins.CPO_RemotePlayerMappinDefinition"`, variant
  `gamedataMappinVariant.CPO_RemotePlayerVariant` (148 in 2.50).
  `PlayerPuppet.RegisterRemoteMappin()` (final.reds:45500) is the
  example call.
- Pings: `gamePingSystem.TriggerPing(instigator)` [N], input action
  `n"MP_TriggerPing"` (final.reds:40547), `gamemappinsPingSystemMappin`,
  `PingSystemMappinController` (397242),
  `gameuiMinimapPingSystemMappinController`, `gamedataPing_Record`,
  `gamedataPingType` (Device=0, Door=1, Elevator=2, Junction=3,
  Location=4, Loot=5, Trap=6), variants `CPO_PingDoorVariant=145`,
  `CPO_PingGoHereVariant=146`, `CPO_PingLootVariant=147`.
- Player list: `gameuiPlayerListGameController` (`OnRemotePlayerAttach`,
  `OnRemotePlayerDetach`, `AddPlayerToList`), `PlayerListEntryData`.
- `cpPlayerSystem` (the concrete player system): `FindPlayerControlledObjects(
position, radius, includeLocalPlayers, includeRemotePlayers, out
[GameObject]) -> Uint32`, `LocalPlayerControlExistingObject(entityID)`,
  `RegisterPlayerPuppetAttachedCallback(object, func) -> Uint32`,
  `GetLocalPlayerMainGameObject()`, `GetLocalPlayerControlledGameObject()`
  [N].
- On every entity: `IsControlledByLocalPeer()`, `IsControlledByAnyPeer()`,
  `IsControlledByAnotherClient()`, `IsReplicated()`,
  `GetControllingPeerID()` [N]; on `gameObject`: `ReplicateAnimFeature(obj,
inputName, value)`, `ReplicateAnimEvent(obj, eventName)`,
  `ReplicateInputFloat/Bool/Int/Vector(obj, inputName, value)`,
  `QueueReplicatedEvent(evt)` [N]; replicated-state classes
  (`gameFPPCameraComponentReplicatedState`,
  `entRagdollComponentReplicatedState`, `gamePingSystemReplicatedState`);
  `gamePuppet.SetCPOMissionData`, `GetCPOMissionVoted`.

All of it is **unverified**: the natives have handlers, but whether the
systems behind them still work without the game's own network layer is
unknown. They are worth one probe each (open question 10) because they
would give markers and pings with vanilla widgets; none of it is needed if
the generic mappin API below works.

### 10.2 Markers on a player (generic, known-good API shape)

`gamemappinsMappinSystem` [N]:
`RegisterMappin(data: MappinData, position: Vector4) -> NewMappinID`,
`RegisterMappinWithObject(data, object: wref<GameObject>, opt slotName:
CName, opt offset: Vector3) -> NewMappinID` (follows the entity),
`RegisterVehicleMappin(data, object, slotName, offset)`,
`UnregisterMappin(id)`, `SetMappinActive(id, active)`,
`SetMappinPosition(id, position)`, `ChangeMappinVariant(id, variant)`,
`SetMappinScriptData(id, scriptData: MappinScriptData)`,
`GetMappin(id)`, `GetMappins(targetType: gamemappinsMappinTargetType
(World=0, Minimap=1, Map=2), out)`.
`gamemappinsMappinData { mappinType: TweakDBID; variant:
gamedataMappinVariant; debugCaption: String; localizedCaption; active:
Bool; visibleThroughWalls: Bool; scriptData }`. Useful variants:
`CPO_RemotePlayerVariant=148`, `CustomPositionVariant=21`,
`DefaultVariant=26`, `VehicleVariant=119`, `ExclamationMarkVariant=49`,
`HazardWarningVariant=63`, `GrenadeVariant=60` (take numbers from RTTI,
not from this list, if the game is patched).

**Each game**: one `RegisterMappinWithObject(data, puppet, n"…", offset)`
per remote player (minimap dot and world marker), re-registered when the
puppet entity is recreated; the vehicle's own mappin
(`VehicleComponent.CreateMappin`, final.reds:93686) covers a driven car.

### 10.3 Pings and shared markers

Owner sends reliable `Ping { kind, position | entity id }`; every game
registers a timed mappin (`RegisterMappin` + `UnregisterMappin` after a
delay through `DelaySystem`), variant `CPO_PingGoHereVariant` or
`CustomPositionVariant`. The map's own custom pin
(`CustomPositionVariant`) is the natural "shared waypoint": wrap the
world-map controller's pin placement on the owner and mirror it.

### 10.4 Nameplates

- Vanilla nameplates are for the entity under the crosshair:
  `gameuiNpcNameplateGameController` (projected HUD) fed by blackboard
  `UI_NameplateData` (`UI_NameplateDataDef`: `EntityID` Variant,
  `IsVisible` Bool, `HeightOffset` Float, `DamageProjection` Int) and the
  struct `gameuiNPCNextToTheCrosshair { npc; name: String; currentHealth;
maximumHealth; level; attitude; … }`; `NameplateVisualsLogicController`,
  `NpcNameplateVisualData`, `NameplateChangedEvent { entity }`,
  `NameplateVisibleEvent`; records `gamedataUINameplate_Record`,
  `gamedataUINameplateDisplayType`. The displayed name is
  `GameObject.GetDisplayName()` [N] / `gamePuppetBase
.GetTweakDBFullDisplayName(useDisplayNameAsFallback)` [N]: it comes from
  the puppet's **record**, so every remote V would read the same record
  name.
- Therefore an always-on player name tag is tandem's own widget: an ink
  widget projected at the puppet's head (`gameObject.GetUISlotComponent()`
  [N], or `entSlotComponent.GetSlotTransform` on a head slot) or a mappin
  with `scriptData` and a custom controller. Health for the tag comes from
  `PlayerState.health`.
- Minimap: `gameuiMinimapContainerController`; mappins registered above
  appear on it according to their definition record.

---

## 11. Open questions (need an in-game test)

1. **A puppet's player blackboard.** Does
   `GetLocalInstanced(puppetID, PlayerStateMachine)` return a blackboard
   for an entity that is not the local player, and do vanilla scripts or
   the animation graph of the third-person body read anything from it?
2. **Listener thread.** On which thread do `RegisterListenerInt` callbacks
   of the player blackboard run (log the thread id in one), and does the
   "Delayed" variant mean "next frame on the main thread"?
3. **Pose stream coverage.** Does the committed pose (engine-knowledge §13.3) of the
   first-person V include vehicle seats, workspots, ladder, swimming and
   ragdoll, or are some of those written after the commit (workspot and
   ragdoll are separate systems)?
4. **Harmless shots.** Does `AI_ShootForwards` from a puppet's weapon
   produce tracers and impacts, and how to stop its damage locally
   (attack record, damage-system hook, god mode on everything it hits is
   not acceptable)? Does `TriggerWeaponEffects(weapon, Shoot)` alone give
   muzzle flash and sound in third person?
5. **Seating a puppet with no AI.** `MountingFacility.Mount` with
   `isInstant`, alone or followed by `WorkspotGameSystem.MountToVehicle(…,
n"OccupantSlots", slot)`: does the body sit, follow the vehicle, and
   unmount cleanly? Does V's cutscene template have the passenger
   animation sets, or does only the pose stream make it look seated?
6. **Kinematic vehicle by name.** Payload and effect of
   `gameQuestOrSceneSetVehiclePhysicsActive` (decompile its handler or
   watch `IsInQuestOrScenePhysicsState`); does a vehicle in that state
   accept `TeleportationFacility.Teleport` every frame smoothly, with
   wheels and passengers following? Compare with hash 1585713002 + 2933986803.
7. **Vehicle cosmetics on a copy.** Where steering angle, wheel spin,
   suspension, RPM and gear live (no blackboard field or native for
   steering); can they be written on a kinematic copy so wheels turn and
   the engine sounds right?
8. **Status effect visuals.** Does `ApplyStatusEffect` on a puppet show the
   effect's VFX (burning, EMP, cloak) without side effects on the local
   game (stims, AI reactions, damage ticks)? Do `cloak_on` / `dash` /
   `landing_*` effects exist on V's third-person entity?
9. **Individual time dilation above 1.** Does
   `SetIndividualTimeDilation(reason, 2.0, …)` speed a puppet up (the
   Sandevistan stand-in of ARCHITECTURE)?
10. **CPO leftovers.** Do `RegisterRemotePlayerMappin`,
    `PingSystem.TriggerPing`, `gameuiPlayerListGameController` and
    `cpPlayerSystem.FindPlayerControlledObjects(includeRemotePlayers)` do
    anything in 2.50 single-player? Is the
    `CPO_RemotePlayerMappinDefinition` record still in TweakDB?
11. **Downed state.** Swallowing `OnDisplayDeathMenu`: does V stay in
    `DeathEvents` indefinitely without side effects (autosave lock, input,
    camera), and can `Revive` + a forced transition bring V back, or must
    the `Immortal` route be taken?
12. **Weapon on V's puppet.** `AddItemToSlot(puppet,
AttachmentSlots.WeaponRight, …)`: does the weapon entity appear in the
    hand of the third-person body (the clothes case of engine-knowledge §12.1 failed
    silently), and with which of `highPriority` / `ignoreRestrictions`?
13. **2.50's player scripts.** Decompile 2.50's own `final.redscripts`
    (redscript CLI on the game machine's `r6/cache`) to replace the older
    reference for the dual-wield fields (`RightWeapon`, `LeftWeapon`,
    `ShootState`, `EquipmentRight/Left`): which enum each carries and
    which state class writes it.
14. **Teleporting a mounted player** and teleporting a vehicle with
    occupants: order of operations for regroup.
