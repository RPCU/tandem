# Combat, NPC damage and quickhacks

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 8. Combat / NPC / damage

- **Detect, don't simulate**: wrap `ScriptedPuppet.OnHit(gameHitEvent)`, always chain
  `wrappedMethod`; report only when the target is one of our puppets **and** the instigator is the local
  player (else every observer reports the same shot) (`code/assets/redscript/Combat.reds:29-168`).
  Damage = `evt.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health)` (armour/resists
  already applied), type `GetDominatingDamageType()`, hit zone = `hitRepresentationResult.hitShapes[0]
.hitShapeName` (CName; vocabulary only readable from live hits — the `gameHitShapeBVH` is not in the
  .ent), weapon `attackData.GetWeapon().GetWeaponRecord().GetID()` (GetWeaponRecord is on WeaponObject,
  not AttackData). Compile-checked on 2.31. [2.31; script API, likely fine on 2.5]
- The hit hook runs on job threads → no ECS/world queries in it (§2 threading).
- Server owns health, magazine, RAM; client sends what the engine computed; server bounds it.
  **Quickhack damage must be 0 in the server rule table** — it already flows through the hit pipeline
  (double-count bug v0.3.104). Quickhack cost (`GetCost()`) includes perks and a random modifier
  (`MAP:2403`).
- Quickhack effects: wrap `OnStatusEffectApplied(ApplyStatusEffectEvent)`, report when
  `instigatorEntityID == player`; victim applies via `StatusEffectHelper.ApplyStatusEffect(player,
tdbid)`; upload telegraph via `OnUploadProgressStateChanged` filtered to
  `EProgressBarContext.QuickHack`/`UPLOAD`, replaying `AIQuickHackStatusEffect.BeingHacked`
  (`Combat.reds:180-340`). Quickhack id from `ScriptableDeviceAction.GetObjectActionID()`
  (`MAP:759-773`). Base vs leveled record ids on the wire: unproven.
- **No flatline**: `GodModeSystem.AddGodMode(player, gameGodModeType.Immortal, n"<source>")` — engine-level,
  damage still lands, dead state never entered; god mode is ref-counted per source CName; remove on
  disconnect. Script-level `OnDeath` wraps and stat-pool floors were bypassed by scripted kills,
  falls, drowning (`code/assets/redscript/Death.reds:52-80`). Revive grace = separate source with
  `Invulnerable` (no TTL; revoke by delayed callback) (`Death.reds:100-133`).
- Downed = `StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.Defeated")`, deliberately
  without `BlockAllMenu` (`Combat.reds:660-701`).
- Inbound effects drained by a 0.25 s `DelayCallback` chain armed per body (network thread → native
  queue → script poll) (`Combat.reds:229-309`). `DelayCallback(cb, t, false)` = real time, ticks while
  paused (`MAP:719-728`).
- **Time dilation must be blocked** (weapon wheel, phone, Sandevistan, Kerenzikov): wrap
  `TimeDilationHelper.SetTimeDilation/SetTimeDilationOnPlayer`; `IsMultiplayer()` gates 17 other files
  (damage etc.) so don't flip it. Suspected in a passenger + weapon-wheel crash
  (`code/assets/redscript/TimeDilation.reds`).
- Difficulty pinned to Very Hard for everyone, read by name not index (`MAP:2402`).
- Server combat (`4e16bb91`, `48d9d109`): any living player to any living target within 250 m, damage
  clamped to 100 per hit; apply server health by SET, not subtract, only on the target's machine
  (`4d8c279d`); LifeState 0 alive / 1 downed / 2 dead / 3 reviving; bleedout is a `DownedAt` deadline;
  stabilise 8 s, revive 12 s, revived at 15 %. Ammo: client polls `GetActiveWeapon` +
  `GetMagazineAmmoCount` (Uint32) — there is no script "weapon fired" event; reload ≥500 ms
  (`88d0a7f3`). Quickhack cooldown: `ObjectAction_Record.Cooldown()` → `Cooldown_Record{Duration(),
Modifiable()}` (`506ac5f1`).
- Clear wanted level on revive: `PreventionBlinkingStatusRequest` with telemetry string
  `'ResetOnPlayerChoice'` → Heat_0, police despawned (`f76877ea`).
