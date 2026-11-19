# Autopilot: what can make V act without a human

Research for the test pilot (`tools/pilot`, `scripts/tandem/Pilot.reds`;
design in [`docs/architecture/autopilot.md`](../../architecture/autopilot.md)).
Two questions: what the game lets a **script** make the player do with its
real gameplay path, and how to feed it **input** from outside for the rest.

Read on 2026-11-19. **Nothing here has run in the game yet**: the game
machine was in use. The first-run checklist in the design document is the
order to verify it in; update the tags here as each line is proven.

**Sources**

- `final.reds:LINE` — the decompiled 2.50 scripts,
  `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`.
- `rtti` — `docs/research/data/rtti-natives-2.50.tsv` (native functions and
  their handler addresses in the 2.50 exe).
- `CW` — Codeware's scripts, `~/.cache/tandem/ref/codeware/scripts` (MIT).
- `nixbook` — the game machine's NixOS configuration,
  `~/Documents/perso/nixbook`.
- No code from CyberpunkMP, NCO or the campaign fork was read for this.

**Tags**

- `[2.5-untested]` — read in the 2.50 scripts or RTTI; what `Pilot.reds`
  uses of it compiles against the game's own bundle (`task scripts:check`,
  0 errors, 0 warnings); not run in game.
- `[2.5-verified]` — ran in game on 2.50 (none yet).
- `[offline-verified]` — ran on the development machine, no game.
- `[documented]`, `[reported]`, `[inference]`, `[local]` — for the input
  research: in the project's or kernel's documentation; reported by several
  users; reasoned, not read anywhere; read in `nixbook`.

---

## 14.1 What a script can do

### 14.1.1 Summary

| Action                               | Script-level?  | How                                                                              | In `Pilot.reds` as        |
| ------------------------------------ | -------------- | -------------------------------------------------------------------------------- | ------------------------- |
| Walk, sprint, jump, climb, dodge     | **no**         | no player move-to, no input injection in script (§14.1.2)                        | the pad                   |
| Teleport                             | yes            | `TeleportationFacility.Teleport`                                                 | `cmd teleport` (existing) |
| Look / aim at a point                | yes            | `TargetingSystem.LookAt(player, AimRequest)`                                     | `act look`                |
| Draw, holster, pick a weapon slot    | yes            | `EquipmentSystemWeaponManipulationRequest`                                       | `act equip`               |
| Fire the weapon in hand              | likely         | `QuestForceShoot` blackboard bool + per-frame `questForceShoot` PSM parameter    | `act fire`                |
| Reload                               | shortcut only  | `WeaponObject.StartReload` (no animation; the real one is input)                 | `act reload`              |
| Melee attack, grenade throw          | **no**         | read from the input system by the state machine                                  | the pad                   |
| Sandevistan and other activated OS   | yes            | `ItemActionsHelper.UseItem`, or the `requestSandevistanActivation` PSM parameter | `act ability`             |
| Heal                                 | shortcut       | `ItemActionsHelper.UseHealCharge` (the animated one is the consumable hotkey)    | `act heal`                |
| Lose health, go down                 | yes            | `StatPoolsSystem.RequestSettingStatPoolValue`                                    | `act hurt`                |
| Quickhack an NPC                     | yes            | `ExecutePuppetActionEvent` on the target                                         | `act hack`                |
| Grab, takedown                       | yes            | `StartTakedownEvent` on the player                                               | `act takedown`            |
| Enter and leave a vehicle (animated) | yes            | `MountingRequest` / `UnmountingRequest` with `isInstant = false`                 | `act mount`, `act exit`   |
| Call V's car                         | yes            | `VehicleSystem.SpawnActivePlayerVehicle`                                         | `act summon`              |
| Drive                                | AutoDrive only | `EnableAutoDriveRequest` (patch 2.3); no throttle or steering natives            | `act autodrive`, the pad  |
| Doors, terminals, lift floors        | yes            | the device's own `ExecuteAction(action, executor)`                               | `act device`, `act lift`  |
| Any other prompt ("press F")         | no native call | the pad's X once the prompt is up (`prompt` in the state)                        | the pad                   |
| Dialogue choice                      | **no**         | committed by the native interaction system from input only                       | the pad (`choose`)        |
| Answer a call                        | yes            | `PickupPhoneRequest`                                                             | `act call`                |
| Loot a body or container             | yes            | `TransactionSystem.TransferAllItems`                                             | `act loot`                |
| Buy from a vendor                    | yes            | `BuyRequest` to the `MarketSystem`                                               | `act vendor`              |
| Lock saving                          | yes            | `SaveLocksManager`, and our own `ScriptableSystem.IsSavingLocked`                | always, from `pilot on`   |

