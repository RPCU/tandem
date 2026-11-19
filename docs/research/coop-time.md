# Bent, stopped and skipped time — what single-player does, per effect

Research for the "Slowed and stopped time" open problem in
[`../architecture/time.md`](../architecture/time.md). It lists every place the game
slows, stops or skips time, how to see it from a plugin or redscript, how to
suppress it, and a proposed co-op rule for each.

## Sources and how far to trust them

- **Scripts**: the decompiled vanilla scripts,
  `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`, cited as
  `final.reds:LINE`.
- **RTTI**: the live dump of game 2.50 (`tools/re/rtti.py`,
  `.cache/re/rtti.json`, CName pool `.cache/re/names.txt`), cited as
  `RTTI 2.50`. Native handler addresses are from that dump.
- **Earlier projects**: facts already in
  [`engine-knowledge.md`](engine-knowledge.md) and
  [`nco-commits.md`](nco-commits.md) are referenced, not repeated.

Tags: everything here is `[unverified]` in the sense of ARCHITECTURE.md —
read from scripts and RTTI, **never tested in a co-op session, and nothing
was run in game for this file**. Three more limits:

1. **The script dump is older than 2.50.** RTTI 2.50 has things the dump
   lacks: classes `TimeDilationPhotomodeDecisions` / `TimeDilationPhotomodeEvents`,
   `gamePSMTimeDilation.Photomode = 2`, `TimeDilationHelper::GetPhotoModeKey`,
   `OnExitToTimeDilationPhotomode` on the Sandevistan, Kerenzikov and focus
   mode states, and a fifth parameter `noDelayedCleanUp: Bool` on
   `TimeDilationEventsTransitions.UnsetTimeDilation`. The dump's README only
   says "v2.0+". Line numbers are the dump's; names and signatures were
   checked against RTTI 2.50 where stated. Bodies of the 2.50-only functions
   are unknown.
2. **TweakDB was not read.** Most values live in TweakDB (`timeSystem.*`
   flats, stats, effector records). The paths are given; a value is given
   only when the script has it as a literal. Fallback defaults in
   `TweakDBInterface.GetFloat(path, default)` calls are the script's
   fallback, not the shipped value.
3. **Decompiled control flow is sometimes wrong** (an `else if` where the
   original clearly had a nested `if`, for example `final.reds:45760-45772`
   and `529120-529149`). Call sites and arguments are reliable; the exact
   branch conditions around them are less so.

## 1. The machinery

### 1.1 Three scopes of time dilation

All on `gameTimeSystem` (`GameInstance.GetTimeSystem`), script declarations at
`final.reds:53842-53921`, natives confirmed in RTTI 2.50:

<!-- prettier-ignore -->
| Scope | Native (RTTI 2.50 signature) | Handler |
| --- | --- | --- |
| global (whole world) | `SetTimeDilation(reason: CName, dilation: Float, duration: Float, easeInCurve: CName, easeOutCurve: CName, listener: handle:tickScriptTimeDilationListener)` | `0x1407d62b0` |
| | `UnsetTimeDilation(reason: CName, easeOutCurve: CName)` | `0x1407d64f4` |
| local player exempt from global | `SetIgnoreTimeDilationOnLocalPlayerZero(ignore: Bool)` | `0x14131b2f8` |
| | `RestorePreviousIgnoreTimeDilationOnLocalPlayerZero()` | `0x142aa9c50` |
| local player's own factor | `SetTimeDilationOnLocalPlayerZero(reason, dilation, duration, easeInCurve, easeOutCurve)` | `0x1408a0c90` |
| | `UnsetTimeDilationOnLocalPlayerZero(reason, easeOutCurve)` | `0x1408a0aa0` |
| one entity | `gameTimeDilatable.SetIndividualTimeDilation(reason, dilation, duration, easeInCurve, easeOutCurve, ignoreGlobalDilation: Bool, useRealTime: Bool)` | `0x142a14b90` |
| | `gameTimeDilatable.UnsetIndividualTimeDilation(easeOutCurve)` | `0x1414e1a14` |

- `dilation` is a multiplier on the time step: `1.0` normal, `0.0` frozen,
  above 1 faster (NPCs get `1.6`, `final.reds:457953`; the fast-forward key
  gives the world `10.0`, `final.reds:40303`).
- `duration` is optional; scripts pass `999.0` for "until unset". An omitted
  duration is shown by the decompiler as a missing argument
  (`SetTimeDilation(n"WorldMap", 0.0)`, `final.reds:358237`).
- Dilations are keyed by `reason`; several can be active. Scripts query
  "any" with `IsTimeDilationActive()` (no reason) to refuse stacking.
  How the engine combines several active reasons (min, product, last) is
  **not known**.
- `gameTimeDilatable` is the base of `gamePuppetBase` and `ItemObject`
  (`final.reds:14724,14746,52793`): puppets and items can each carry an
  individual factor; vehicles and devices are not `TimeDilatable` (inferred
  from the class tree, not checked for vehicles).
- "LocalPlayerZero" is the engine's local player index 0: the API has no
  notion of a second player. A co-op puppet of another V is an NPC-rig
  `gamePuppet` here, so the per-entity API applies to it.

### 1.2 `TimeDilationHelper` (script, `final.reds:240278-240515`)

Static wrappers used by UI code. Every one returns `false` and does nothing
when the global `IsMultiplayer()` is true (`240310,240361,240382,240393,
240404,240416,240425,240440,240449`).

- `SetTimeDilationWithProfile(requester, profileName: script_ref<String>,
enable, allowMultipleTimeDilationSimultaneously) -> Bool`
  (`240292`): reads `timeSystem.<profile>.{reason, timeDilation,
playerTimeDilation, duration, easeInCurve, easeOutCurve}` from TweakDB
  (`240279-240290`, `240483-240490`) and sets **both** the global factor and
  the local player's factor; with `enable = false` it unsets both.
- `SetTimeDilation(...)`, `SetTimeDilationOnPlayer(...)`,
  `UnSetTimeDilation(...)`, `UnSetTimeDilationOnPlayer(...)`,
  `SetIndividualTimeDilation(target, ...)`,
  `UnsetIndividualTimeDilation(target, ...)`.
- `CanUseTimeDilation(player)` (`240456`): in a vehicle only for PSM
  `Vehicle` states 1, 4, 6, with perk `Cool_Left_Milestone_1`, and not in
  `BaseStatusEffect.PlayerInFinisherWorkspot`.
- Reason keys: `GetSandevistanKey` → `n"sandevistan"`,
  `GetSandevistanVersusSandevistanKey` → `n"sandevistanVersusSandevistan"`,
  `GetKerenzikovKey` → `n"kereznikov"` (sic, the misspelling is the real
  CName), `GetFocusModeKey` → `n"focusMode"`, `GetFocusedStateKey` →
  `n"focusedStatePerkDilation"`, `GetTimeDilationKey` → `n"timeDilation"`
  (`240492-240514`). 2.50 adds `GetPhotoModeKey` (value unknown).

Known profiles (string literals in scripts): `"radialMenu"`, `"hubMenu"`,
`"vehicleRadioMenu"`; TweakDB-only groups read flat by flat:
`focusModeTimeDilation`, `vehicleControlFocusModeTimeDilation`, `meleeHit`,
`meleeHitStrong`, `nanoWireBreach`, `settings`.

Many callers **bypass the helper** and call `TimeSystem` directly (PSM
states, perks, world map, vendor, tutorial, time skip, minigame): wrapping
the helper alone does not cover them.

### 1.3 The player state machine `TimeDilation`

A PSM state machine named `n"TimeDilation"`
(`stateContext.GetStateMachineCurrentState(n"TimeDilation")`,
`final.reds:542964`) with states seen in scripts and in the 2.50 CName pool:
`timeDilationReady`, `sandevistan`, `kerenzikov`, `timeDilationFocusMode`,
and on 2.50 `timeDilationPhotomode`. Its transitions extend
`TimeDilationTransitions` (`282182`) and `TimeDilationEventsTransitions`
(`522186`), which add:

- `SetTimeDilationGlobal(...)` (`522191`): sets the exemption flag first —
  `SetIgnoreTimeDilationOnLocalPlayerZero(true)` for `n"sandevistan"`, and
  for `n"focusMode"` when
  `timeSystem.focusModeTimeDilation.applyTimeDilationToPlayer` is false;
  `false` otherwise — then `TimeSystem.SetTimeDilation`.
