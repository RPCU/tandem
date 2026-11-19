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
than delete them. [2.5-verified]

**The full vehicle loop runs** (mock player `--mode drive`): copy spawned
from the record, the driver's puppet mounted in `seat_front_left`
(`MountingFacility.Mount`, no AI), the copy teleported every frame to the
interpolated `VehicleState`, the puppet unmounted when the driver gets out
(`MountingFacility.Unmount`), our movement controller re-attached 0.5 s
later (mounting detaches it), the driver leaving the session while seated.
[2.5-verified]

### 13.12 The car radio and the pocket radio (2026-11-18, 2.50 scripts read)

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
