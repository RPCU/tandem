# Down, revive, respawn

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.20 Down, revive, respawn: the levers (2026-11-18, read in final.reds, not run)

Used by `scripts/tandem/Revive.reds`; every line is [2.5-untested] until
the `revive-*` scenarios pass.

- Second Heart keeps V alive with a god-mode **override**, not a source:
  `SecondHeartStatListener.OnStatChanged` (243476) calls
  `GodModeSystem.EnableOverride(V, Immortal, n"SecondHeart")` /
  `DisableOverride(V, n"SecondHeart")` (natives at 87686-87688). A plain
  `AddGodMode` on V did not protect it (§13.11): tandem uses its own
  override source. [2.5-untested]
- With that health floor, `DeathDecisions.EnterCondition` (528038) enters
  the death state when `HasSecondHeart` and health <= 1.1 % (and V is not
  Invulnerable); otherwise only at the minimum or `GetIsDead`. Wrapping it
  lets a co-op floor enter the same way. [2.5-untested]
- `DeathEvents.OnEnter` (527754) → `HighLevelTransition.StartDeathEffects`
  (283766): custom death animation, `ForceTemporaryUnequip`, `Vitals = 1`,
  `global_death_enter` / `ui_death` sounds, then
  `SetIsResurrectionAllowedBasedOnState` (283786): Second Heart allowed
  (`ForcePreventResurrect == 0`, `HasSecondHeart`, not `DeathLand`, no
  `BaseStatusEffect.ForceKill`) sets the permanent parameter
  `isResurrectionAllowed`; else `DisplayDeathMenu = true` (unless the
  `block_death_menu` fact) and health to 0. [2.5-untested]
- `DeathDecisionsWithResurrection.ToResurrect` (528472): allowed and
  3 s (`stateDuration`) in the state. Holding it false keeps V in the
  death state with no menu; letting it go runs `ResurrectEvents` (529013:
  applies `BaseStatusEffect.SecondHeart`, `Vitals = 2`, `PlayerResurrect`
  anim event; on exit a `ResurrectEvent` → `PlayerPuppet.OnResurrected`
  45083, health 100 %, or 20 % `IsMultiplayer()`). `ResurrectDecisions`
  (528747) leaves when the `SecondHeart` effect has run out. Whether that
  effect also starts the cyberware's cooldown is unknown. [2.5-untested]
- No `gameDeathEvent` while Immortal, so none of `PlayerPuppet.OnDeath`
  (45314: `BlockAllMenu`, autosaves invalidated) happens to a downed V.
  [2.5-untested]
- Vanilla multiplayer's downed player: `PlayerPuppet.OnIncapacitated`
  (45051) enables the `Revive` interaction layer (on the player entity:
  our NPC-template puppet has none) and makes it a `Deadbody` to senses;
  `RefreshCPOVisionAppearance` (45061) outlines another client's
  incapacitated player through walls (`VisionAppearance{fill 1, outline
4, showThroughWalls}`, `VisionModeSystem.ForceVisionAppearance` /
  `CancelForceVisionAppearance`, 49958). The `Revive` choice handler is
  `ScriptedPuppet.OnInteraction` (22542). [2.5-untested]
- An interaction prompt without an interaction component: write
  `UIInteractions.InteractionChoiceHub` (`InteractionChoiceHubData{id,
active, title, choices}`, 243011; `InteractionChoiceData{inputAction,
isHoldAction, localizedName}`, 390126) and `VisualizersInfo{activeVisId,
visIds}` (390208): the interaction widget (394589) shows the buttons
  only while `activeVisId` is the hub's id. Input: `GameObject.
