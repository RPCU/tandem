# Stealth, detection and takedowns

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.44 Combat state, detection and stealth for a guest (2026-11-18, scripts only)

Read in the 2.50 scripts (`final.reds` line numbers); none of it tried in
game yet. Used by `scripts/tandem/Stealth.reds`.

- **What puts V in combat.** `PlayerCombatController` (242633) keeps flags
  (crouch, `m_isTracked`, quickhack, Johnny) and `VerifyActivation`
  (242719) picks a state: tracked → `ActivateCombat` (242876: PSM
  blackboard `PlayerStateMachine.Combat` = 1, `AnimFeature_CombatState`,
  `FastTravelSystem.AddFastTravelLock(n"InCombat")`, combat chatter,
  `NotifyGameTone(n"EnterCombat")`, `HandleCombatMix`); crouched and not
  tracked → stealth (3); neither → out of combat (2, `LeaveCombat` tone,
  lock removed). `m_isTracked` is set only by
  `OnStartedBeingTrackedAsHostile` / `OnStoppedBeingTrackedAsHostile`
  (242808-242816), public, the event argument unused; `PlayerPuppet`
  forwards the native AI events `StartedBeingTrackedAsHostile` /
  `StoppedBeingTrackedAsHostile` (importonly, 315706-315709) to them
  (46292-46298). `PlayerPuppet.OnCombatStateChanged` (43634) listens to
  the PSM value and sets `m_inCombat` (`IsInCombat()`, 45377), the perk
  blackboard's `CombatStateTime`, `UpdateVisibility`.
  `AttackData.IsPlayerInCombat` (52593) reads the PSM value directly.
  Tandem's guest calls the two controller functions from an `@addMethod`
  on `PlayerPuppet` (the controller is the private `m_combatController`),
  with `null` for the event, and wraps the `PlayerPuppet` callbacks so a
  local "stopped" does not end a combat the host's NPCs still hold.
  [2.5-untested]
- **NPC side of "in combat with V".** `NPCPuppet.IsInCombatWithTarget(npc,
target)` (130026): high-level state Combat and the target tracker's
  threat for the target is `Hostile`. The host ORs it with
  `GetDetection >= 100` for `NpcEntry::detected`. [2.5-untested]
- **The stealth meter.** The HUD meter and the detection perks read the
  `UI_Stealth` blackboard (`highestDetectionOnPlayer`,
  `highestHostileDetectionOnPlayer`, 243024; `DetectionMeterEffector`
  300783 stacks an effect per `detectionStep`), written by the engine.
  The indicator over an NPC is its sense component's `StealthMappin`
  (`CreateSenseMappin`, 155542); `StealthMappinController.OnUpdate`
  (406522) takes the level from the native `GetDetectionProgress()`, not
  from script. `NPCPuppet.SetDetectionPercentage` (125823) writes
  `PuppetState.DetectionPercentage`, which some effectors compare
  (313954). Tandem writes the blackboard (scale assumed 0..100, as
  `GetDetection`) and each proxy's percentage, and creates the proxy's
  sense mappin while it notices us. To check in game: whether the engine
  overwrites the blackboard every frame, and what a proxy's mappin shows
  with its senses off (likely no progress: then the per-proxy meter needs
  native work). [2.5-untested]
- **What NPC senses see of V.** V's visible object is the
  `senseVisibleObject` component (ScriptedPuppet 19809;
  `GetVisibleObjectComponent`). `PlayerPuppet.SetInvisible` (43505)
  toggles it (`IsInvisible`). `UpdateVisibilityModifier` (40819) queues
  `VisibleObjectDetectionMultEvent{multiplier = Visibility stat}` on V;
  `UpdateSecondaryVisibilityOffset` (43599) queues
  `VisibleObjectSecondaryPositionEvent` with Z
  `player.stealth.crouchVisibilityZOffset` (0.6) crouched or
  `chestVisibilityZOffset` (1.3). Tandem queues the same events on a
  puppet and toggles its grafted `senseVisibleObjectComponent` (§13.11)
  for camo. [2.5-untested]
- **Optical camo.** Status effects tagged `CamoActiveOnPlayer` (269230)
  or `ActiveCamo` (44019). Their `AttachOpticalCamoVisionBlockerEffector`
  (249803, an `AttachCapsuleVisionBlockerEffector` 249727) registers a
  capsule vision blocker on the owner with the sense manager's
  `VisionBlockersRegistrar`. With the relic
  (`CanPlayerExitCombatWithOpticalCamo`, 44019-44056) in combat:
  `SetInvisible(true)`, each hostile threat's tracker
  `DeactivateThreat(V)`, `ExitCombatOnOpticalCamoActivatedEvent` later.
  Tandem applies the V's camo effect to its puppet (owner: the puppet) for
  the look and the blocker; whether a player camo effect runs on an NPC
  body is to check. [2.5-untested]