- Scanner name: hook `CompileScannerChunks` (GetDisplayName is a native import, can't wrap)
  (`392b28f2`).
- Inbound-effect poll must be armed per body on player attach, not in OnConnected (menu connect means no
  body yet) (`92230cf6`). [2.5-verified]
- Death loop: immortality blocks death not damage → naked level-1 respawned into a fight gets farmed in
  20-30 s loops; add an Invulnerable window post-revive (`MAP:1379-1386`).
- No world-object sync at all (doors, containers, elevators) (`MAP:391-393`).

**CAMP's PvE model** (`CampaignCombat.reds`, `CampaignCombat.cs`, `3fc8462`, `7c6570f`; 2.31, untested)

- Every game has its own copy of each enemy (spawn nodes mirrored) with its own AI; only health and death
  sync. Host every 0.5 s (full list every 3 s, NPCs within 150 m of any player): `key;static;record;x;y;z;
health‰;state;hostile` — key `EntityID.ToHash`, `EntityID.IsStatic`, record `TDBID.ToNumber`, health
  `GetStatPoolValue(id, Health, perc=true)×10`, state alive / `IsDead()` / `IsDefeated`. Registry from
  wrapped `NPCPuppet.OnGameAttached/OnDetach` (skip crowd and our puppets).
- Guest binding: **static EntityIDs are identical in every game** (placed/community NPCs); else nearest
  unbound same-record alive NPC ≤12 m; hostile unmatched ≤80 m for ≥5 s → stand-in spawned, removed after
  15 s unreported. Health goes down immediately, up only 3 s after the guest's own hit; copies >30 m off
  are teleported. Host death → `Kill(localV)`, defeat → `SoftKill`.
- Guest hits: wrap `NPCPuppet.OnHit`, instigator == local V, damage as ‰ of max health (levels differ),
  batched every 0.5 s → host `RequestChangingStatPoolValue(-damage)` or `Kill`.
- Players: with ≥2 in world `GodMode Immortal`; ≤1 % health → downed (Invulnerable + NoWeapons/ForceCrouch/
  NoJump/NoMovement); others see `BaseStatusEffect.FollowerDefeatedStory` on the puppet; revive = stand
  ≤2.5 m for 4 s → 35 %; 45 s bleed-out.
- Money is an inventory item (`MarketSystem.Money()` ItemID; vendors move it client-side,
  vendor.script:1180); 17 vanilla paths change it; inventory is `(TweakDBID, qty)` so a restore loses
  mods/tier/attachments (`NEW-SERVER-AUTHORITY-HANDOFF.md:21-102`). Reconcile by difference, never add
  totals; protect `RightArm`, `LeftArm`, `BaseFists` slots.

### 13.10 Damage to an NPC another game owns (2026-11-18)

- `DamageSystem.DealDamages(hitEvent)` is where a hit takes health away:
  `StatPoolsManager.ApplyDamage(hitEvent, forReal, out lost)` with
  `forReal = !IsClient()`. With `forReal` false it measures the loss (the
  `SDamageDealt` list, per stat pool) without applying it; vanilla's own
  client path. Wrapping it (`@wrapMethod`, private is fine) catches every
  ordinary hit after armour, mitigation and the ±10 % variance: 40
  physical became 9.65 on a ganger proxy. [2.5-verified]
- Hits with `hitFlag.Kill` never reach `DealDamages`:
  `ProcessInstantKill` sets `DealNoDamage` and calls
  `RequestSettingStatPoolMinValue(target, Health, instigator)`, so they
  need their own wrap. [2.5-verified]
- A scripted hit for tests: a `gameHitEvent` with a new `AttackData`
  (`SetSource`, `SetInstigator`, `SetAttackDefinition(IAttack.Create(ctx))`
  with `ctx.record = TweakDBInterface.GetAttackRecord(t"Attacks.VehicleImpact")`),
  `attackComputed.SetAttackValue(N, Physical)` on a new
  `gameAttackComputed` (kept by the pipeline), queued with
  `DamageSystem.QueueHitEvent(hit, target)`. [2.5-verified]
- On the host, `RequestChangingStatPoolValue(id, Health, -damage,
instigator, false, false)` kills an NPC at 0 like any death: the next
  `NpcStates` round sees `IsDead()`. No hit reaction plays (no hit event
  reached it). [2.5-verified]
- `ScriptedPuppet.Kill(instigator, skipAnim, noRagdoll)` sets health with
  `RequestSettingStatPoolValueIgnoreChangeMode`, outside the damage
  pipeline: safe on a proxy from inside a damage hook. [2.5-tested]
- Status effects a hit carries (`AttackData.GetStatusEffects()`, the
  attack record's `ApplyImmediately` effects) are queued by
  `DamageSystem.ProcessStatusEffects` and applied **a frame later**: they
  are not in `StatusEffectHelper.GetAppliedEffects` right after the call.
  The target's `NPCPuppet.OnStatusEffectApplied(ApplyStatusEffectEvent)`
  sees each one (`isNewApplication`, `instigatorEntityID`, `staticData`),
  whatever applied it (hits, buildup, quickhacks). [2.5-verified]
- A status effect's damage ticks are DoT hits (`AttackData.IsDoT`, attack
  type 2) through the whole pipeline, with the effect's instigator as
  instigator and source. Applied with no instigator
  (`ApplyStatusEffect(target, id, EntityID())`), the ticks name the
  target itself. Burning on a ganger: about 5 health a tick, ~20 ticks.
  [2.5-verified]
- `AttackData.AddStatusEffect(id, stacks)` on a scripted hit applies
  that effect through the normal rules (immunity checks): a test hit can
  set an NPC on fire. [2.5-verified]

### 13.11 Host NPCs against a guest's puppet (2026-11-18, first look)

`tools/game.sh cmd npc hostile [puppets]` spawns a Tyger Claws ranged
ganger 12 m ahead with its brain on, made hostile to the puppets (and to V
unless `puppets`), and reports every 5 s; `puppet group NAME` sets the
puppets' attitude group. With the mock player in front mode:

- The puppet's attitude group is `panam` (inherited from its Panam-based
  record). `AttitudeAgent.SetAttitudeTowards(puppetAgent, AIA_Hostile)`
  reads back as hostile (`GameObject.GetAttitudeBetween` = 2). In the
  `player` group, hostility to the puppet made the NPC hostile to V as
  well (it shot V although set neutral to it): never put puppets in the
  `player` group. [2.5-verified] Puppets now get groups of their own
  (§13.17).
