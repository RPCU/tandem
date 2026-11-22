# Vehicles and radio

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 6. Vehicles

**Sync model**: the occupant with authority (driver) simulates locally and sends the vehicle transform
as movement; everyone else runs the car **kinematic** and drives it with `ForceMoveTo` every frame.
Authority = parentage on the server + `authority_epoch` bumped on transfer (revoke before assign; late
packets from the previous simulator dropped) (`MAP:1166-1171`, `docs/NETCODE-AUTHORITY.md`).

**Calls** (`App/World/VehicleSystem.cpp:30-73`, `InterpolationSystem.cpp:95-98,891-943`)

<!-- prettier-ignore -->
| Hash / slot | What | Status |
| --- | --- | --- |
| **4039776020** `SetIsPlayerControlled(BaseObject*, bool)` (called from `vehicle::actions::DriveAction::OnStart`) | remote: false; local again: true | in 2.50; used on 2.5 (vehicle rows in 2.5 logs) [2.5-verified, lightly] |
| vtable **+0x328** on `WheeledBaseObject` `(bool)` | "turn on engine" | hand slot from 2.31 [unverified on 2.5] |
| vtable **+0x280** on `vehicle::BaseObject` `(WorldTransform const&)` | `ResetWithNewTransform` — CAMP only, declared, never called (`campaign/code/client/App/World/InterpolationSystem.cpp:60-63`); absent from NCO | [2.31-only, unused] |
| `engineData->unk61 = 0` | after the engine call | engineData at **0xBD0** on 2.5 (jackhumbert `155d285a`) [unverified] |
| **1620777158** `SetFlags(BaseObject*, u32)` with 0x10 then 0x80 | remote-driven flags | [unverified on 2.5] |
| **1585713002** `SetKinematic(BaseObject*, bool)` | true for every network-spawned car, false when we get authority | [2.5-verified, lightly] |
| vtable **+0x1F0** on `vehicle::IMoveSystem` `(EntityID const&, bool)` | `SetSimpleMovement(id, true)` before each ForceMoveTo | hand slot [unverified on 2.5] |
| **2592348558** `EnablePhysics(BaseObject*, u32 flag, bool)` with flag 0x100 when `(physicsState & 0x100)==0`; `physicsState` at **0x260** | modelled on `DriveToPoint::OnUpdate_Internal` (**3713079867**) | [unverified on 2.5] |
| **2933986803** `ForceMoveTo(BaseObject*, WorldTransform const&, float frameDeltaMs)` | per-frame remote drive | [unverified on 2.5] |
| **457775029** `ForceTransform(BaseObject*, WorldTransform const&, bool)` | commented out (ForceMoveTo used instead) | — |
| **1160782872** `MountingFacility::IsMountedToObject`, **3120376212** `GetMountingInfo` | declared, unused | — |

- 2.5 struct placements (vendored SDK, from jackhumbert `155d285a` 2026-10-25): `BaseObject` size
  0xBC0, isOnGround 0x25C, physicsState 0x260, acceleration 0x264, handbrake 0x26C, isReversing 0x2AB,
  burnout 0x2C4, `rigidBody` **0x2D8**, chassisType 0x340, `worldTransform` **0x350**, archetype 0x3B0,
  `runtimeData` **0x5F0**, isVehicleOnStateLocked 0x6F2, `engineData` **0xBD0** (WheeledBaseObject);
  `RigidBody::currentTransform` 0x150 (`NCO-PROVENANCE.md:71`, `vehicleBaseObject.hpp:204-254`).
  CW: vehicle `RecordID` at **0x658** (`CW Red/Vehicle.hpp:5`). [2.5-verified by CW for RecordID; rest from SDK asserts]
- No NCO commit says the vehicle-sync natives were exercised end-to-end on 2.5; `MAP` vehicle sections
  date from 2.31 (2026-08), and the commit bodies never name slots 0x328/0x1F0/0x280 (they exist only
  in source). Treat all vtable slots above as unverified.
- **2.5 made the game's `VehicleSystem` a NATIVE class** (`IVehicleSystem`, natives
  `SpawnActivePlayerVehicle`, `SpawnPlayerVehicle`, `ToggleSummonMode`…); a mod class registering RTTI
  name "VehicleSystem" → 9 validator errors, game closes. Never reuse a game class name (`84df9a48`).
  [2.5-verified]
- 2.5 P0 two-player crash ~90 s in: AV with RIP at a heap address (corrupt function pointer) after ~960
  `[OrphanVehicle] samples 0` lines — suspected the player's own summoned car replicated back as a second
  remote copy (`c92f26b1`). [unverified, open]
- Car radio + vanilla summon are gated by quest fact `unlock_car_hud_dpad` (→ blackboard
  `UIGameData.Popup_Radio_Enabled`, vehicleSystem.script:70); absent from template saves (`9ec6a206`).
- Downed while seated: instant unmount, then force the workspot out after 0.5 s, ≤4×, then teleport
  (`62e48492`). [unverified]
- 2.31 crash sequence (fixed by the threading fix, kept as a signature): quit in a car, rejoin → server
  replays the orphan car → `OnVehicleReady` → `MakeRemoteDriven` (SetIsPlayerControlled, engine vcall,
  engineData, SetKinematic) → dead 1.6 s later; a 9-player sweep: 20× 0xC0000005 + 8× 0x80000003, all
  2–10 s after a queued remote-vehicle mount (`a0553f70`, `a2709440`).
- Garage (2.31 script): `EnablePlayerVehicle(name: String, enable, despawnIfDisabling)`,
  `GetPlayerUnlockedVehicles(out array<PlayerVehicle{name, recordID, isUnlocked}>)`; takes a String,
  and TweakDBID→string is impossible in release → store names (`64951cc9`, `1a4174e1`).

**Mounting / seats**

- Remote puppet into seat: queue `MountAIEvent{name="Mount", data=MountEventData{slotName,
mountParentEntityId, isInstant=true, ignoreHLS=true}}` on the character (`World/VehicleSystem.reds:72-94`).
  Exit: `AIEvent "ExitVehicle"` (plays animation; "messes up orientation, sometimes crashes" note).
  `VehicleComponent.MountEntityToSlot` and `MountingFacility.Mount` variants were tried/commented.
