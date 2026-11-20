# Presence

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.35 Presence: a remote V seen and heard (2026-11-19, scripts and RTTI only)

What the game offers to make a remote V **seen and heard** on its puppet,
and a host's NPC on its proxy: audio, hit reactions, bullets, the HUD. The
design is in [`../../architecture/presence.md`](../../architecture/presence.md).

**Sources.** `final.reds:N` is line N of the decompiled 2.50 scripts
(`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`); `CW` is Codeware's
script imports (`~/.cache/tandem/ref/codeware/scripts`); `SDK` is
`vendor/RED4ext.SDK`; `RTTI` is `docs/research/data/rtti-*-2.50.tsv`. Read on
2026-11-19. Nothing was copied from CyberpunkMP or NCO.

**Tags**, as in `engine-knowledge.md`: [2.5-untested] = read in the 2.50
game's own scripts or data, compiled against them (`task scripts:check`),
not yet run in game; [to-hear] / [to-see] = built, and only a person in the
game can tell whether it sounds or looks right; [native] = decided inside
the engine, no script lever found. Nothing here is [2.5-verified] yet: the
game machine was off limits while this was written.

#### Playing a sound on an entity, in 3D

- `GameObject.PlaySoundEvent(self, eventName)` queues an `AudioEvent`
  (`entAudioEvent`: `eventName`, `emitterName`, `nameData`, `floatData`,
  `eventType`, `eventFlags`) on the entity (`final.reds:12131`). The audio
  system plays it from the entity's emitter: attenuation and occlusion are
  the event's own. `GameObject.PlaySound(self, name, opt emitter)` is the
  same for an entity with an id (`:12079`). `AudioSystem.Play(name, opt
entityID, opt emitter)` with no entity is 2D (`:149579`): never used
  here. [2.5-untested]
- Whether an event is positional is a property of the Wwise event, not of
  the call: an event authored for the player's own ears (`ui_*`,
  `w_feedback_*`, `time_dilation_focused_enter`) stays 2D even when queued
  on a puppet. Player-authored locomotion events (`lcm_*`) on another
  entity: [to-hear].
- Voice: `GameObject.PlayVoiceOver(self, voName, debugContext, opt delay,
opt answeringEntity, opt canPlayInVehicle)` queues `SoundPlayVo` on the
  entity (`:12047`); it returns silently for an entity mounted in a vehicle
  unless `canPlayInVehicle`. The line is resolved natively from the
  context name and the entity's voice (its Character record's `voiceTag`,
  `:19242`): no gender switch is set from scripts. [2.5-untested]
  - V's own contexts: `onPlayerHit` (`:42998`), `meleeAttack`
    (`:288665`), `meleeDeflect` (`:42725`), `regularLanding` (`:558026`),
    `hardLanding` (`:281030`), `veryhardLanding` (`:281044`, also the
    death landing), `climbStart` (`:529542`), `Vault` (`:556460`). No death
    context for V exists in scripts (its death cry is [native]).
  - NPC contexts: `hit_reaction_light` (`:468815`), `hit_reaction_heavy`
    (`:469696`), `vo_any_damage_hit` (`:23603`), `stlh_death` (`:168424`),
    `start_dead` (`:168426`), `battlecry_curse`, `grenade_throw`, `bump`.
  - **Open** [to-hear]: tandem's puppet records have `voiceTag = ""`
    (`tweaks/tandem/puppet.tweak`). Whether V's contexts resolve on such a
    body, and in whose voice, needs ears; `tools/game.sh cmd presence vo
NAME` plays any context on every puppet to find out. The likely fix,
    if silent, is V's own voice tag on the puppet records by body.