- **The NPC's senses never detect the puppet**
  (`SenseComponent.GetDetection(puppetId)` stays 0 at 3–12 m in plain
  view), and `TargetTrackingExtension.InjectThreat(npc, puppet)` puts it
  in combat (high-level state 2) without adding a threat
  (`TargetTrackerComponent.GetThreats(false)` empty). It then attacks
  (seen in game: it closes in and swings at the puppet) but **no hit on
  the puppet ever reaches the damage system**, not even with the puppet's
  invulnerability check skipped (`InvulnerabilityCheck` wrap, needed
  anyway: the puppet record is invulnerable, so every hit on it would end
  as "no damage" before `DealDamages`). And it still hurts V, even set
  neutral to V. Likely cause: the puppet's entity (V's TPP cutscene
  template) lacks what NPC senses and hits need (visible object,
  targeting, hit representation shapes). Next: compare the components of a puppet and of an NPC
  (`tools/game.sh cmd inspect`), then spawn puppets from a template that
  has them, or add them at assembly (Codeware). [2.5-verified]
- Injected into combat while hostile to V, the ganger shot V: ranged
  hits of type 10, 29.6 physical each. [2.5-verified]
- `GodModeSystem.AddGodMode(V, Invulnerable|Immortal, n"Default")`
  returns true but `HasGodMode` is false right after and V still died:
  script god mode does not protect V this way. Tests that fight must not
  depend on it. [2.5-verified]
- V's death screen ("Flatlined") stops the game thread: the watchdog
  reports it as silent (no crash frames in `tools/game.sh bt`). [2.5-verified]
- **Fixed: the puppet is seen.** A puppet built from V's TPP cutscene
  template lacks the components NPC senses work with. Grafting the donor
  NPC's `senseVisibleObjectComponent` **and** `senseComponent` into the
  puppet as it is built (Codeware `Entity/Extract` callback,
  `EntityBuilderTemplateWrapper.AddComponent(component.Clone())`) makes a
  hostile NPC detect it (`GetDetection` 100, `IsAgentVisible` true when in
  sight) and keep it as its `CombatTarget`. The visible object alone was
  not enough (an NPC is seen through its senses' own visible object; its
  separate `VisibleObject` component is the dead-body one, off while it
  lives). [2.5-verified]