- Local driver promotion: `MountingFacility.Unmount(current)` then mount driver seat after **0.35 s**
  delay (same-frame mount races the unmount) (`NetworkWorldSystem.reds:571-605`, `804e64c4`); driver
  slot name from `VehicleComponent.GetDriverSlotName()` (`seat_front_left`). Driver-exit handoff order:
  front right → back left → back right (`eaeaa792`). A car whose driver leaves is parked, not destroyed
  (`bec22350`).
- Seat ids on the wire = `CName` hash of the slot name (FNV1a64; `seat_front_left` =
  `0xb000b1d029d0cea0`) (`MAP:906-919`). Validate seat **identity**, not count.
- Boarding someone else's car: the engine's mount flow wakes the local copy's physics → re-assert
  kinematic on enter (else "he drives, I'm stationary") (`VehicleSystem.cpp:199-208`).
- A network copy spawning on top of the local world's parked instance of the same car → depenetration
  impulse "cars explode when someone gets in": make every network car kinematic before its first frame
  (`VehicleSystem.cpp:22-29`).
- Seat swap = exit+enter within 1 ms; process order bug emptied the car and destroyed it under the
  driver → exits must name vehicle+seat (`NEW-SERVER-NETCODE-PORTING-HANDOFF.md:236-239`).