- A status effect's sounds are data: `StatusEffect_Record.SFX(out
[StatusEffectFX_Record])`, each with `Name()` and `ShouldReapply()`
  (`:18233`, `:203977`); the game plays them with `GameObject.PlaySound(
this, sfx.Name())` when the effect is applied and stops them when it is
  removed (`GameObject.StartStatusEffectSFX` / `StopStatusEffectSFX`,
  `:12274`, `:12303`). So the sounds of an effect can be played on another
  entity without applying the effect. [2.5-untested]

#### What makes each sound of a V

<!-- prettier-ignore -->
| Sound | Where it comes from in the game | Lever for a puppet | Tag |
| --- | --- | --- | --- |
| Footsteps | Animation: a clip's `animAnimEvent_FootPlant` raises `entFootPlantedEvent {customAction: CName, footSide: animEventSide}` on the entity (`RTTI`, `SDK`, `CW`); the footstep system (`gameFootstepSystem`, `audioFootstepsMetadata`) picks the sound from the surface, the footwear and the speed. No script | Queue `entFootPlantedEvent` on the puppet (constructible through `CW`) | [2.5-untested] [to-hear] |
| …seen by scripts as | `AudioEvent`s on the entity: `MinotaurMechComponent.OnAudioEvent` reads `enm_mech_minotaur_loco_fs_heavy` (`:292585`), the exoskeleton `lcm_npc_exo_*` (`:294097`) | `@addMethod(NPCPuppet) cb OnAudioEvent` tells whether a puppet's own clips step audibly | [2.5-untested] |
| Footsteps off / on | `GameObject.PlaySoundEvent(player, n"disableFootsteps" / n"enableFootsteps")` (`:44277`) | A kill switch if the puppet's own and ours ever double | [2.5-untested] |
| Jump | Animation only (`PushAnimationEvent(n"Jump")`, `:557420`); double, charged and hover jumps post `lcm_player_double_jump` (`:557507`, `:557584`, `:557740`) and the effect `charged_jump` | A foot plant; `lcm_player_double_jump` on the puppet | [to-hear] |
| Landing | Voice `regularLanding`; hard: effect `landing_hard` + voice `hardLanding`; very hard: `landing_very_hard` + `veryhardLanding`; death: `landing_death` (`:281021-281058`, `:558026`) | Two foot plants, the voice, the effect by name | [to-hear] [to-see] |
| Dash, slide | `lcm_dash` + effect `dash` (`:553664`), `lcm_dash_katana` (`:555501`); the end of a slide `lcm_fs_additional_slide_stop` (`:553426`); a plain dodge posts nothing | The same events on the puppet | [to-hear] |
| Climb, vault | Voices `climbStart` (`:529542`), `Vault` (`:556460`) | The voice | [to-hear] |
| Shot | [native]: `WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction, opt blackboard)` (`:54864`) and `AI_ShootAt` (`:54056`) are natives; an NPC's shot also pushes the anim event `Shoot` to owner and weapon (`:173344`). No script names a shot sound | `TriggerWeaponEffects(Shoot / SilencedShoot)` + `PushEventToReplicate(weapon, n"Shoot")`; the real shot of §4 for the bullet | [to-hear] |
| Automatic fire | `weapon.SetupBurstFireSound(n)` once per burst (`:54044`, `:172952`); the loop ends with `TriggerWeaponEffects(ExitShoot)` (`AIWeapon.OnStopShooting`, `:173118`) | `ExitShoot` 0.3 s after a puppet's last shot | [to-hear] |
| Reload | `TriggerWeaponEffects(EnterReload / ExitReload)` and the anim event `Reload` on the weapon (`:438283-438315`); no audio name in scripts | The same | [to-hear] |
| Melee swing | `weapon.AI_PlayMeleeAttackSound(isQuickMelee)` (native, `:54054`); V's effort is the voice `meleeAttack` | Both | [to-hear] |
| Melee impact | `TriggerWeaponEffects(weapon, MeleeHit, blackboard)` in the weapon's hit handler (`:55308`); blocks `MeleeBlock` (`:528203`); per-material impact audio is [native] (`gameaudioeventsHitEvent`) | `MeleeHit` on the puppet's weapon when its player's swing landed (`PlayerPuppet.OnMeleeHitEvent`, `:45746`, is the local hook) | [to-hear] |
| Draw | [native]: `AudioNotifyItemEquippedEvent {itemName}` (`:316373`) is never built in scripts; the engine sends it when an item enters a slot | Nothing to add if `AddItemToSlot` on the puppet triggers it | [to-hear] |
| Holster | `AudioNotifyItemUnequippedEvent {itemName = record.EntityName()}` queued on the owner (`:59820`) | The same event on the puppet | [2.5-untested] [to-hear] |
| Sandevistan, Kerenzikov | No audio is posted from scripts: the status effects `BaseStatusEffect.SandevistanPlayerBuff` (`:494085`) and `KerenzikovPlayerBuff` (`:525574`) carry their `SFX` records | Their `SFX` names played on the puppet (§1) | [2.5-untested] [to-hear] |
| Mantis blades, arms | Items like any weapon: equip is the engine's notification, swings `AI_PlayMeleeAttackSound`; only relic-perk charges are script-posted (`w_cyb_mantis_spy_perk_charged`, `:55179`…) | Held + Shot as for any weapon | [to-hear] |
| Pain | `PlayerPuppet.OnHitSounds`: voice `onPlayerHit` and `w_feedback_player_damage` (the hit's own 2D feedback) (`:42952`) | The voice only | [to-hear] |
| Flashlight | None: vanilla V has only debug hooks for components its entity does not have (`OnTogglePlayerFlashlightEvent`, `:43071`, behind `!IsFinal()`). No weapon light exists | Nothing to sync | [2.5-untested] |

#### Hit reactions

- The chain: `NPCPuppet.OnHitAnimation` (`:128385`) → `HitReactionRequest`
  → `HitReactionComponent.EvaluateHit` (`:184601`) → `GetReactionType`
  (`:186654`) → `StoreHitData` fills `m_animHitReaction`
  (`AnimFeature_HitReactionsData`, `:187899`: `hitDirection`,
  `hitIntensity`, `hitType`, `hitBodyPart`, `npcMovementSpeed`,
  `npcMovementDirection`, `stance`, `animVariation`, `hitSource` as Int32;
  `useInitialRotation`, `hitDirectionWs: Vector4`, `angleToAttack`,
  `initialRotationDuration`). [2.5-untested]
- **Only a twitch is the component's own**:
  `SendTwitchDataToAnimationGraph()` (`:187759`) applies the feature under
  `n"hit"` and pushes the event `n"hit"`. Every other reaction is the
  **AI's**: `SendDataToAIBehavior` only stores a stim name; the behaviour
  tree runs a task (`AIHitReactionTask`, `:459454`, with `Impact`,
  `Stagger`, `Knockdown`, `Pain`, `Guardbreak` subclasses) whose
  `SendDataToAnimationGraph` (`:459725`) launches the native animation
  action `ActionHitReactionScriptProxy` (`Stop()`, `Setup(data, opt
fastForward)`, `Launch()`; the component's
  `GetHitReactionProxyAction()`, `:184012`) and applies the feature; block
  and parry are the feature plus the events `PlayBlock` / `PlayParry`.
  **A brain-off proxy therefore plays no reaction beyond a twitch.**
- Death: `OnDeath` → the tree's `AIDeathReactionsTask` (`:457921`):
  `PlayHitReactionAction` (`:458028`, the same action with `hitType` 7) or
  `TurnOnRagdoll` (`:458007`). `NPCPuppet.Kill` (`:125843`) only zeroes
  health: **a brain-off proxy that is killed plays no death animation**
  (this was the open "hit reactions and ragdolls" of NPC animation).
- Enums: `animHitReactionType` (`:565`) None 0, Twitch 1, Impact 2,
  Stagger 3, Pain 4, Knockdown 5, Ragdoll 6, Death 7, Block 8, GuardBreak
  9, Parry 10, Bump 11; `EAIHitDirection` Left 0, Back 1, Right 2, Front 3;
  `EAIHitIntensity` Light 0, Medium 1, Heavy 2, Explosion 3;
  `EAIHitBodyPart` Head 1 … RightLeg 6; `EAIHitSource` Ranged 0,
  MeleeSharp 1, MeleeBlunt 2, QuickMelee 3; `stance` 0 standing, 1
  crouching.
- Ragdoll: `QueueEvent(CreateForceRagdollEvent(reason))` (`:188095`), an
  impulse 0.1 s later with `CreateRagdollApplyImpulseEvent(position,
impulse, radius)` (`:188128`), `CreateDisableRagdollEvent` (`:216196`);
  the state arrives as `NPCPuppet.OnRagdollEnabledEvent` /
  `OnRagdollDisabledEvent` (`:127397`, `:128004`), `IsRagdolling()`
  (`:128184`). The component gives `GetOverridenHitDirection()` and
  `GetRagdollImpulse()` (`:184074`, `:184088`).
- The action extracts motion (`HitReactionStopMotionExtraction`,
  `:376696`): on a proxy the entity is moved by tandem's controller, so the
  open point is whether the two fight for a frame. [to-see]
- Whether `Launch()` plays with the AI component off cannot be read from
  scripts. [to-see] (`tools/game.sh cmd presence react TYPE` plays any
  reaction on every proxy.)
- V itself: `PlayerPuppet.OnHitAnimation` (`:42755`) pushes
  `AnimFeature_PlayerHitReactionData` under `HitReactionData` and the event
  `Hit` (a camera shake and a first-person flinch); staggers and
  knockdowns are status effects driving its state machine (`Knockdown`,
  `Felled` in `LocomotionDetailed`). What V's body does is in its pose
  stream; a V has no hit reaction component (so no wounds: Outcomes.reds
  keeps a puppet's limbs).

#### Bullets: tracer, impact, decal

- No script draws a tracer, an impact or a decal. An NPC's shot is one
  native call, `weapon.AI_ShootAt(targetPositionProvider, targetObject,
instigator, ammoCost, projectileParams, projectilesPerShot, charge, opt
maxSpread, opt muzzleOffset)` (`:54056`), after
  `AIWeapon.SetAttackBasedOnTimeDilation(owner, weapon, opt attack)`
  (`:173366`) picks the weapon's NPC attack; the ray, the tracer
  (`gameEffectPostAction_BulletTrace`), the impact and its decal
  (`gameEffectExecutor_BulletImpact`) and their sounds belong to the
  attack's game effect. Which bullets carry a tracer is not in scripts
  (stats `WeaponVFX_TracerFxScale`, `WeaponVFX_DecalFxScale`). [native]
- Pieces exist to do it by hand (`WeaponItem_Record.FxPackage()`:
  `NPC_vfx_hitscan_trail()`, per-material `Vfx_impact_*()` with
  `Impact_decal()`; `FxSystem.SpawnEffect(FxResource, WorldTransform)`,
  `FxInstance.UpdateTargetPosition`), but decals have no spawn function and
  the material under the impact would have to be traced. Not used.
- No attack flag makes a bullet "visual only". `hitFlag.DealNoDamage` (16)
  still runs the pipeline; `DisableVFX` (4) is never read by scripts.
- **So a remote shot's bullet is a real NPC shot from the body's weapon at
  the point the shooter's game says it ended**, with no ammunition, the Vs
  present ignored by its collision (`gameprojectileWeaponParams.