### 14.1.2 Movement and input: nothing in script

- **No move-to for the player.** `AIMoveToCommand` (`final.reds:262294`) is
  for an `AIComponent`; the player state machine has an `AiControlled`
  state (`AiControlledDecisions`, `:527524`) that is entered only when the
  player has an AI component and the temporary `playerControlled` is false,
  and no vanilla script enters it. `pilot probe` logs whether our V has an
  AI controller component. `[2.5-untested]`
- **No input injection.** `ListenerAction` (`:192853`) is a native struct
  with no settable field, and the state machines read
  `scriptInterface.IsActionJustPressed(n"…")` / `GetActionValue`, which go
  to the native input system. Searched the RTTI dump for `Simulat`,
  `Inject`, `FakeInput`, `ForceInput`, `inputContext`, `InputSystem`.
- **What only limits movement**: the `ForceWalk` / `ForceIdle` /
  `ForceFreeze` state machine parameters (`:281340`), the status effects
  `GameplayRestriction.ForceCrouch` (`:155359`), `NoMovement` (`:283592`),
  `PSMImpulse` (`:249712`, a physics push). None walks V anywhere.
- **The one exception worth a live probe:** the game ships its developers'
  functional-test system. `rtti` lists, with real handlers in the retail
  exe: `FunctionalTestsGameSystem.FakeInputPressAction`,
  `FakeInputReleaseAction`, `FakeInputClickAction`, `FakeInputHoldAction`,
  `FakeInputAxisAction`, `FakePressButton`, `FakeSetAxis`, `TakeOverInput`,
  `ReleaseAllInput`, `NavigateFlatTowards`, `LookAtPosition`,
  `TeleportPlayer`, and `PlayerFunctionalTests.SetCameraOrientation`. `CW`
  declares them without parameters (generated imports), and no script
  getter returns the system (`TestStepLogic.GetFunctionalTestsGameSystem`
  is on a test-only class). Whether a retail game instance **has** the
  system is unknown. `tools/game.sh cmd pilot probe` reads it without
  calling anything: `TandemPilotNative.ProbeFunctionalTests`
  (`plugin/src/pilot_natives.cpp`) looks the class up in RTTI, asks the
  game instance for a system of that type, and logs every function with
  its parameter names and types. If the instance is there, input by
  **action name** (no device, no focus, no Steam) becomes possible from the
  plugin, and would replace the pad. `[2.5-untested]`

### 14.1.3 Look and aim

- `TargetingSystem.LookAt(instigator, out params: AimRequest)`
  (`:108394`), with `BreakAimSnap(instigator)` first (`:108402`) and
  `BreakLookAt(instigator)` to end it (`:108404`). Vanilla turns V's view
  with it in `ForcePlayerLookat_Kurt` (`:514922-514949`: duration 0.1,
  precision 0.01, pitch and yaw adjusted, `checkRange` false,
  `processAsInput` true) and at `:245579` (`endOnTimeExceeded` with a
  `maxDuration`). `AimRequest`'s fields: `:167777`. `Pilot.reds` sets every
  field (the VM does not clear a struct local) and re-issues the request
  five times a second while a look is held. `[2.5-untested]`
- No yaw or pitch setter on the camera: `FPPCameraComponent` has
  `ResetPitch()` only (`:174901`). Fallback: the pad's right stick, closed
  on the view vector in the state (`face`).
- The view itself, for the state: `CameraSystem.GetActiveCameraForward()`
  (`:174229`) and `GetActiveCameraWorldTransform(transform)` (`:174219`;
  vanilla reads the transform after the call, `:148115`). `[2.5-untested]`