- **Stimuli.** `StimBroadcasterComponent.TriggerSingleBroadcast(contextOwner,
type, radius, investigateData, propagationChange)` (149948) is how V's
  shots are heard (`ShootEvents.OnEnter` 548102: `Gunshot` at 25 m indoors
  or `GetGunshotRange()`, `SilencedGunshot` + `IllegalAction` when
  silenced; muzzle brake: none). With no broadcaster on an entity,
  `StimBroadcasterComponentHelper.CreateStimEvent` +
  `ProcessSingleStimuliBroadcast` (150609) do the same. `gamedataStimType`
  values: `Explosion` 24, `Gunshot` 28, `SilencedGunshot` 49. Grenades
  broadcast from the projectile (`Explosion`, 127920), not from V.
  [2.5-untested]

### 13.46 Takedowns, finishers, carried bodies and "the player" reactions (2026-11-18, scripts only)

Read in `final.reds` for `scripts/tandem/Takedowns.reds` and
`Reactions.reds`; none of it run in a session yet.

- **Every takedown's NPC side is one game effect**:
  `TakedownGameEffectHelper.FillTakedownData(executionOwner, activator,
target, effectName, effectTag, opt statusEffect)` (252966) runs
  `GameEffectSystem.CreateEffectStatic(n"takedowns", tag, activator)` with
  the target as shared `entity`. Tags: the grapple start
  (`GrappleTarget`, `GrappleTargetCover`, `GrappleTargetPlayerCrouch`,
  from `TakedownGrapplePreyEvents.OnEnter` 535235), `kill`,
  `setUnconscious`, `setUnconsciousTakedownNetrunner`,
  `setUnconsciousTakedownMassiveTarget`, `setUnconsciousAerialTakedown`
  (`LocomotionTakedownEvents.DefeatTarget` 285348, which also sends
  `takedown_defeat` for the non-lethal ones), and aerial or boss workspot
  names (`SelectSyncedAnimationAndExecuteAction` 285240). A static
  function can be wrapped (`@wrapMethod` on it, as NCO does for
  `TimeDilationHelper`). [2.5-untested]