RegisterInputListener(listener, n"Choice1")` (11423) and the listener's
  `OnAction(ListenerAction, ListenerActionConsumer)` with
  `IsButtonJustPressed` / `IsButtonJustReleased` (192853). [2.5-untested]
- The HUD progress bar: `UI_HUDProgressBar` (195213) `Header`,
  `BottomText`, `CompletedText`, `MessageType` (a `SimpleMessageType`
  variant), `Progress` 0..1, `Active` (the Twintone scan drives it so,
  451258). [2.5-untested]
- Getting out of the death state for good is `ExplorationEvents.OnEnter`
  (525154): `Vitals = 0`, the vitals and death anim features reset,
  `global_death_exit`; `GroundDeathEvents.OnEnter` (528540) blocks movement
  and sets the death camera's parameters first. The `dying` screen effect
  is started only when V enters the death state already dead (527775) and
  stopped by its exit; `GameObjectEffectHelper.BreakEffectLoopEvent(V,
n"dying")` (106053) ends it by hand. [2.5-untested]
- `BaseStatusEffect.PlayerInvulnerable` is what Bloodswell puts on V
  (307839). `PlayerPuppet.SetInvisible` (43505) toggles V's visible object
  (what NPC senses see). [2.5-untested]

Read 2026-11-21 for a V down in a car, in water, and the respawn spot
(`Revive.reds`); every line [2.5-untested]:

- **Seated.** The death state runs beside the vehicle state machine: its
  death type is Ground in a seat (`GetDeathType` 283737: falling, else
  `LocomotionSwimming`, else Ground) and `SetDeathCameraParameters`
  (283749) also sets `VehicleCameraParams` when the `Vehicle` machine is
  active, so vanilla's V dies in its seat. The only death exit,
  `DeathExitingDecisions` (551187), is for a bike (`vehClass` 1) and a V
  that `IsDead()`, which a V held at tandem's Immortal floor never is: a
  downed V stays seated. `ResurrectEvents` has no vehicle check (the
  Second Heart stand-up in a seat). [2.5-untested]
- **The game's forced exit**: `ImmediateExitWithForceEvents.Unmount`
  (550633) is a `MountingFacility.Unmount` of an `UnmountingRequest` with
  only `childId` and `isInstant`; `ExitingDecisions.EnterCondition`
  (550593) leaves the seat when such an unmount is pending (`IsExitForced`).
  Where V stands on getting out: `VehicleObject.CanUnmount(isPlayer,
object, opt direction)` (87761) → `vehicleUnmountPosition{direction,
position}` (288051, `vehicleExitDirection` 7842: 0 none … 5 top).
  Wrecked: `VehicleObject.IsDestroyed` (87959); upside down:
  `IsVehicleUpsideDown` (88944, from `VehicleFlippedOverEvent`) or
  `ComputeIsVehicleUpsideDown` (world up below the horizon). A stop:
  `VehicleObject.ForceBrakesUntilStoppedOrFor(seconds)` (87873).
  [2.5-untested]
- **Water.** `AIScriptUtils.GetWaterLevel(game, Vector3, out level)`
  (158907) answers true only for a point under water (`HasHeadUnderwater`
  21360); the state machine's own `GetWaterLevel(from, to, out level)`
  (247454) is only inside a state. Vanilla swims at 1.2 m + 0.1 over the
  feet (`SwimmingDecisions`, 525631) and finds the floor with a raycast down
  from the surface. `SwimmingDecisions.ToDeath` (525674) leaves for the
  death state only at the health minimum; the death type is then Swimming.
  No script query returns "the nearest shore": the nearest navmesh point
  in a sphere is `AINavigationSystem.FindPointInSphereForCharacter`, and
  `gamePuppet.GetLastValidNavmeshPoint()` (19442) the last navmesh point
  a puppet stood on (whether the engine updates it for the player is
  unknown). Static raycasts: `SpatialQueriesSystem.
SyncRaycastByCollisionGroup(from, to, n"Static", out TraceResult, true,
false)` (177828). [2.5-untested]

### 13.110 Viktor's clinic: where, and when the story has it (2026-11-21, game data and final.reds read, not run)

Used by `Revive.reds` (`VikSpot`, `VikOutOfReach`, `FollowFar`).

- **Where.** The game's journal quest mappins (exported from the 2.x
  journal by the RED modding wiki, "Reference: Quest Ids", updated
  2025-08-20: `journal-quest-data.json`, read from the wiki's repository):
  `#ws_victor_vector_default`, Viktor's own spot (mq033's "Talk to
  Viktor", mq045 "Paid in Full"), at (-1542.592, 1230.243, 11.519);
  `#q307_mp_viktor_chair`, the ripper's chair (q307 "Sit in the ripper's
  chair"), at (-1546.669, 1234.674, 12.301). A community teleport list
  (ali213.net's console guide, "RIPPERDOC - VIKTOR'S CLINIC [WATSON -
  LITTLE CHINA]") gives (-1546.726, 1227.393, 11.520): the floor of
  Viktor's spot (z 11.52), 4.4 m from it and 7.3 m from the chair, which
  lies along +Y (yaw -0.45°, forward (-sin, cos)); tandem gets V up there.
  The district enum has the clinic as `LittleChina_VictorsClinic` (95,
  `gamedataDistrict`, 4716). [2.5-untested]
- **When.** Before V has met Vik, `q001_ripperdoc_done` < 1 (vanilla's
  ripperdoc tutorial reads it: 42352, 226288, 387895). Watson's lockdown
  is Act 1: `q101_done` (the end of "Playing for Time", the last Act 1
  quest) is unset until then (the hub's Act 1 tarot is gated on it,
  332681); whether the fact is set exactly when the lockdown lifts is
  unchecked. In Watson: the district manager's current district
  (`PreventionSystem.GetCurrentDistrict`, 138920) or one of its parents
  (`District_Record.ParentDistrict`, 51044) has `Type()` `Watson` (128).
  [2.5-untested]
- **A fast travel's jump to a position.** `FastTravelSystem.PerformFastTravel`
  (50415) teleports to a node (`TeleportToNode`); to a position tandem
  makes the same steps with `TeleportationFacility.Teleport` (192802).
  `FastTRavelSystem.FastTravelLoadingScreenFinished` (50908) is written by
  the native side and only read (listeners) by scripts; whether it goes
  false when a fast travel starts is unknown, so tandem writes false
  before its jump and waits for true. [2.5-untested]