AddObjectToIgnoreCollisionWith`), and its hits dropped where every hit
  enters (`DamageSystem.ProcessPipeline`): the shooter's game decided what
  it hit. Shots.reds fires a host NPC's shot at a guest's own V the same
  way, there to decide the hit. [2.5-untested] [to-see]
- A native struct local is not cleared by the script VM
  (`gameprojectileWeaponParams`, `:173689`): set every field the shot
  reads (`targetPosition`, `smartGunSpreadOnHitPlane`, `charge`,
  `smartGunAccuracy`, `smartGunIsProjectileGuided`, `hitPlaneOffset`,
  `shootingOffset`, `ignoreWeaponOwnerCollision`, `range`,
  `ignoreMountedVehicleCollision`).
- Where the local V's shot ended: `TargetingSystem.GetLookAtPosition(
instigator, opt withLOS, opt ignoreTransparent) -> Vector4` and
  `GetLookAtObject` (`:108386`, `:108382`), the game's own crosshair
  query. Zero when nothing is there. [2.5-untested]

#### HUD

- Vanilla HUD bars are bound through the `.inkhud`: scripts rarely name
  their `.inkwidget`. The companion bar is the Flathead's only
  (`CompanionHealthBarGameController`, `:441305`); the boss bar
  (`BossHealthBarGameController`, `:190914`) shows a name and
  `NameplateBarLogicController.SetNameplateBarProgress(value / 100)`. The
  cut multiplayer's player list (`gameuiPlayerListGameController`,
  `:438964`) and HUD root (`CpoHudRootGameController`, `:438343`) point at
  `multi\gameplay\gui\widgets\…` resources that are assumed not to ship.
- A script-built widget on the HUD: `GameInstance.GetInkSystem().GetLayer(
n"inkHUDLayer").GetVirtualWindow()` (`CW`: `inkSystem`,
  `inkLayerWrapper`) and `widget.Reparent(window)`. The HUD's space is
  3840 × 2160 whatever the screen. A widget covering it must never be
  interactive. [2.5-untested]
- The native look from script: font
  `base\gameplay\gui\fonts\raj\raj.inkfontfamily` (`SetFontFamily`,
  `SetFontStyle(n"Medium" | n"Regular" | n"Semi-Bold")`), colours through
  `SetStyle(r"base\gameplay\gui\common\main_colors.inkstyle")` +
  `BindProperty(n"tintColor", n"MainColors.Red" | n"MainColors.Blue" …)`
  (names seen in scripts: Red, Blue, PanelRed, PanelDarkRed, SupBlue,
  MildRed, ActiveBlue, Purple, Grey, DarkRed, ActiveYellow, ActiveRed,
  ActiveGreen). No arrow or skull atlas part is named in scripts, Codeware
  or the modding docs: the arrow is drawn from two `inkRectangle`s.
- Projected widgets are methods of `inkProjectedHUDGameController`
  (`RegisterScreenProjection`, `:266527`), protected: a system cannot
  register one. The vanilla nameplate shows one entity, the one aimed at
  (`UI_NPCNextToTheCrosshair.NameplateData`), within a range, never all
  the nearby ones: an always-on nameplate is our own widget.
- The camera for our own projection: `CameraSystem.
GetActiveCameraWorldTransform(out Transform)`, `GetActiveCameraForward()`,
  `GetActiveCameraRight()`, `GetActiveCameraFOV()` (`:174219-174229`);
  `ProjectPoint(Vector4) -> Vector4` (`:174233`) has no script caller, so
  its convention is unknown. **Open**: whether `GetActiveCameraFOV` is the
  horizontal or the vertical angle (read as horizontal; `tools/game.sh