### 14.1.4 Weapons

- Draw and holster: `EquipmentSystemWeaponManipulationRequest` (`:178788`,
  `owner` = the player) queued to the `EquipmentSystem` scriptable system,
  as vanilla does at `:44680`. `EquipmentManipulationAction` (`:11190`):
  `RequestLastUsedOrFirstAvailableWeapon`, `RequestWeaponSlot1..4`,
  `RequestFists`, `UnequipWeapon`. `[2.5-untested]`
- Fire: both `SemiAutoDecisions` (`:551751`) and `FullAutoDecisions`
  (`:552677`) listen to the player state machine blackboard bool
  `PlayerStateMachine.QuestForceShoot` (`:53772`); it only arms their
  enter condition, which then needs the temporary parameter
  `n"questForceShoot"` true (`GetQuestForceShootName`, `:286834`) and a
  shot being possible. No vanilla script writes either (quests do,
  natively). `Pilot.reds` sets the bool and queues a
  `PSMPostponedParameterBool{id = n"questForceShoot", value = true}` on V
  every frame while firing. It is the player's own weapon state machine,
  so ammo, recoil, sound and our `Shots.reds` hooks apply. **The least
  certain action here**; the pad's right trigger is the fallback the
  routines use. `[2.5-untested]`
- Reload: `WeaponObject.StartReload(opt duration)` (`:54046`) refills
  without the state machine (vanilla: an instant reload perk, `:524833`).
  The animated reload reads the input buffer. `[2.5-untested]`
- Not possible: melee attacks and grenade throws are decided from input
  (`:248828`, `:518447` for dodge).

### 14.1.5 Cyberware, health, death

- `ItemActionsHelper.UseItem(executor, itemID)` (`:69438`) on
  `EquipmentSystem.GetData(player).GetActiveItem(gamedataEquipmentArea.SystemReplacementCW)`
  is what `PlayerPuppet.ActivateIconicCyberware` does (`:40346-40374`).
  The Sandevistan itself: `UseSandevistanAction.StartAction` queues
  `PSMPostponedParameterBool{id = n"requestSandevistanActivation"}`
  (`:223936`; `requestSandevistanDeactivation` at `:223964`), allowed when
  `TimeDilationHelper.CanUseTimeDilation(player)` (`:240456`).
  `[2.5-untested]`
- `ItemActionsHelper.UseHealCharge(executor, itemID)` (`:69456`) is what
  the consumable state applies at its cast point (`:515924`); the item is
  the d-pad-up hotkey's, `GetItemIDFromHotkey(EHotkey.DPAD_UP)` (`:59594`).
  `[2.5-untested]`
- Health: `StatPoolsSystem.RequestSettingStatPoolValue` (`:83577`, already
  used by `revive down`). Alone, health 0 is death: `PlayerPuppet.OnDeath`
  (`:45314`) invalidates autosaves and the death menu follows
  (`:283812`, `:335229`); nothing reloads by itself. In a session with
  another V up, `Revive.reds` makes it "down". `act hurt all` refuses
  unless that protection is on.

### 14.1.6 Quickhacks, takedowns, loot, vendors, the phone

- Quickhack: `ExecutePuppetActionEvent{actionID}` (`:201075`) queued on the
  NPC; `ScriptedPuppet.OnExecutePuppetAction` (`:22452`) builds the
  `PuppetAction`, sets the player as executor and runs `ProcessRPGAction`
  (`:26605`: RAM cost, upload with its stat pool and listener `:28078`,
  effects, `CompleteAction` `:28124`). The scanner is not needed. Not
  checked on this path: exposure, range, line of sight (the wheel's
  `GetAllChoices` does that). Already used by `hack npc`. `[2.5-untested]`
  for our V on a non-proxy NPC.