- Entering a car must join the existing entity (remember your own car's server id), or a fresh entity is
  spawned per entry (7 copies stacked once) (`MAP:1179-1181`).
- Disconnect while driving then rejoin reproduced THE crash 1.6 s after `MakeRemoteDriven` (flecs race,
  fixed); clearing vehicles at zero players server-side (`MAP:1312-1318`).
- Remote vehicle-mount crash: **unresolved**, dominates crash reports (`NEW-SERVER-NETCODE-PORTING-HANDOFF.md:242-243`).

**Vehicle damage** (2.31 sources, `MAP:785-819`)

- Vehicle health = stat pool `Health` on the vehicle's EntityID; read `GetStatPoolValue(id, Health,
false)`, set `RequestSettingStatPoolValue`, observe `RequestRegisteringListener(id, Health, l)` →
  `OnStatPoolValueChanged`. `VehicleObject.DamagePipelineFinalized` fires **before** the pool settles.
  Street car ≈ **1050 HP**; one shotgun blast = **14 hits within 1 ms** (~8 each) → batch per shot.
  Destruction `gameVehicleDestructionEvent`, `m_damageLevel` 0-3. Whether a kinematic remote car runs
  the damage pipeline: unknown. [2.31-only]

### 13.6 Moving a vehicle we do not simulate (2026-11-18)

`scripts/tandem/VehicleProbe.reds` (`tools/game.sh cmd vehicle probe
[record]`): a dynamic `Vehicle.v_standard2_archer_hella_player` spawned
ahead of V and moved every frame (≈85 frames/s) with the teleportation
facility (`Teleport` with the vehicle, a position and
`EulerAngles(0, 0, yaw)`) along a 6 m circle at 5 m/s.

- The car is exactly where each teleport puts it: **zero drift between
  frames** over 20 s, and `GetLinearVelocity()` reads 0, so its own
  physics does not fight the teleports (or is asleep). [2.5-verified]
- Seen on screen: the car follows the circle; height and orientation
  against the ground were not checked closely (the test was cut short: the
  player was playing). A teleport sets the body's origin at the given
  position: a remote car must be placed at the driver's car's own
  transform, not at a ground point.
- Consequence for tandem: a puppet vehicle can be driven by teleporting it
  every frame to the interpolated `VehicleState`. Still to check: wheels
  turning and suspension (velocity 0 may leave them still), engine sound
  without RPM, collisions of other things against it, a passenger seated in
  it, and whether one teleport per frame is cheap enough with several cars.

**Deleting a car copy freezes the game** (2026-11-18, three engine hang
dumps in `REDEngine/ReportQueue`, all threads waiting in `ntdll`, no stacks
in the dump): `DynamicEntitySystem.DeleteEntity` on a dynamic vehicle that
has had a puppet mounted in it (mounting facility, `MountingRequest`,
instant) froze the whole game at once, whether the body was still seated
or had been unmounted 0.3 s before (`IsMountedToAnything` false). The probe
car, never mounted, deletes fine. Deleting the mounted _body_ after
unmounting it is fine. tandem now leaves car copies parked and never deletes
them in a running game. Not yet found: what the vehicle keeps of a past
occupant (seat reservation in `VehicleComponentPS`? the workspot system?)
and a safe way to remove such a car. [2.5-verified]

**It is a crash, not a hang** (2026-11-18, `tools/game.sh cmd vehicle clear`
on a parked copy whose driver had got out, then `tools/game.sh bt`): a job
thread faults at `sub_14071ca98+0x15`, a virtual call (`[obj]+8`) on an
object that is already gone. It is reached from `sub_140dbb958+0x75`, which
walks a table of objects (`*param_1`, 0x38-byte entries, `ushort` indices)
and calls `sub_140206a14` on each, under the job at `sub_1402e0400`. So
some engine list keeps a raw pointer to the deleted car, most likely
because of its past occupant, and the next pass uses it. The hang dumps
were the game's report queue waiting on that crash. Next: name that table
(decompile `sub_140dbb958`'s caller and the job's owner), then either take
the car out of it before deleting, or reuse parked copies (a pool) rather
than delete them. [2.5-verified] (Decompiled since: §13.101.)

**The full vehicle loop runs** (mock player `--mode drive`): copy spawned
from the record, the driver's puppet mounted in `seat_front_left`
(`MountingFacility.Mount`, no AI), the copy teleported every frame to the
interpolated `VehicleState`, the puppet unmounted when the driver gets out
(`MountingFacility.Unmount`), our movement controller re-attached 0.5 s
later (mounting detaches it), the driver leaving the session while seated.
[2.5-verified]

### 13.43 The car radio and the pocket radio (2026-11-18, 2.50 scripts read)

Read from the 2.50 scripts (`final.reds` line numbers), used by
`scripts/tandem/Radio.reds`; none of it has run in a session yet.

- **Stations** are the `ERadioStationList` index 0..13 (`final.reds`
  9476: AGGRO_INDUSTRIAL 0 … DOWNTEMPO 4 … IMPULSE_FM 13); their audio
  names (`radio_station_09_downtempo`…) come from
  `RadioStationDataProvider.GetStationName` (198877), or
  `GetStationNameByIndex(index, isUI)` (`station_none` for -1). The radio
  popup lists them in another order (`GetRadioStationUIIndex` /
  `GetRadioStationByUIIndex`, 198811/198844) and converts before sending.
  [2.5-untested]
- **The car's radio**, `VehicleObject` natives (87773–87839):
  `ToggleRadioReceiver(Bool)`, `SetRadioReceiverStation(Uint32)` (an
  `ERadioStationList` index: the pocket radio hands its own index to it,
  244342), `NextRadioReceiverStation()`, `SetRadioTier(Uint32, Bool)`,
  `IsRadioReceiverActive()`, `WasRadioReceiverPlaying()`,
  `GetCurrentRadioIndex() -> Uint32` (read back as that index, 244354),
  `GetRadioReceiverStationName()`, `GetRadioReceiverTrackName() -> CName`
  (a localisation key: the popup shows it with `SetLocalizedText`,
  392189). [2.5-untested]
- **Setting it the game's way**: a `VehicleRadioEvent { toggle;
setStation; station }` (215564) queued on the car reaches
  `VehicleComponent.OnVehicleRadioEvent` (93566): `toggle` turns the
  receiver on (or, already on, steps to the next station), `setStation`
  then sets `station`; both update the vehicle blackboard
  (`VehRadioState`, `VehRadioStationName`) and the HUD
  (`UIVehicleRadioEvent`). The popup's choice
  (`VehicleRadioPopupGameController.Activate`, 392196) goes through
  `QuickSlotsManager.SendRadioEvent(toggle, setStation, index)` (240038):
  `(true, true, station)` to tune, `(false, false, -1)` for off. The
  short press of the radio button is a `RadioToggleEvent` (93543).
  [2.5-untested]
- **No playback position**: nothing in the scripts reads or sets where a
  song is. `GetRadioStationCurrentTrackName(stationName)` (244526) names
  the current song of a station; `AudioSystem.RequestSongOnRadioStation(
stationName, songName)` (149608, no script caller) asks a station for a
  song, presumably from its start; whether it takes the track name the
  receiver reports, and whether it cuts in at once, is unknown. So two
  games can be on the same station but not at the same point of a song;
  tandem asks the passenger's station for the driver's song, which lines
  up when the driver's song has just begun. [2.5-untested]
- **The pocket radio** is `PlayerPuppet.GetPocketRadio()` (40614), a
  `PocketRadio` (244010) playing through audio switches on the player's
  `pocket_radio_emitter` (`radio_port_station`, `radio_port_on`,
  `radio_port_off`). Restrictions (`PocketRadioRestrictions`, 1002: scene
  tier, phone call, `VehicleBlockPocketRadio`…) go through
  `HandleRestriction(restriction, Bool)` (244196): while any is set it
  turns off; once all are lifted it waits for the status effect
  `BaseStatusEffect.PocketRadioRestrictionUnlockDelay` (244165), then
  plays its selected station again (or stays off if it was off).
  [2.5-untested]
- **Pocket radio and cars in vanilla**: only the **driver's seat**
  (`seat_front_left`) links the two (`PlayerPuppet.OnMountingEvent` /
  `OnUnmountingEvent`, 40460/40485): with the setting
  `/gameplay/radioport` `radioport_sync_to_car` on, getting in hands the
  pocket station to the car (`HandleVehicleMounted`, 244340) and getting
  out takes the car's back (`HandleVehicleUnmounted`, 244346). In a car
  with the setting off, the pocket radio ignores radio events
  (`ShouldIgnoreEvents`, 244077). Nothing in the scripts turns it off for
  a passenger seat, hence tandem's restriction. [2.5-untested]
- Volumes are each player's own settings (`/audio/volume`
  `CarRadioVolume`, `RadioportVolume`, read by the popup): not synced.
  [2.5-untested]

### 13.41 Vehicle combat: the hooks and setters (2026-11-19, 2.50 scripts read)

Read from the 2.50 game's own scripts (`final.reds` of the game's bundle,
line numbers of that dump); none of it run in game yet: every line is
[2.5-untested]. Used by `scripts/tandem/Wheels.reds`
(`docs/architecture/vehicle-combat.md`).

- **Damage of a car, read**: health pool `Health` on the vehicle's id (as
  traffic, `Traffic.reds`); the damage stage `VehicleDef.DamageState`
  (0–3, `final.reds:182365`; 3 taken as burning); `IsDestroyed()`
  (`:87959`, the PS's `GetIsDestroyed`); `VehicleComponentPS.GetIsSubmerged`
  (`:95483`); `WheeledObject.GetFlatTireIndex() -> Int32` (`:160886`, one
  index: tandem keeps the bits it has seen). [2.5-untested]
- **Dents**: `VehicleComponent.OnGridDestruction(evt:
VehicleGridDestructionEvent)` (`:92968`) gets the grid's `state: [Float;
16]` on every impact. Setting it: `VehicleObject.SetDestructionGridPointValues(layer:
Uint32, values: [Float; 15], accumulate: Bool)` (`:87949`); vanilla's own
  `VehicleComponent.OnVehicleQuestVisualDestructionEvent` (`:91447`) fills
  layer 0 with 15 body points and layer 1 with the roof value: tandem maps
  the event's cells 0–14 to layer 0 and cell 15 to layer 1 (inferred from
  that function; check the look of a copy in game). [2.5-untested]
- **Parts**: `VehicleComponent.OnVehicleOnPartDetached(evt:
VehicleOnPartDetachedEvent { partName: CName })` (`:93487`; names seen:
  `Trunk`, `Hood`, `HoodLeft`, `HoodRight`, `DoorFrontLeft`… with `_A`,
  `_B`, `_C`, `BumperFront`, `BumperBack`); `VehicleObject.DetachPart(partName)`
  (`:87787`). [2.5-untested]
- **Tyres**: `WheeledObject.ToggleBrokenTire(tireID: Uint32, toggle: Bool)
-> Bool` (`:160884`). [2.5-untested]
- **Explosion on a copy**: the game's own, by setting the health pool to its
  minimum (`RequestSettingStatPoolMinValue`, as `Traffic.reds` does for a
  proxy); a wreck put there: `VehicleComponentPS.SetIsDestroyed(true)` and
  the record's `DestroyedAppearance()`. [2.5-untested]
- **A vehicle running into anything**: `GameObject.OnVehicleHit(evt:
gameVehicleHitEvent { vehicleVelocity, preyVelocity })` (`:12386`) builds
  the `Attacks.VehicleCollision` hit and queues it; its source is the
  vehicle. The game's kill rule (`ProcessVehicleHit`, `:85677`): relative
  speed ≥ `AIGeneralSettings.vehicleHitKillThreshold` (25) kills, crowd or
  civilian ≥ `vehicleHitCrowdKillThreshold` (20). [2.5-untested]
- **Knocking an NPC down**: `CreateForceRagdollEvent(debugSourceName)`
  (`:188095`), `CreateRagdollApplyImpulseEvent(worldPos, impulse, radius)`
  (`:188128`), queued on the puppet. The impulse scale (tandem: the car's
  velocity × 70) is a guess to tune. [2.5-untested]
- **Pushing a car**: `PhysicalImpulseEvent { bodyIndex, worldPosition:
Vector3, worldImpulse: Vector3, radius }` (`:293073`) queued on the
  vehicle, as vanilla's bike knock-over does (`:550686`); with
  `PhysicsWakeUp()`. [2.5-untested]
- **Bumps**: `VehicleObject.OnVehicleBumpEvent(evt: VehicleBumpEvent {
impactVelocityChange, hitVehicle, hitNormal })` (`:88458`, `:218437`), the
  one `Traffic.reds` uses. [2.5-untested]
- **Mounted weapons**: `VehicleComponent.OnWeaponShootEvent(evt:
VehicleMountedWeaponShootEvent { weaponID, slotID })` (`:90549`) runs for
  every shot of the vehicle's own weapons (vanilla broadcasts the gunshot
  stim there); `VehicleObject.NPCShoot(target: Vector4, projectiles:
Uint32)` (`:87931`) fires them at a point; `GetActiveWeapons`,
  `IsArmedVehicle` (`:87925`). No call found that turns a turret to an
  aim, or plays a mounted weapon's muzzle without a real shot.
  [2.5-untested]
- **Seat states of the V** (`PlayerStateMachine.Vehicle`): `GunnerEvents`
  (`:548570`) sets it to 2 and equips a ranged weapon; Turret 5 and
  DriverCombat 6 are the enum's (`coop-player-vehicles.md` §8.8). Vanilla's
  gunner seats are a scene's (`PuppetVehicleState.GunnerSlot`, `:3483`): no
  seat name says "gunner". [2.5-untested]
- **The hard exits** run on the player's state machine, for the local V
  only: `SpeedExitingEvents.OnEnter` (`:551283`, jumping out on the move,
  `VehicleExitKnockdownProtection`), `ImmediateExitWithForceEvents.OnEnter`
  (`:550633`) which `CollisionExitingEvents` (`:550742`, applies
  `BikeKnockdown`) and `DeathExitingEvents` (`:551214`) run through `super`.
  [2.5-untested]
- **Chases**: `InVehicleCombatDecorator` (`:473704`) drives a vehicle after
  its driver's `CombatTarget` (`ChaseNewTarget` sends an
  `AIVehicleChaseCommand { target, distanceMin 3, distanceMax 10 }` to the
  mounted vehicle; `Deactivate` interrupts it). Prevention and the dynamic
  spawn system chase `GetLocalPlayerMainGameObject()` directly
  (`:142449`, `:189618`). [2.5-untested]

### 13.70 World vehicles changing hands, the owner's turret, passenger combat in a copy (2026-11-19, 2.50 scripts read)

Read from the 2.50 game's own scripts (`final.reds` of the game's bundle,
`~/.cache/tandem/ref/gamescripts/vanilla`, line numbers of that dump) and
the live RTTI dump (`data/rtti-natives-2.50.tsv`); none of it has run in
game: every line is [2.5-untested]. Used by `scripts/tandem/Traffic.reds`,
`Wheels.reds`, `Transit.reds` (`architecture/traffic.md`, "Several owners,
and hand-overs"; `architecture/vehicle-combat.md`).

**A traffic car changing hands** (the giver keeps its body as a proxy, the
receiver's proxy becomes the real car):

- A crowd car is driven along its lane by the native traffic system through
  its crowd member component (§13.32). `VehicleObject` keeps it as
  `m_crowdMemberComponent` (`:87702`, `GetCrowdMemberComponent()` `:88205`)
  and changes how it drives with `ChangeMoveType(CName)` (`:143068`) on its
  own `m_drivingTrafficPattern`: `n"stop"` when stuck or when its driver is
  knocked out (`OnStuckEvent` `:88430`, `:88640`, `:88659`), `n"panic"`
  (`:88615`), `n"normal"` back in its lane (`:88928`), or a crowd setting's
  `movementType` (`OnCrowdSettingsEvent` `:88421`). tandem's `TandemHold`
  sets `stop` on the giver's car while it is a proxy, `normal` when it is
  that game's again. Whether `stop` keeps the traffic system from moving
  it against the teleports is the probe. [2.5-untested]
- A spawned car (a dynamic entity, like a proxy) goes into traffic the way
  Delamain's cab does when it goes free: an `AIVehicleJoinTrafficCommand
{ needDriver: false; useKinematic: true }` (`:217147`, class `:217291`,
  its handler `AIDriveJoinTrafficCommandHandler` `:472292` maps both
  fields) sent with `vehicle.GetAIComponent().SendCommand(command)`
  (`GetAIComponent() -> AIVehicleAgent` `:87883`, `AIComponent.SendCommand`
  `:143634`). tandem's `TandemDriveOn` sends it to a woken proxy after an
  impulse of its mass times the giver's velocity (`PhysicalImpulseEvent`,
  `GetTotalMass` `:87921`, as for a pushed car §13.41). Whether a car with
  a woken NPC driver drives on, and how it joins a lane, is the probe.
  [2.5-untested]

**The owner's turret for a remote gunner**:

- **No script call sets a vehicle turret's aim.** `VehicleObject`'s weapon
  calls are `GetActiveWeapons(out [wref<WeaponObject>])` (`:87925`),
  `IsArmedVehicle()` (`:87927`), `EnableNPCCombat(Bool)` (`:87929`),
  `NPCShoot(target: Vector4, projectiles: Uint32)` (`:87931`) and
  `IsNPCShooting()` (`:87933`); RTTI lists the same as `vehicleBaseObject.*`.
  The aim limits are TweakDB data (`VehicleWeapon_Record`: `MinYaw`,
  `MaxYaw`, `MinPitch`, `MaxPitch`, `CycleTime`, `WeaponShootAnimEvent`…,
  `:544418`; a vehicle's list `Vehicle_Record.Weapons`, `:95128`). The 2.0
  armed cars' class `vehicleArmedCarBaseObject` and
  `vehicleToggleQuestWeaponEnabledEvent` have no script functions; the
  quest nodes (`questUseWeapon_NodeType`…) are not callable. [2.5-untested]
- **Vanilla's armed cars fire at a point** with
  `AISubActionShootFromCar_Record_Implementation` (`:451963`): if
  `IsArmedVehicle()` and not `IsNPCShooting()`, `EnableNPCCombat(true)`,
  then `NPCShoot(targetPosition, RandRange(vehicles.npc_armed_car
.minBurstProjectiles, maxBurstProjectiles))`. It needs no player. tandem
  fires the owner's real vehicle so at the remote gunner's `MountedFire`
  point (`TandemWheelsSync.FireAt`), and copies' cosmetic shots the same
  way (they lacked `EnableNPCCombat`). Whether `NPCShoot` also turns the
  turret, works with a player at the wheel, and on the Basilisk (a
  `TankObject`, for which `IsArmedVehicle` may be false: the
  `gunner-turret` scenario notes it) is for the game to show. [2.5-untested]
- **The gunner seat is not a turret** in the player state machine:
  `GunnerDecisions` (`:548550`) checks the slots `gunner_back_left` and
  `gunner_back_right`, `GunnerEvents` (`:548570`) equips the V's own gun;
  the vehicle state `Turret` (5, `:6796`) is set by no script;
  `TurretTransition` (`:517239`) is the rip-off of a security turret.
  [2.5-untested]
- **No animation feature turns a vehicle's turret** from scripts: the
  nearest, `AnimFeature_ProceduralDriverCombatData { isEnabled,
yawDirectionFlipped, yaw, pitch, roll }` (`:298801`, applied as
  `n"ProceduralDriverCombatData"` in `DriverCombatFirearmsEvents`,
  `:550206`), turns the V's body; the vehicle gets only `n"VehicleState"`
  (`AnimFeature_VehicleState { tppEnabled }`, `:92220`), `n"SteeringLimit"`
  and `n"spoiler"`. The probe tandem keeps (`wheels turret PART`): a
  `LookAtAddEvent` (`:172727`; `bodyPart`, `SetStaticTarget`, `SetStyle`,
  `SetLimits`, removed with `LookAtRemoveEvent.QueueRemoveLookatEvent`,
  `:174614`) queued on the vehicle, as a security turret turns
  (`SensorDevice.CreateLookAt`, `:152184`); the part names are in the
  vehicle's rig (WolvenKit), not in the scripts. Other routes, unproven:
  the animgraph's inputs (`AnimationControllerComponent.SetInputFloat`…,
  `:57766`), a turret mesh component's `SetLocalOrientation` (`:19484`).
  [2.5-untested]
- **A muzzle without a hit**: `WeaponObject.TriggerWeaponEffects(weapon,
gamedataFxAction.Shoot)` (`:54864`) and `AnimationControllerComponent
.PushEventToReplicate(weapon, n"Shoot")`, as `AIWeapon.Fire` does after
  its shot (`:173340`); no tracer (the projectile draws it). Not used:
  the owner fires real shots and drops their hits instead. [2.5-untested]
- **The shot event** `VehicleMountedWeaponShootEvent { weaponID; slotID }`
  (`:298664`) is the engine's; `VehicleComponent.OnWeaponShootEvent`
  (`:90549`) only broadcasts the gunshot stim. A hit of a weapon shot
  carries the weapon (`AttackData.GetWeapon()`, `:52485`), which tandem uses
  to tell the owner's vehicle's shots from its collisions. [2.5-untested]

**Passenger combat in a copy of a partner's car** (spawned by
`DynamicEntitySystem`, moved by teleports, the partner's puppet mounted at
the wheel, no AI driver):

- The chain: `MountFromParent` adds the vehicle state machine unless the
  mount is silent (`:291571`, `silentUnmount` `:291579`); `EnteringEvents`
  (`:548360`); `PassengerDecisions.EnterCondition` →
  `IsPassengerInVehicle` (a native, `:548455`); `PassengerEvents.OnEnter`
  (`:548479`) puts the seat's restriction on, `SetVehicleStatusEffects`
  (`:282655`): `GameplayRestriction.DriverCombatFirearms` when the record's
  `VehDataPackage().DriverCombat().Type()` allows it
  (`GetVehicleAllowsCombat` `:90458`, `DoesVehicleSupportFireArms`
  `:283453`), else `NoWeapons`; drawing (`EmptyHandsEvents.OnUpdate`
  `:540780`: scene tier ≤ 2, no `NoWeapons`, the driver combat weapon
  request `:540835`); `EnteringCombatDecisions` (`:549539`: no `NoCombat`
  nor `VehicleScene` tag, no `Turret` tag on the record, a weapon in hand);
  `CombatEvents.OnEnter` sets the vehicle state 2 and opens the window
  (`:549741`). [2.5-untested]
- **Every check reads the record or the V**, never the driver, an AI, the
  car's speed, its persistent state or how it moves: a copy of the same
  record passes them as the real car does. [2.5-untested]
- **The one that reads the occupants**: entering by the game's own door,
  `stateMachineInitData.occupiedByNonFriendly` is set from each occupant's
  attitude to V (`MountEntityToSlot` `:291016`, `GetAttitudeOfPassenger`
  `:291043`, `:90268`), and in `seat_front_right` it costs V its weapons
  (`PassengerEvents.OnEnter`, `noWeaponsRestrictionApplied`, `:548503`);
  it also asks the non-friendly occupants out (`:548428`). tandem's own
  mount (`TandemVehicles.Seat`) passes `false`. A partner's puppet is made
  a friend of V (`Attitudes.reds`), but per agent, so the game may read it
  otherwise for a moment: tandem gives the weapons back in a copy whose
  occupants are only players' puppets (`PassengerEvents.OnEnter`,
  `Wheels.reds`). [2.5-untested]
- **A driver the game reads as inactive** is pulled out by a passenger
  getting in (`QueueHijackExitEventToInactiveDriver`, `:89962`;
  `ScriptedPuppet.IsActive` `:21056`: alive, not defeated, unconscious or
  turned off). A puppet is active; a dead puppet at the wheel would be put
  out. [2.5-untested]
- **Carried along** (a two-seat car, no seat): tandem's carry is a silent
  mount, so no vehicle state machine runs and the state never reaches
  Combat; tandem's restrictions keep the V unarmed. `NoWeapons` alone does
  not holster a weapon already drawn (`OnGameplayRestrictionAdded` has no
  branch for it, `:179041`); `NoCombat` does (`ForceEmptyHands`,
  `:540418`): `TandemTransit.Restrict` now puts both on. [2.5-untested]

### 13.101 Removing a car copy: the stub path, and what crashed (2026-11-21, game 2.50, exe read)

Lead: a study of another co-op mod (NightCoop), facts only: it spawns its
vehicle mirrors with a transient id (`gameIDynamicEntityIDSystem`
+0x1B0) and an entity stub (`gameIEntityStubSystem` +0x1B0), and removes
them through the stub system's +0x1C8 and +0x1C0 and
`gameIPopulationSystem` +0x1F8. Decompiled on 2.50:

- **Stub system** (`gameEntityStubSystem`, vtable `0x1431083C0`, RTTI size
  0xC8): +0x1B0 (`0x1402FA044`, hash 184577501) `CreateStub(out token,
request, callback)` queues a request (0xE0-byte entries at +0x48, under
  the lock at +0xB8); +0x1B8 (`0x1409CF378`) the same without an
  orientation (a restore); +0x1C0 (`0x140CB341C`, hash 303576408)
  `CancelStub(token)`: only while the token is still `Requested` (byte
  +0x10 = 0), takes the request out and marks it `Cancelled` (4); +0x1C8
  (`0x1402FD588`, hash 569519567) `DeleteStub(stub)`: tells the
  persistency system (+0xB0, `gameIPersistencySystem`, set at init from
  the game instance) to forget the id (its slot +0x200), then moves the
  stub to a list at +0x58 that the system's update destroys later;
  +0x1D0 (`0x140219D60`) `FindStub(id)`. These slots match
  RED4ext.SDK's `gameIEntityStubSystem.hpp`. [2.5-static]
- **Two SDK headers are one slot short on 2.50**: the population system's
  `RegisterEntity` is +0x1F0 (`0x140465F50`: request in, finds the stub
  with the stub system's +0x1D0) and `RemoveEntity(u32 id, …)` +0x1F8
  (`0x1402FCAFC`, hash 46405381: drops the id from its lists and
  despawns), where the vendored SDK says +0x1E8 and +0x1F0; the id
  system's transient id is +0x1B0 (`0x1404FAC20`: an atomic counter at
  +0xE0 kept under 0xFFFFFF), where the SDK says +0x1A8. Do not call these
  through the vendored headers. [2.5-static]
- **This is the path tandem already used.** Codeware's
  `DynamicEntitySystem` (MIT, `src/App/World/DynamicEntitySystem.cpp`)
  spawns with `GetNextTransientID`, `CreateStub` and the population
  system's `RegisterEntity`, and `DeleteEntity` is `RemoveEntity` then
  `DeleteStub`. NightCoop's only addition is `CancelStub`, which matters
  only for a copy deleted before its stub exists (it would otherwise be
  created after its deletion). The stub path is not a way around the
  crash of §13.6. [2.5-static]
- **What the crash was** (the stack of §13.6, decompiled): the job
  `0x140DBB958` (scheduled by `0x140DBBD14`) walks a range of entities
  queued for detaching (`u16` indices at +0x1FFDA4 into 0x38-byte records)
  and detaches each (`0x140206A14`: state byte +0x156 to 5, each
  component's slot +0x190, the entity's +0x168, state 2); the fault is the
  entity's own virtual call (`0x14071CA98`+0x15) on an entity already
  freed. So the car was freed while still queued for a detach. The
  per-entity state machine is `0x1410D1C04`. [2.5-static]
- **It does not always crash** (tandem logs, 2026-11-21): `test reset`
  (`TandemSystem.ClearTestCars`, `DynamicEntitySystem.DeleteEntity`)
  deleted three car copies in which the mock player's puppet had been
  seated, each about 1.5 s after the puppet's body was deleted, and the
  game went on. The crash of 2026-11-18 deleted the car 0.3 s after an
  unmount, or with the body still seated. [2.5-verified]
- **Design, not built** (needs a game to prove): delete a parked copy
  only when nobody is mounted in it, its last occupant's body is gone or
  unmounted for at least 2 s, and it is not the car of anyone in the
  session; through `DynamicEntitySystem.DeleteEntity` (no vtable call of
  ours); oldest first once more than a few are parked. To prove it: a
  scenario that seats a puppet, unmounts it, waits T in {0.3, 1, 2, 5} s
  and runs `vehicle clear`, `tools/game.sh bt` on a freeze. [2.5-untested]

### 13.140 A dynamic entity deleted before its stub exists: freed under the population system (2026-11-21, game 2.50, exe and Codeware source read)

The guest of the first two-game session crashed 23 ms after its first
playing tick (26 street proxies spawned in it, after 2.6 s of queued street
events): `0xC0000005` at `+0x22CA442`, a read of a non-canonical address
that was text (`rcx` `0x6544203043394605`, "4E9C0 De" as the pointer).

- **The stack, named**: thread proc, job runner `0x1402732D0`, the job
  `0x140D32E84` scheduled by `0x140C6C7B0` under the name
  `PopulationSystem/UpdatePooling`, its body `0x1404F6B5C` (hash
  3030128083; `this` is the population system, the update's time step in
  `rdx`), `0x1404F5C18` (hash 4106949370), `0x14046986C`, `0x140469888`
  (hash 3274775909), `0x141FFADA0`, `0x1422CA420` (hash 1301483273: a
  shared spin lock taken for reading). [2.5-static]
- **`0x1404F5C18`** takes two lists of the population system: the ids
  removed since (u32, +0x78 under the lock +0x70), then the registrations
  made since (0x38-byte entries, +0x60 under the lock +0x58: the spawn
  definition, the u32 id at +0x28, flags). For each registration it asks
  the stub system (+0x1FC0, slot +0x1D0 `FindStub`) for the stub and reads
  its place without a null check: `0x140469888` takes `[stub + 0x30]`,
  locks its byte +0xD1 and reads +0x80..+0x88 (or the attached entity's
  transform, +0xA8, state byte +0x156 = 4). The fault is that lock:
  `[stub + 0x30]` was text, so the stub's memory had been freed and
  reused. [2.5-static]
- **`RegisterEntity`** (`0x140465F50`) appends that entry when the stub's
  record resolves to something to spawn; **`RemoveEntity`**
  (`0x1402FCAFC`) takes a registration still in that list out again and
  adds the id to the removed list. Both call `FindStub` and read
  `[stub + 0x30]` unchecked: each needs a live stub. `DeleteStub`
  (`0x1402FD588`) moves the stub to the list the stub system destroys
  later. In order, a spawn then a deletion of a body whose stub exists is
  safe. [2.5-static]
- **What frees a stub outside that order** (Codeware 1.21,
  `DynamicEntitySystem.cpp`, MIT): `CreateEntity` queues `CreateStub`
  with a callback that holds the entity's state; `DeleteEntity` before the
  callback ran finds no stub in the state and only forgets the state. The
  callback then runs all the same: the state takes the stub
  (`UniquePtr<EntityStub>`), `RegisterEntity` queues the registration, and
  the callback's reference, the last one, goes: the stub is freed, still
  in the stub system's map and with its registration queued. The next
  pooling update reads it. While the freed memory is not reused the read
  succeeds and an orphan spawns; this is also §13.101's "created after its
  deletion". [2.5-static]
- **Cause of the crash: not proven**, the likeliest by far: the guest
  logged no removal (they are silent) and ran at `log = info`; the host's
  street has cars leaving the shared range every second, and a join's first
  tick takes every event queued since the session began in one batch, so a
  spawn and its removal met in one tick. Against the mock player the tick
  runs every frame and few vehicles move. [2.5-untested]
- **tandem deletes a body only once it exists** (`TandemEntities.Delete`,
  `scripts/tandem/Entities.reds`): at once when
  `DynamicEntitySystem.IsSpawned`, else at the first tick it is (or after
  10 s: its stub is long there). The first held deletion of a game is
  logged at `info` (`entities: body … deleted before it exists`).
  `tools/game.sh bt` names this crash site. [2.5-untested]

### 13.141 A teleport of the player's mount every frame stops every spawn; the vehicle's kinematic move (2026-11-22, game 2.50, two instances and exe read)

A guest riding in the host's car copy saw none of the host's traffic nor
its pedestrians: 89 street proxies asked again in 25 s, and 0 NPC proxy
bodies in 3 minutes of ride (5, 7, 4 a minute on foot before it), every
body `IsManaged`, neither `IsSpawning` nor `IsSpawned`.

- **The cause**: `TeleportationFacility.Teleport` on an entity the player
  is mounted to (a seat or a slot: the mounting facility's parent of V) is
  a teleport of the player, and in a frame with one the population system
  spawns nothing of what is registered with it (Codeware's
  `DynamicEntitySystem.CreateEntity`: vehicles and NPCs alike). tandem
  teleported the car copy every frame (`TandemVehicles.Follow`), moving or
  not. [2.5-verified]
- **How it was measured** (instance b, 30 frames/s; a vehicle asked 14 m
  from V and an NPC 8 m from it, the time until `GetEntity` returns each):

  | V is                                                        | vehicle           | NPC          |
  | ----------------------------------------------------------- | ----------------- | ------------ |
  | on foot                                                     | 0.07 s            | 0.20 s       |
  | at the wheel of its own real car (the host)                 | within the frame  | not measured |
  | in a seat of a dynamic car nobody moves                     | 0.07 s            | 0.06 s       |
  | in that seat, the car teleported to where it is every frame | never (10.7 s)    | never (9.3 s) |
  | … the teleports stopped                                     | 0.065 s later     | 0.39 s later |
  | in that seat, the car teleported every 2nd frame            | 0.10 s            | not measured |
  | in that seat, the car teleported every 4th frame            | 0.09 s            | not measured |
  | in the host's car copy, followed every frame                | never (8.9 s)     | never (7.5 s) |
  | … the follow stopped                                        | 0.10 s later      | 0.39 s later |
  | … the follow back on, another asked                         | never (6.5 s)     | not measured |
  | … out of the seat                                           | 0.075 s later     | not measured |

  A car teleported every frame with nobody of ours in it stops nothing
  (the street's proxies, `vehicle probe`). It is not the seat, its
  workspot, the car being a dynamic entity, nor speed: the car stood
  still. Whether one frame without a teleport among many is enough was not
  measured (every 2nd and every 4th were). [2.5-verified]
- **`Entity.SetWorldTransform`** (Codeware: the placed component's
  transform) does not move a vehicle: asked every frame for a point up to
  20 m away, the car stayed within 0.003 m of where it was, V in it.
  [2.5-verified]
- **The vehicle's own kinematic move** (address-library hash 2933986803,
  `0x1402E3270` on 2.50, five callers in the exe;
  `(vehicleBaseObject *, WorldTransform const &, float seconds)`): writes
  the transform to the runtime data (`+0x5F0`: position `+0x10`,
  orientation `+0x20`), and in its physics half (`0x1402E33FC`, on the
  object at `+0x2D0`) gives the rigid body (`+0x2D8`: linear velocity
  `+0x18`, angular `+0x24`) the velocity that reaches the transform in
  `seconds`, or puts the body there at rest when that is 100 m/s or more
  (the float at `0x1437BA1F4`), then moves the physics proxy; it ends by
  copying the body's velocities to the runtime data. `+0x2D8` and `+0x5F0`
  are read without a check. Nothing in it goes through the teleportation
  facility. [2.5-static]
- **What the decompile says the move does on its own**: the transform is
  written to the rigid body in both branches (`0x1406E101C`), so the body
  is where it was told whatever the velocity; under 100 m/s the physics
  proxy (the weak handle at `+0x2F0`, its id at `+0x160`) gets a kinematic
  target, or, when a query on the proxy says so (`0x140BA9FF8`, mode
  `0x15`) or it is farther than the step allows, its transform and both
  velocities set outright. With no physics object at `+0x2D0` only the
  proxy's transform is set. Not read: what `+0xB0` and `0x14061DDC8` do
  first, which update phase the engine's five callers run in (tandem calls
  it from the script tick), and whether a body whose physics sleep is
  moved. [2.5-static]
- **tandem** (`TandemSystem.MoveBody`, `scripts/tandem/System.reds`;
  `docs/architecture/vehicles.md`, "How a copy is moved"): the kinematic
  move for every vehicle another game runs (`TandemWheels.Move`,
  `plugin/src/engine/vehiclemove.cpp`, behind the class size `0xBC0`,
  `kinematic_ride = off` to turn it off), a teleport for a first
  placement, over 8 m and for a body that does not follow; the car our V
  is mounted to never teleported two frames running while a body of ours
  waits to spawn (`TandemEntities.Create` keeps the list). Built by CI,
  **never called in game** (the game machine went off before its first
  call): that the body and what is mounted to it follow, that the spawns
  come, the wheels, the sound, a contact with our V, and that nothing
  crashes are all to test. [2.5-untested]
- **A hot reload of the scripts in a session** crashed the guest 5 s later
  (`0xC0000005` at `+0x2AE4A10`, a job thread, once; `tools/game.sh reload`
  on instance b, 2026-11-22): restart the game instead while it is in a
  session. [2.5-verified]

### 13.150 CrystalCoat / TwinTone: a vehicle's custom paint (2026-11-22, 2.50 scripts read, not run in game)

All from the decompiled 2.50 scripts (`final.reds`), nothing run:
[2.5-untested] throughout.

- **The template** is `VehicleVisualCustomizationTemplate`: `hasUniqueTemplate`,
  `genericData` (`GenericTemplatePersistentData`: `primaryColorDefined` and
  `primaryColorR/G/B` as `Uint8`, the same for `secondary`,
  `lightsColorDefined`, `lightsColorHue` in degrees) and `uniqueData`
  (`UniqueTemplateData`: `recordId` of a `VehicleColorTemplate` record,
  `twintoneModelName`, multilayers, decals, clear coats, all read from the
  record by `VehicleVisualCustomizationTemplate.FromRecord`). `IsValid`: a
  unique one with multilayers, or a generic one with a primary colour or a
  lights' colour. `Equals`: unique by `recordId`; generic by its flags, its
  colours within 2 and its hue within 2.
- **Where it lives**: `VehicleComponentPS`, persistent:
  `m_vehicleVisualCustomizationTemplate` (a
  `SavedVehicleVisualCustomizationTemplate`: the generic data, or the
  unique record's id and model name), `m_vehicleApperanceCustomizationActive`,
  `…InDistanceTermination`, `m_vehicleAppearanceCustomizationBlockedByDamage`;
  public `Get/SetVehicleVisualCustomizationTemplate`,
  `Get/SetVehicleVisualCustomizationActive`,
  `SetVehicleVisualCustomizationnBlockedByDamage` (two n),
  `SetVehicleApperanceCustomizationInDistanceTermination`. A player's
  templates per vehicle record are stored on the player
  (`PlayerPuppet.GetVehicleVisualCustomizationComponent`,
  `vehicleVisualCustomizationComponent.RetrieveVisualCustomizationForVehicle`).
- **How the game applies one**: the UI queues a
  `NewVehicleVisualCustomizationEvent {template, reset}` on the vehicle
  (`VehicleObject.OnNewVehicleVisualCustomizationEvent`): it asks
  `VehicleComponent.CanApplyTemplateOnVehicle`, writes the template to the
  state, stores it on the player (`StoreVisualCustomizationDataForIDEvent`),
  touches the wanted level (`ProcessHeatLevelOnVisualCustomization`), then
  `ExecuteVehicleVisualCustomizationEvent {set, reset, instant}` after
  0.8 s. `VehicleComponent.ExecuteVehicleVisualCustomization` takes the
  current paint off (`DisableCurrentCustomization`: the unique decals
  hidden, the default decals back, the `vvc_color*` effects' loops broken),
  sets the state's active flag, queues
  `NewVehicleVisualCustomizationTemplateEvent {template, isInstant}` 0.6 s
  later and `VehicleCustomizationLightsEvent` 0.5 s (1.6 s with a paint)
  later, and gives the player `BaseStatusEffect.VehicleVisualModCooldown`.
  The template event ends in the protected
  `VehicleComponent.ApplyVehicleVisualCustomizationTemplate`: the native
  `VehicleCustomizationComponent` (`VehicleObject.GetCustomizationComponent`,
  kept as `VehicleComponent.m_customizationComponent` from the component's
  `OnGameAttach`) gets `SetDefaultDecalsEnabled(false)` and
  `SetupGenericCustomization(record.ExcludedComponentsGeneric(),
  record.GenericCustomizationMask(), packedA, packedB, isInstant,
  record.CustomizeCarRims())` (a colour packed as
  `r + g / 100 + b / 10000`, each 0.01 to 0.99: `PackColorForEffect`), or
  `SetupUniqueCustomization` and `ShowUniqueCustomizationDecalsWithDisolve`.
  The lights event (`OnProcessVehicleVisualCustomizationLights`) reads the
  state's template: `VehicleLightQuestChangeColorEvent` for the head,
  interior (and a bike's utility) lights from the hue, or back to the
  record's colours; only where the record says `CustomizeLights`.
- **Who may**: `CanApplyTemplateOnVehicle` wants
  `GetIsVehicleVisualCustomizationEnabled` (the fact
  `vvc_visual_customization_for_all_vehicles`, or the record's
  `HasVisualCustomization` with the fact `vvc_visual_customization_unlocked`),
  no damage block, no teaser record, an up-to-date one (`mq058_update_applied`
  where the record asks), and for a unique template the vehicle's
  `TwintoneModelName`. The request events therefore do nothing in a game
  that has not unlocked the feature; the protected apply asks nothing.
- **What takes it off by itself**: a part falling off (trunk, hood, a door,
  a bumper: `OnVehicleOnPartDetached`) calls
  `SignalDamageToVehicleVisualCustomization` (the paint off, the state
  blocked); the local player unmounting starts
  `CheckVehicleVisialCustomizationDistanceTermination` (1 s later, then
  every 0.5 s): past a squared distance of 32 from the local player the
  paint is switched off (`SwitchVehicleVisualCustomizationStateEvent`, the
  active flag cleared, the template kept), and
  `OnVehicleFinishedMounting` switches it on again. The local player
  mounting also writes that player's stored template for the record into
  the state (`SyncVehicleVisualCustomizationDefinition`, an empty one when
  it has none), where the feature is enabled.
- **What tandem does** (`scripts/tandem/Paint.reds`, built, not run): reads
  the owner's car's state; on a copy writes the state and calls the
  protected apply through an added method, 0.6 s after
  `DisableCurrentCustomization` when a paint was on. To see in game
  [to-see]: that `SetupGenericCustomization` paints a vehicle whose game has
  not unlocked CrystalCoat and a record without `HasVisualCustomization`
  (an empty mask); that it starts the `vvc_color` effect itself (no script
  starts it, only breaks its loop); the look of an instant apply on a copy
  that just spawned.
