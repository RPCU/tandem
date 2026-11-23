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
  `SDamageDealt` list, per stat pool) without applying it: vanilla's own
  client path. A wrap (`@wrapMethod`, private is fine) catches every
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
  `DamageSystem.ProcessStatusEffects` and applied **a frame later**: not
  in `StatusEffectHelper.GetAppliedEffects` right after the call.
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
  targeting, hit representation shapes). Next: compare a puppet's
  components with an NPC's (`tools/game.sh cmd inspect`), then spawn
  puppets from a template that has them, or add them at assembly
  (Codeware). [2.5-verified]
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
- Where the target's side reads the **attack record** for a V as target:
  `ProcessPlayerIncomingDamageMultiplier` 84831 (the record's
  `PlayerIncomingDamageMultiplier` times
  `PreventionSystem.GetDamageToPlayerMultiplier`, any attack type),
  `ProcessPlayerFixedPercentageOverride` 85107 (explosion or pressure
  wave with the `OverridePlayerDamageWithFixedPercentage` hit flag) and
  `ProcessGrenadeExplosionDamageToPlayer` 85250
  (`PlayerGrenadeDamageOverride`), both setting damage from V's health;
  `ProcessDefensiveState` 218586 blocks only an `Attack_Melee_Record`
  (`CanSkipBlock`). No stage reads the record's stat modifiers (they feed
  the effect's own attack, `attackStatModList` 87452), and there is no
  push or knockdown damage type (`gamedataDamageType`: physical, thermal,
  electric, chemical). So a 10-physical melee hit rebuilt from
  `Attacks.ForwardPush` (the player's shove, `Attack_GameEffect`, 537376)
  that took 392.96 (2026-11-21) is most likely its player multiplier; the
  value is TweakDB, not read here. [R]
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
- `OnHackTargetEvent` returns without a word when the target already has
  `AIQuickHackStatusEffect.BeingHacked` (a trace is then dropped, any other
  hack retried 2 s later) or the record's `TargetPrereqs` refuse it. A
  trace's upload (`UploadFromNPCToPlayerListener.OnStatPoolValueChanged`, 195063) stops at any tick when the V is in combat, inactive, farther than
  its `RevealPositionMaxDistance` stat from the `HackingMinigame`
  blackboard's `LastPlayerHackPosition`, in a scene tier that forbids it,
  has `BaseStatusEffect.RevealInterrupted`, or the netrunner and its squad
  are inactive; a record with `InterruptionPrereqs` is stopped by those
  alone. `RevealPlayerPositionIfNeeded` (126798) sets
  `LastPlayerHackPosition` to the V's position when it queues the trace: a
  trace queued another way must set it too. [2.5-untested]
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
- `gameDismBodyPart`'s native values are not the scripts' 0..5: `EnumInt`
  of `LEFT_ARM` logged 14, and a thrown body's own dismemberment 7168
  (several parts at once), 2026-11-19. Map by name
  (`TandemOutcomes.PartIndex` / `PartOf`), never `IntEnum` from 0..5.
  [2.5-verified]
- A strike on an NPC the host's damage system flags `DealNoDamage`
  (invulnerable, or friendly-fire ignored) changes nothing: the first
  NPC near the test field (488.85 health) took one strike and then none
  (2026-11-19); the log line now says which check refused. [2.5-untested]

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

### 13.36 The Relic tree's levers (2026-11-19, read in final.reds, not run)

- The tree is nine `Espionage_*` perks (`gamedataNewPerkType` 71-79,
  `:4056-4064`); only six script sites name one (`:45572`, `:59798`,
  `:204522`, `:219870`, `:224165`, `:250322`). The rest are stat flags set
  by the perks' TweakDB records: `CanUseNewMeleewareAttackSpyTree`
  (`:54661`, `:248568`, `:420484`), `CanPlayerExitCombatWithOpticalCamo` /
  `BlockOpticalCamoRelicPerk` (`:44019-44025`), `CanPlayerDodgeOnDetection`
  (`:39706`). [2.5-untested]
- Three Relic arm-attack effects reach their target with **no
  instigator**: `MantisBladesRelicDismemberment` after a leap
  (`DamageSystem`, `:85964`: target not a player, instigator a player with
  the perk, `JustLeaped`), `GorillaArmsOnePunchNPCMark` and
  `MantisBladesGrandFinaleNPCMark` (`NPCPuppet
.OnApplyRelicMeleewareDamageOnNPCEvent`, `:128082-128103`; the decompiler
  nests the Mantis branch inside the Gorilla one). An
  `ApplyStatusEffectEvent` for them has no `instigatorEntityID`.
  [2.5-untested]
- The Gorilla Arms and Mantis Blades charged attacks queue a new hit of the
  player on their target after a delay
  (`ApplyRelicMeleewareDamageToTarget`, `:552771`, flags `OnePunch` or
  `GrandFinale` and `RelicGoldenNumbers`; `IsCloseEnoughForGrandFinale`
  1.25 m and 3.5 m from the local player): ordinary hits. [2.5-untested]
- The marks act only on a dead, defeated or about-to-be NPC
  (`NPCPuppet.OnStatusEffectApplied`, `:126464-126476`): `OnePunchedMark`
  ragdolls it (`HitReactionComponent.ProcessOnePunchAttackHitImpact`,
  `:186189`, damage history from the last hit's instigator) and sends
  `SetSlowMoForOnePunchAttackEvent` to `GetPlayer` after
  `Items.StrongArms.slowMoDelay`, which sets the `deflect` time dilation
  (`:39668-39679`); `GrandFinaleMark` applies
  `MantisBladesInstantDismembermentSpyBuff` (`:186228`). [2.5-untested]
- Any status effect tagged `Dismember` makes
  `StatusEffectManagerComponent` (an NPC component) queue a
  `ForcedDeathEvent`, cut the head (coarse) and start `blood_onscreen` on
  the **local player** (`:321244-321266`). [2.5-untested]
- Breaches (Vulnerability Analytics): `BreachFinderComponent` is a native
  component **of the player** (`:204383`); it tracks one `BreachComponent`
  on one NPC (`GetTrackedBreachComponent`, `GetTrackedBreachPuppet`). The
  roll, the duration and the cooldown are the player's
  (`NewPerks.CyberwareBreachParams.*`). `TryProcessBreachHit` (`:204452`)
  takes ranged or melee hits, not headshots, needs a weapon and a
  `PlayerPuppet` instigator, and adds `CriticalHit`, `WeakspotHit` and
  `BreachHit`. The breach wears down from the `gameTargetDamageEvent` the
  instigator gets (`OnDamageDealt`, `:204502`: `damage > 0` and the
  `BreachHit` flag). Destroyed, it fires `Attacks.BreachEMPExplosion`
  with the player as instigator and source, electric damage a share of
  the breach's health (`SpawnFinalAttack`, `:204533`), or, with
  `Espionage_Right_Perk_1_1`, `CyberwareBreachStreak` (`:204522`).
  [2.5-untested]
- An NPC's dismemberment tells its attacker with a `DismembermentInstigated`
  event (`HitReactionComponent.NotifyAboutDismembermentInstigated`,
  `:184341`), called right after `DismembermentComponent
.RequestDismemberment` and **whatever that request did**
  (`:185423-185437`, `:185548-185595`, `:185647`, `:185683`): a request we
  refuse still tells. `PlayerPuppet.OnDismembermentInstigated` (`:45544`)
  writes `PlayerPerkData.DismembermentInstigated` and gives the Relic's
  Mantis Blades buff when the weapon is `Cyb_MantisBlades` and the target
  was not dead yet. A request made outside the hit reaction (the
  `Dismember` effects above) tells nobody. [2.5-untested]
- Emergency Cloaking's exit (`:44019-44048`, `:39724-39733`): only while
  `m_inCombat`; `PromoteOpticalCamoEffectorToCompletelyBlocking`,
  `SetInvisible(true)`, `DeactivateThreat(player)` on the hostile NPCs of
  the player's own tracker, then after
  `Items.AdvancedOpticalCamoCommon.exitCombatDelay` (1.5 s)
  `BaseStatusEffect.MemoryWipeExitCombat` on each (no instigator) and the
  sound `ui_gmpl_stealth_avoid_combat`. [2.5-untested]
- An effect tagged `ClearThreats` empties the NPC's whole target tracker
  (`TargetTrackerComponent.ClearThreats`, `NPCPuppet.OnStatusEffectApplied`
  `:126486-126489`), every V in it; `ExitCombatWithPerk` passes
  `MemoryWipeExitCombat` on to its squadmates with the same instigator
  (`:126490-126503`). Which wipes carry which tag is TweakDB. [I]
- The second chance (Sensory Protocol): `CanAvoidCombatWithDodge`
  (`:39705`) is the stat flag, crouching, and not in combat or within
  `AIGeneralSettings.avoidCombatBuffer` of its start. An NPC entering
  combat gives `SecondChancePerkTimeDilation` and queues three
  `DodgeToAvoidCombatEvent`s at 1, 2 and 3 s (`NPCStatesComponent.OnCombat`,
  `:168333-168349`); each succeeds if V just dodged
  (`JustDodgedActiveBuffer`), the NPC's senses do not see V
  (`IsAgentVisible`) and no cooldown runs: `MemoryWipeOnDodge` on the NPC,
  the sound, the dilation removed, `DodgeOutOfSightCooldown` and
  `SecondChancePerkCooldown` on V (`:39682-39696`). [2.5-untested]

### 13.50 Enemy scaling: the stats and how modifiers stack (2026-11-19, read in final.reds, not run)

What `scripts/tandem/Scaling.reds` relies on (`architecture/npcs.md`,
"Enemy scaling").

- **Modifier types** (`gameStatModifierType`, `:510`): `Additive` 0,
  `AdditiveMultiplier` 1, `Multiplier` 2. The game's scripts use a
  `Multiplier` of 0 to switch a stat off whatever else it has
  (`StunApplicationRate`, `:556144`; `RollForPlusOnMaxQuality` on an item,
  `:131085-131092`) and one of `1 / x` to undo a perk's factor
  (`Deceleration`, `:553270-553285`): a `Multiplier` multiplies the stat's
  final value. [R] The whole formula, (Σ additive) × (1 + Σ additive
  multipliers) × Π multipliers, is native. [I]
- **Where the difficulty, the level and the rarity come in**: an NPC's
  stats come from its record's modifiers, mostly curves of its
  `PowerLevel` (`puppet_powerLevelToHealth`; the rarity presets
  `puppet_preset_*_mods` read with its `PowerLevel`,
  `RPGManager.GetRarityMultiplier`, `:35335-35360`); a curve takes an
  optional `gameDifficulty` (`StatsDataSystem.GetValueFromCurve`,
  `:73095-73101`). Our `PowerLevel` modifier (§13.38.1) moves that base;
  a `Multiplier` of ours multiplies the result once. [R] Which NPC stat
  reads the difficulty is TweakDB. [I]
- **The stats** (`gamedataStatType`): `Health` 787 (`:2159`: an NPC's
  maximum health, read as such by `SendDamageRequestToPreventionSystem`,
  `:85856`), `Armor` 33 (`:1405`), `Poise` 1139 (`:2511`),
  `StaggerDamageThreshold` 1480 and `…InCover` 1482 (`:2852`, `:2854`),
  `KnockdownDamageThreshold` 938 and `…InCover` 940 (`:2310`, `:2312`),
  `HitShapeArmor` 820 (`:2192`). Stat pools (`gamedataStatPoolType`,
  `:3602`): `Health` 17, `Poise` 27. [R] That the `Poise` stat is the poise
  pool's maximum is TweakDB (`BaseStatPools.Poise`): `tools/game.sh cmd npc
scale` logs the stat and the pool's maximum side by side. [I]
- **Armour** (`DamageSystem.ProcessArmor`, `:86621`): the target's `Armor`
  (or its `HitShapeArmor`, when a player's hit lands on an armoured hit
  shape and that is more), less the weapon's armour penetration, times
  `StatsDataSystem.GetArmorEffectivenessValue`, divides each damage value
  (never below 1); it runs on the game that computes the target's side of
  the hit: for a guest's hit on a proxy, the guest's, against the proxy's
  stats. [R]
- **Poise** is drained by a player's hit in `DamageSystem` (`:85845`,
  `RequestChangingStatPoolValue` on `Poise`, an amount the attack decides,
  never a share of the maximum): a larger pool takes more hits to break.
  [R]
- **Hit reaction thresholds** (`HitReactionComponent`, `:183465`): the
  component reads the NPC's `Health` **once**, as `m_totalHealth`
  (`GetDBParameters`, `:183600-183603`, a task queued when it is attached,
  `:184409`); the impact, stagger and knockdown thresholds are
  `m_totalHealth × stat / 100` (`SetHitReactionThresholds`,
  `:185085-185110`), the stats followed through a stats listener
  (`:187866-187876`). A health change later reaches only its current health
  (`NPCHealthListener`, `:187800-187805`). So a `Health` modifier added
  after the attach leaves the thresholds in points as they were; ours on the
  threshold stats is ×poise × unscaled health ÷ `m_totalHealth`, read
  through an added method (`TandemTotalHealth`). [R] The wound,
  dismemberment and defeat thresholds are shares of the same total (`:183629-183750`):
  unscaled, as vanilla's. [R]
- **Pools** (`StatPoolsSystem`, `:83541-83577`): `GetStatPoolValue(…,
perc)` and `RequestSettingStatPoolValue(…, perc)` read and set a share;
  `GetStatPoolMaxPointValue` the maximum; `IsStatPoolAdded`. A pool's own
  answer to its maximum changing is native: we set the share back. [R] / [I]
- **Bosses and named enemies**: `ScriptedPuppet.GetNPCRarity()` (native,
  `:19424`), `gamedataNPCRarity` (`:4950`: Boss 0, Elite 1, MaxTac 2,
  Normal 3, Officer 4, Rare 5, Trash 6, Weak 7), `IsBoss` (`:20825`). No
  separate "named" flag in the scripts: a named enemy is a record of its
  own, usually Elite, Officer or Boss. [R]

### 13.60 Breach Protocol: where a breach's outcome becomes effects (2026-11-19, read in final.reds, not run)

What tandem's Breach Protocol between players (`architecture/devices.md`,
"Breach Protocol") rests on. Line numbers are `final.reds` of 2.50.

- **Every breach ends in one event.** The minigame's end queues
  `AccessPointMiniGameStatus{minigameState}` (a native event, `:215004`)
  for the entity named by `NetworkBlackboard.DeviceID`
  (`NetworkInkGameController.CloseGame`, `:481325-481336`; the shipped
  screen is native, `gameuiHackingMinigame*`, inferred to do the same).
  The won programs are on the `HackingMinigame` blackboard,
  `ActivePrograms` and `ActiveTraps` (`array<TweakDBID>` variants,
  `:47186`), written before the event. A mod that skips the minigame
  (AccessPointSkip: `Device.PerformDive` wrapped) sets that blackboard and
  queues the same event: hooking the event covers it. [2.5-static]
- **Its handlers.** `Device.OnAccessPointMiniGameStatus` (`:100932`; the
  access point's override calls super, `:316738`) →
  `ScriptableDeviceComponentPS.HackingMinigameEnded` (`:32864`) →
  `SetMinigameState` (persistent `m_hackingMinigameState`, `:29208`) and
  `FinalizeNetrunnerDive` (`:32888`) → `ResolveDive` (`:32822`):
  `SetExposeQuickHacks` (with `isRemote` from
  `NetworkBlackboard.RemoteBreach`) to the device's backdoor access point,
  or, a remote breach of a device with no backdoor, to every one of its
  access points (`GetAccessPoints`, `:29104`). A failure raises the
  security system's alarm with the local player as culprit (`:100941`).
  The NPC's handler, `ScriptedPuppet.OnAccessPointMiniGameStatus`
  (`:23041`), asks its `PuppetDeviceLinkPS` (`AcquirePuppetDeviceLink`
  by entity id) for `PerformNPCBreach` (`:113261`): an `NPCBreachEvent` to
  its parent link, passed on to the access points
  (`CommunityProxyPS.OnNPCBreachEvent`, `:208784`), whose
  `OnNPCBreachEvent` (`:119726`) breaches and refreshes the network, or
  counts a failed attempt and alerts its NPCs. An NPC with no link (a
  spawned one, a proxy) does nothing there. [2.5-static]
- **Programs become effects in `AccessPointControllerPS.RefreshSlaves`**
  (`:119138`), reached from `OnSetExposeQuickHacks` (`:119753`) and
  `OnNPCBreachEvent` through `RefreshSlaves_Event` (`:113014`, the lazy
  children). It reads the blackboard **of the game it runs on**: loot
  (`NetworkDataMineLootAll*`, `NetworkLootQ003`, `minigame_v2.FindAnna`'s
  fact), then `ProcessMinigameNetworkActions` (`:119626`) for the access
  point and each child, and `SetExposeQuickHacks` to each child;
  `RewardMoney` (`:119451`, once per network: `m_moneyAwarded`, from the
  local player's cyberdeck quality) and `RPGActionRewards.Hacking`. The
  money and the XP go to `GetPlayerMainObject`. [2.5-static]
- `ProcessMinigameNetworkActions`: traps first (`MinigameTraps.MaterialBonus`:
  a material to the local player; `IncreaseAwareness`: a
  `SetDetectionMultiplier` of 10 to the device's `SensorDevice`), then each
  program whose `<record>.targetClass` CName is the device's class or
  `None`: the device's own minigame action of that name
  (`GetMinigameActionByName`, `:30113`), else a bare `PuppetAction` set up
  on it, `RegisterAsRequester(device)`, `SetExecutor(GetPlayer)`,
  `SetObjectActionID(program)`, `ProcessRPGAction`.
  `FilterRedundantPrograms` keeps `NetworkTurretFriendly` over
  `NetworkTurretShutdown`. [2.5-static]
- **NPCs' daemons**: `ScriptedPuppetPS.OnSetExposeQuickHacks` (`:25602`)
  reads the same blackboard and runs each program its character record
  lists (`ObjectActions`) as a `PuppetAction` from `GetPlayerMainObject`;
  `MinigameTraps.SquadBuff` puts `MinigameAction.BuffDamageReductionMinigame`
  on it; `NetworkLootShard`, `NetworkLootMaterials`, `NetworkLootMoney`
  pay the local player; perks of the local player read the program count
  (`ThreeOrMoreProgramsMemoryRegPerk`, `MinigameNextInstanceBufferExtensionPerk`).
  Daemon effects seen in scripts: `MinigameAction.ICEBrokenMinigameMinor`,
  `…Medium`, `…Major`, `…Placide` (`:12254`), `VulnerabilityMinigame`
  (`:85452`). Whether any of them carries the `Quickhack` gameplay tag is
  TweakDB. [2.5-static]
- **Running a daemon from someone else**: `BaseScriptableAction
.ProcessRPGAction` (`:26605`) pays the executor's cost unless
  `SetCanSkipPayCost(true)` (`:27838`, `CanSkipPayCost` `:26943` reads the
  executor as a `PlayerPuppet`); `CompleteAction` gives the record's
  `Rewards` (to the requester's stats) and quickhack XP to the executor as
  a `PlayerPuppet` (none for a puppet). [2.5-static]
- **A second breach is the game's to refuse**: the jack-in is offered while
  `!WasHackingMinigameSucceeded()` (`:29580`, `:98834`, `:32689`), which
  reads `m_hackingMinigameState`, not the network's `m_isBreached`;
  `IsBreached` prereqs (`:310042`) and the device's `IsBreached`
  (`:29052`: any of its access points `IsNetworkBreached`) lock the
  quickhack. `BreachAccessPointEvent` (`:119812`) sets `m_isBreached` and
  exposes the access point's quickhacks, nothing else;
  `ResetNetworkBreachState` undoes it. An `@addMethod` on
  `ScriptableDeviceComponentPS` can set `m_hackingMinigameState`
  (protected). [2.5-static]
- **Trap: `SetExposeQuickHacks` on an access point runs `RefreshSlaves`**
  (`OnSetExposeQuickHacks` → `RefreshSlaves_Event`): the daemons of
  whatever the local blackboard last held, and `RewardMoney` to the local
  V if the network never paid. Never send it to an access point to say
  "exposed"; `BreachAccessPointEvent` does that. [2.5-static]

To verify in game (none has run):

1. The shipped breach screen queues `AccessPointMiniGameStatus` with the
   blackboard written first (the hook reads it in the event's handler).
2. A daemon run on the host from a guest's puppet (`SetExecutor(puppet)`,
   `SetCanSkipPayCost`): a camera shuts down, a turret turns friendly to
   the players, an NPC gets its ICE broken; nothing is paid by the host's V.
3. Whether the Breach Protocol quickhack is offered on a proxy (it has no
   device link: `SQUAD_NETWORK`), and which effects it leaves there.
4. The daemon effects' tags, so the list in `Breach.reds`
   (`IsDaemonEffect`) can be the record's instead.
5. The held outcome (`Breach.reds`, the host's grant decides the payout):
   - `Device.OnAccessPointMiniGameStatus` (`:100932`) is all of the game's
     handling: `HackingMinigameEnded` (`:32864`: `SetMinigameState`, then
     `FinalizeNetrunnerDive`, `:32888`, which runs `ResolveDive` on a win
     and always `ActionToggleNetrunnerDive(true)`, the dive's end). The
     rewards are paid from `ResolveDive` → `SetExposeQuickHacks` →
     `AccessPointControllerPS.RefreshSlaves` (`:119138`). An NPC's
     (`ScriptedPuppet`, `:23041`): `PerformNPCBreach` (`:113261`) to its
     access points, then the `NetworkBreach` time dilation is unset.
     [2.5-untested]
   - Handled with `HackingMinigameState.Unknown`, neither branch runs: the
     dive ends, nothing is breached, alerted or paid _(inferred from the
     code above; to verify: V leaves the personal link's workspot)_.
   - To verify: holding the handler one ping (the wrap returns without
     calling it, the tick calls the entity's handler directly) leaves no
     visible stall after the minigame's own end screen; the direct call of a
     `cb` from an added method reaches `AccessPoint`'s override
     (`:316738`, the lights).

### 13.75 A status effect's look, without its gameplay (2026-11-19, read in final.reds, not run)

- **Where a status effect names its look.** `StatusEffect_Record` has
  three lists for it (`:18188-18271`): `VFX()` and `SFX()`, each of
  `StatusEffectFX_Record` (`Name()`, `ShouldReapply()`, `:203977`), and
  `Packages()` of `GameplayLogicPackage_Record`, whose `Effectors()` are
  `Effector_Record`s naming their script class (`EffectorClassName()`,
  `:17453`); each effector reads its own fields from TweakDB in its
  `Initialize` (`record + t".field"`). `UiData()` is the buff bar's icon
  and text, no look. [2.5-untested]
- **How the game plays it on any character.** `GameObject
.OnStatusEffectApplied` (`:12246`) runs `StartStatusEffectVFX`
  (`:12262`: `GameObjectEffectHelper.StartEffectEvent(this, name)` for each
  VFX, on a new application or when `ShouldReapply`) and
  `StartStatusEffectSFX` (`:12274`: `GameObject.PlaySound`);
  `OnStatusEffectRemoved` breaks the loops (`BreakEffectLoopEvent`,
  `:12291`) and stops the sounds on the final removal. A defeated NPC
  keeps them (`CacheFXOnDefeated`, `:465043`, then
  `RemoveCachedStatusEffectFXEvent`). The names resolve in the entity's own
  effect lists (`entEffectSpawnerComponent`s; V's `fx_status_effects`), so
  one name can be a body effect on an NPC and a screen effect on V
  (`BaseStatusEffect.Burning`'s `status_burning`: the modding wiki's
  "creating custom FX" guide). [2.5-untested]
- **The visual effectors.** `PlayVFXEffector` (`:307141`): `vfxName`
  started on `ActionOn`, broken on `ActionOff`/`Uninitialize`, or started
  only then with `startOnUninitialize`; `fireAndForget` starts it again on
  each repeat. `ApplyShaderEffector` (`:299449`: `overrideMaterialName`,
  `overrideMaterialTag`, `applyToOwner`, `applyToWeapon`) and
  `ApplyShaderOnObjectEffector` (`:299350`: the same on its
  `applicationTarget`, `overrideMaterialClearOnDetach`) both create a
  static game effect (`GameEffectSystem.CreateEffectStatic(name, tag,
owner)`, `:53063`), set `EffectSharedData.enable` and `.entity`, and
  `Run()` it; off is the same with `enable` false. Every other effector is
  gameplay (stats, damage, AI, vision blockers such as
  `AttachOpticalCamoVisionBlockerEffector`, `:249803`). [2.5-untested]
- **The player's own records.** Effects on V are often separate records
  from an NPC's for the same thing: a hit's status effects on a player are
  `PlayerBleeding`, `PlayerBurning`, `PlayerPoisoned`,
  `PlayerElectrocuted`, on an NPC `Bleeding`, `Burning`… (`:85994-86038`);
  an NPC's burning spreads to V as `PlayerBurning` (`:21039`). A puppet
  (`player_*_tpp.ent`, `tweaks/tandem/puppet.tweak`) is V's own template,
  so V's records' names are the likeliest to resolve on it; whether they
  give a body effect there is to see. [to-see]
- **Camo is tagged** `CamoActiveOnPlayer` on a V (`:269230`) and `Cloak`
  on an NPC (`NPCPuppet.OnStatusEffectApplied`, `:126484`: removes its
  sense mappin; `:127055` puts it back). [2.5-untested]
- Tandem (presence.md, "Status effects"): a V's records that show (VFX, or
  a visual effector) travel as `StatusVisuals`; the partner's game plays
  them on the puppet by these same calls, never applying the effect.
  [2.5-untested]
- **The game's soft slow-motion cue** (for a V another V's bubble slows,
  time.md): the Focus perk, when it slows time a little, starts
  `cool_perk_focused_state_fullscreen` on V with
  `time_dilation_focused_enter` (`AimingStateEvents`, `:542843-542849`;
  Deadeye's reload the same, `ReloadEvents`, `:547592-547596`) and ends
  with `BreakEffectLoopEvent` and `time_dilation_focused_exit`
  (`PlayerPuppet`, `:44513-44514`; `:547671-547673`). A Sandevistan's own enter has no screen effect in the
  scripts (its camera curve `Sandevistan` and a Razer Chroma animation,
  `:524139-524146`; its look is `SandevistanPlayerBuff`'s, applied by the
  cyberware); Berserk plays `slow` (`:224030`). [2.5-untested]

### 13.151 Dual wielding (akimbo): two weapons, two state machines, what tells them apart (2026-11-22, 2.50 scripts read, not run in game)

Source: the game's own 2.50 bundle decompiled (`tools/scripts-decompile.sh`,
`~/.cache/tandem/ref/gamescripts/game/final.reds`; lines below are that
file's, `:N`). The public dump (`vanilla/final.reds`) predates patch 2.4
and has none of it but the NPC record field `DualWieldShootingStyle`.
Everything here is [2.5-untested]: read, compiled against (`task
scripts:check`), never run.

- **What can be paired.** `gameItemData.CanDualWield(owner)` (`:38607`):
  the item has the tag `DualWield` and the owner bought the perk
  `gamedataNewPerkType.Cool_Central_Milestone_2`; an item also tagged
  `QuestPreventDualWield` needs its record's `QuestFactPreventDualWield`
  fact above 0. The tags have names in scripts
  (`RPGManager.GetDualWieldWeaponTag`, `GetQuestPreventDualWieldWeaponTag`,
  `:59435`); `WeaponObject.WeaponHasTag` reads the weapon record's tags
  (`:59084`). Which records carry `DualWield` is TweakDB's (not read here:
  the patch notes say pistols and revolvers).
- **Where the second weapon lives.** The weapon equipment area has six
  slots: 0-2 the right hand's, 3-5 the left's, paired by
  `EquipmentSystemPlayerData.GetOppositeWeaponSlotIndex` (0↔3, 1↔4, 2↔5;
  `IsLeftWeaponSlotIndex`, `:87204-87229`). `EquipWeapon` puts a pairable
  weapon in the slot opposite an active pairable one (`:84646`). In the
  hands: `AttachmentSlots.WeaponRight` and `AttachmentSlots.WeaponLeft`
  (`GameObject.GetRightActiveWeapon`, `GetLeftActiveWeapon`, `:12210`); the
  left placement is the item record's second `PlacementSlots` entry
  (`EquipmentSystem.GetPlacementSlot(item, isLeftHandWeapon)`, `:55756`).
  The arm launcher also sits in `WeaponLeft` (`LeftHandCyberware`
  transitions' `GetLeftHandWeaponObject`, `:282676`): a left item is not
  always a second gun.
- **The game's own test of "two weapons in hand".**
  `GameObject.GetDualWieldWeaponNames` (`:12217`, telemetry): both slots
  hold a `WeaponObject`, not the same one, both `WeaponHasTag(n"DualWield")`.
  `DefaultTransition.HasDualWeaponEquipped` (`:235131`): right and left both
  equipped. `GameObject.IsDualWielding(object)` (`:12230`) reads the
  `PlayerStateMachine` blackboard's `IsDualWielding` (a Bool, 2.50 only),
  which the left hand's equipment states write (`EquipCycleInitEvents`,
  `:539003`; `UnequippedEvents`, `:537813`, `:537825`): a player's state machine only, nothing a
  puppet NPC has.
- **Equipping.** `EquipmentSystemPlayerData.RefreshCurrentItems`
  (`:84919`) sends the equipment state machine a request per hand
  (`SendPSMWeaponManipulationRequest`, instances `RightHand` and
  `LeftHand`, `:87484`), of type `EquipmentManipulationRequestType.DualEquip`
  (3) when both are valid. The upper body state machine has a `dualWield`
  state (`DualWieldDecisions` / `DualWieldEvents`, `:542103-542161`:
  `PlayerStateMachine.UpperBody` = 7, `gamePSMUpperBodyStates.DualWield`).
- **Animation switches.** On the body: the animation wrapper `dual_wield`
  (`AnimationControllerComponent.SetAnimWrapperWeight(owner, n"dual_wield",
1.0)`, with the blackboard flag, `:539004`; 0 on unequip, `:537814`), the
  equip event's `weaponEquipAnimFeature.dualWield` and
  `AnimFeature_LeftHandAnimation.lockLeftHandAnimation = true` (`:528411-528425`).
  On the weapons: `AnimationControllerComponent.RefreshDualWieldAnimWrapperWeight(owner)`
  (`:57750`) sets `RightHand` 1 / `LeftHand` 0 / `dualWield` 1 on the right
  weapon and `LeftHand` 1 / `RightHand` 0 / `dualWield` 1 on the left when
  both are held, all 0 otherwise; **`ScriptedPuppet` calls it from
  `OnItemAddedToSlot`, `OnItemRemovedFromSlot` and
  `OnItemVisualsAddedToSlot`** (`:23422-23464`), so an NPC body given two
  weapons gets the weapons' wrappers without us. While dual wielding the
  aim feature is not pushed to the weapon (`:543920`) and the zoom is the
  stat `DualWieldZoomLevel` (`:50191`).
- **One weapon state machine per weapon.** A `WeaponTransition`'s
  `scriptInterface.owner` is its weapon (`GetWeaponObject`, `:285729`); its
  instance is named `WeaponRight` or `WeaponLeft`
  (`IsRightWeaponLogic` / `IsLeftWeaponLogic`:
  `stateMachineInstanceData.referenceName`, `:285910`). So
  `ShootEvents.OnEnter`, `ReloadEvents.OnEnter/OnExit`, `QuickMeleeEvents`,
  the charge states and their parameters (`RightShootingSequence.*` /
  `LeftShootingSequence.*`, `:285429`…) all run per hand, and each weapon
  has its own shared blackboard (`ChargeStep`). The animation events are
  the hand's: `RightShoot` / `LeftShoot` (`:549525`), `RightReload` /
  `LeftReload`, `EndRightReload` / `EndLeftReload`, `reload_mask` 0 / 1
  (`:548804-548809`), `RightCycleRound` / `LeftCycleRound`.
- **Which hand fires.** A `Shoot` state machine writes
  `PlayerStateMachine.ShootState` (`gamePSMShootStates`: Idle 0,
  RightWeapon 1, LeftWeapon 2, BothWeapons 3, the Valentinos' frenzy 4-6;
  `RightWeaponEvents`, `LeftWeaponEvents`, `:522592`, `:522720`); a weapon's
  machine fires only when the state names it (`WeaponTransition.ShouldShoot`,
  `:285644`). Both read the one `RangedAttack` input.
- **Per-hand events elsewhere.** `ForceResetAmmoEvent.SetIsLeftHandWeapon`
  (`ReloadWeaponEffector` queues one per hand, `:305051`);
  `WeaponEquipEvent.isLeftWeapon` (`:528420`); telemetry's
  `LogWeaponAttackPerformed(weapon, leftHandWeapon, rightHandWeapon)`.
- **NPCs had two weapons before.** `AISubActionShootWithWeapon_Record.DualWieldShootingStyle`
  (`Sequence`: the weapons fire in turn, `:417661`) is in the older dump
  too; `ScriptedPuppet.GetWeaponLeft` exists for it.

What tandem does with it: `docs/architecture/players.md`, "Dual wielding".
To check in game: that `AddItemToSlot(puppet, WeaponLeft, …)` takes a
pistol on the puppet's body (its record's slots are Panam's), that the left
weapon sits right in the hand, that the `dual_wield` wrapper changes
anything on a body whose pose is streamed, and which records carry
`DualWield`.