- **Fixed: the puppet is hit.** NPCs have an `entColliderComponent`
  named `HitPhysicalQueryMesh`, the shape bullets and blows are tested
  against; V's TPP template has none, so the ganger's shots and swings
  passed through. Grafted from the donor too, its shots land: hits of
  type 10, 31.9 physical each, reach `DamageSystem.ProcessPipeline` for
  the puppet (measured, never applied there). [2.5-verified]
- Vanilla's inject-combat-target pattern: `GameObject.ChangeAttitudeToHostile`,
  `TargetTrackingExtension.InjectThreat`, then `SetThreatPersistence`
  with `AIThreatPersistenceSource.CommandInjectCombatTarget` (final.reds
  169949). `GetThreats(false)` still read 0 while the NPC fought the
  puppet: not a usable measure of who it fights; its `CombatTarget`
  behaviour argument is. [2.5-verified]

### 13.12 Splitting a hit between the attacker's game and the target's (2026-11-18)

Read in the 2.50 scripts (`final.reds`), for NPC hits on a guest's puppet
(the host runs the attacker's side, the guest's game the target's) and
guests' skill XP. None of it run in game yet.

- The pipeline (`ProcessPipeline` 84290): `PreProcess` 84558
  (`ConvertDPSToHitDamage`, `CalculateDamageVariants`, quick exit,
  invulnerability, immortality, death check, `ModifyHitData`), then
  `Process` 84914 (returns at once on `DealNoDamage`, else the modifier
  stages and `DealDamages` 85771), `ProcessHitReaction`, `PostProcess`
  85801 (`ProcessStatusEffects` 85883). [2.5-untested]
- Attacker's side, for an NPC instigator: `ConvertDPSToHitDamage` 84575
  (divides by the weapon's `ProjectilesPerShot`; needs a weapon),
  `CalculateSourceModifiers` 86219 (bonus vs. target type, charge, pierce,
  ricochet, crit, damage multipliers, cyberware, stealth, spreading),
  `ProcessEffectiveRange` 86554 (weapon range falloff, explosion distance).
  `CalculateDamageVariants`, `ProcessLocalizedDamage`, `ProcessCriticalHit`
  and `ProcessQuickHackModifiers` only act for a player instigator.
  [2.5-untested]
- Target's side: `ProcessDamageReduction` 84618 (resistances per damage
  type, explosion/melee/DoT/boss/mech resistances), `ProcessDodge` 84781
  (`IsDodging` stat → no damage), `ProcessEvasion` 84795, `ProcessMitigation`
  84808, `ProcessPlayerIncomingDamageMultiplier` 84831 (target a
  `PlayerPuppet`), `ProcessPlayerFixedPercentageOverride` 85107 and
  `ProcessGrenadeExplosionDamageToPlayer` 85250 (difficulty),
  `CalculateTargetModifiers` 86213 (damage reductions, `ProcessArmor` 86621,
  on-vehicle mitigation), `ProcessBlockAndDeflect` 87114,
  `ProcessOneShotProtection` 85069 (per-hit cap by difficulty), and in
  `StatPoolsManager` overshield, armour pool, `CannotKillPlayer`, god-mode
  immortality and minimum health (`ApplyDamageSingle` 219067,
  `DrainStatPool` 219283). [2.5-untested]
- **No flag skips the attacker's side.** `DeterministicDamage` is read only
  by `ProcessCriticalHit` (86769), which already returns for a player
  target; `CannotModifyDamage` by `ProcessCriticalHit`,
  `ProcessOnVehicleMitigation` (86704: a target-side stage, wrongly skipped
  for a forwarded hit) and the no-op `ClearDamage` (52652). So tandem wraps
  the stages instead: on the host the target's stages return for a hit on a
  puppet; on the guest the three attacker stages return for a forwarded
  hit (`scripts/tandem/Combat.reds`). [2.5-untested]
- `ProcessArmor`, `ProcessOneShotProtection`, `ConvertDPSToHitDamage` and
  the melee block all need `attackData.GetWeapon()`; armour penetration
  comes from `GetArmorPenetrationValue(weapon, hitEvent)` 87006 (protected,
  wrappable). A forwarded hit carries the host's value. [2.5-untested]
- `CheckForQuickExit` 86416 drops a hit with no source, and turns a hit
  from an attacker friendly to the target into no damage unless the hit has
  `hitFlag.FriendlyFire` (86453; the flag is read nowhere else, and only
  removed for player instigators, 86413). [2.5-untested]
- The attack record's status effects are applied by `ProcessStatusEffects`
  from `GetAttackDefinition().GetRecord().StatusEffects()` (85930), and the
  target's application-rate rolls (`ProcessStatusEffectApplicationStats` 85983) use the instigator's stats: a hit rebuilt from the same record and
  the proxy as instigator brings them along. [2.5-untested]
- Skill XP: `DrainStatPool` (219283-219327) calls
  `RPGManager.AwardExperienceFromDamage(hitEvent, percentDrained)` 36969;
  it requires an active, XP-awarding (`AwardsExperience`: not crowd, not
  police) puppet target, a player instigator, and reads the weapon's item
  type for the skill. The request is queued per target entity and paid by
  `ProcessQueuedCombatExperience(entity)` (80726) when the player gets
  `TargetNeutraliziedEvent` (45336), sent by `ScriptedPuppet.RewardKiller`
  (21851) only to a `PlayerPuppet` killer; `FindAndRewardKiller` (12550)
  uses the damage history, else the `Kill` instigator. No other kill XP is
  given in scripts. [2.5-untested]

### 13.14 Quickhacks between games (2026-11-18, scripts only)

Read in `final.reds`; none of it seen in a session yet.

- An NPC's upload is a `QuickHackUpload` stat pool **on the target**
  (`ScriptableDeviceAction.StartUpload` 28078, `QuickHackUploadListener`
  194694): its `OnStatPoolAdded` queues `UploadProgramProgressEvent`
  (`STARTED`, `EProgressBarType.UPLOAD`, `duration` = the action's
  activation time) on the target, and at 100 % `CompleteAction` then
  `COMPLETED`. `ScriptedPuppet.OnUploadProgressStateChanged` (24388) puts
  `AIQuickHackStatusEffect.BeingHacked` (and `BeingHackedWithTraceableHack`
  when `QuickHackableHelper.WillHackRevealPlayer`, 123133) on the target at
  `STARTED` and removes both at `COMPLETED`. One pool per target: two
  uploads by the vanilla path on one NPC would share it. [2.5-untested]
- The queue (`QuickHackableQueueHelper.PutActionInQuickhackQueue` 122261)
  and cost skipping (`CanSkipPayCost` 26943) work only for a `PlayerPuppet`
  executor; a puppet executor on the host gets none of it.
  `GetActivationTime` (26793) takes the executor's stats. [2.5-untested]
- A quickhack effect on an NPC (gameplay tag `Quickhack`,
  `NPCPuppet.OnStatusEffectApplied` 126476 → `OnQuickHackEffectApplied` 126593) alerts and traces only when `instigatorEntityID ==
GetPlayerID()` and the effect lacks the `Stealth` tag:
  `NonStealthQuickHackVictimEvent` (126787) → `RevealPlayerPositionIfNeeded`
  (126798: a `HackTargetEvent` with `AIQuickHack.HackRevealPosition`
  queued on the player, unless in combat or already revealed) or
  `NPCStatesComponent.AlertPuppet` (168014), then
  `AIActionHelper.TryChangingAttitudeToHostile` (169916). [2.5-untested]
- NPC hacks: `AISubActionQuickHack.Hack` (493744) queues a `HackTargetEvent`
  (195237: netrunner, target, action record, settings) on the target;
  `ScriptedPuppet.OnHackTargetEvent` (22119) runs it as an
  `AIQuickHackAction` with the netrunner as executor; `PlayerPuppet`
  (45832) adds the reveal state. Queued on our V with a proxy as
  `netrunnerID`, it should run the hack from the proxy. [2.5-untested]
- `ExecutePuppetActionEvent{actionID}` (201075) queued on an NPC runs a
  quickhack from the local player on it, by the normal path (22452). [2.5-untested]
- Spread (`SpreadInitEffector` 316185, `SpreadEffector` 316386) reads
  `GetPlayer(game)` at initialisation for stats, spread map
  (`HackingDataDef.AddItemToSpreadMap`) and the spread effect's
  instigator; a `SpreadEffector` with no map entry for its action does not
  spread. [2.5-untested]
- Vehicle quickhacks reach `VehicleComponentPS.OnVehicleOverrideForceBrakes`
  / `…Explode` / `…Accelerate` (96426-96451) and call
  `VehicleObject.ActivateNetrunnerQuickhack` (87829); the police hacker
  loop stops a player's car with `ForceBrakesUntilStoppedOrFor` (87873,
  194573). Whether `ActivateNetrunnerQuickhack(Accelerate)` acts on a car a
  player drives is not known. [2.5-untested]