- Takedown: choosing the prompt makes `ScriptedPuppet` queue on the
  activator `StartTakedownEvent{slideTime 0.8, target, actionName}`
  (`PushTakedownActionEventToPSM`, `:22628`; the class `:252771`);
  `gamestateMachineComponent.OnStartTakedownEvent` (`:291500`) adds the
  `LocomotionTakedown` state machine. Action names
  (`TakedownUtils.TakedownActionNameToEnum`, `:285424`): `GrappleTarget`,
  `Takedown`, `TakedownNonLethal`, `TakedownNetrunner`,
  `TakedownMassiveTarget`, `BossTakedown`, `LeapToTarget`, `ForceShove`.
  A second event during a grapple to finish it is read, not proven
  (`GrappleStandDecisions.ToTakedownExecuteTakedown`, `:536157`).
  `[2.5-untested]`
- Loot: `TransactionSystem.TransferAllItems(source, target)` (`:52091`) is
  the body's "Loot" choice (`ScriptedPuppet.LootAllItems`, `:22557`).
  Picking one line of a loot list is native. `[2.5-untested]`
- Vendor: `MarketSystem.GetVendorItemsForSale(vendor, checkPlayerCanBuy)`
  (`:130368`), then a `BuyRequest` (`:202194`; `owner` = the vendor,
  `items` of `TransactionRequestData`) queued to
  `MarketSystem.GetInstance(game)` (`:130467`), as
  `VendorDataManager.BuyItemFromVendor` does (`:230189`). No UI needed.
  `[2.5-untested]`
- Phone: `PickupPhoneRequest{CallInformation, shouldBeRejected}`
  (`:241186`) queued to the `PhoneSystem`, as the phone HUD does
  (`:408455`); the handler writes the call's fact, which moves the quest
  on (`:240662`). `[2.5-untested]`
- Dialogue choices: **input only.** `UIInteractions.DialogChoiceHubs`,
  `ActiveChoiceHubID`, `SelectedIndex` (`:195335`) are outputs to the UI;
  the scene interface (`:193095`) has no "choose". The state carries
  `choices` and `sel` so the pad can move the selection and confirm.
  Whether `SelectedIndex` follows the dialogue selection is `[2.5-untested]`.

### 14.1.7 Devices

- No "activate the interaction in front of V" native (`InteractionManager`
  `:199584`, `InteractionComponent` `:94789`).