- `SetTimeDilationOnLocalPlayer(...)` (`522214`).
- `UnsetTimeDilation(stateContext, scriptInterface, reason, easeOutCurve)`
  (`522236`): unsets global and local-player factors, then queues a
  `CleanUpTimeDilationEvent { reason }` on the player **1.0 s later in real
  time** (`DelayEvent(..., 1.0, false)`).
- `PlayerPuppet.OnCleanUpTimeDilationEvent` (`43305`): unless the reason is
  `n"focusMode"` and PSM `Vision == 1`, it unsets the reason again with no
  curve, resets the local player factor and
  `SetIgnoreTimeDilationOnLocalPlayerZero(false)`. This is the game's
  safety net; a mod that suppresses a dilation must let this run or
  reproduce it, or the exemption flag stays set.
- `SetCameraTimeDilationCurve(curveName)` (native on
  `StateGameScriptInterface`, `247392`): camera curve `n"Sandevistan"` or
  `n"Kerenzikov"`.

### 1.4 Ease curves

CNames passed as `easeInCurve` / `easeOutCurve` (scripts and 2.50 name
pool): `Linear`, `Log`, `SandevistanEaseIn/Out`, `KerenzikovEaseIn/Out`,
`KereznikovSlideEaseIn/Out`, `KereznikovDodgeEaseIn/Out`,
`MeleeHitEaseIn/Out`, `DiveEaseIn/Out`, `ScanEaseIn`, `FocusModeEaseIn`,
`SlideEaseIn`, `RadialMenuEaseIn`, `NPCSandevistanEaseIn`,
`NpcSandevistanEaseOut`, `ProjectileEaseOut`, `ReflexRecorderEaseIn/Out`.
Their shapes and lengths are engine data, not read.

### 1.5 Observing time dilation

<!-- prettier-ignore -->
| What | How |
| --- | --- |
| is a reason (or any) active | `TimeSystem.IsTimeDilationActive(opt reason) -> Bool` (`0x140e35e58`) |
| current factor | `TimeSystem.GetActiveTimeDilation(opt reason, opt ignoreCurves) -> Float` (`0x141190c94`) |
| one entity | `HasIndividualTimeDilation(opt reason)`, `GetTimeDilationValue()`, `IsIgnoringGlobalTimeDilation()`, `IsIgnoringTimeDilation()` on `gameTimeDilatable` |
| end of a dilation | a `TimeDilationListener` (script class over `tickScriptTimeDilationListener`) passed to `SetTimeDilation`; callback `OnFinished(reason: CName)` (`final.reds:497550-497559`, `535846-535876`) |
| Sandevistan on/off | PSM blackboard `PlayerStateMachine.TimeDilation` (Int, `gamePSMTimeDilation`: 0 Default, 1 Sandevistan, 2.50: 2 Photomode); prereq `TimeDilationPSMPrereq` (`311836`) |
| PSM state | `GetStateMachineCurrentState(n"TimeDilation")` |
| player events | `CleanUpTimeDilationEvent` (player), `ResetTimeDilation { easeOut, global }` (NPC, `204655`, handled `128605`), `QuickHackTimeDilationOverride` (`208468`) |
| status effects | `BaseStatusEffect.KerenzikovPlayerBuff`, type `gamedataStatusEffectType.Sandevistan`, tag `FocusedCoolPerkSE`, tag `DeadeyeQuickReloadSE`, `BaseStatusEffect.SecondChancePerkTimeDilation`, tag `Berserk` |
| NPC side | AI action blackboard `AIAction.ownerTimeDilation`, `ownerGlobalTimeDilation` (`180109`) |

There is no script or RTTI event fired on every `SetTimeDilation`
(`gameTimeDilationEvent` exists in RTTI 2.50, size 0x68, with no reflected
fields; who sends it is unknown). From the plugin, the reliable observer is
a per-frame poll of `IsTimeDilationActive(reason)` over the reason list in
§7, or a hook on the native handler (§1.6).

`NCO` found that a caller passing a listener hangs if the call is dropped:
pass it through at `1.0` instead (`nco-commits.md:141`).

### 1.6 Suppressing: the choke points

In order of coverage:

1. **Native hook on `gameTimeSystem.SetTimeDilation` (`0x1407d62b0`) and
   `SetTimeDilationOnLocalPlayerZero` (`0x1408a0c90`)**, with a policy per
   `reason`. These are the script-native handlers: they see every script
   caller (PSM, perks, UI, effectors). Whether native callers (quest node
   `questTimeDilation_NodeType`, scene fast-forward
   `n"SceneSystemFastForward"`, photo mode) go through the same function or
   an inner one is **not known**. Needs an address-library hash for 2.50
   (the address above is the raw 2.50 handler) and the listener caveat
   above.
2. **Redscript `@wrapMethod`** on `TimeDilationHelper.SetTimeDilation`,
   `SetTimeDilationOnPlayer`, `SetTimeDilationWithProfile` (what NCO did,
   `engine-knowledge.md` §8), plus `PlayerPuppet.SetSlowMo`, plus each
   direct caller listed per effect below. More code, no raw address.
3. **`IsMultiplayer()`**: the vanilla scripts already disable the helper and
   Sandevistan when it is true, but it gates 17 other script files (damage,
   equipment, grenades…): do not flip it (`engine-knowledge.md` §8).

## 2. Time dilation, effect by effect

Each entry: how the game does it → detect → suppress → proposed rule.
"Owner" is the player whose V triggers the effect. The rule every entry
follows: **the host's world never slows or stops for one player; a V's own
time effects are local to that player and shown to the others as what they
are.**

### 2.1 Sandevistan

- **How.** `UseSandevistanAction.StartAction` queues PSM bool
  `requestSandevistanActivation` (`final.reds:223943`);
  `SandevistanDecisions.EnterCondition` (`523675`) refuses when
  `IsMultiplayer()`, in takedown, when PSM `HighLevel != 1`, or when another
  dilation is active that is not focus mode, the focused perk, Kerenzikov or
  Sandevistan itself. `SandevistanEvents.OnEnter` (`524119`):
  - reason `n"sandevistan"`, factor = stat
    `gamedataStatType.TimeDilationSandevistanTimeScale` (per cyberware
    item), duration `999.0`, no ease-in, ease-out `n"SandevistanEaseOut"`;
  - **global, local player exempt** (`SetIgnoreTimeDilationOnLocalPlayerZero(true)`
    through `SetTimeDilationGlobal`);
  - removes an active Kerenzikov first; PSM blackboard `TimeDilation = 1`;
    camera curve `n"Sandevistan"`; pays
    `TimeDilationSandevistanEnterCost` from stat pool `SandevistanCharge`.
  - `OnUpdate` (`524179`) re-applies when the stat changes.
- **Ends.** `ToTimeDilationReady` (`523700`): deactivation requested
  (`requestSandevistanDeactivation`, queued by `DisableSandevistanAction`
  `223964`, entering a workspot `549012`, takedown `534981`,
  `DeactivateTimeDilationCW` `282610`), reason no longer active, or
  `SandevistanCharge` at minimum. `OnExit` (`524165`): unset with
  `SandevistanEaseOut`, blackboard back to 0, removes status effects of type
  `Sandevistan`, then the delayed cleanup.
- **Side effects that read it.** Weapon cycle time is divided by the factor
  (`WeaponTransition.CalcCycleTimeDeltaFactor`, `286955`); ranged attacks
  switch to the `PlayerTimeDilated` / `SecondaryPlayerTimeDilated` attack
  records while any dilation is active (`286770`), and NPC weapons to
  projectile attacks `NPCTimeDilated` (`AIWeapon.SetAttackBasedOnTimeDilation`,
  `173366`); perk `Reflexes_Right_Perk_2_3` makes bullet deflection free
  during any dilation (`87242`); achievement counter (`51487-51509`).
- **NPC Sandevistan** is a per-entity dilation, see §2.14.
- **Detect.** `IsTimeDilationActive(n"sandevistan")`; PSM blackboard
  `TimeDilation == 1`; status effect type `Sandevistan`.
- **Suppress.** Drop or rewrite the `n"sandevistan"` global call (§1.6).
  Blocking the state through `IsMultiplayer()` is not an option.