### 13.24 A hit's whole outcome (2026-11-18, read in final.reds, not run)

Line numbers are the 2.50 `final.reds` dump's. All [2.5-untested].

- In game (2026-11-19, scenario `outcomes-blade`, host): a strike's hit of
  the guest's puppet (attack type Melee, a built hit shape on the left
  arm, the puppet holding `Items.Preset_Katana_Default`,
  `hitFlag.ForceDismember`) taking the last health left the NPC at
  0.078 of 78 (the 0.1 % custom limit) instead of killing it, and never
  reached `RequestDismemberment`; the next strike killed it. So a lethal
  strike now calls `Kill` and cuts the limb itself. [2.5-verified]
- What keeps V whole: `PlayerPuppet` has no `HitReactionComponent` (it
  extends `AIMandatoryComponents`, NPC-only) and no
  `DismembermentComponent` (`ScriptedPuppet` requests `Dismemberment0701`,
  `:19751`, found only on NPC bodies). No record field or stat turns
  dismemberment off (none in the 2.50 scripts). Ways a limb goes:
  `HitReactionComponent.ProcessWoundsAndDismemberment` (`:185481`: crippled
  `BaseStatusEffect.Crippled<zone>`, `Dismembered*`, the requests below),
  `DismembermentComponent.RequestDismemberment` (static, `:188532`),
  `RequestDismembermentEvent` → `ScriptedPuppet.OnRequestDismemberment`
  (`:21481`), and the component's own `OnDismemberment`,
  `OnDismembermentExplosion`, `OnDismembermentDebris` callbacks (`:188580`).
  [2.5-untested]