- A device runs a chosen action through its protected
  `ExecuteAction(action, executor)` (`:98567`: `RegisterAsRequester`,
  `SetExecutor`, `ProcessRPGAction` with the device's role component).
  `Pilot.reds` adds `Device.TandemPilotExecute` to reach it, takes the
  device V looks at (`TargetingSystem.GetLookAtObject`, `:108382`), lists
  its actions for V (`GetDevicePS().GetActions(actions, GenerateContext(Direct, …))`,
  `:32102`) and runs the one named. Doors offer `ToggleOpen`
  (`DoorControllerPS.ActionToggleOpen`, `:135132`); the lift's floors are
  `LiftControllerPS.ActionGoToFloor(count, current, floor, name, authorized)`
  (`:137814`, floors from `GetFloors()` `:137788`). Whether
  `GetLookAtObject` returns the lift's panel from inside is unknown.
  `[2.5-untested]`

### 14.1.8 Vehicles and AutoDrive

- Entering: a `MountingRequest` whose `mountData.isInstant` is false makes
  the player's state machine play the entry
  (`gamestateMachineComponent.MountFromParent`, `:291569`); `TandemVehicles.Seat`
  is the instant variant. Leaving: an `UnmountingRequest` that is not
  instant (`IsExitForced`, `:283117`, then `ToExiting`, `:548343`).
  `[2.5-untested]`
- The called car's entity: blackboard `VehicleSummonData.SummonedVehicleEntityID`
  (`:190029`).
- **AutoDrive (patch 2.3)**: the scriptable system `AutoDriveSystem`
  (`:216326`, extends `NativeAutodriveSystem`). `EnableAutoDriveRequest`
  (`:217283`) → `OnEnableAutoDriveRequest` (`:216531`): refuses off the
  road or off a valid lane, then `GetAutodriveAvailable()` (`:216337`: a
  player vehicle, no brake or accelerate quickhack on it, out of combat,
  not aiming, vehicle state 1, 3 or 6, car health above the threshold),
  then `SetAutodriveEnabled(true)`. `DisableAutoDriveRequest` (`:316886`),
  `ToggleFreeRoamRequest` (`:316889`). The key's handler
  (`AutoDriveController.OnAction`, `:442307`) queues the same requests;
  any accelerate or brake input turns it off. Destination: the tracked
  mappin or quest; with nothing tracked it cruises (free roam). State:
  blackboard `UI_AutodriveData` (`:216865`: `AutoDriveEnabled`,
  `AutoDriveAvailable`, `FreeRoamEnabled`, `DestinationRemainingLength`).
  Tracking a custom map pin needs the map menu (protected native,
  `:358177`). `[2.5-untested]`
- AI drive commands (`AIVehicleDriveToPointAutonomousCommand`, `:189546`)
  exist; vanilla uses them with V aboard only as a **passenger** (Delamain,
  `:217147`). No throttle or steering natives on `vehicleBaseObject`
  (`ForceBrakesFor`, `TurnEngineOn`, `SetVehicleRemoteControlled` only).

### 14.1.9 Saving

- `SaveLocksManager.RequestSaveLockAdd(game, reason)` / `RequestSaveLockRemove`
  (`:195179`, `:195188`): a scriptable system whose `IsSavingLocked()` is
  true while its list is not empty (not persistent: a loaded save forgets
  it). Vanilla reasons: `Autodrive`, `PersonalLink`, `ReplayTutorial`.
- Any `ScriptableSystem` can lock saving by overriding `IsSavingLocked()`
  (`:48411`; `PreventionSystem`, `BraindanceSystem` do). `TandemSystem`
  already does; it now also answers true once the pilot has been on, from
  a flag in the plugin (`TandemPilotNative.SavesLocked`), which survives
  loaded saves until the game exits.
- The pause menu's save and the quick save check the lock (`:340425`,
  `:340441`). Autosaves: `gameAutoSaveSystem.RequestCheckpoint` /
  `InvalidateAutoSaveRequests` (`:203129`); that the **native** autosave
  honours script locks is not visible in script. `[2.5-untested]`: the
  pilot therefore also compares the save folder before and after each run.

### 14.1.10 Where the state's fields come from `[2.5-untested]`

All read on the game thread in `TandemPilot.WriteState`: position and
heading from the player (or the vehicle of `VehicleComponent.GetVehicle`,
its speed from `GetLinearVelocity`); the view from the camera system;
`crouch`, `sprint`, `air` from the state machine blackboard's `Locomotion`
(`gamePSMLocomotionStates`: Crouch 1, Sprint 2, Jump 4, DodgeAir 7,
SlideFall 10) and `Fall`; `combat` from `Combat`, `ads` from `UpperBody`
(`Aim`), `tier` from `HighLevel`, `takedown`, `carry`, `dead` (`Vitals`),
`vstate` (`Vehicle`); `menu` from `UI_System.IsInMenu`; `call` from the
phone system's last call phase; `choices`, `sel`, `prompt` from the
`UIInteractions` blackboard; `auto` from `UI_AutodriveData`; `car` from
`VehicleSummonData.SummonedVehicleEntityID`; `savelock` from
`GameInstance.IsSavingLocked` (`final.reds:14340`); NPC counts from
`GetNPCsAroundObject(30)` and `GameObject.GetAttitudeTowards`, not within
2 s of a loading screen. The checklist's `show` steps are where each is
first compared with what is on screen.

---

## 14.2 Virtual input

### 14.2.1 The game machine `[local]`

- `hardware.uinput.enable`, plus the udev rule
  `KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="uinput", TAG+="uaccess"`;
  the user is in `input`, `uinput` and `ydotool`
  (`nixbook`: `nixosModules/core.nix`, `nixosModules/tools.nix`,
  `lib/userConfig.nix`). No NixOS change should be needed for a pad.
- Sway with `WLR_BACKENDS=headless,libinput`, XWayland on,
  `input "type:keyboard" xkb_layout fr`. Steam starts in Big Picture, with
  `hardware.steam-hardware`, proton-ge and proton-cachyos. Sunshine runs as
  a user service and makes its own uinput pads for remote players.

### 14.2.2 Why a gamepad