- The grappled NPC is in a workspot tagged `Grappled`
  (`ScriptedPuppet.OnWorkspotStartedEvent` 22563 applies
  `BaseStatusEffect.Grappled`); `ScriptedPuppet.IsBeingGrappled` (21171)
  reads it. Synced animations: `grapple_sync_kill[_02|_03]`,
  `grapple_sync_nonlethal[_crouch]`, `grapple_sync_recover` (break free),
  `grapple_sync_shove`, `grapple_sync_death`, played with
  `WorkspotGameSystem.SendJumpToAnimEnt` (`PlayExitAnimation` 285006).
  With a **puppet** as activator (tandem's host) the grapple start effect
  (`GrappleTarget`) does **not** take: half a second later the NPC is not
  `IsBeingGrappled`, so tandem falls back to `Grappled` plus placing the
  NPC 0.6 m in front of the puppet each frame (scenario `takedowns-host`,
  2026-11-18). [2.5-verified] The `kill` effect with a puppet does kill
  the NPC, but the game names no killer (`GetKiller` is not the puppet):
  the host credits the player itself. [2.5-verified] A non-lethal
  takedown run the same way (`setUnconscious` effect plus the
  `takedown_defeat` signal, on an NPC in the fallback grapple) was followed
  within seconds by a stopped game thread (watchdog, no backtrace taken);
  tandem now applies `BaseStatusEffect.Unconscious` directly instead, as
  `TakedownUnmountPreyEvents` does. [2.5-tested: cause not confirmed]
- **The grapple is two steps** (2.50 scripts, 2026-11-22): after the
  effect, `GrappleMountEvents.OnEnter` mounts the NPC on V
  (`MountingRequest`, child the NPC, parent V, slot `grapple`), and
  `TakedownUnmountPreyEvents.OnEnter` unmounts it (an `UnmountingRequest`
  with the child alone). `ScriptedPuppet.IsBeingGrappled` is true for the
  `Grappled` status **or** for a mount on the slot `grapple` alone;
  `NPCPuppet.OnHitVFX` and `HitReactionComponent.EvaluateHit` read the
  same mount (the human shield effect, the grappled twitch). Whether a
  puppet's body (an NPC entity) has that slot is not known: tandem probes
  it (`ScriptedPuppet.GetSlotComponent().GetSlotTransform(n"grapple")`)
  and reads the mount back (`architecture/stealth.md`). [2.5-untested]
  - Measured on instance b (2026-11-22): **V has no `grapple` slot
    either**: none of her slot components (`slots`, `OccupantSlots` with
    `bothHands` and `leftShoulder` on `WeaponLeft`, `ItemAttachmentSlots`…)
    names one, so vanilla's mount does not place the NPC by a slot. A
    proxy mounted on a puppet on `grapple` is taken (`IsBeingGrappled`
    true) but stays where it stood, 3 to 3.5 m away. [2.5-verified]
  - **A slot cannot be added from script**: on the body being built
    (Codeware `Entity/Extract`), a slot appended to `Item_Attachment_Slot`,
    an `entFallbackSlot` (`grapple` → `Root`), an existing slot renamed
    `grapple`, and a new `SlotComponent` (its binding cloned from
    `Item_Attachment_Slot`) with `grapple` and `leftShoulder`, are each in
    the component's `slots` but `GetSlotTransform(n"grapple")` stays false:
    the name lookup is built with the template (`entSlotComponent`
    `unk140..0x1A0`). Left: a native that rebuilds it, or the slot in the
    template's resource (ArchiveXL). [2.5-verified]
  - **How `entSlotComponent` finds a slot** (2.50 exe, decompiled
    2026-11-22): `+0x150` is a `HashMap<CName, uint32>` (RED4ext's layout,
    0x30 bytes, hash = the name's high ^ low 32 bits, nodes of 0x18 then
    4-byte buckets in one allocation, its allocator at +0x28) from slot
    name to index in `slots`, built once from `slots` after the component
    is loaded (vtable +0x28, `0x1403F7810`); `+0x140` is a DynArray of one
    0x28-byte bone binding per slot, sized to `slots` when the component
    attaches to its skeleton (vtable +0x180, `0x140BEED68` →
    `0x140405B6C`; flag `+0x88 & 2`), emptied at its detach (`0x14020606C`).
    Every lookup by name goes through the map (`0x140202734`, ~60 callers:
    `GetSlotTransform` `0x140CA2200` → `0x140CA22E4`, the mounting
    facility's), then `0x14032A4D4` reads `slots[i]` and, once attached,
    binding i (out of bounds for a slot appended after the attach). So a
    slot added before the attach to both `slots` and the map is found and
    bound like the others: `TandemBody.AddSlot` (`plugin/src/engine/slots.cpp`,
    feature `body slots`). [2.5-verified: `GetSlotTransform(n"grapple")`
    true on the puppet after it, instance b]
  - **No NPC has a `grapple` slot either** (instance b, 2026-11-22): the
    donors' (Kerry's and Panam's templates) slot components are
    `fx_communities_slots`, `Item_Attachment_Slot` (Head, Chest, the
    weapon and item slots…), `UI_Slots`, `HitRepresentation`,
    `Breach_Attachment_Slots` and `jaw`, none with it. Vanilla's grapple
    mount names a slot nobody has: it only records the relation
    (`IsBeingGrappled`), the takedown's workspot places the NPC. A puppet's
    `grapple` slot would be tandem's own (tried: on `Root`, 0.6 m ahead,
    the fallback's place). [2.5-verified]
  - **A mount on the puppet's slot does not move the NPC** (instance b,
    takedowns-host): half a second after the mount, a real NPC grabbed from
    afar was still 2.4 m (plain request, slot in `Item_Attachment_Slot`),
    24.7 m (with `mountData`: instant, silent, `slotName`, as
    `TandemVehicles.Seat`) and 25.8 m (plain, slot in an `OccupantSlots`
    of the puppet's own) from its holder; `IsBeingGrappled` and the
    facility's record were right each time. The mounting facility branches
    on `mountData` (`0x14089BFC0`: without it the relation is recorded and
    told, `0x14089B6F0`); an attach to a parent's slot exists
    (`0x1408A6628`: the parent's component named `OccupantSlots`, hash
    0x59A85A5102735D07, slot index by the map, then a request queued on the
    child, `0x140751DB0`), reached from a vtable near
    `gamemountingMountingEvent`'s; what makes it run for a child is not
    known. V's `OccupantSlots` holds `leftShoulder` (the carry), and V's
    TPP template has none (its slot components: `fx_slots`,
    `Q203_Cutscene_Lighting`, `fx_communities_slots`,
    `Item_Attachment_Slot`, `UI_Slots`, `HitRepresentation`,
    `Breach_Attachment_Slots`). Tried once on instance b: a script `Clone`
    of the body's `Item_Attachment_Slot` named `OccupantSlots` with a new
    id (`TandemBody.RenameSlots`), its index built by `AddSlot` (logged
    `built`), added to the template. The mount then **did** move a proxy,
    to 7588 m from its holder (the copy's transform not bound to the
    skeleton), and the game thread was silent 5.1 s after that body came.
    Not shipped: `Body.reds` gives no slot yet, the grapple keeps the
    per-tick fallback. Next: an `OccupantSlots` bound as the template's own
    (its `parentTransform` binding to `root`, before the attach), or the
    component in the template's resource (ArchiveXL). [2.5-verified: the
    moves measured; the attach path: read in the exe only]
- **A thrown body leaves at `ThrowEvents.OnExit`**, not `OnEnter` (the
  kill, `ThrownNPC`, the pose): direction from the camera at the release,
  an aim assist turn, the impulse between `ragdollMaxImpulse` and
  `ragdollMinImpulse` by the camera's pitch, 30 when combat is not
  allowed; applied 0.1 s later (`CreateRagdollApplyImpulseEvent`).
  [2.5-untested]
- **A failed grapple, vanilla's way**: `TakedownGrappleFailedDecisions`
  (535796) enters when `ShouldInstantlyBreakFree` (530408: a massive target
  of too high a level); `GrappleBreakFreeDecisions.EnterCondition` (537741)
  is true after the hold time (4.5 s), on `GrappleFailed`, or in deep
  water; `GrappleBreakFreeEvents.OnEnter` (537827) clears the NPC's
  workspot and `Grappled`, `InjectThreat`s V and plays the break-free
  animation. tandem's refused grab makes that condition true.
  `TakedownReleasePreyEvents` (538977) ends a grapple without a takedown.
  [2.5-untested]
- **Finishers**: `FinisherAttackEvents.ApplyFinisher` (250093) plays
  `CreateEffectStatic(n"playFinisher", name, instigator)` on the target
  (`PlayFinisherGameEffect` 250132; the name from the weapon's item type or
  tags, `GetFinisherNameBasedOnWeapon` 250182), then queues a
  `Attacks.Finisher_Fake_Attack` hit with `DealNoDamage` and
  `hitFlag.FinisherTriggered` (250115) and `MarkForDeath`s the NPC. The
  offer: `NewPerkFinisherCondition` (514031), health below the attacker's
  `NewPerkFinisher*_TargetHealthMax` stat (`IsInFinisherHealthThreshold` 13152) or `BaseStatusEffect.FinisherActiveStatusEffect` (a broken guard,
  `NPCPoiseListener` 203306). The blunt "hold" finisher kills at once
  (`TriggerNewPerkFinisherBluntHold` 22433). [2.5-untested]
- **Carrying**: the body runs the `CarriedObject` state machine
  (`scriptInterface.owner` = the body, `executionOwner` = V).
  `CarriedObjectEvents.OnEnter` (526537) mounts it on V
  (`MountingRequest`, slot `leftShoulder`) and calls
  `NPCPuppet.MountingStartDisableComponents` (129544); the pose comes from
  the `Carry` (`AnimFeature_Carry`, `ECarryState` 10593) and `Mounting`
  features. States: `PickUpEvents` (531436), `DropEvents` (533304),
  `ReleaseEvents` (536937), `ThrowEvents` (534389: `Kill(V, true, true)`,
  `ThrownNPC`, ragdoll, an impulse along the camera, 100 by default),
  `DisposeEvents` (533904). A forced drop is the PSM parameter
  `forceDropBody` (`PSMPostponedParameterBool`, 135452). [2.5-untested]
- **Disposal**: `DisposalDevice.OnDisposeBody` (210049),
  `OnTakedownAndDisposeBody` (210105), `OnNonlethalTakedownAndDisposeBody`
  (210112) unmount the body from the **local** player, play the device
  and NPC workspots, then on `OnWorkspotFinished` (210236) kill it,
  `SetNPCDisposedFact` and hide it for good. [2.5-untested]
- **"The player" in host NPC code** (§2.1 of `coop-perks.md`):
  `NPCStatesComponent.OnCombat` (168307; second chance and gag for
  `GetPlayer`, 168333-168352), `NPCPoiseListener.OnStatPoolMinValueReached`
  (203306; finisher offer and sound for the local player; the hit's author
  is `NPCPuppet.GetLastHitInstigator` 128511),
  `ForceMoveInCombatWhistleEffector.Initialize/ActionOn` (317768, the local
  player's position), `UpdateWhistlePosition.Activate` (317836),
  `PlayerPuppet.OnDodgeToAvoidCombatEvent` (39682, reads the NPC's senses,
  which a proxy has off). Still "the player" and left as TODO:
  `ForceMoveInCombatEffector.StartMovement`'s facing target (317559),
  `ApplyLegendaryWhistleEffector` (317913), `NPCPuppet.OnQuickHackEffectApplied`
  (126593, quickhack alerts: the quickhack work), `SpreadInitEffector`
  (316185), `SetFriendlyEffector` (315998),
  `ScriptedPuppet.ProcessQuickHackQueueOnDefeat` (21698),
  `SenseComponent.ReevaluateDetectionOverwrite` (156337),
  `DamageSystem.IsImmune` (86146), `GorillaArmsOnePunchNPCMark` (126464),
  `DisposalDevice`'s local player (210051). [2.5-untested]

### 13.91 A V's movement noise, and the gates that ask for the player (2026-11-20, read in final.reds, not run)

- **What V gives out as it moves**, all through
  `StimBroadcasterComponent.TriggerSingleBroadcast(owner, type)` with no
  radius (the stimulus record's): `StandEvents.OnTick`, on the ground above
  `footstepStimuliSpeedThreshold` (2.5 by default): `FootStepRegular` (25)
  every tick, unless the stat `CanWalkSilently` ≥ 1 (`:550412`, `:281152`);
  `SprintEvents`: `FootStepSprint` (26) every 0.2 s unless `CanRunSilently`
  ≥ 1 (`:552267`, `:281136`), once entering a slide from a sprint
  (`:553249`) and 0.15 s into a dodge (`:553725`); landings
  (`BroadcastLandingStim`, `:289818`): `LandingRegular` (35), `LandingHard`
  (34), `LandingVeryHard` (36, the death landing too), unless
  `CanLandSilently` ≥ 1, the fall was under 1.2 m, or (regular) V lands
  crouching. `CrouchEvents` broadcasts nothing: a crouched V is silent.
  [2.5-untested]
- **How an NPC takes a stimulus**: `ReactionManagerComponent.HandleStimEvent`
  (`:143953`, queued as a script task, `PostPhysics`) →
  `ShouldEventBeProcessed` (active, a reaction stim, a preset other than
  `NoReaction`) → `ShouldStimBeProcessed` (`:144978`) → `ProcessStimParams`,
  `ProcessReactionOutput`. Two of its gates ask for the player:
  - tag `Stealth`: `ReactOnPlayerStealthStim(owner, source)` (`:148207`,
    static) reads the attitude only when `source.IsPlayer()` (or a vehicle:
    the local player's), then wants hostile, or neutral with a security
    system whose zone the source trespasses. For any other source the
    attitude is never read (it stays the enum's first value, friendly):
    false.
  - tag `PlayerOnly` (`:145046`): dropped unless the source is a device, a
    player, or `stimInvestigateData.attackInstigator` is a player.
    The stimulus records' tags and radii are TweakDB (`stims.*`), not in
    scripts: tandem logs them once a kind from the event
    (`StimuliEvent.IsTagInStimuli`, `radius`, `stimRecord.IsReactionStim()`).
    [2.5-untested]
- `attackInstigator` is read after the gates only by the ally-help paths
  (`StimFilters.CanTriggerAllyHelp` / `NeutralHelp` / `EnemyHostility`:
  gunshots, bullets, explosions, hits, calls, never footsteps or landings),
  `ShouldIgnoreCombatStim` (returns at once for a source that is not a
  player) and, later, by the reaction's behaviours (`:476979` turns the NPC
  hostile to it): so it must be gone before `ProcessReactionOutput`. The
  pipeline itself rewrites a stim event's fields in place
  (`stimEvent.sourceObject = …`, `:146325`).
- `StimBroadcasterComponentHelper.CreateStimEvent(owner, type, radius,
investigateData, propagationChange)` and `ProcessSingleStimuliBroadcast`
  are what `TriggerSingleBroadcast` runs (`:149948`): the same from any
  entity with or without the component.