- The pipeline (`DamageSystem.ProcessPipeline` `:84290`): `PreProcess`
  (`ConvertDPSToHitDamage`, `CheckForQuickExit`, invulnerability,
  immortality, death checks, `ModifyHitData`: resistances, localized
  damage, `ProcessInstantKill`, dodge, evasion, mitigation), `Process`
  (returns at once on `DealNoDamage`; source, target, global modifiers,
  one-shot protection, then `DealDamages` → `SendDamageEvents`:
  `gameTargetDamageEvent` to the instigator, `gameDamageReceivedEvent` to
  the target), `ProcessHitReaction` (`ReactToHitProcess`), `PostProcess`
  (status effects, returned damage, and **poise**). [2.5-untested]
- Poise is drained in `PostProcess` (`:85800`) only for a player
  instigator with a finisher available: the health attack value times
  `NewPerks.Reflexes_Right_Milestone_3.poise*Multiplier`s, by
  `RequestChangingStatPoolValue(target, Poise, -x, instigator, false,
false)`. It is not gated by `forReal`, so a guest's hit drains its
  proxy's poise. `NPCPoiseListener.OnStatPoolMinValueReached` (`:203306`)
  applies `FinisherActiveStatusEffect` for the local player.
  [2.5-untested]
- `ApplyDamageSingle` (`:219072`) caps a hit at health − 1 for finisher
  grace (`IsFinisherGrace`, player instigator, melee/throwable) and for
  bleeding ticks with `Reflexes_Master_Perk_5`, and zeroes a boss's poise
  then; otherwise a hit taking all health gets `hitFlag.WasKillingBlow`,
  also with `forReal` false. `DrainStatPool` (`:219283`, real damage only)
  keeps health above `AttackData.GetMinimumHealthPercent()` and above
  1 % for god mode `Immortal`. [2.5-untested]
