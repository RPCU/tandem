# NCART metro, rides and quest vehicles

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.27 The NCART metro, rides and quest vehicles (2026-11-18, 2.50 scripts and resource list read, not run)

Read in the 2.50 script dump (`final.reds`, line numbers below) and
RedHotTools' resource list; used by `scripts/tandem/Transit.reds`,
`Npcs.reds` and `System.reds`. Nothing here has run in game yet: every
item is [2.5-untested] unless marked.

**The metro is a vehicle V mounts.**

- The train is `ncartMetroObject extends AVObject` (216019), its vehicle
  component `NcartMetroComponent extends VehicleComponent` (291468): V gets
  on through the mounting facility like into any car, so
  `VehicleComponent.GetVehicle` / `PlayerPuppet.GetMountedVehicle` return
  the train and the state machine's `MountedToVehicle` is true. Its
  template: `base\open_world\metro\ue_metro\entities\ue_metro_train.ent`;
  its passengers are NPCs of their own (`ue_metro_passenger_*.ent`); the
  ride is the `ue_metro` quest (per station `phases\stations\*.questphase`,
  not in scripts). Whether the train entity has a static id (the same in
  every game) is unknown: tandem checks the class of whatever entity has
  the rider's train id.
- `VehicleComponent.IsDriver` is false for every `AVObject` (89437-89452),
  the metro included, whatever the seat.
- While V rides, it has a status effect tagged `MetroRide`: the pocket
  radio stays allowed and its toggles go to the train (`PocketRadio`
  244237, 244370-244382); the radio HUD checks it too (531048, 483377).
  The train keeps itself level with `PitchUp`/`PitchDown` transform
  animations unless fact `ue_metro_disable_pitch_adjustment` (216100) and
  turns round with a 180° teleport (`PerformYawAjustment`).
- Stations: `ENcartStations` 1..19 (10800), the same numbers in the facts
  `ue_metro_active_station` (set by `NcartStationMarker` when the metro
  passes it: 492639-492646) and `ue_metro_next_station`; names
  `LocKey#95259..95277` (`NcartTrainInkControllerBase`, 284641); lines
  `ue_metro_show_line_[a-e][12]` (343001).
- The station: entering a subway gate's `SubwayGateArea` (`DataTerm`,
  237572-237600) sets `ue_metro_display_options` = 1, records the gate as
  `FastTRavelSystem.LastSubwayGateUsed` and teleports the gate's metro
  animation entity to it. The gate opens the world map
  (`OpenWorldMapDeviceAction`, 50624); NCART stations are fast travel
  mappins of variant `Zzz17_NCARTVariant` (50553). Fast travelling from
  there is ordinary fast travel (`FastTravelPrefetchRequest` to the gate,
  facts `ue_metro_map_ui_ft_clicked`, `ue_metro_ft_system_prefetch_*`;
  358871-358902, 359078-359090): a loading screen and a jump.
- **Skipping a ride is a speed-up, not a teleport.** Under a status effect
  tagged `CustomFastForward`, V gets the fast-forward prompt
  (`PlayerPuppet.SetCustomFastForwardEnabled`, 44213-44255); holding
  `SceneFastForward` sets the global dilation `customFFTimeDilation` to
  **10** with the `transition_glitch_loop` effect and the
  `motion_light_fast_2d` sound, releasing it unsets it (40298-40311). That
  the metro ride grants `CustomFastForward` is the quest's data, not read
  [unverified]. tandem sends `kTransitSkipping` while it is active.
- Delamain's cab: `DelamainTaxiSystem`, UI context `DelamainTaxi`, status
  tag `DelamainTaxi` (the pocket radio stays allowed, 483377-483379).

**Story scenes in vehicles.**

- The player state machine's vehicle state (`PlayerStateMachine.Vehicle`,
  `gamePSMVehicle`: 1 Driving, 2 Combat, 3 Passenger, 5 Turret, 6
  DriverCombat, 7 Scene) is Scene while a scene tier 3..5 holds V in a
  vehicle (`SceneDecisions`, 551665-551700; `SceneEvents` 552330): the
  vehicle camera goes to scene mode and V is put on the vehicle's
  `OccupantSlots` workspot (`WorkspotSystem.MountToVehicle`). The scene
  switches V between `PuppetVehicleState` IdleMounted, IdleStand,
  CombatWindowed, CombatSeated, Turret and GunnerSlot
  (`scenePuppetVehicleState`), drawing a ranged weapon for the combat ones.