- **Rule (proposed).** Owner-local. Nobody's world slows. Stand-in: the
  owner's V is sped up by `1 / TimeScale` for the Sandevistan's duration
  (candidate: `SetTimeDilationOnLocalPlayerZero(n"sandevistan", 1/scale)`
  with no global call — whether a player factor above 1 works is an open
  question, §8). The owner's charge, cooldown, weapon-cycle factor and
  time-dilated attack records stay as in single-player. The others see that
  V's puppet move fast, with the Sandevistan trail effect: the state is a
  field of that player's replicated state (authority row "a V: stance…").
- **Decided instead (2026-11-18)**: a time bubble. The NPCs around that V
  are slowed by its factor for everyone (per-entity dilation on the host),
  the owner keeps its slow motion on its own screen, no V is sped up.
  Rules per mechanism: ../architecture/time.md (slowed and stopped time) and
  `scripts/tandem/Time.reds`.

### 2.2 Kerenzikov

- **How.** `KerenzikovDecisions.EnterCondition` (`525475`): requires
  `HasKerenzikov`, 0.1 s since the last deactivation, no other dilation
  except the focused perk; then either `requestKerenzikovActivation`
  (`284962`), or dodge / slide / `coolExitJump` locomotion within
  `AimInTime + m_activationGracePeriod` combined with aiming (PSM
  `UpperBody == 6`) with a ranged or throwable weapon, charging left-hand
  cyberware, the grenade preview, or — in driver combat above 5 m/s —
  handbrake plus aim (`525529`). `KerenzikovEvents.OnEnter` (`527831`):
  - reason `n"kereznikov"`; world factor = stat
    `TimeDilationKerenzikovTimeScale`; **player factor** = stat
    `TimeDilationKerenzikovPlayerTimeScale`, doubled while sliding
    (`527884`); duration `999.0`; ease-in `n"KereznikovSlideEaseIn"` or
    `n"KereznikovDodgeEaseIn"`, ease-out `n"KerenzikovEaseOut"`;
  - **global, player not exempt but given its own (faster) factor**;
  - applies `BaseStatusEffect.KerenzikovPlayerBuff` (its expiry ends the
    state, `525573`); sets PSM blackboard `Locomotion = 3`; camera curve
    `n"Kerenzikov"`; ends an active `focusedStatePerkDilation`.
- **Ends.** `ClearKerenzikov` (`527816`) on exit or forced exit: unset with
  `KerenzikovEaseOut`, removes the buff; requested by
  `requestKerenzikovDeactivation[WithEaseOut]` (`554557,554560,554834`),
  leaving aim, losing the stat.
- **Related.** Perk `Reflexes_Inbetween_Left_3` cuts gravity to 5 % during
  an air dash while the state is `kerenzikov` (`543278`).
- **Detect.** `IsTimeDilationActive(n"kereznikov")`;
  `GetStateMachineCurrentState(n"TimeDilation") == n"kerenzikov"`; the buff.
- **Suppress.** Rewrite the `n"kereznikov"` global call; keep the buff and
  the state so the PSM stays consistent.
- **Rule (proposed).** Owner-local, same as Sandevistan: no world slow-down;
  the owner keeps the buff's other benefits. Stand-in to decide by play
  test: a short speed-up of the owner's V (player factor `1/TimeScale ×
