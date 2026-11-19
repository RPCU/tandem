# Time dilation

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

### 13.15 Time dilation in a shared world (2026-11-18, scripts and RTTI only)

Read from the 2.50 RTTI natives (`docs/research/data/rtti-natives-2.50.tsv`)
and the script dump; used by `scripts/tandem/Time.reds`, **not yet run in
game**. Research behind it: `coop-time.md` §1-2.

- The time system has exactly three scopes and no render-only one: global
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
  entity's time. A host therefore cannot show its own NPCs slowed while
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
  `OnRunningUpdate`), outside the script tick. Once it crashed loading a
  save as host, in the script VM about three calls deep under
  `TandemTime.Frame`, before "playing". Once it froze right after
  "playing", when the work began (no backtrace). Suspects, most likely
  first, all found by reading the code (none confirmed):
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
     from `TandemSystem.Tick` once the world plays. The plugin's per-frame
     call is back to `TandemPause.Frame` alone.
  3. Something else that started at "playing" on the same frame (stealth,
     takedowns): not Time.reds.

  No deadlock found: the time natives each take `Runtime::m_lock` once and
  release it before returning. They never call the engine or the network
  while holding it, and nothing holds it while waiting on the game thread.
  No loop in Time.reds or `TimeBubbles::Update` can run forever.
  [2.5-untested]