- Gunner seats are slots `gunner_back_left` / `gunner_back_right`
  (`GunnerDecisions`, 548550: vehicle state 2, ranged weapon out).
  `VehicleObject.IsPerformingSceneAnimation`, `GetActiveWeapons`,
  `IsArmedVehicle`, `CanSwitchWeapons`, `NPCShoot(target, projectiles)`,
  `EnableNPCCombat` are natives of every vehicle (87700-87940); a driver of
  a vehicle with `gamedataDriverCombatType.MountedWeapons` fires its
  mounted guns (`ToDriverCombatMountedWeapons`).
- **The Basilisk** is a `TankObject extends VehicleObject` (174400;
  `gamedataVehicleType.Panzer`, model `Basilisk`; template
  `base\vehicles\special\v_militech_basilisk_01__basic_01.ent`, quest
  record names not read). Its pilot sits in `seat_front_left`: mounting
  there turns V's hit shapes off and makes targeting ignore the tank
  (`TogglePlayerHitShapesForPanzer`, `ToggleTargetingSystemForPanzer`,
  90643-90646, 92281-92320), and a failsafe makes the pilot's door
  available whenever its interaction is evaluated
  (`EvaluatePanzerInteractions`, 91791).
- Seats: `VehicleComponent.GetSeats` (the record's
  `VehDataPackage.VehSeatSet`, each `SeatName()`), `HasSlot`,
  `IsSlotOccupied` (the mounting facility's child in that slot),
  `IsSlotAvailable`, `GetFirstAvailableSlot` (which also skips seats the
  AI component reserves), `IsDriverSlot` (90016-90440, 89349).
- Hiding a body reversibly: `IVisualComponent.TemporaryHide(on)` (106206;
  vanilla swaps broken and fixed meshes with it, 511696-511718).
  `HideIrreversibly` (19392) is for good.

**A V in a vehicle with no seat for it** (a guest riding along, Transit.reds).

- No camera to lend: the vehicle's FPP/TPP cameras and its scene camera
  mode (`SetVehicleCameraSceneMode`, `vehicleRequestCameraPerspectiveEvent`)
  are driven by the player state machine's vehicle states, which only run
  for a V mounted in a seat (`VehicleComponent.OnMountingEvent` adds the
  `Vehicle` state machine, 291579-291602); `StateGameScriptInterface.
ActivateCameraSetting` (247390) switches settings of V's own camera.
  Only photo mode has a free camera. A carried V therefore looks through
  its own eyes.
- A mount without the vehicle state machine exists: a mounting request
  whose `mountEventOptions.silentUnmount` is set adds no `Vehicle` state
  machine (`MountFromParent`, 291579); vanilla mounts the player that way
  when it switches seats (`SwitchSeatsDecisions`, 549395-549460). A body
  is mounted in a trunk on slot `trunk_body` with
  `preservePositionAfterMounting` (`VehicleComponent.MountEntityToSlot`,
  92792), the slot not being a seat. tandem carries a V riding along that
  way (silent, position kept, slot `trunk_body` of its own copy): whether
  the player's body then follows the car, how its own locomotion and
  collision behave while mounted, and whether every copy has the slot
  (bikes, AVs, the tank) are [unverified]; the mount is checked after
  0.5 s and a 5 Hz placement takes over when it did not hold.
  `MountEventData.initialTransformLS` (160911) is another way to place a
  mounted child relative to its slot (not used).
- `WorkspotGameSystem.PlayInDevice(vehicle, player, n"lockedCamera",
n"trunkBodyDisposalPlayer", …)` plays the player in a workspot of a
  vehicle (the trunk body sequences, 92673-92760) while the car stands;
  `MountToVehicle` (53240) is the moving case, and it takes a seat slot.
- Restrictions a script can put on V and take off by record:
  `GameplayRestriction.NoMovement`, `NoJump`, `NoWeapons`,
  `NoWorldInteractions`, `VehicleNoInteraction`, `ForceCrouch`,
  `NoCameraControl` (all used by vanilla, `t"GameplayRestriction.*"`), and
  `BaseStatusEffect.Invulnerable`. Status effects can be saved: tandem
  removes its own on load.
- `WorkspotSystem.MountToVehicle(parent, child, slide, delay,
n"OccupantSlots", slot)` (53240) is the scene's way into a seat's
  workspot: a seat again.
- Codeware's `Entity.AddComponent` only initializes a component added at
  the entity's assembly (its builder events); `entHardTransformBinding`
  binds components within one entity. Neither parents the player to a
  vehicle.