PlayerTimeScale`), or nothing but the buff. Others see a normal dodge.

### 2.3 Scanner / focus mode, and the quickhack panel

- **How.** `TimeDilationFocusModeDecisions.EnterCondition` (`528798`): in
  vision mode, not in a braindance, flag
  `timeSystem.focusModeTimeDilation.enableTimeDilation`, and
  `ShouldActivate` (`528829`): an enemy or sensor device visible (with flag
  `enableTimeDilationOnlyEnemyOrSensorVisible`) or an active vehicle
  visible; never when PSM `Vehicle == 4` or in takedown.
  `TimeDilationFocusModeEvents` (`529099`):
  - reason `n"focusMode"`; values from
    `timeSystem.focusModeTimeDilation.{timeDilation, playerTimeDilation,
easeInCurve, easeOutCurve, applyTimeDilationToPlayer}` (and
    `timeSystem.vehicleControlFocusModeTimeDilation.*` when standing on a
    remote-controlled vehicle — as decompiled, those values are then
    overwritten by the normal ones, `529150-529156`); duration `999.0`;
  - set in `OnUpdate` (`529159`) only while **no** other dilation is active;
  - **global**; player exempt unless `applyTimeDilationToPlayer`, in which
    case the player gets `playerTimeDilation`.
- **Ends.** Leaving vision mode or `ShouldActivate` false (`528808`);
  `OnExit` unsets with the profile's ease-out. The delayed cleanup is
  skipped while PSM `Vision == 1` (`43307`).
- **Quickhack panel.** `QuickhacksListGameController.SetVisibility` queues
  PSM bool `quickHackScreen` (`207262,207281`); `HUDManager` queues
  `quickHackChangeTarget` (`49046,49098`); `QuickHackTimeDilationOverride`
  sets blackboard `PlayerStateMachine.OverrideQuickHackPanelDilation`
  (`207685`). **No script in the dump reads `quickHackScreen` or that
  blackboard field**; `quickHackChangeTarget` is read only by
  `IsCameraRotated` helpers (`282216-282243`). So the slow-down while
  picking a quickhack is the focus-mode dilation above (inferred); a native
  or 2.50-only reader is possible.
- **Quest control.** `questSetScanningTime_NodeType` exists (RTTI 2.50);
  not studied.
- **Detect.** `IsTimeDilationActive(n"focusMode")`; PSM blackboard
  `Vision == 1`; `UI_Scanner.ScannerLookAt`.
- **Suppress.** Rewrite the `n"focusMode"` global call, or flip
  `timeSystem.focusModeTimeDilation.enableTimeDilation` with a TweakXL
  record (cleanest: the state is then never entered).
- **Rule (proposed).** Owner-local and **no dilation at all** in co-op:
  scanning and quickhack selection happen in real time (the usual co-op
  answer; upload times already cost real seconds). Others see the scanning
  V as scanning (stance/animation in its replicated state).

### 2.4 Cool perks that slow time

- **Focus (aiming).** `AimingStateEvents.OnEnter` (`542782`, call at
  `542858`): with perk `Cool_Left_Milestone_2` at level 2, stamina above
  `NewPerks.Cool_Left_Milestone_2.focusedStaminaThreshold` (fallback 90)
  and a handgun, revolver, sniper or precision rifle, it applies
  `BaseStatusEffect.FocusedCoolPerkSE` (`542818-542838`); if perk
  `Cool_Inbetween_Left_2` is also bought: reason `n"focusedStatePerkDilation"`,
  factor `1.0 - NewPerks.Cool_Inbetween_Left_2.timeDilationStrength`
  (script fallback 0.15 → 0.85), duration `12.0`, curves
  `MeleeHitEaseIn` / `MeleeHitEaseOut`. **Global**, exemption flag left
  untouched (the player slows with the world unless a previous effect left
  the flag set). Ends on aim exit (`543247`, ease-out
  `timeSystem.meleeHitStrong.easeOutCurve`), when the status effect is
  removed (`PlayerPuppet.OnStatusEffectRemoved`, `44505-44511`), or when
  Kerenzikov starts (`527852`). Sound `time_dilation_focused_enter/exit`,
  screen effect `cool_perk_focused_state_fullscreen`.
- **Deadeye quick reload.** `ReloadEvents.OnEnter` (`547540`, call at
  `547583`): with status tag `DeadeyeQuickReloadSE` and a Cool firearm:
  reason `n"coolReloadPerkDilation"`, factor
  `1.0 - NewPerks.Cool_Left_Perk_3_2.timeDilationStrength` (fallback 0.7 →
  0.3), duration `NewPerks.Cool_Left_Perk_3_2.timeDilationDuration`
  (fallback 2.0), same curves. **Global.** Unset in `OnExitCleanup`
  (`547676`).
- **Second chance (dodge out of sight).** `NPCStatesComponent.OnCombat`
  applies `BaseStatusEffect.SecondChancePerkTimeDilation` to the player when
  an NPC is about to enter combat (`168343`), removed on a successful dodge
  (`39693`). The scripts only check its tag (`553681`); any dilation it
  carries is in its TweakDB record (probably a `SetTimeDilationEffector`,
  §2.13) — **not read**.
- **Stats with no script reader**: `TimeDilationOnDodges*`,
  `TimeDilationOnHealthDrop*`, `TimeDilationWhenEnteringCombat*`,
  `TimeDilationGeneric*`, `TimeDilationKerenzikovDuration`,
  `TimeDilationSandevistanDuration/CooldownBase/RechargeDuration`
  (`final.reds:2967-2986`). Used by TweakDB (effectors, stat pools) or dead.
- **Detect.** `IsTimeDilationActive` on the two reasons; the status tags.
- **Suppress.** Rewrite the two global calls; the status effects keep their
  other bonuses.
- **Rule (proposed).** Owner-local, no world slow-down. Stand-in: none, or
  a small owner-only buff (aim sway / reload speed) decided by play test.

### 2.5 Melee hit stop

- **How.** `PlayerPuppet.OnMeleeHitEvent` (`45746`) delays a
  `MeleeHitSlowMoEvent` by `timeSystem.meleeHit[Strong].delay` when
  `timeSystem.meleeHit[Strong].enabled`; `OnMeleeHitSloMo` (`45789`):
  reason `n"meleeHit"`, factor / duration / curves from
  `timeSystem.meleeHit.*` or `timeSystem.meleeHitStrong.*`
  (`timeDilation`, `duration`, `easeInCurve`, `easeOutCurve`; a negative
  duration becomes 0.1). **Global**, self-expiring, never unset by script.
- **Detect.** `IsTimeDilationActive(n"meleeHit")`.
- **Suppress.** Rewrite the call, or set `timeSystem.meleeHit.enabled` and
  `timeSystem.meleeHitStrong.enabled` to false with TweakXL.
- **Rule (proposed).** Dropped in co-op (a hit-stop of a few frames on the
  whole world for one player's punch). Owner-only camera shake/FOV kick can
  replace it later.

### 2.6 Deflect, one-punch and boss finisher slow-motion

- **How.** Reason `n"deflect"`, **global**, self-expiring:
  - `PlayerPuppet.SetSlowMo(amount, duration)` (`39676`), called by
    `OnSetSlowMoForOnePunchAttackEvent` with `Items.StrongArms.slowMoAmount`
    / `slowDuration` (`39668`);
  - `MeleeDeflectAttackEvents.OnUpdate` (`554273`): after `slowMoStart` in
    state, if no dilation is active, PSM static parameters `slowMoAmount`,
    `slowDuration`, `slowMoEaseIn`, `slowMoEaseOut` (script fallbacks 0.1,
    0.1, `Linear`, `Linear`);
  - `MeleeStrongAttackEvents.OnUpdate` (`552903`): the same, only when the
    PSM parameter `bossFinisherAttack` is set.
- **Finishers in general** have no dedicated dilation in the scripts
  (status `BaseStatusEffect.PlayerInFinisherWorkspot` only blocks
  Sandevistan/Kerenzikov, `240471`, and keeps Sandevistan alive, `523713`).
- **Detect.** `IsTimeDilationActive(n"deflect")`.
- **Suppress.** Wrap `PlayerPuppet.SetSlowMo` (NCO did) and the two
  `OnUpdate`s, or the native hook.
- **Rule (proposed).** Dropped in co-op, like §2.5.

### 2.7 Breach protocol and network dive

- **Minigame.** `NetworkInkGameController.StartMinigame` (`481072`):
  `SetTimeDilation(n"minigame", 1.0e-5)`, no duration, **global**; unset in
  `CloseGame` (`481325`). It also sets PSM blackboard `IsInMinigame`
  (`481065`). Whether this script controller is the one behind the shipped
  breach screen (the native `gameuiHackingMinigame*` UI, blackboard
  `HackingMinigame.State`, `416081`) is **not checked**.
- **Dive.** Reason `n"NetworkBreach"` is only ever **unset** in scripts:
  `ScriptedPuppet.OnAccessPointMiniGameStatus` (`23041`, curve
  `timeSystem.nanoWireBreach.easeOutCurve`, fallback `DiveEaseOut`). The
  set is native or data-driven (inferred): `AccessPoint` keeps a
  `BreachViewTimeListener` (`316681`, class at `497550`) whose `OnFinished`
  re-toggles the personal link (`316801`). Menu scenario
  `MenuScenario_NetworkBreach` (`335252`).
- **Detect.** `IsTimeDilationActive(n"minigame")` /
  `(n"NetworkBreach")`; blackboard `IsInMinigame`;
  `AccessPointMiniGameStatus` event.
- **Suppress.** Rewrite both reasons to 1.0 (keep the listener alive:
  `OnDiveFinished` must still fire).
- **Rule (proposed).** Owner-local UI, world keeps running. The breach
  timer is already a real countdown; the owner's V is busy and vulnerable,
  shown to others in the jack-in animation. Which game owns the breached
  network (host) is a separate authority question.

### 2.8 Radial wheels, phone and small popups (profile `radialMenu`)

- **How.** `TimeDilationHelper.SetTimeDilationWithProfile(player,
"radialMenu", true, true)` on open and `(…, false, false)` on close:
  **global + local player**, values from `timeSystem.radialMenu.*`.
  Callers: weapon wheel `RadialWheelController.OnOpenWheelRequest` /
  `Shutdown` (`421184`, `420652`); vehicle/call wheel
  `QuickSlotsManager.OnQuickSlotButtonHoldStartEvent` (`239092`); phone
  dialer `PhoneDialerGameController.Show` (`387527`, close `387554`); phone
  contacts `NewHudPhoneGameController` (`409195`, `409339`); message popup
  `PhoneMessagePopupGameController.SetTimeDilatation` (`348640`, not when
  opened from the phone); `TwintoneOverrideGameController` (`402407`).
  Codeware's `InGamePopup` uses the same profile.
- **Other profiles through the same helper:** `"vehicleRadioMenu"`
  (`VehicleRadioPopupGameController`, `392091`, base
  `BaseModalListPopupGameController.SetTimeDilatation` `277538`);
  `vehicleColorSelectorGameController` (`467785`) and
  `FrameSwitcherMenuGameController` (`330714`) take the profile name from a
  widget property not visible in scripts.
- These also push `UIGameContext.ModalPopup` or `RadialWheel` and the visual
  state `inkModalPopupState`.
- **Detect.** `IsTimeDilationActive(<reason of the profile>)` (the reason
  CName is `timeSystem.radialMenu.reason`, not read);
  `UIGameContext`; blackboard `UI_QuickSlotsData`.
- **Suppress.** Wrap the helper's three setters (all these go through it);
  NCO found the wheel then works without slow-down
  (`nco-commits.md:582`).
- **Rule (proposed).** Menus do not slow a co-op world: no dilation, the
  wheel and phone are used in real time. Local only; nothing is shown to
  others except weapon changes.

### 2.9 Hub menu: inventory, character, crafting, journal, map entry

- **How.** `MenuHubGameController.OnInitialize` (`336840`): broadcasts
  `inkMenuLayer_SetMenuModeEvent(inkMenuMode.HubMenu, inkMenuState.Enabled)`
  (`336868`), `SetTimeDilationWithProfile(player, "hubMenu", true, true)`
  (`336921`) and god mode `Invulnerable` with source `n"HubMenu"`
  (`336922`). `OnUninitialize` (`337194`) reverses all three. Same in
  `RadialMenuHubGameController` (`338809`, `339214`). **It does not call
  `PauseGame`**: the world is slowed or frozen by the profile
  (`timeSystem.hubMenu.timeDilation` / `playerTimeDilation`, not read), and
  the player is made invulnerable because the world may still tick.
  Inventory, cyberware, perks, crafting, journal, codex are sub-menus of
  this hub and add nothing of their own in the scripts read.
- **Detect.** The `SetMenuModeEvent` (mode `HubMenu`); blackboard
  `UI_System.IsInMenu`; god mode source `n"HubMenu"`.
- **Suppress.** Wrap the helper (as §2.8). Keep or drop the invulnerability
  deliberately (see rule).
- **Rule (proposed).** No dilation. The owner's V stands in the world while
  its player is in a menu and **can be hurt**: drop the `HubMenu`
  invulnerability in co-op (or keep it for the first version and list it as
  an exploit). Others see the V idle; a "in menu" marker over the puppet is
  a cheap addition to the player state.

### 2.10 World map

- **How.** `WorldMapMenuGameController.OnInitialize` (`358211`):
  `SetTimeDilation(n"WorldMap", 0.0)` directly on the time system (no
  helper, no duration, no curve) and god mode `Invulnerable` source
  `n"WorldMap"` (`358237-358240`); `OnUninitialize` unsets both
  (`358261`). **Global freeze**; the local player factor is not set, so the
  player's own time follows the exemption flag.
- **Detect.** `IsTimeDilationActive(n"WorldMap")`.
- **Suppress.** Wrap the two callbacks or filter the reason natively.
- **Rule (proposed).** No freeze; map is a local screen. Fast travel from
  the map is §4.3.

### 2.11 Time-skip wheel

- **How.** `TimeskipGameController.OnInitialize` (`383574`):
  `SetTimeDilation(n"TimeSkip", 0.0)` + god mode `Invulnerable` source
  `n"TimeSkip"` (`383597-383600`), undone in `OnUninitialize` (`383605`).
  The skip itself is §4.1.
- **Rule (proposed).** No freeze; see §4.1 for who may skip.

### 2.12 Vendor, ripperdoc, wardrobe, appearance change (reason `VendorStash`)

- **How.** Four controllers share a private `SetTimeDilatation(enable)`:
  `SetTimeDilation(n"VendorStash", 0.0, n"Linear", n"Linear")` **and**
  `SetTimeDilationOnLocalPlayerZero(n"VendorStash", 0.0, n"Linear",
n"Linear")` — world and player both frozen — and the two unsets:
  - `FullscreenVendorGameController` (`404099`; on `403959`, off `404019`);
  - `VendorHubMenuGameController` (ripperdoc, `388041`): enabled **1.0 s
    after opening** through `RipperdocTimeDilationCallback` (`387919-387924`,
    `388436`), disabled in `UninitializeExtraRipperdocData` (`387929`);
  - `WardrobeUIGameController` (`384791`; `384700`, `384726`);
  - `characterCreationBodyMorphMenu` (`375605`; on at `374632` only when
    the edit mode is not `NewGame`, off `374718`).
- **Detect.** `IsTimeDilationActive(n"VendorStash")`.
- **Suppress.** Wrap the four `SetTimeDilatation` methods (same name, four
  classes) or filter the reason natively.
- **Rule (proposed).** No freeze. Vendor stock and money are per player
  (authority table: inventory/money are the player's own); the ripperdoc
  chair animation is a local scene.

### 2.13 Tutorial popups

- **How.** `PopupsManager.ShowTutorial` copies `PopupSettings.pauseGame`
  (native struct, `319898`) into `TutorialPopupData` (`357079`);
  `TutorialPopupGameController.OnInitialize` (`403243`): if not
  `closeAtInput` and `pauseGame`, `PauseGame(true)` (`403336`) =
  `SetTimeDilation(n"UI_TutorialPopup", 0.0)`; undone in `OnUninitialize`.
  Despite the name it is a **global dilation to 0**, not a pause.
- **Detect.** `IsTimeDilationActive(n"UI_TutorialPopup")`; blackboard
  `UIGameData.Popup_IsShown` (`341376-341380`).
- **Suppress.** Wrap `TutorialPopupGameController.PauseGame`, or force
  `pauseGame = false` in `ShowTutorial`.
- **Rule (proposed).** Never freezes. Tutorials become non-blocking
  (`closeAtInput`-style) in co-op.

### 2.14 Scene fast-forward

- **How.** Two mechanisms:
  - native scene system: `SceneSystemInterface.FastForwardingActivate(mode:
scnFastForwardMode)` / `FastForwardingDeactivate()` (`193100`); scripts
    only observe the reason `n"SceneSystemFastForward"`
    (`FastForwardAvailableEvents.OnUpdate`, `520893-520916`) and apply
    `GameplayRestriction.FastForward` (`521819`);
  - script: while a status effect tagged `CustomFastForward` is on the
    player (`44213`, `44241`), holding `SceneFastForward` calls
    `SetTimeDilation(n"customFFTimeDilation", 10.0)` — the **world runs ten
    times faster** — and release unsets it (`40298-40311`, also `44253`).
- **Detect.** `IsTimeDilationActive(n"SceneSystemFastForward")` /
  `(n"customFFTimeDilation")`; status tag `FastForward`.
- **Rule (proposed).** Host-only, as part of "scenes for guests" (an open
  problem of its own): a guest cannot fast-forward the host's scene. First
  version: disabled for everyone in a session.

### 2.15 Effector, status-effect and AI driven dilation

- **`SetTimeDilationEffector`** (`307231`, RTTI 2.50 size 0xb0): TweakDB
  effector with flats `reason`, `dilation`, `duration`, `easeInCurve`,
  `easeOutCurve`, `affectsPlayer`. `ActionOn`: skipped when any dilation is
  active or duration is 0; if `!affectsPlayer`,
  `SetIgnoreTimeDilationOnLocalPlayerZero(true)`; then a **global**
  `SetTimeDilation`. `ActionOff` unsets (reason `n"berserk"` with no
  ease-out). Which records use it (Berserk, second chance, low-health
  slow-mo items…) needs a TweakDB search for `effectorClassName =
