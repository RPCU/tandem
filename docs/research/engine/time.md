# Time dilation

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.42 Time dilation in a shared world (2026-11-18, scripts and RTTI only)

Read from the 2.50 RTTI natives (`docs/research/data/rtti-natives-2.50.tsv`)
and the script dump; used by `scripts/tandem/Time.reds`, **not yet run in
game**. Research behind it: `coop-time.md` §1-2.

- Exactly three scopes, no render-only one: global
  (`gameTimeSystem.SetTimeDilation` / `UnsetTimeDilation` /
  `GetActiveTimeDilation` / `IsTimeDilationActive`), the local player's
  own (`SetTimeDilationOnLocalPlayerZero`, the exemption flag
  `SetIgnoreTimeDilationOnLocalPlayerZero`) and per entity
  (`gameTimeDilatable.SetIndividualTimeDilation(reason, dilation,
duration, easeIn, easeOut, ignoreGlobalDilation, useRealTime)`,
  `UnsetIndividualTimeDilation`, `HasIndividualTimeDilation`,
  `GetTimeDilationValue`, `IsIgnoringGlobalTimeDilation`; final.reds
  14724-14744). Besides them only attacks
  (`gameAttack_GameEffect.OverrideTimeDilationDriver(timeDilatable)`,
  effector drivers `Source`/`Target`, final.reds 299725-299729) follow an
  entity's time. So a host cannot show its own NPCs slowed while
  simulating them at normal speed. [2.5-untested]
- A per-entity dilation is a single slot (`UnsetIndividualTimeDilation`
  takes no reason): setting ours replaces an NPC's own (hit reactions
  `VeryStrongHit`, `MeleeLegKnockdown`, NPC Sandevistan). Vanilla slows a
  single NPC with it: `SetTemporaryIndividualTimeDilation`, 0.05 for 2.5 s
  (final.reds 525198-525204); NPC Sandevistans speed one up (factors above
  1, `AISubActionApplyTimeDilation`, final.reds 427367-427503).
  [2.5-untested]
- `GetActiveTimeDilation()` with no reason is the current global factor
  (`WeaponTransition.CalcCycleTimeDeltaFactor` reads it that way,
  final.reds 286960). [2.5-untested]
- Sandevistan lasts only while `IsTimeDilationActive(n"sandevistan")`
  (`SandevistanDecisions.ToTimeDilationReady`, final.reds 523705), and the
  scanner's state re-sets its dilation whenever none is active
  (`TimeDilationFocusModeEvents.OnUpdate`, final.reds 529164): a dilation
  that must stop slowing the world is re-set at normal speed under the
  same reason, never unset. Whether a factor of exactly 1.0 still reads as
  active is checked once at run time (Time.reds falls back to 0.999 and
  logs it). [2.5-untested]
- While any global dilation is active, NPC guns fire projectiles instead
  of hitscan (`AIWeapon.SetAttackBasedOnTimeDilation`, final.reds 173373):
  a dilation kept active at normal speed keeps that. [2.5-untested]
- The implant's world factor is a stat on the player:
  `gamedataStatType.TimeDilationSandevistanTimeScale` (read by
  `SandevistanEvents.OnEnter`, final.reds 524119-524126),
  `TimeDilationKerenzikovTimeScale` and `TimeDilationKerenzikovPlayerTimeScale`
  (`KerenzikovEvents`, final.reds 527840, 527894). [2.5-untested]
- Effect names on the Sandevistan boss's entity: `sandevistan_low_00..03`,
  `sandevistan_medium_00..03`, `sandevistan_high_00..03`
  (`DisableAllSandyEdgerunnerFxs`, final.reds 296270, 297181).
  Whether a puppet's entity has any of them is unknown; the NPC
  Sandevistan's own effect is in TweakDB/entity data not read. Try names
  with `tools/game.sh cmd time fx NAME`. [unverified]
- `GameInstance.GetSimTime` is assumed dilated and `GetEngineTime` real
  (final.reds 14148-14150); `time test` logs the ratio of the two.
  [2.5-untested]
- **First run, 2026-11-18: the game broke twice** while Time.reds ran from
  the plugin's per-frame call (RED4ext running-state update,
  `OnRunningUpdate`), outside the script tick: a crash loading a save as
  host, in the script VM about three calls deep under `TandemTime.Frame`,
  before "playing"; and a freeze right after "playing", when the work
  began (no backtrace). Suspects, most likely first, all from reading the
  code (none confirmed):
  1. `GetActiveTimeDilation()` with no dilation active answers something
     below 0.99 (0, say): `exempt` was then true on every frame, and the
     first frame set a per-entity dilation (unset, then set) on every
     remote puppet and proxy, entities moved by tandem's own movement
     controller (a hand-built engine class). Fixed: exemption only while
     `IsTimeDilationActive()` and 0 < factor < 0.99, and never on those
     entities.
  2. Engine calls made outside the script tick, and behind the loading
     screen: time system queries, `FindEntityByID`, per-entity dilation and
     effect events, while the world is being built. Fixed: the work runs
     from `TandemSystem.Tick` once the world plays; the plugin's per-frame
     call is back to `TandemPause.Frame` alone.
  3. Something else that started at "playing" on the same frame (stealth,
     takedowns): not Time.reds.

  No deadlock found: the time natives each take `Runtime::m_lock` once and
  release it before returning, never call the engine or the network while
  holding it, and nothing holds it while waiting on the game thread. No
  loop in Time.reds or `TimeBubbles::Update` can run forever.
  [2.5-untested]