- A uinput **keyboard or mouse** goes libinput → Sway → the focused surface
  → XWayland → Wine: it types into whatever has the focus, and the keyboard
  layout applies. That explains the earlier observation: the game binds by
  letter, and with the French layout evdev code 17 (the key labelled W on a
  QWERTY board) types `z`, the radio key; code 31 is `s` on both layouts.
  `[inference]`, consistent with what was seen.
- A uinput **gamepad** is not handled by libinput: the compositor never
  sees it, only programs that open the evdev or joystick node do (Steam,
  the game). No layout, analog sticks. `[documented]`
- Under Proton the game sees pads through SDL in `winebus`; inside the
  pressure-vessel container SDL watches `/dev/input` with inotify, so a
  device created after the game started is found. `[documented]` That the
  game takes a pad that appears mid-session is `[inference]` (XInput is
  polled; the game switches glyphs to the last device used, `[reported]`).
- **Steam Input** is the open point. With it on (Big Picture's default),
  Steam grabs the pad, hides it from the game
  (`SDL_GAMECONTROLLER_IGNORE_DEVICES`, set when the game launches) and
  feeds the game its own virtual pad instead; it follows the **focused**
  Steam window, so an unfocused game means the pad drives Big Picture or
  the desktop configuration. `[reported]` Hence the pilot requires the
  game's window to have the focus, never declares a way to press Guide,
  Start or Back, and the checklist tries the pad both created before the
  game starts and after.
- Mouse look would need pointer lock and raw relative motion through
  XWayland, with a known misreading when an absolute pointer (Sunshine's)
  was the last one used (Proton issue 10189, `[reported]`). The right
  stick has a response curve and aim assist, so it is closed on the view
  vector, and exact aiming is the script's look-at.

### 14.2.3 The device `[offline-verified]` for its creation

`tools/pilot/internal/pad/uinput_linux.go` makes what the kernel's xpad
driver makes for a wired Xbox 360 pad, which is also the shape of
Sunshine's own virtual pad (inputtino), already working on that machine:

- name `Microsoft X-Box 360 pad`, bus USB (3), vendor `045e`, product
  `028e`, version `0110`;
- keys `BTN_A B X Y TL TR SELECT START MODE THUMBL THUMBR` (0x130, 0x131,
  0x133, 0x134, 0x136, 0x137, 0x13a–0x13e): all eleven declared, because
  SDL numbers buttons by which key codes exist;
- axes `ABS_X/Y/RX/RY` −32768..32767 (fuzz 16, flat 128), `ABS_Z/RZ`
  0..255 (the triggers), `ABS_HAT0X/Y` −1..1 (the d-pad); stick up is the
  low end of the axis; no force feedback (it would have to be served).

Created once on the development machine (kernel 7.3,
`TANDEM_PILOT_UINPUT=1 go test ./internal/pad/`): the kernel listed it with
`Bus=0003 Vendor=045e Product=… Version=0110`, handlers `eventN jsN`,
`KEY=7cdb000000000000 0 0 0 0` and `ABS=3003f`, which is xpad's own
signature, and removed it on close. The events it emits were not read back
there (Steam runs on that desktop): `TANDEM_PILOT_UINPUT=events` does that
on a machine nobody is using.

### 14.2.4 Default bindings the routines assume `[reported]`

Left stick move, right stick look, A jump, B crouch (two quick presses
with a direction: dodge), X interact / reload, Y draw and holster, L3
sprint, R3 quick melee, RT fire / accelerate, LT aim / brake, d-pad up the
consumable, d-pad up/down and X in a dialogue, LB+RB the operating system
(from memory, unconfirmed), hold L3 for AutoDrive. The player's own
bindings may differ: the routines check every effect on the state rather
than trust a button.

### 14.2.5 Two instances `[inference]`

Each instance gets its own pad with its own product id (`--product`), and
is launched with `SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x045e/0xPID`
and Steam Input off, so each reads only its pad. Pads need no focus under
plain Wine XInput, which makes them the only input that can drive two
windows at once; whether the game itself ignores a pad while unfocused is
to be measured (the focus check is `--focus off` then).