"SetTimeDilationEffector"`. One hook covers all of them.
- **`SetTemporaryIndividualTimeDilation`** (effect executor, `525198`):
  target puppet gets factor `0.05` for `2.5` s, curves `Linear` /
  `DiveEaseOut`, reason `None` — **per entity**, through the helper (so off
  when `IsMultiplayer()`).
- **`AISubActionApplyTimeDilation`** (`427367`, record class
  `gamedataAISubActionApplyTimeDilation_Record`: `Reason`, `Multiplier`,
  `Duration`, `EaseIn`, `EaseOut`, `OverrideMultiplerWhenPlayerInTimeDilation`,
  `UseRealTime`, `Condition`): **per NPC** `SetIndividualTimeDilation`.
  With reason `n"sandevistanVersusSandevistan"` the NPC's factor is
  `min(1 / GetActiveTimeDilation(n"sandevistan", true), override)`
  (`427392-427399`): an NPC with a Sandevistan cancels the player's.
  Duration `< 0` → 600 s, else `Duration × dilation`. Reset through
  `ResetTimeDilation` (`128605`).
- **Hit reactions**, per NPC: `n"VeryStrongHit"`, factor
  `ragdollImpulse / knockdownImpulseThreshold`, 0.5 s
  (`HitReactionComponent`, `186976`); `n"MeleeLegKnockdown"`, 1.6, 0.5 s
  (`457953`, `459497`).
- **Followers**: `AIFollowerInterpolateFollowingSpeed` (`455510`) speeds a
  companion up to `m_maxTimeDilation` by distance, no duration, removed
  with `n"KereznikovDodgeEaseOut"`.
- **Detect.** `HasIndividualTimeDilation(reason)` /
  `GetTimeDilationValue()` on the NPC.
- **Rule (proposed).** NPC time is **host state** (authority row "NPCs: AI,
  position, health"): per-entity dilations run on the host only and reach
  guests as the NPC's resulting motion; guests' proxies (AI off) never set
  one. `sandevistanVersusSandevistan` reads the **local** player's
  Sandevistan: on the host it must be driven by "any V near this NPC has
  Sandevistan active" from replicated player state. Global effectors:
  rewritten to owner-local like §2.1.

### 2.16 Quest-driven dilation

- **How.** Quest graph node `questTimeDilation_NodeType` (RTTI 2.50:
  `params: array<handle:questTimeDilation_NodeTypeParam>`), with targets
  `questTimeDilation_World { reason: CName, operation }`,
  `_Player { operation, globalTimeDilationOverride }`,
  `_Puppet { operation, globalTimeDilationOverride, puppets }`,
  `_Entity { …, parentTimeDilationOverride, entities: array<NodeRef> }`,
  and operations `questTimeDilation_Start { dilation, duration,
easeInCurve, easeOutCurve }` / `_Stop { easeOutCurve }`. Enum
  `questETimeDilationOverride`: 0 (unnamed), 1 `Ignore`, 2 `Inherit`.
  Entirely native; reasons are quest data.
- **Detect.** Poll `IsTimeDilationActive()` with no reason and subtract the
  known reasons, or hook the quest node.
- **Rule (proposed).** Quests are host-owned: a quest dilation is part of
  the host's scene and is **replicated to guests as an event** (reason,
  target, factor, duration, curves), the one case where everyone's world
  slows together.

### 2.17 Photo mode

- **Scripts (dump).** No dilation call; `PhotoModeSystem.IsPhotoModeActive()`,
  `CanPhotoModeBeEnabled()`, `IsExitLocked()` (`53346`, RTTI 2.50
  `gamePhotoModeSystem`, `IsPhotoModeActive @ 0x140d36a38`), blackboard
  `PhotoMode.IsActive` (`248383`), `PhotoModeEnableEvent` (`316336`).
- **RTTI 2.50.** PSM state `timeDilationPhotomode`
  (`TimeDilationPhotomodeDecisions.EnterCondition/ExitCondition`,
  `TimeDilationPhotomodeEvents.OnEnter/OnExit/OnForcedExit`),
  `gamePSMTimeDilation.Photomode = 2`, `GetPhotoModeKey`: on 2.50 photo
  mode is (also) a script time dilation whose reason and values are **not
  in our dump**. Whether the native side additionally pauses is unknown.
- **Rule (proposed).** A guest's photo mode freezes nothing: either it is
  refused in a session (`CanPhotoModeBeEnabled` path) or it becomes a free
  camera over a live world. Needs the 2.50 scripts first.

### 2.18 Debug

`DebugCheatsSystem.Increase/DecreaseGlobalTimeDilation`,
`Increase/DecreasePlayerTimeDilation` (`315719`; RTTI 2.50 handlers
`0x142a23914`, `0x1409a93d8`, `0x1427913ec`, `0x142790788`), sample class
`sampleTimeDilatable` with reason `n"ScriptsDebug"` (`535871`). Useful for
testing §8 questions; not gameplay.

## 3. Pause

Three different things are called "pause":

### 3.1 Game pause: `inkISystemRequestsHandler.PauseGame()`

- Natives `PauseGame()` (`0x14192a468`), `UnpauseGame()` (`0x14191a8a0`),
  `IsGamePaused() -> Bool` (`final.reds:272696-272700`, RTTI 2.50). Reached
  only through `inkGameController.GetSystemRequestsHandler()`; no
  `GameInstance` accessor (`nco-commits.md:552`). The session state machine
  has `gsmState_SessionPaused` and `gsmMenuState_InGamePause` (RTTI 2.50
  class names; the link to `PauseGame` is inferred).
- **Script callers — only two:**
  - `PauseMenuBackgroundGameController.OnInitialize` (`340145`): broadcasts
    `SetMenuModeEvent(PauseMenu, Enabled)` then `PauseGame()`;
    `OnUninitialize` broadcasts `Disabled` then `UnpauseGame()`. Opened by
    `MenuScenario_PauseMenu.OnEnterScenario` (`337366`,
    `OpenMenu(n"pause_menu_background")`). Save, load, settings, credits,
    DLC are sub-menus of this scenario (`337375-337399`).
  - `ShardNotificationController` (reading a shard, `342230`):
    `PauseGameState(true)` on player attach, `UnpauseGame()` on close and in
    `OnUninitialize` (`342273`, `342316-342328`).
- `FrameSwitcherMenuGameController` closes itself if `IsGamePaused()`
  (`330716`).
- **Delays under pause.** `DelaySystem.DelayCallback(cb, t,
isAffectedByTimeDilation = false)` runs on real time and still ticks
  while paused (NCO, `nco-commits.md:429`); delays created with `true`
  presumably do not (inferred).
- **Stopping it.** NCO: `PauseGame` is a native import and cannot be
  wrapped; wrapping `OnInitialize` and calling `UnpauseGame()` inline
  removes the menu; calling it **0.25 s later** through a real-time
  `DelayCallback` works because the `SetMenuModeEvent` is consumed a frame
  later (`engine-knowledge.md` §9, 2.31). Alternatives not tried by
  anyone: `@replaceMethod` on the two `OnInitialize`/`PauseGameState`
  bodies without the call; a plugin hook on the native handler
  (`0x14192a468`) that returns early while a session is active — the
  single choke point for both callers and for native callers, if any.
- **What must keep running under an open pause menu in co-op:** the plugin's
  frame tick (sampling the local V, applying snapshots). If the tick is
  driven by a game-thread update that the pause stops, a paused host
  freezes every guest's world. This is the first thing to test (§8).

### 3.2 Game-clock pause: `TimeSystem.SetPausedState`

- RTTI 2.50: `SetPausedState(paused: Bool, source: CName)` (`0x142aa9ea0`),
  `IsPausedState() -> Bool` (`0x142aa972c`). The dump declares
  `SetPausedState()` with no parameters (`53918`) and **no script calls
  it**. The quest node `questPauseTime_NodeType { source: CName, pause:
Bool }` has the same two fields: quests pause **the time of day** per
  source (inferred from the matching fields and from the only reader).
- Reader: `IsPausedState()` blocks the time-skip wheel
  (`GameTimeUtils.CanPlayerTimeSkip`, `332562`;
  `ActivityCardsHelper.CanPlayerSkipTime`, `446528`).
- The engine's own replication record
  `gameTimeSystemReplicatedState { paused: Bool @0x40, gameTime: GameTime
@0x44 }` (RTTI 2.50; `gameTimeSystem` derives from
  `gameIReplicatedGameSystem`) carries exactly this flag and the clock.
- **Rule (proposed).** Host state, replicated: guests mirror `paused` and
  `gameTime` (§5).

### 3.3 "Pause" by dilation to zero

World map (§2.10), time-skip wheel (§2.11), vendor family (§2.12), tutorial
popups (§2.13), the minigame (§2.7, `1.0e-5`) and possibly the hub menu
(§2.9, value unread) freeze the world with `SetTimeDilation(reason, 0.0)`
and no pause. `IsGamePaused()` is false during these; only
`IsTimeDilationActive(reason)` shows them.

### 3.4 What each screen does

<!-- prettier-ignore -->
| Screen | Mechanism | Reference |
| --- | --- | --- |
| pause menu (+ save/load/settings) | `PauseGame()` | `340149` |
| shard reader popup | `PauseGame()` | `342319` |
| hub: inventory, character, crafting, journal | profile `hubMenu` (global + player), `Invulnerable` | `336921` |
| world map | `n"WorldMap"` = 0.0, `Invulnerable` | `358237` |
| time-skip wheel | `n"TimeSkip"` = 0.0, `Invulnerable` | `383597` |
| vendor / ripperdoc / wardrobe / appearance | `n"VendorStash"` = 0.0 world and player | `404099`, `388041`, `384791`, `375605` |
| tutorial popup (`pauseGame`) | `n"UI_TutorialPopup"` = 0.0 | `403342` |
| weapon wheel, vehicle wheel, phone, message popup | profile `radialMenu` | §2.8 |
| radio popup | profile `vehicleRadioMenu` | `392091` |
| breach minigame | `n"minigame"` = 1.0e-5 | `481075` |
| photo mode | native; 2.50 PSM state `timeDilationPhotomode` | §2.17 |
| braindance editor | scene "rewindable section" (pause/speed/direction on `SceneSystemInterface`, `193104-193122`); another timeline, out of scope here | — |
| loading screen, death screen | not studied | — |

**Rule (proposed) for all of §3.** No menu, popup or pause stops a co-op
world. The pause menu opens over a running game for host and guests; the
V of a player in a menu stays in the world. A real pause can exist later as
an explicit host action replicated to everyone ("session paused"), never as
a side effect of one player's UI.

## 4. Skipped time

### 4.1 Waiting (time-skip wheel)

- **Open.** Hub button → `HubTimeSkipController.OnTimeSkipButtonPressed`
  (`332307`) shows `TimeSkipPopupData`
  (`base\gameplay\gui\widgets\time_skip\time_skip.inkwidget`, queue
  `modal_popup`); radial hub equivalent `334498`; menu scenario
  `MenuScenario_TimeSkip` (`335851`, menu `n"time_skip"`), event
  `OnOpenTimeSkip` (`338683`).
- **Allowed when** `GameTimeUtils.CanPlayerTimeSkip(player)` (`332543`):
  not in combat, no status tag `NoTimeSkip`, clock not paused, not moving
  vertically, not swimming (state 2), not PSM `Vehicle == 4`, not driving
  above 13.8 m/s, scene tier below 3, security zone `SAFE`, no phone call,
  not carrying, not in a lore animation scene, not chased by police,
  hardware not disabled, autodrive off. Facts such as
  `mq013_block_timeskip` exist in the name pool (quest side).
- **Apply.** `TimeskipGameController.Apply` (`383716`):
  `timeSystem.SetGameTimeByHMS(Hours(now) + hoursToSkip, Minutes(now),
Seconds(now), n"ui_menu_timeskip")`, then
  `GameTimeUtils.FastForwardPlayerState(player)` (`332610`: raises health
  to the out-of-combat regen threshold, sets stamina to 100 and removes
  every status effect that has a remaining duration), then plays the wheel
  animation. The engine answers with the native event
  `gameTimeSkipFinishedEvent` (script name `TimeSkipFinishedEvent`,
  `383516`) → `OnTimeSkipFinishedEvent` (`383813`) → close →
  `TimeSkipFinishEvent` broadcast (`335848`, listeners `347376`,
  `409140`). What the engine does between the call and the event
  (streaming, crowd reset, weather roll, possibly a teleport: CNames
  `teleportPositionAfterTimeSkipping` / `teleportRotationAfterTimeSkipping`
  exist) is native and unknown.
- **Quests see it** through `questTimeSkipped_ConditionType { mode:
questTimeSkipMode }` (`PreSkip` 0, `PostSkip` 1, `Any` -1).
- **Suppress.** Wrap `TimeskipGameController.Apply` (NCO,
  `nco-commits.md:141`) or disable the hub button
  (`CanPlayerTimeSkip`).
- **Rule (proposed).** Time of day is host state. Guests: wheel disabled
  (a wrap of `CanPlayerTimeSkip` returning false keeps the button greyed).
  Host: allowed only when every player satisfies the vanilla conditions
  (first version: host disabled too); the skip is then one replicated
  event — new `gameTime`, reason — and every game applies
  `SetGameTimeByHMS` and its own `FastForwardPlayerState`.

### 4.2 Sleeping

No script sets the time for a bed: the bed interaction is quest/scene data
(inferred from the absence of any caller; the only script
`SetGameTimeByHMS` call is the wheel, `383727`, and
`SetGameTimeBySeconds` has none). The time change is one of the quest
nodes of §4.4 and the buff is a status effect (`Sleep`, `Rested` tags).
**Rule (proposed):** as quest-driven time, host only; a guest using a bed
gets the animation and the buff, not the time change.

### 4.3 Fast travel

`FastTravelSystem.PerformFastTravel` (`50415`): removes gameplay
restrictions, writes the destination to blackboard `FastTRavelSystem`,
resets camera pitch and calls
`GameInstance.GetTeleportationFacility(...).TeleportToNode(player,
markerRef)`, then `SetFastTravelStarted()`; an autosave is requested after
the loading screen. **No game-time change in scripts**; whether the
loading screen advances the clock is native and unknown. Requests:
`PerformFastTravelRequest` (`237972`), locks `AddFastTravelLock(reason)`
(`InCombat`, `InVehicle`, `PreventionSystem`). **Rule:** already decided in
../architecture/travel.md (tether and regroup on fast travel); for time, nothing to
do unless §8 shows a clock jump.

### 4.4 Quest-driven time

Native quest nodes under `questTimeManagerNodeDefinition { type }` (RTTI
2.50):

<!-- prettier-ignore -->
| Node type | Fields |
| --- | --- |
| `questSetTime_NodeType` | `source: CName`, `hours`, `minutes`, `seconds: Int32` |
| `questShiftTime_NodeType` | `timeShiftType: questETimeShiftType` (`ShiftByTime` 0, `ShiftToHour` 1), `preventVisualGlitch: Bool`, `hours`, `minutes`, `seconds: Uint32` |
| `questPauseTime_NodeType` | `source: CName`, `pause: Bool` |

Conditions: `questGameTimeDelay_ConditionType { days, hours, minutes,
seconds }`, `questRealtimeDelay_ConditionType { hours, minutes, seconds,
miliseconds }`, `questTimePeriod_ConditionType { begin, end: GameTime }`.
Phone-call and holocall "time skips" are these nodes (inferred).
**Rule (proposed):** quests run on the host only, so these fire on the host
only; guests receive the result through the clock replication of §5. A
realtime delay condition on the host is unaffected by anything a guest
does, as long as the host's world never pauses (§3).

### 4.5 Game time against real time

- `TimeSystem.RealTimeSecondsToGameTime(seconds: Float) -> GameTime`
  (`0x1418f0bd8`) is the conversion; the scale is engine/TweakDB data.
- TweakDB flat `timeSystem.settings.realTimeMultiplier` is read by
  `GameTimePrereq` (`310403`, fallback 1.0) to convert a stat into a delay.
  Its value was not read. Measure with
  `RealTimeSecondsToGameTime(1.0)` in game before hard-coding anything
  (the commonly quoted figure is 8 game seconds per real second).
- **Global dilation and the clock.** Whether the time of day advances at
  the dilated rate (Sandevistan slowing the sun) is not visible in scripts;
  `GetSimTime()` (`EngineTime`) is the dilated simulation clock used for
  gameplay timestamps, `GameInstance.GetEngineTime()` the undilated one
  (inferred from usage: cooldown-like checks use `GetSimTime`, UI/input
  timing uses `GetEngineTime`). Open question §8.
- **Weather.** No script links weather to the time system. Scripts can only
  read: `WeatherSystem.GetRainIntensity()`, `GetRainIntensityType()`,
  `GetRainMoisture()`, `GetRainPuddles()`,
  `RegisterWeatherListener(WeatherScriptListener)` (`252163`). The wheel
  shows a weather icon after a skip (`383799`, `383994`). Setting weather
  is covered in `engine-knowledge.md` §1 (Codeware setter, sticky forced
  state).

## 5. Game time itself

- **Type.** `GameTime` is a 4-byte struct `{ seconds: Int32 }` (RTTI 2.50)
  with script natives `MakeGameTime(days, hours, opt minutes, opt
seconds)`, `Day()`, `Hour()`, `Minute()`, `GetSeconds`, `Seconds`,
  `Minutes`, `Hours`, `Days`, `ToString`, `IsAfter` (`118342`). It counts
  seconds since day 0, days included (`Days()`), so it is an absolute
  date-time, not a time of day.
- **Read.** `TimeSystem.GetGameTime() -> GameTime` (`0x1412aec64`),
  `GetGameTimeStamp() -> Float` (`0x1418432ac`), `GetSimTime() ->