### 13.40 The scanner's slow time (2026-11-19, scripts and RTTI only)

Read from the 2.50 script dump and RTTI natives for the scanner's time
bubble (`scripts/tandem/Time.reds`, `../architecture/time.md`); **not yet run
in game**. The earlier reading is `coop-time.md` §2.3.

- **One profile, no stat.** `TimeDilationFocusModeEvents.OnEnter`
  (final.reds 529099-529156) takes its factor, the player's factor, the
  curves and `applyTimeDilationToPlayer` from the TweakDB flats
  `timeSystem.focusModeTimeDilation.*`, reason `focusMode` (the
  `vehicleControlFocusModeTimeDilation` branch is overwritten right after,
  as decompiled). No stat feeds it: the 2.50 `gamedataStatType` time
  stats are the Sandevistan's, Kerenzikov's, `TimeDilationGeneric*`,
  `TimeDilationOnDodges*`, `TimeDilationOnHealthDrop*` and
  `TimeDilationWhenEnteringCombat*` (final.reds 2967-2986); the scanner's
  own stats are `ScanTimeReduction` (how long a scan takes to complete,
  `UI_PlayerScanningTime`, final.reds 198451), `ScanDepth`,
  `KiroshiPierceScanAngle`, `KiroshiPierceScanRange` and
  `KiroshiMaxZoomLevel`, none of them a speed. No script changes the
  profile, so **no perk, implant or Kiroshi optics changes how much the
  scanner slows time**: every build gets the same factor. Anything that
  slows time on a trigger does it through `SetTimeDilationEffector` under
  its own reason (§13.42), already a bubble. [2.5-untested]
- **The same in and out of combat.** `TimeDilationFocusModeDecisions`
  (final.reds 528796-528844) defines `IsPlayerInCombatState` and
  `IsPlayerLookingAtQuickHackTarget` and calls neither. It enters in vision
  mode, outside a braindance, when `enableTimeDilation` is set and
  `ShouldActivate` holds: never in vehicle state 4
  (`gamePSMVehicle.Transition`) or a takedown; with
  `enableTimeDilationOnlyEnemyOrSensorVisible`, only while
  `TargetingSystem.IsAnyEnemyOrSensorVisible` (a native, RTTI
  `0x140d66acc`) is true; else while an active vehicle is in view
  (`IsAnyActiveVehicleVisible`, `0x142ab6024`). It leaves when either
  fails (`ToTimeDilationReady`). So the slow motion comes and goes as
  enemies or sensors enter and leave the view, in combat or not. What the
  native counts as an enemy (hostile only, or any NPC) is not read.
  [2.5-untested]
- **How it slows.** `OnUpdate` (final.reds 529159-529188) sets the global
  dilation for 999 s, and only while no other dilation is active: one
  already set (ours held at normal speed, another ability) keeps it from
  setting its own. `SetTimeDilationGlobal` (final.reds 522191-522212)
  exempts the player for `focusMode` unless `applyTimeDilationToPlayer`
  (`SetIgnoreTimeDilationOnLocalPlayerZero(true)`), else the player gets
  `playerTimeDilation`. `OnExit` and `OnForcedExit` unset it with the
  profile's ease-out; `CleanUpTimeDilationEvent` leaves it while
  `Vision == 1` (final.reds 43307). The Sandevistan may start during it
  (its enter condition excepts `focusMode`, final.reds 523681).
  [2.5-untested]
- **The quickhack panel adds none.**
  `QuickhacksListGameController.SetVisibility` queues PSM bools
  `quickHackScreen` (final.reds 207262, 207281) and `HUDManager`
  `quickHackChangeTarget` (49046, 49098), through
  `PSMPostponedParameterBool` (207318); no script reads the first. The slow
  motion while picking a quickhack is the same `focusMode` one.
  [2.5-untested]
- **Reading its factor.** `GetActiveTimeDilation(reason, ignoreCurves)`
  (final.reds 53882): with `ignoreCurves` the factor asked for, not the
  eased one. Time.reds sends that as the bubble's speed. The profile's
  values were not read: the first scan of a session logs them
  (`time: our abilities 8 (world speed X)`). [unverified]