cmd presence hud camera` logs the engine's projection next to both
  readings, `presence hud fov v` switches). [to-see]
- Hiding: photo mode `GameInstance.GetPhotoModeSystem(game).
IsPhotoModeActive()` (`:53351`) and the blackboard
  `PhotoMode.IsActive` (`:248383`); cinematics the player state machine's
  `SceneTier` (GameplayTier 1..5, `:3104`; 3 and up take the controls);
  menus `UI_System.IsInMenu` (`:247977`); braindances `Braindance.IsActive`
  (`:48378`). Whether the HUD layer is hidden whole in photo mode is not
  in scripts (no HUD controller listens to it): hidden explicitly.
  [2.5-untested]
- A blackboard listener fires while a menu pauses the world; a
  `DelaySystem` tick does not. The menu flag of Presence is sent from the
  listener.
- V's health for display: `StatPoolsSystem.GetStatPoolValue(id,
gamedataStatPoolType.Health, true)` is a percentage 0..100 (`:83547`,
  as the boss bar divides it by 100, `:190946`).

### 13.93 Swimming's effects and sounds, a projectile put on a path (2026-11-20, read in final.reds, not run)

**Swimming.** V's swimming is a state machine of its own
(`LocomotionSwimming*`, PSM `Swimming`: 0 none, 1 surface, 2 diving, 3
climbing out; `PlayerState::flags` carries it). What each state plays, all
on V's entity (`final.reds:N`):

<!-- prettier-ignore -->
| Moment | What the scripts start | Tag |
| --- | --- | --- |
| on the surface (`SwimmingSurfaceEvents.OnEnter` / `OnExit`) | effect `swimming_surface`, started and stopped (`:524022`, `:524033`); status effect `BaseStatusEffect.WaterCollision` | [2.5-untested] |
| a stroke on the surface | effect `swimming_surface_stroke` every `timeBetweenSurfaceSwimmingStroke` while swimming forward (`:523953`); fast: `swimming_surface_stroke_fast` every `timeBetweenSwimmingStroke` (`:526468`) | [2.5-untested] |
| going under (`SwimmingTransitionEvents.OnEnter`) | sound `hold_breath` (a `SoundPlayEvent` on V, `:245499`), effect `swimming_enter_dive`, a rumble (`:527582`) | [2.5-untested] |
| under (`SwimmingDivingEvents`) | effect `swimming_diving`, started and stopped (`:528133`, `:528148`); strokes `swimming_diving_stroke` every `timeBetweenDivingStroke` (`:528114`), fast `swimming_diving_stroke_fast` (`:528639`) | [2.5-untested] |
| back up (`SwimmingSurfaceEvents.OnEnterFromDiving`) | sound `surface_from_dive` (`:524001`) | [2.5-untested] |
| leaving the water (`LocomotionSwimmingEvents.ExitCleanup`) | both loop effects stopped (`:522159`) | [2.5-untested] |

- The stroke intervals are TweakDB parameters of the states (script
  default 1.0 s each): not readable outside the state machine. tandem
  strokes once a second while the puppet moves, every 0.6 s above 3.2 m/s
  (`presence::Observer`): by eye against the pose. [to-hear]
- No script names a splash for V entering water: it is in the effects, or
  native. A body in water has script-named world sounds: an NPC's ragdoll
  plays `w_melee_water_medium_splash` for its torso and `w_bul_hit_water`
  for a limb (`NPCPuppet.OnRagdollBodyPartWaterImpactEvent`, `:128235`),
  `gmp_ragdoll_floor_splash` (`:127710`); a grenade `w_bul_hit_water_heavy`
  (`:175829`).
- **Open** [to-hear] [to-see]: the effects are named on V's own entity
  (`player_*_fpp.ent`); whether the puppet's body
  (`player_*_tpp_cutscene.ent`) carries them, and whether they hold the
  swimming sounds, is not in scripts. tandem plays both layers, the
  effects and the NPC splashes (`presence swim both|fx|sound` picks one).

**A projectile on a path.** `ProjectileComponent` (`:177321`):
`ClearTrajectories()`, `AddParabolic(ParabolicTrajectoryParams)`,
`AddLinear`, `AddFollowCurve`, `SetDesiredTransform(Transform)`,
`SetOnCollisionAction`, `SetEnergyLossFactor` (a grenade's bounciness, set
once in `OnProjectileInitialize`, `:175625`).
`ParabolicTrajectoryParams.GetAccelVelParabolicParams(accel, vel, opt
zVelocityClamp)` has a speed and no direction: the direction is the
projectile's orientation (`DropToFloor` falls with 0.0001, `:177051`).
`SetDesiredTransform` is how a homing miss turns a round before its next
trajectory (`:538565`). So a grenade copy is put on the real one's path by:
`ClearTrajectories`, a teleport to the point facing the velocity,
`SetDesiredTransform` of the same, `AddParabolic` with the grenade's gravity
(`DeliveryMethod().AccelerationZ()`) and the speed. Whether the trajectory
starts from the desired transform or from where the entity was that frame
is not in scripts. [2.5-untested] [to-see] A grenade's bounces arrive as
`BaseGrenade.OnCollision(gameprojectileHitEvent)` (`:175816`), a per-entity
callback (not hooked: the path is sampled on the tick).