EngineTime` (`0x140ebc124`).
- **Write.** `SetGameTimeBySeconds(seconds: Int32)` (`0x142aa9cd8`;
  RTTI 2.50 also lists a parameterless `SetGameTime()` at the **same
  address**), `SetGameTimeByHMS(hours, minutes, seconds: Int32, opt reason:
CName)` (`0x142aa9d50`; hours may exceed 23, the wheel passes
  `Hours(now) + n`). Whether `SetGameTimeBySeconds` takes an absolute
  `GameTime` (days included) or seconds of the current day, and whether
  either setter triggers the time-skip machinery (event, world reset,
  quest `TimeSkipped` condition) is **not known**.
- **Listeners.** `RegisterListener(entity, event, expectedTime: GameTime,
repeat: Int32, opt sendOldNoifications) -> Uint32`,
  `RegisterDelayedListener(entity, event, delay: GameTime, repeat, …)`,
  `RegisterIntervalListener(entity, event, expectedHour, timeout,
opt repeat)`, `RegisterScriptableSystemIntervalListener(systemName,
request, expectedHour, timeout, opt repeat)`, `UnregisterListener(id)`
  (`53884-53916`). Users: city lights (`251691`), device time tables
  (`260991`, `468477`, `499135`), vehicles (`93739`). These fire on each
  game from its own clock: with the guest's clock slaved to the host, a
  guest's own listeners (lights, shop hours) follow for free.
- **What a host would broadcast** (matches the engine's own
  `gameTimeSystemReplicatedState`):
  - `gameTime: Int32` seconds and the host-clock microsecond stamp at which
    it was sampled (../architecture/smoothing.md: absolute times are integers);
  - `paused: Bool` (the §3.2 clock pause);
  - the real-time ratio once, at `Welcome`, if it is found to be
    configurable;
  - on join, on every skip/set (reliable, with the reason CName), and at a
    low rate (NCO used 30 s, `engine-knowledge.md` §1) to correct drift.
    Guests extrapolate between updates with the ratio and call
    `SetGameTimeBySeconds` only when the error exceeds a threshold, to avoid
    re-triggering whatever a set triggers.

## 6. Authority rows this implies

Proposed additions to the table in ARCHITECTURE.md (not applied there):

<!-- prettier-ignore -->
| State | Owner | Others |
| --- | --- | --- |
| a V's time effects (Sandevistan, Kerenzikov, perks) | that player's game | shown as that V's state; never slow their world |
| a V's menus, popups, photo mode | that player's game | nothing stops; V stays in the world |
| NPC individual time dilation | host | result seen through the NPC's motion |
| quest time dilation, scene fast-forward | host | replicated event / followed |
| game clock, clock pause, time skips | host | applied; guests cannot skip |
| session pause (if ever) | host, explicit | applied to all |

## 7. Summary table

Scope: **G** global (`SetTimeDilation`), **G−P** global with the local
player exempt, **G+P** global plus an own player factor, **E** per entity,
**Pause** `PauseGame`, **Clock** game time.

<!-- prettier-ignore -->
| Effect | Reason / trigger | Scope | Detect | Proposed rule |
| --- | --- | --- | --- | --- |
| Sandevistan | `sandevistan`; `SandevistanEvents.OnEnter` `524119` | G−P | reason; PSM BB `TimeDilation == 1` | owner-local; owner sped up instead; others see fast V |
| Kerenzikov | `kereznikov`; `KerenzikovEvents.OnEnter` `527831` | G+P | reason; PSM state `kerenzikov`; `KerenzikovPlayerBuff` | owner-local; no world slow-down |
| scanner / quickhack | `focusMode`; `TimeDilationFocusModeEvents` `529159` | G−P or G+P (TweakDB) | reason; PSM `Vision == 1` | off in co-op (TweakXL flag) |
| Cool focus perk | `focusedStatePerkDilation`; `542858` | G | reason; tag `FocusedCoolPerkSE` | owner-local, dilation dropped |
| Deadeye reload perk | `coolReloadPerkDilation`; `547583` | G | reason; tag `DeadeyeQuickReloadSE` | owner-local, dilation dropped |
| second chance perk | status `SecondChancePerkTimeDilation`; TweakDB | ? | status effect | as effectors |
| melee hit stop | `meleeHit`; `PlayerPuppet.OnMeleeHitSloMo` `45789` | G | reason | dropped (TweakXL `enabled`) |
| deflect / one-punch / boss finisher | `deflect`; `39679`, `552917`, `554284` | G | reason | dropped |
| breach minigame | `minigame` (1.0e-5); `481075` | G | reason; BB `IsInMinigame` | owner-local UI, real time |
| network dive | `NetworkBreach`; set natively, unset `23064` | G (inferred) | reason | owner-local, real time |
| wheels, phone, popups | profile `radialMenu`; §2.8 | G+P | profile reason; `UIGameContext` | no dilation |
| radio popup | profile `vehicleRadioMenu`; `392091` | G+P | profile reason | no dilation |
| hub menu | profile `hubMenu`; `336921`, `338809` | G+P | `SetMenuModeEvent(HubMenu)`; `IsInMenu` | no dilation; V vulnerable |
| world map | `WorldMap` (0.0); `358237` | G | reason | no freeze |
| time-skip wheel | `TimeSkip` (0.0); `383597` | G | reason | no freeze; skip is host-only |
| vendor, ripperdoc, wardrobe, appearance | `VendorStash` (0.0 / 0.0); `404099` `388041` `384791` `375605` | G+P | reason | no freeze |
| tutorial popup | `UI_TutorialPopup` (0.0); `403342` | G | reason | non-blocking |
| scene fast-forward | `SceneSystemFastForward` (native); `customFFTimeDilation` (10.0) `40303` | G | reasons; tag `FastForward` | host-only; off at first |
| effector | TweakDB `reason`; `SetTimeDilationEffector` `307231` | G or G−P | reason (e.g. `berserk`) | owner-local |
| quest dilation | quest data; `questTimeDilation_NodeType` | G / P / E | `IsTimeDilationActive()` minus known | host, replicated event |
| NPC Sandevistan / AI | record `Reason`; `AISubActionApplyTimeDilation` `427444` | E | `HasIndividualTimeDilation` | host-only NPC state |
| NPC hit reactions | `VeryStrongHit`, `MeleeLegKnockdown` | E | same | host-only |
| dive executor | `None`, 0.05 for 2.5 s; `525204` | E | same | host-only |
| photo mode | 2.50 PSM `timeDilationPhotomode`; native | ? | `IsPhotoModeActive()`; BB `PhotoMode.IsActive` | no freeze; maybe refused |
| pause menu | `PauseGame()`; `340149` | Pause | `IsGamePaused()`; `SetMenuModeEvent(PauseMenu)` | never pauses a session |
| shard popup | `PauseGame()`; `342319` | Pause | `IsGamePaused()` | never pauses |
| clock pause | `SetPausedState(paused, source)`; quest node | Clock | `IsPausedState()` | host, replicated |
| waiting | `SetGameTimeByHMS(…, n"ui_menu_timeskip")`; `383727` | Clock | `TimeSkipFinishEvent`; native `gameTimeSkipFinishedEvent` | host-only, replicated |
| sleeping, calls | quest nodes `questSetTime` / `questShiftTime` | Clock | clock jump | host-only, replicated |
| fast travel | `TeleportToNode`; `50415` | none seen | BB `FastTRavelSystem` | tether/regroup (ARCHITECTURE.md) |

## 8. Open questions (each needs an in-game test)

1. Does the plugin's frame tick keep running while `PauseGame()` is active,
   and while the global dilation is `0.0`? (Decides whether a host in a
   menu freezes the guests.)
2. Do native callers (quest node, scene fast-forward, photo mode, the
   `NetworkBreach` dive) pass through the script-native handler
   `0x1407d62b0`, or must each be hooked separately?
3. Can the local player's factor exceed 1
   (`SetTimeDilationOnLocalPlayerZero(reason, 2.0)`)? Does it speed up
   movement, animation, weapon fire, stamina and cooldowns alike? This is
   the Sandevistan stand-in.
4. With a global dilation active on one game only, how do proxies behave
   there: do plugin-driven transforms stay on host time while engine-driven
   animation slows? Does `SetIndividualTimeDilation(..., ignoreGlobalDilation
= true)` on every proxy cancel a local global dilation?
5. How does the engine combine several active reasons, and what does
   `IsTimeDilationActive()` return for a factor of exactly 1.0 (the "pass
   through at 1.0" trick for listener callers)?
6. Does skipping `SetTimeDilation` break callers that wait on a
   `TimeDilationListener` (`OnDiveFinished`), as NCO reported for 2.31?
7. Values on 2.50: `timeSystem.hubMenu.*`, `radialMenu.*`,
   `vehicleRadioMenu.*`, `focusModeTimeDilation.*`, `meleeHit*.*`,
   `nanoWireBreach.*`, `settings.realTimeMultiplier`, the Sandevistan and
   Kerenzikov stat ranges, and every record using
   `SetTimeDilationEffector` (read `tweakdb.bin` or dump with TweakXL).
8. The 2.50 scripts: bodies of `TimeDilationPhotomode*`,
   `GetPhotoModeKey`, `OnExitToTimeDilationPhotomode`, the
   `noDelayedCleanUp` parameter, and any reader of `quickHackScreen` /
   `OverrideQuickHackPanelDilation`. Decompile the game's own
   `final.redscripts` for 2.50 and diff against this dump.
9. Is the game clock dilated by global time dilation, and stopped by
   `PauseGame()`? (Compare `GetGameTime()` over a real-time minute in each
   state.)
10. `SetGameTimeBySeconds`: absolute `GameTime` or seconds of day? Does a
    set fire `gameTimeSkipFinishedEvent`, the quest `TimeSkipped`
    condition, a crowd/traffic reset or a visual glitch
    (`preventVisualGlitch` on the quest node suggests one)? How large a
    correction can be applied silently?
11. Does fast travel or a loading screen move the clock?
12. `SetPausedState(paused, source)`: confirm it stops only the time of day
    and that sources are reference-counted.
13. Does the `HubMenu` / `WorldMap` / `TimeSkip` invulnerability matter
    once the world no longer freezes (damage events from the host arriving
    while a guest is in the map)?
14. Which controller really runs the breach minigame on 2.50
    (`NetworkInkGameController` or the native hacking minigame), and what
    sets `n"NetworkBreach"`?
15. Does the 0.25 s delayed `UnpauseGame()` still work on 2.50, and does a
    native hook on `PauseGame` leave the pause menu usable (cursor, input
    context, save/load)?