- Non-lethal: `hitFlag.Nonlethal` (22) comes from native code (blunt
  weapons, non-lethal mods); scripts only read it. Human NPCs have the
  defeat mechanic (`SetIsDefeatMechanicActive(Human)`, `:125575`):
  `NPCDeathListener` puts a 0.1 % custom limit on health; reaching it
  marks the NPC for defeat and queues `gamePotentialDeathEvent`;
  `OnPotentialDeath` (`:128692`) applies `BaseStatusEffect.Unconscious`
  when the damage history (`m_receivedDamageHistory`, filled by
  `ProcessDamageReceived` from `gameDamageReceivedEvent`, cleared on a new
  frame's damage) holds a non-lethal hit, then `Defeated`.
  `HitReactionComponent.CanDieCondition` (`:185838`) refuses the kill on a
  non-lethal hit. `ScriptedPuppet.IsUnconscious` / `IsDefeated` read the
  status effects. [2.5-untested]
- Hit reactions: `HitReactionComponent.EvaluateHit` (`:184600`) returns at
  once without a hit shape (`GetBodyPart`: `hitRepresentationResult
.hitShapes[0]`), so a scripted hit with none plays no reaction. The
  shape's `userData` is a `HitShapeUserDataBase` (`m_hitReactionZone`,
  `m_dismembermentPart`, `m_hitShapeType`, `m_hitShapeTag`). The strength
  is the frame damage `NPCPuppet.SetHitEventData` (`:128363`) takes from
  `attackComputed`'s health total times the attack weapon's
  `HitReactionFactor` / `HitWoundsFactor` / `HitDismembermentFactor`
  stats, and zero for a live target with `DealNoDamage`. Dismemberment
  goes through the static script function
  `DismembermentComponent.RequestDismemberment` (`:188532`, a
  `DismembermentEvent` and an audio event; it also buffs the local player
  with `Reflexes_Master_Perk_5`). [2.5-untested]
- Difficulty: `StatsDataSystem.GetDifficulty()` / `SetDifficulty`
  (`:73092`, `gameDifficulty` Easy 0, Hard 1, VeryHard 2, Story 3). On the
  target's side it scales one-shot protection (`ProcessOneShotProtection`
  `:85069`), the fixed share of explosions
  (`ProcessPlayerFixedPercentageOverride` `:85107`) and grenade damage to
  players (`:85250`); story mode (the `story_mode` fact) cuts damage to a
  player to 0.1 % unless `hitFlag.IgnoreDifficulty` (`ScalePlayerDamage`
  `:87494`). NPC damage itself is the attacker's side. [2.5-untested]
- Friendly fire: `CheckForQuickExit` (`:86416`) marks a hit on a friendly
  target `DealNoDamage` + `FriendlyFireIgnored` unless the hit has
  `hitFlag.FriendlyFire`. [2.5-untested]

### 13.28 Firing an NPC's shot from a proxy (2026-11-18, scripts only)

Used by `scripts/tandem/Shots.reds` (NPC shots at a guest's V, simulated
by the guest's game); none of it run in game yet.

- `AIWeapon.Fire` (final.reds 173136-173364) is the script entry of every
  NPC shot: it aims (the target's `Head` slot or best targeting component,
  15 cm lower for a player), asks the target's `TargetShootComponent.
HandleBeingShot` (173757) for the miss offset (the player-side "time
  between hits" rules, `coefficientMultiplier` = the AI's tbh), picks the
  attack (`SetAttackBasedOnTimeDilation`, 173366) and calls a native
  `WeaponObject.AI_ShootAt(positionProvider, target, instigator, ammoCost:
Uint16, params, projectilesPerShot: Uint8, charge, opt maxSpread, opt
muzzleOffset)` (54056), then the weapon effects. Its aiming needs the
  owner's senses (`IsAgentVisible`, 173210), which a proxy has off: the
  guest calls `AI_ShootAt` itself, with a static position provider at
  the aim plus the offset, `hitPlaneOffset` = the offset, ammo cost 0.
  [2.5-untested]
- A proxy has no weapon in hand by default; `TandemWeapons.Hold` (the
  transaction system) puts the NPC's weapon record in `WeaponRight`; the
  shot waits up to 0.5 s for `IsAttached()`. [2.5-untested]
- A hit whose instigator is the proxy reaches the guest's own pipeline
  (`DealDamages`) like any NPC hit on V. `CheckForQuickExit` drops it if
  the proxy's attitude to V is friendly (86453). [2.5-untested]
