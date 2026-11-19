# Perks, skills and cyberware in a shared world

Research for the "Cyberware, skills and perks in a shared world" open
problem in [`../architecture/players.md`](../architecture/players.md): every player perk and
ability of 2.50 (perks 2.0, Phantom Liberty) read for what breaks when two Vs
share one world. The model it is checked against: the host's game owns NPCs,
devices and the world; each player's game owns its own V; a guest sees the
host's NPCs as proxies with their brain off and sends its hits to the host
(damage computed on the attacker's game, applied by the owner,
`scripts/tandem/Combat.reds`).

It extends, and does not repeat:

- [`coop-time.md`](coop-time.md): every time dilation (Sandevistan,
  Kerenzikov, focus, Deadeye, melee hit stop, deflect slow-mo,
  `SetTimeDilationEffector`);
- [`coop-player-vehicles.md`](coop-player-vehicles.md) §4: arm and leg
  cyberware, optical camo, status effects on a puppet;
- [`coop-npcs-combat.md`](coop-npcs-combat.md) §4–§6 and §9: the hit
  pipeline, death and second heart, takedowns, the quickhack upload flow.

## Sources and how far to trust them

<!-- prettier-ignore -->
| Cited as | What |
| --- | --- |
| `final.reds:LINE` | decompiled vanilla scripts, `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` |
| `Combat.reds:N` | tandem's own `scripts/tandem/Combat.reds` (and `Npcs.reds`) as of commit `d465118` |
| `coop-*.md §N` | the other research files in this directory |

Tags: **[R]** read in the scripts at the cited line (the same as `[S]` in
`coop-npcs-combat.md`); **[I]** inferred from what was read, not stated
anywhere. Nothing here was run in game or in a co-op session.

Limits:

1. **No TweakDB.** A perk is a `NewPerk` record whose effects are mostly
   TweakDB data: stat modifiers and effectors with prereqs. Scripts name
   only 79 of the 189 perks (`gamedataNewPerkType`, `final.reds:3984-4176`),
   where a state script or the damage system tests "is this perk bought".
   The other perks act through effector classes whose **scripts** are read
   here (§1.3) but whose **records** (which perk uses which effector, with
   which numbers) are not. §9 lists what could not be seen.
2. Perk names in the game's UI are not in the scripts. A perk is named here
   by its enum (`Body_Right_Milestone_2`), with the in-game name only where
   the scripts spell it (`IsWreckingBallAllowed`, `gunzerkerLevel`,
   `JuggernautEffector`, `EdgerunnerHealthReduction`).
3. The decompiler sometimes mis-nests `if … else if` chains; calls and their
   arguments are reliable, exact branch conditions less so.

## Contents

1. [How perks reach the game](#1-how-perks-reach-the-game)
2. [Root causes: what breaks many perks at once](#2-root-causes-what-breaks-many-perks-at-once)
3. [Perk trees](#3-perk-trees)
4. [Relic tree (Espionage)](#4-relic-tree-espionage)
5. [Cyberware abilities](#5-cyberware-abilities)
6. [Quickhacks, cyberdeck and Intelligence](#6-quickhacks-cyberdeck-and-intelligence)
7. [Vehicle combat](#7-vehicle-combat)
8. [Summary by risk](#8-summary-by-risk)
9. [What could not be seen](#9-what-could-not-be-seen)
10. [Tests owed](#10-tests-owed)
11. [Leftovers, item by item (2026-11-19)](#11-leftovers-item-by-item-2026-11-19)

---

## 1. How perks reach the game

### 1.1 `PlayerDevelopmentSystem` is already multi-player, and blind to puppets

- `PlayerDevelopmentSystem` keeps `persistent m_playerData:
[PlayerDevelopmentData]`, **one entry per attached player**, created in
  `OnPlayerAttach` (`final.reds:79163-79199`) [R]: the cancelled
  multiplayer's shape.
- `PlayerDevelopmentSystem.GetData(owner)` returns the entry whose owner id
  is `owner`'s, else `null` (`final.reds:79225-79241`) [R]. Every perk test
  in the scripts has the form `GetData(x).IsNewPerkBought(perk)` or the
  form `GetInstance(x).IsNewPerkBought(x, perk)`.
- So **on the host, a guest's puppet has no perks**: `GetData(puppet)` is
  `null` and every test on it answers 0 [I] (redscript returns the default
  on a null `this`). On a guest, its own V is attached and its perks answer
  as in solo. That is why the guest's hit pipeline must run on the guest
  (it does: `Combat.reds:65-118`), and why any **target-side** or
  **NPC-side** code on the host that asks about "the attacker's perks" or
  "the player's perks" is wrong for guests (§2).
- Perks, attributes, skills (proficiencies), XP and respecs are each
  player's own (authority table: "level, XP, perks … that player's game
  (save)"). Buying or selling a perk is a menu action on the owner's game;
  the hub menu's time stop is `coop-time.md` §2.9.

### 1.2 Perk checks in the scripts

176 references to `gamedataNewPerkType.*` outside the enum, in five kinds of
places [R]:

<!-- prettier-ignore -->
| Where the test runs | Examples | Runs on (co-op) |
| --- | --- | --- |
| the player's state machine (locomotion, melee, aim, carry, consumable, dodge) | `SprintDecisions.IsWreckingBallAllowed` `551935`, `DodgeEvents.OnEnter` `553630`, `CarryEvents` `532578` | the V's own game: fine |
| `DamageSystem`, attacker side (`GetData(instigator)` after `instigator.IsPlayer()`) | `GetVehiclePerksDamageMultiplier` `87097`, `ProcessVehicleTarget` `85525` | the attacker's game: fine (guest runs it on the proxy) |
| `DamageSystem` / `StatPoolsManager`, target side (`hitEvent.target as PlayerPuppet`) | `ProcessBulletBlockAndDeflect` `87209`, `ProcessGrenadeExplosionDamageToPlayer` `85250` | **the host for NPC hits on a guest's puppet: wrong** |
| the target NPC's components (`HitReactionComponent`, `DismembermentComponent`, poise, quickhacks) | `IsValidBodyPerkDismemberAttack` `185964`, `DismembermentConditions` `185884`, `NPCPoiseListener` `203307` | both: the proxy on the guest, the real NPC on the host |
| UI, ripperdoc, inventory, perk screen | `RipperDocGameController.SpawnPerks` `226895`, `CanSellNewPerk` `79672` | the owner's game: fine |

### 1.3 Effectors and the perk blackboard

Perks without a script test act through **effectors**: script classes
deriving from `Effector` (155 of them, root at `final.reds:83753`),
instantiated by TweakDB records and switched on by prereqs. The ones that
matter for co-op, grouped by what they touch [R, classes; which perk uses
each is TweakDB]:

- on the owner V only: `ModifyStatPoolValueEffector` `299104`,
  `OvershieldEffectorBase` `303021`, `JuggernautEffector` `300896`,
  `SadismEffector` `303078`, `BloodlustHealingEffector` `306663`,
  `ReloadWeaponEffector` `306535`, `UseConsumableEffector` `308320`;
- on the attack the V makes (attacker side, `ModifyAttackEffector`
  `261261` and children): `ModifyDamageEffector` `305262`,
  `ModifyDamageWithDistance` `305599`, `ModifyDamageWithVelocity` `305575`,
  `ModifyDamageWithLeapedDistance` `305486`, `RipAndTearEffector` `306239`,
  `AddStatusEffectToAttackEffector` `304529`, `ConvertDamageToDoTEffector`
  `300435`, `ModifyAttackCritChanceEffector` `304487`;
- on other entities: `TriggerAttackOnNearbyEnemiesEffector` `253230`,
  `TriggerAttackOnTargetEffect` `308528`, `PropagateStatusEffectInAreaEffector`
  `307039`, `SpreadEffector` `316386`, `SetTargetHealthEffector` `307010`,
  `ForceDismembermentEffector` `305428`, `ApplyObjectActionEffector`
  `315352`, `SetFriendlyEffector` `315998`;
- on shared systems: `SetTimeDilationEffector` `307231`,
  `NotifyPoliceEffector` `305855`, `RewardPlayerWithCrimeScoreEffector`
  `306913`, `TryDeactivatePreventionByMask` `305783`, `SetFactEffector`
  `307285`, `ToggleFactEffector` `307639`, `BroadcastStimEffector` `299908`.

Many perk prereqs listen to the **`PlayerPerkData` blackboard**
(`PlayerPerkDataDef`, `final.reds:180151-180161`) [R]: per-player counters
the player's own game bumps when something happens to or because of that V.

<!-- prettier-ignore -->
| Field | Written by | Read by (prereq) |
| --- | --- | --- |
| `WoundedInstigated` | `PlayerPuppet.OnWoundedInstigated` `45535` (event from the NPC's hit reaction) | `WoundedTriggeredPrereq` `310122` |
| `DismembermentInstigated` | `PlayerPuppet.OnDismembermentInstigated` `45544`, from `HitReactionComponent.NotifyAboutDismembermentInstigated` `184341` | `DismembermentTriggeredPrereq` `298725` |
| `EntityNoticedPlayer` | `PlayerPuppet.OnBeingNoticed` `40852`, sent by an NPC's `SenseComponent` `156355` | `EntityNoticedPlayerPrereq` `313206` |
| `CombatStateTime` | `PlayerPuppet` on entering combat `43656` | `PlayerCombatStateTimePrereq` `313795`, `CanAvoidCombat` `39715` |
| `LeapedDistance`, `LeapTarget` | mantis-blade leap | `ModifyDamageWithLeapedDistance` `305486` |
| `UsedHealingItemOrCyberware` | `PlayerPuppet` on a healing item or cyberware `44437` | overshield milestone `44448` |

Each of these is fed by an event that, in co-op, either happens on the
guest's own game (wounding and dismembering a proxy, leaping) or happens on
the **host** (an NPC noticing the V, the V entering combat) and must be
forwarded (§2.6).

---

## 2. Root causes: what breaks many perks at once

These are the findings that matter most: each one breaks a whole family of
perks, and each has one rule.

### 2.1 "The player" is one object, and on the host it is the host's V

NPC-side and system-side scripts that react to "the player" fetch the local
one, so on the host they act for the host's V even when a guest's puppet is
the one involved [R]:

<!-- prettier-ignore -->
| Code | What goes wrong on the host |
| --- | --- |
| `NPCStatesComponent.OnCombat` `168340-168360`: `GetPlayer(game)` then `CanAvoidCombatWithDodge` / `Gag` | an NPC entering combat (with anyone) offers the **host's** V the dodge-out-of-sight slow-mo and the gag |
| `NPCPoiseListener.OnStatPoolMinValueReached` `203306-203318`: `GetLocalPlayerMainGameObject()` | a broken guard offers a finisher if the **host's** V has one available, and plays its sound on the host's V |
| `DamageSystem.IsImmune` `86146-86155`: perk `Cool_Right_Perk_2_1` read from the local player | poison immunity of a thrown weapon's target is decided by the host's perks (only for status effects the host applies) |
| `RPGManager.AwardExperienceFromDamage` `36988-36993`: XP multiplier from `GetPlayer(inst)` | (only matters if XP is computed on the host; see §2.4) |
| `VehicleComponent.DamagePassengerInCollision` `91316`: `player == passenger` | a guest's puppet in the host's car takes no collision damage, and the guest's own game has no collision (§7) |
| `SenseComponent.ReevaluateDetectionOverwrite` `156337-156355`: `target.IsPlayer()` | `OnBeingNoticed` (and the perks it feeds) only fires for the host's V |

**Rule.** On the host, anything that reacts to "the player" for an event
involving a guest's puppet must be redirected to that guest: the host sends
the event (`NpcCombatStart{npc, player}`, `NoticedBy{npc, player}`,
`PoiseBroken{npc}`), and the guest's game runs the player half (apply
`SecondChancePerkTimeDilation`, offer the gag, the finisher, the sound) on
its own V and its proxy. Where the NPC half must change (`MemoryWipeOnDodge`,
`GagOpportunity` on the NPC, `FinisherActiveStatusEffect`), it is a request
back to the host, like `NpcEffect`. The host's own V keeps the vanilla path
only when it is the one involved. [I]

### 2.2 A hit is more than health

`Combat.reds:92-104` sends a guest's hit as health lost only
(`affectedStatPool == Health`), and the host applies it with
`RequestChangingStatPoolValue` plus a generic reaction
(`Npcs.reds:281-292`, `React` with `Attacks.REMOVE_BulletWithDamage` or
`Attacks.ForwardPush`). Perks decide much more than health on the attacker's
game [R]:

- **Poise.** Melee and blunt perks break an NPC's `Poise` pool, which opens
  finishers (`FinisherActiveStatusEffect`, `203306-203318`;
  `IsFinisherAvailable` `514132`). `StatPoolsManager` also zeroes the poise
  of bosses and MaxTac on a finisher-grace or bleed kill (`219104-219111`,
  `219122-219130`). Poise loss is not in `NpcHit`.
- **Finisher grace and "leave at 1 HP".** `ApplyDamageSingle`
  (`219091-219135`) caps the damage at health − 1 when the hit is a
  finisher-grace hit (`IsFinisherGrace` `219156`) or a bleed tick with perk
  `Reflexes_Master_Perk_5`, so the finisher can be played. The guest
  measures this against its **proxy's** health, which trails the host's by
  a round trip and 1 % steps; the host then subtracts an amount and may
  kill instead of leaving 1 HP.
- **Non-lethal.** Blunt weapons and non-lethal mods defeat instead of
  killing (`ScriptedPuppet.RewardKiller`: `gameKillType.Defeat`,
  `isAnyDamageNonlethal`, `final.reds:21851-21866`); the host's NPC never
  sees the flag.
- **Hit reactions and dismemberment.** `HitReactionComponent` decides
  stagger, knockdown and dismemberment from the attack and the attacker's
  perks: `Body_Left_Milestone_3` (shotgun/LMG dismember, `185955-185975`),
  `Reflexes_Master_Perk_5` (bleeding lowers the threshold, `185881-185891`),
  `Reflexes_Right_Perk_2_2` (reaction type, `186714`),
  `ForceDismembermentEffector` `305428`. On a guest this runs on the proxy
  (its corpse is cut up there) and on-dismember perks fire there; on the
  host the generic reaction runs and the real corpse stays whole.

**Rule.** `NpcHit` grows into "the outcome the attacker's game computed":
health, poise and other pools lost; flags `nonlethal`, `capAtOneHp`
(finisher grace / bleed), `killingBlow`; the reaction it chose (none,
stagger, knockdown, with direction and impulse) and dismemberment (body
part, wound type). The host applies them as given (`capAtOneHp` → set health
to max(health − amount, 1)), plays that reaction and that dismemberment on
the real NPC, and replicates poise like health. Each game still evaluates
"finisher available" for its own V (§2.1). [I]
Status (2026-11-18, untested in game): `NpcStrike` carries health, poise,
non-lethal, leave-1-HP, killing blow, the minimum health and the hit shape;
the host plays the real reaction and dismemberment from a hit of the
guest's puppet, poise comes back as `NpcPoise`, wounds as `NpcWound`,
knock-outs as `NpcGone` reason 3 (../architecture/combat.md "A hit carries its whole
outcome"). The reaction is the host's own (its rules, from the attack and
shape), not the one the guest's game picked for the proxy.

### 2.3 On-kill perks fire at the moment of the hit

Many "on kill" perk effectors are hit prereqs evaluated while the killing
hit is processed [R]:

- `TargetKilledHitPrereqCondition` (`221104-221119`): true when the target
  `WasJustKilledOrDefeated()` or `IsAboutToDieOrDefeated()`;
- `TargetKilledPrereqState` (`309862-309868`): `WasJustKilledOrDefeated()`;
- both flags are set by the NPC's health listener when health reaches its
  minimum or its custom limit (`203273-203290`).

On a guest the hit is measured, not applied (`forReal = false`), so the
proxy's health does not reach 0 during the hit and these prereqs are false;
the proxy dies later, when the host's death arrives (`Npcs.reds:442-455`,
`Kill(GetPlayer)` for our kills), with no hit in flight. Only
`TargetCanGetKilledByDamagePrereqCondition` (`221121-221147`), which
compares the damage to the target's current health, works on a proxy.
`TargetNeutraliziedEvent` (queued to the killer by `RewardKiller` only
when the killer is a `PlayerPuppet`, `final.reds:21851-21871`) does reach the guest's V through the proxy's
`Kill(GetPlayer)`, so XP-on-neutralize and UI work.

**Rule.** The guest predicts the kill: when its measured damage is ≥ the
proxy's health (the same test as
`TargetCanGetKilledByDamagePrereqCondition`), it marks the proxy
`MarkForDeath`/`MarkForDefeat` and sets `m_wasJustKilledOrDefeated` before
the hit's effectors run, so on-kill perks fire on the hit as in solo, and
sends `killingBlow`. The host decides: if its NPC survives (another
player's heal, a race), it answers with the health and the guest clears
the marks. Two players' simultaneous lethal hits: the host credits the
first to arrive and sends the killer in `NpcGone`. [I]

### 2.4 Guests earn no combat XP

Skill XP from damage is **queued per hit** by
`RPGManager.AwardExperienceFromDamage` (`36969`), called from
`StatPoolsManager.DrainStatPool` (`219283-219327`), which runs only when
damage is applied for real (`ApplyDamageSingle`: `else if forReal { …
DrainStatPool }`, `219136-219145`). The queue is **processed** on
neutralize: `PlayerPuppet.OnTargetNeutraliziedEvent` →
`ProcessQueuedCombatExperience` (`final.reds:45336-45346`, `80726`) [R]. A guest's hit on a proxy is measured with `forReal = false`
(`Combat.reds:94`), so nothing is queued and the kill pays no skill XP [I]
(level XP from the kill itself may still come from other paths: to check).

**Rule.** In the guest's `DealDamages` wrap, call
`RPGManager.AwardExperienceFromDamage(hitEvent, percentLost)` with the
measured health percentage, as `DrainStatPool` would. When any NPC dies, the
host tells **every** player who damaged it (`NpcGone` already goes to all),
and each game processes its own queue for that proxy, so assists pay XP
like kills (a co-op choice; solo has only one player). [I]

### 2.5 A V's defensive perks must run on that V's game

NPC hits on a guest's puppet are measured **on the host**
(`Combat.reds:75-86`, `ApplyDamage(…, false)`), where the target is a
puppet with no perks and no `PlayerPuppet` type. Every target-side perk is
therefore missing [R]:

- bullet block and deflect with a blade (`Reflexes_Right_Milestone_2`,
  `Reflexes_Right_Perk_2_1`, `Reflexes_Right_Perk_2_3`:
  `ProcessBulletBlockAndDeflect` `87209-87285`, `DamageManager.CanBlockBullet`
  `218567`, `ProcessDefensiveState` `218666`) — a deflected bullet even
  spawns an attack back at the shooter (`ProcessBulletDeflect` `87288`);
- explosion damage reduction (`Tech_Left_Perk_3_01`:
  `ProcessPlayerFixedPercentageOverride` `85107-85160`,
  `ProcessGrenadeExplosionDamageToPlayer` `85250-85300`, which also scale by
  **difficulty**);
- overshield, `Juggernaut`/`Unstoppable` (`IsJuggernautPerkContitionSatisfied`
  `518032`), damage-reduction effectors (`HardToKillDamageModificationEffector`
  `300717`, `NanoTechPlatesEffector` `303383`), armour, resistances, dodge
  invulnerability (`BaseStatusEffect.DodgeInvulnerability`), Second Heart.

**Rule.** The host sends the **attack**, not a number: `PlayerHit{attack
record, weapon record, instigator npc, hit position/direction, flags, the
attacker-side values the host computed}`. The guest builds a `gameHitEvent`
on its own V with the proxy as instigator and runs it through its damage
system, so its own target side (block, deflect, perks, armour, difficulty,
god modes, death) applies as in solo. A deflected bullet's counter-attack
is then the guest's own attack on a proxy, sent back as an `NpcHit`. [I]

### 2.6 Detection-driven perks need the host's detection

Stealth and combat state are per player (../architecture/stealth.md "Each player is
detected on their own"). Perks read them through [R]:

- `UI_Stealth.highestDetectionOnPlayer` / `highestHostileDetectionOnPlayer`
  (`DetectionMeterEffector` `300783-300830`: stacks a status effect per
  detection step);
- `PlayerPerkData.EntityNoticedPlayer` (`OnBeingNoticed` `40852-40874`,
  fired only for `target.IsPlayer()` at `156351-156355`);
- `PlayerPerkData.CombatStateTime` (combat entry `43656`) and
  `PlayerPuppet.CanAvoidCombat` (`39699-39720`);
- the V's `Visibility` stat, pushed to the engine as
  `VisibleObjectDetectionMultEvent` on the V (`UpdateVisibilityModifier`
  `40818-40828`, listener `243466`): crouch, perks and cyberware that make a
  V harder to see act only on the **local** visible object;
- optical camo: `SetInvisible(true)` on the V and `DeactivateThreat(this)`
  on each hostile NPC's tracker (`44021-44043`).

On a guest none of these see the host's NPCs; on the host the guest's
puppet has its NPC record's visibility.

**Rule.** The host owns detection and reports it per player (../architecture/stealth.md
already plans a per-player bit and level): the guest writes it into its
`UI_Stealth` blackboard, queues `OnBeingNoticed` / tracked-as-hostile on
its own V (`coop-npcs-combat.md` §9.1), and its perks follow. The guest
sends its V's `Visibility` value and camo state; the host applies them to
the puppet (`VisibleObjectDetectionMultEvent` on the puppet, `SetInvisible`
and threat drops for camo). [I]
Status (2026-11-18, untested in game): the guest's combat state follows
the host's `detected` bit through the combat controller, and `UI_Stealth`
follows `NpcAwareness` (engine-knowledge §13.12); `OnBeingNoticed` is not
queued yet.

### 2.7 Effects that run inside NPCs run twice

A status effect a guest's attack puts on a proxy runs on the proxy (its
look, its damage ticks, which reach the host as hits) and is forwarded to
the host (`NpcEffect`, `Combat.reds:144-160`), where it runs again on the
real NPC; the host's ticks deal nothing (`Combat.reds:68-72`). Effectors
inside such effects that change an NPC **without** a hit escape that split
[R/I]:

- `SetTargetHealthEffector` (`307010-307036`) sets the owner NPC's health
  directly (on the proxy: overwritten by the host's next `NpcHealth`; on the
  host: real — fine, if the effect was forwarded);
- `PropagateStatusEffectInAreaEffector` (`307039`), `SpreadEffector`
  (`316386`), `TriggerAttackOnNearbyEnemiesEffector` (`253230`): on the
  guest they reach only proxies (fine: each proxy's new effect is forwarded
  by the same wrap; each attack is a hit); on the host the forwarded effect
  may propagate **again** to the host's NPCs, doubling the spread.

**Rule.** Area and propagation effectors run on **one** game. Effects
forwarded with `NpcEffect` carry an `alreadySpread` flag (or the host strips
propagation effectors from forwarded effects); the guest's propagation,
computed on its proxies, is the one that counts. NPCs beyond the guest's
60 m proxy radius are out of reach of the guest's area effects: acceptable
for the first version, stated. [I]
Status (2026-11-18, untested in game): on the host every hit instigated
by a guest's puppet (or car copy) that is not one of its strikes is
dropped, so a forwarded effect's own explosion or propagation attack never
deals damage twice; a guest's area effects on another V go as `AreaHit`.

### 2.8 Each game has its own difficulty

`StatsDataSystem.GetDifficulty()` (`73092`) scales NPC damage to players
(`85075`, `85138`, `85276`), NPC behaviour (`291711`), and difficulty-based
effectors (`ApplyStatusEffectBasedOnDifficultyEffector` `303127-303160`) [R].
With §2.5's rule, each guest's own difficulty would decide how hard NPCs hit
it. **Rule:** a session plays at the host's difficulty; guests switch to it
on join and back on leave (balance). [I]
Status (2026-11-18, untested in game): done (`Difficulty`,
`StatsDataSystem.SetDifficulty`, Outcomes.reds).

### 2.9 A guest's noise does not reach the host's NPCs

Perks change how much noise a V makes: silent kills and the gag
(`CanPlayerGagOnDetection`, stim radius 3 m, `526200-526230`), the ground
slam's `gamedataStimType.Explosion` broadcast (`558290`),
`BroadcastStimEffector` `299908`. Stimuli are broadcast on the attacker's
game; the host's NPCs never hear a guest (`coop-npcs-combat.md` §9.3).
**Rule:** the host re-broadcasts a guest's stimuli from its puppet (type,
radius, position), sent with the shot or action that made them. [I]
Status (2026-11-18, untested in game): a guest's single broadcasts from
its V (`StimBroadcasterComponent.TriggerSingleBroadcast`, wrapped) travel
as `Noise` and are broadcast again from its puppet; projectile and grenade
explosions are not covered yet.

---

## 3. Perk trees

One row per perk the scripts show, plus the mechanics the task listed.
Risk: **B** blocker, **R** needs a rule, **C** cosmetic, **F** fine as is.

### 3.1 Body

<!-- prettier-ignore -->
| Perk (enum) | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Body_Central_Milestone_3` (lvl 3) | healing item or cyberware → overshield += 30 % of max health (`44437-44462`; fallback `NewPerks.Body_Central_Milestone_3.overshieldPercentage` 0.3); selling resets it | own V | own game; overshield bar is a V stat pool, no sync needed beyond a VFX if any | F |
| `Body_Central_Perk_3_2` (Juggernaut/Unstoppable) | `IsJuggernautPerkContitionSatisfied` (`518032-518045`): overshield > 0 → exhaustion ignored; `JuggernautEffector` `300896`, `UnstoppableEffector` `303924` | target side of NPC hits | §2.5 | R |
| `Body_Left_Milestone_3` (lvl 3) | shotgun/LMG/HMG ranged hits can dismember (`HitReactionComponent.IsValidBodyPerkDismemberAttack` `185955-185975`, `instigator.IsPlayer()`) | runs on the proxy only; host corpse whole | §2.2 (dismemberment travels with the hit) | R |
| `Body_Right_Milestone_1` | vehicle impact damage × 0.5 to the perked driver's car, × 1.5 when the perked player rams (`85515-85530`); NPC passengers × 1.5 (`86379-86388`); no collision damage to the perked passenger (`91305-91330`) | collision outcomes on the driver's game; passengers' damage | §7 | R |
| `Body_Right_Milestone_2` (Wrecking Ball, lvl 2) | sprint body slam with a blunt weapon (`IsWreckingBallAllowed` `551935-551948`, `MeleeBodySlamAttackDecisions` `556045`, `MeleeTransition.MeleeSprintStateCondition` `248874`) | melee hit on a proxy: fine; knockdown it causes | §2.2 reaction | R |
| `Body_Right_Perk_3_1` | superhero landing range grows with fall time (`SuperheroLandEvents.OnUpdate` `558246-558300`): AoE attacks `Attacks.GroundSlamFar/Near`, explosion stimulus, `stagger_effect` | AoE hits proxies (fine); stim not heard by host; others must see the slam | §2.9; `Shot`-like event with the effect name for the puppet | C |
| `Body_Master_Perk_5` | carry and **throw** bodies: fast pickup (`531469`), sprint while carrying (`526933`), `CanThrowCarriedNPC` (`532578`, `CarryDecisions.ToThrow` `532400`); `ThrowEvents` kills the thrown NPC, `ThrownNPC`, ragdoll impulse (`534389-534478`) | carrying itself moves a **host-owned NPC** (the carry state machine runs on the NPC, `scriptInterface.owner` = NPC); the throw kills and ragdolls it | carrying is a takedown-like request: host attaches the real NPC/corpse to the guest's puppet, guest carries its proxy; throw = `Kill` + impulse sent to the host | B |
| `Bloodlust` (effector) | `BloodlustHealingEffector` `306663-306729`: dismembering a live enemy within range restores overshield | needs dismemberment on the guest (it has it, on the proxy) | fine once §2.2 keeps proxy dismemberment | F |
| `Sadism` (effector) | `SadismEffector` `303078-303124`: with overshield, restores healing-item charges | own V | — | F |
| `Rip and Tear` (effector) | `RipAndTearEffector` `306239` (attacker-side `ModifyDamageEffector`) | attacker side | — | F |

### 3.2 Reflexes

<!-- prettier-ignore -->
| Perk (enum) | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Reflexes_Central_Milestone_2/3`, `Reflexes_Central_Perk_2_1` | dash (and air dash at 3) with "leap to target": `DodgeEvents.OnEnter` `553620-553670` moves V to a hostile NPC near the crosshair (`LeapToTarget`), `PlayerMadDashLocomotionBuffer`; fierce dash doubles range | target is a proxy: fine; the puppet must show the dash (`dash` effect, `lcm_dash`) | locomotion through the pose stream; `dash` VFX/sound as an event | C |
| `Reflexes_Central_Perk_1_1` | slide deceleration modifier (`553280`) | own V | — | F |
| `Reflexes_Inbetween_Left_3` | air dash during Kerenzikov: gravity × 0.05 (`TryToActivateAirKerenzikovPerk` `543278-543300`, `DodgeDecisions.ToFall` `553503`) | V floats in the air while the world is slowed: with Kerenzikov owner-local (`coop-time.md` §2.2) the hover stays | puppet placement must allow mid-air hover (ARCHITECTURE "every way a V moves") | C |
| `Reflexes_Inbetween_Right_2` | blade leap attack: tags the target with the scanner (`MeleeLeapAttackObjectTagger` `420445-420470`), weakspot check (`288723`) | tagging is local UI: fine | — | F |
| `Reflexes_Left_Milestone_1` | cool exit from a car (`ExitingDecisions.IsCoolExitAllowed` `550620`), feeds Kerenzikov's `coolExitJump` | an exit animation others must see | ARCHITECTURE "getting in and out of a car, every way" | C |
| `Reflexes_Master_Perk_2` (Gunzerker) | unequipping an SMG reloads it (`HandleWeaponUnequip` `524818-524830`) | own V | — | F |
| `Reflexes_Master_Perk_3` | dash and double jump queue `ReflexesMasterPerk3Triggerd` (`553857`, `557523`) | own V | — | F |
| `Reflexes_Master_Perk_5` | bleeding: lowers dismember threshold (`185881-185891`), bleed ticks leave 1 HP and zero bosses' poise (`219113-219131`), `DismembermentComponent.RequestDismemberment` `188573` | §2.2 (cap at 1 HP, poise, dismemberment) | §2.2 | R |
| `Reflexes_Right_Milestone_2`, `_Perk_2_1`, `_Perk_2_3` | blade blocks bullets for stamina, deflects them back at a stamina threshold, free during any time dilation (`87209-87285`) | target side of NPC hits on a guest: computed on the host, perks absent | §2.5 | B |
| `Reflexes_Right_Perk_2_2` | crit/reaction changes (`ProcessCriticalHit` `86793`, `HitReactionComponent.GetReactionType` `186714`) | reaction chosen on the proxy | §2.2 | R |
| `Reflexes_Right_Perk_3_1`, `Reflexes_Right_Perk_3_4` | blade finisher buffs (`FinisherEndEvents.ApplyFinisherBuffs` `250331`), finisher distance × 2 (`514155-514160`) | finishers are synced workspots on a host NPC | §3.6 finishers | B |
| Kerenzikov-related, Sandevistan-related | time dilation | `coop-time.md` §2.1, §2.2 | owner-local | B |

### 3.3 Technical Ability

<!-- prettier-ignore -->
| Perk (enum) | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Tech_Left_Milestone_3` + `Tech_Inbetween_Left_3` | projectile launcher explosion radius × 1.1 (`ProjectileLauncherRound.ExecuteGameEffect` `219665-219690`); `PostManiac` refills a launcher charge (`44109`) | explosion on the guest hits proxies (fine); others see no projectile | `Throw`-like event for launcher rounds (`coop-player-vehicles.md` §4.1, §4.4) | C |
| `Tech_Left_Perk_2_3` | faster injector animation in combat (`ChangeConsumableAnimFeature` `514972-514990`, reads `GetLocalPlayerMainGameObject().IsInCombat()`) | needs the guest's combat state (§2.6) | §2.6 | R |
| `Tech_Left_Perk_3_01` | own explosions hurt V less (`85153`, `85289`) | target side | §2.5 (self damage is local anyway) | F |
| `Tech_Left_Perk_3_3` | flash/smoke/recon grenades recharge ×2 faster (`GrenadesChargeStatListener` `201395-201410`) | own V | — | F |
| `Tech_Right_Milestone_1` | faster lock-on of the vehicle missile launcher (`419890-419905`) | UI | §7 | F |
| `Tech_Right_Milestone_3` | tech weapon "perfect charge" window and its effects (`WeaponObject.OnUpdateWeaponCharge` `55115-55145`) | others must see/hear the charge and the charged shot | charge state in `Shot` | C |
| tech weapons through walls | charged shots pierce cover (weapon stats, native) | the guest raycasts its own copy of the static world (same geometry); a door open on one game and shut on the other differs | doors are host devices (ARCHITECTURE "the second player acts on the world") | F |
| `Tech_Central_Milestone_2/3`, `Tech_Central_Perk_2_2/3_2/3_3`, `Tech_Master_Perk_3` (Edgerunner: `EdgerunnerHealthReduction`, `Tech_Master_Perk_3_Humanity`) | cyberware capacity, side upgrades, ripperdoc, perk selling (`79672-79748`, `81375-81477`, `221675`, `226895-227014`) | own V, menus | — | F |
| `Tech_Master_Perk_2` | iconic cyberware activation buff (`PlayerPuppet.IconicCyberwareActivated` `40325-40333`) | own V | see §5 | F |

### 3.4 Cool

<!-- prettier-ignore -->
| Perk (enum) | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Cool_Left_Milestone_2`, `Cool_Inbetween_Left_2` | Focus: aiming slows **the world** (`AimingStateEvents.OnEnter` `542818-542858`) | global time dilation | `coop-time.md` §2.4: owner-local, no world slow-down | B |
| `Cool_Left_Perk_3_2` (Deadeye) | reload slow-mo, global (`547583`) | global time dilation | same | B |
| second chance (`CanPlayerDodgeOnDetection`) | an NPC entering combat gives the player `SecondChancePerkTimeDilation`, a crouch dodge out of sight puts `MemoryWipeOnDodge` on the NPC (`168340-168360`, `39682-39699`) | uses `GetPlayer` on the NPC's game (§2.1), slows time, changes a host NPC | §2.1 + `coop-time.md` §2.4; done, §4 Relic tree (Sensory Protocol) | B → fixed, to verify |
| gag on detection (`CanPlayerGagOnDetection`) | NPC gets `GagOpportunity` when it notices a perked player (`168360`); silent-kill stim radius 3 m (`526214`) | `GetPlayer` on the host | §2.1, §2.9 | R |
| `Cool_Central_Milestone_3` (crouch-sprint) | crouch sprint and slide transitions (`289630`, `550988-553388`) | locomotion | pose stream; `LocomotionDetailed` `CrouchSprint` | F |
| `Cool_Central_Perk_3_4` | stamina cost by locomotion (`281583`) | own V | — | F |
| `Cool_Right_Milestone_1` | damage preview on silent-kill weapons in stealth (`HandleDamagePreview` `246946-246965`, reads PSM `Combat`) | needs combat state (§2.6) and the proxy's health | §2.6 | R |
| `Cool_Right_Perk_2_1` | throwables' poison ignores immunity, read from the **local** player (`DamageSystem.IsImmune` `86146-86155`) | §2.1 on the host | resolved on the guest; host applies forwarded effects without the immunity check | R |
| `Cool_Right_Perk_3_1`, `_3_2`, `_3_4`, `Cool_Master_Perk_4` (Juggler etc.) | picking a thrown knife back up (`MeleeProjectile.OnInteractionActivationEvent` `519709-519730`), knife cooldowns reset on finisher/kill (`250265`, `43966`), forced crit after slide/dodge/vehicle (`554813-554830`) | a thrown knife is a local projectile object; others never see it; "on kill" resets need §2.3 | `Throw` event so others see the knife fly and stick; §2.3 | C |
| `Cool_Inbetween_Right_3` | throwable finisher range grows with `Cool_Inbetween_Right_3_Stacks` **on the target** (`514177-514188`) | stacks live on the proxy (guest's own): fine; on the host they don't exist | finisher is the guest's (§3.6) | F |
| `Cool_Left_Milestone_1` | driver-combat damage multiplier (`GetVehiclePerksDamageMultiplier` `87097-87105`), time dilation allowed in vehicles (`CanUseTimeDilation` `240469`) | vehicle combat | §7 | R |
| stealth damage bonuses | `StealthHitDamageBonus/Multiplier` stats on the attacker, applied when the attacker is not in combat | needs the guest's combat state | §2.6 | R |

### 3.5 Intelligence

See §6 (quickhacks, queue, Overclock, cyberdeck).

### 3.6 Mechanics the perks build on

- **Finishers** (`NewPerkFinisherLayer` interaction on every NPC,
  `ScriptedPuppet.ProcessNewPerkFinisherLayer` `22394-22436`;
  `NewPerkFinisherCondition.Test` `514031-514130`): a melee weapon class
  perk unlocks a finisher on a target below a health threshold
  (`IsInFinisherHealthThreshold` `13152`, the attacker's stats) or with
  broken poise (`FinisherActiveStatusEffect`). It is a synced workspot like
  a takedown; the blunt "hold" variant throws and kills the NPC
  (`TriggerNewPerkFinisherBluntHold` `22433-22436`: `ThrownNPC`,
  `Kill(player, true, true)`). The monowire finisher's range grows with
  the target's **quickhack queue size** (`514208-514209`), host state.
  **Blocker** (shared NPC, animation on two entities): same rule as
  takedowns (`coop-npcs-combat.md` §9.4): played locally on the proxy,
  `Takedown`-like request to the host, the NPC frozen there during it,
  poise and queue size replicated so the condition matches. [I]
- **Takedowns, grapples, carrying bodies, disposal**:
  `coop-npcs-combat.md` §9.4; carrying is §3.1 `Body_Master_Perk_5` above
  (the carried body is a host NPC moved by a player). **Blocker.**
- **Air dash, double jump, charge jump, superhero landing**: the V's own
  locomotion (`coop-player-vehicles.md` §4.5); the landing's AoE attacks
  are hits on proxies. **Cosmetic** (placement of an airborne puppet).
- **Smart weapons**: target locks and homing bullets on the guest's
  proxies; damage is the guest's. Others see the shot, not the curving
  projectiles (`Shot` carries no projectile). **Cosmetic.**
- **Stealth bonuses, silent kills, detection**: §2.6, §2.9.

---

## 4. Relic tree (Espionage)

The Phantom Liberty tree (the DLC is required of every player, so both
games have its records). Nine perks, `Espionage_*` (`3984-4176`);
`PlayerDevelopmentData.IsEspionageMilestonePerk` (`81595-81597`) lists the
three milestones [R]. Only six script sites read them by name (`45572`,
`59798`, `204522`, `219870`, `224165`, `250322`); the rest reaches the game
through stat flags and status effects from TweakDB, not read here: those
rows are classified by the mechanism they use.

Each player's tree is its own: perks and Relic points live in that game's
`PlayerDevelopmentSystem` (§1.1), nothing of them is sent. A player who has
not unlocked the tree yet (story progress) beside one who has loses
nothing and breaks nothing: every row below runs on the game of the V that
owns the perk, and the other game only sees its results (strikes, effects,
wounds, stealth, time bubbles), which need no perk on the receiving side.

Audit of 2026-11-19, scripts only, none of it run in game. "Guest" and
"host" name whose V uses the perk. W works, D degraded, B broken; "B →
fixed" is what this pass changed. In-game names are the perk screen's
[I] (the scripts name the enums only, Limits 2).

### Relic tree

<!-- prettier-ignore -->
| Perk | Mechanism [R] | Host user | Guest user | State |
| --- | --- | --- | --- | --- |
| **Jailbreak** (`Espionage_Central_Milestone_1`) | stat flag `CanUseNewMeleewareAttackSpyTree` (`54661`, `248568`, `420484`): each arm's charged attack, below; the monowire's quickhack slot (`59798`, `224165`) | vanilla on its own NPCs; the guests see the health, wounds and deaths | each charged attack is our V's hit on proxies: strikes (ARCHITECTURE combat), kill credit by the strike's puppet; a V never loses a limb (`KeepsLimbs`, and now instant dismemberments too) | per arm, below |
| Mantis Blades leap, **Spatial Mapping** (`Central_Perk_1_4`) | `CanPerformRelicLeap` (`248483`, `420479`), `MantisBladesInvulnerableLeapRelicBuff` on V; the hit after a leap puts `MantisBladesRelicDismemberment` on the target **with no instigator** (`85964`); the grand finale queues a hit on each NPC within 3.5 m and marks it `MantisBladesGrandFinaleNPCMark`, also with none (`128097-128101`), which on a dead NPC gives `MantisBladesInstantDismembermentSpyBuff` (`186228`); an effect tagged `Dismember` kills and beheads (`StatusEffectManagerComponent`, `321244-321266`); buff `Espionage_Central_Milestone_1_Buff_MantisBlades` on a limb our V cut from a live NPC (`45572`) and after a finisher (`250322`) | W | leap and invulnerability local: W (the proxy trails the host's NPC by the draw delay, so a leap at a running target lands a little behind: D, cosmetic). The two marks stayed on the proxy, whose limbs are the host's: **B → fixed**, forwarded (`TandemOutcomes.IsRelicMark`), from a proxy our hit has killed already too (`ProxyEvenDead`: they come a frame after the hit), and put on the host's NPC dead or not (`Npcs.ApplyEffect`); the host's NPC loses its head, `NpcWound` shows it on every proxy. The blood that beheading starts on "the local player's" screen (`321266`) was the host's: **B → fixed**, stopped on the host for a guest's kill (`IsGuestsDismemberment`). The buff: the proxy's own hit reaction tells our V of the limb (`NotifyAboutDismembermentInstigated` runs after the `RequestDismemberment` we refuse, `185423-185437`, `185548-185595`): W, nothing to add (an earlier draft told our V again on `NpcWound`: it would have counted every cut twice) | fixed, to verify |
| Gorilla Arms charged punch, **Limiter Removal** (`Central_Perk_1_2`) | `ApplyRelicMeleewareDamageOnNPCEvent` (`128082`): a hit queued on an NPC within 1.25 m, `GorillaArmsOnePunchNPCMark` on human trash-to-elite targets, **no instigator** (`128094`); the mark on a dead or defeated NPC ragdolls it and sends `SetSlowMoForOnePunchAttackEvent` to "the player" (`126464-126472`), slow motion under the `deflect` reason (`39668-39679`) | W; slow motion local by the time rules (`deflect`, time.md); the guests' proxies die without the fling: D, cosmetic | the hit is our V's: a strike (computed once; the host drops what our puppet would compute, combat.md). The mark stayed on the proxy: **B → fixed**, forwarded, applied on the host's dead or knocked-out NPC (Gorilla Arms are blunt: usually knocked out). That mark would have sent the one-punch slow motion to the **host's** V: **B → fixed**, the host drops it (`TandemOutcomes.IsGuestsOnePunch`, `OnSetSlowMoForOnePunchAttackEvent`). Our own slow motion: `deflect`, local, no bubble (time.md). Another V's puppet as the target: friendly, no damage | fixed, to verify |
| Monowire charged hit, **Data Tunneling** (`Central_Perk_1_3`) | `HasMonowireWithQuickhackSelected` (`54659`, `549229`): the charged hit applies the slotted quickhack at once (`m_IsAppliedByMonowire`, `398945`) and spreads it on light hits (`MonoWireQuickhackContagiousHittableTarget`, `127087`) | W | the quickhack's effects land on the proxy from our V and go as `NpcEffect`; spreading runs on our game only (combat.md, quickhacks). No upload, so no `NpcHack`, as in single player (no upload bar there either) | W, to verify |
| Projectile Launch System charge, **Launch Capacity Override** (`Central_Perk_1_1`) | `ProjectileLauncherRound.SetAttackRecordBasedOnAction` picks `relicAttack` on a charged shot (`219870`) | W; the round is not drawn on the guests (no projectile sync): D, cosmetic | the explosion is our V's: a strike per proxy, `AreaHit` on another V's puppet. The round is not drawn on the others: D, cosmetic | D (cosmetic) |
| **Emergency Cloaking** (`Espionage_Left_Milestone_Perk`) | `CanPlayerExitCombatWithOpticalCamo` unless `BlockOpticalCamoRelicPerk` (`44019-44048`): in combat the camo blocks fully, `SetInvisible`, each hostile NPC's tracker drops V, 1.5 s later `MemoryWipeExitCombat` on it (`39730`) and a sound; that effect's `ClearThreats` empties the NPC's tracker and its `ExitCombatWithPerk` passes it to the squad (`126486-126503`) | the wipe freed every guest those NPCs hunted too: **B → fixed**, the host puts the other Vs back on their trackers (`TandemStealth.OthersHunted` / `HuntAgain`); only the host's V leaves combat | `Stealth` carries camo and "fully blocking"; the host turns the puppet's visible object off and drops the puppet from the trackers of the NPCs around either V, and now resets their detection of it (`Npcs.Detected` would have held the guest in combat on a full meter): **D → fixed**. No memory wipe for a guest (it would free every V). Our combat state follows the host's detection. The "combat avoided" sound (vanilla's comes from our tracker's NPCs; proxies are not there): **D → fixed**, played when the host's NPCs let go | fixed, to verify |
| **Sensory Protocol** (`Espionage_Left_Perk_1_2`) | stat flag `CanPlayerDodgeOnDetection` (`39706`: crouched, combat just started): an NPC entering combat gives `SecondChancePerkTimeDilation` and three `DodgeToAvoidCombatEvent`s (`168337-168349`); a dodge while the NPC does not see V (`IsAgentVisible`) wipes its memory (`MemoryWipeOnDodge`) with a sound and cooldowns (`39682-39696`) | W; the host's NPCs entering combat with a guest no longer offer it to the host's V (Reactions.reds). Its wipe freed every guest the NPC hunted: **B → fixed**, as Emergency Cloaking | the host sends `NpcCue` (combat start), our game slows time and queues the dodge checks. The check asked "has the host's NPC detected us", which includes being in combat with us, so it always refused: **B → fixed**, our game checks the dodge and cooldowns, the host checks its NPC's sight of our puppet (`TandemReactions.DodgedOutOfSight`) and answers `OutOfSight` (new `NpcCueKind`, protocol 7) for the sound and cooldowns. The slow motion is an effector dilation: Time.reds learns its reason as the game sets it, a time bubble like any other (time.md) | fixed, to verify |
| **Vulnerability Analytics** (`Espionage_Right_Milestone_1`) | the V's own `BreachFinderComponent` (`204383`) rolls and tracks one breach on one NPC's `BreachComponent`; a ranged or melee hit on it (not a headshot, a weapon needed) is a forced crit and weak spot (`TryProcessBreachHit` `204452`, `ProcessBreachHit` `204478`); `OnDamageDealt` wears it down (`204502`); destroyed, it fires `Attacks.BreachEMPExplosion` from the V, electric damage a share of the breach's health (`SpawnFinalAttack` `204533`) | W | **per player, by the game's design**: the finder is the V's, so each player rolls, sees and hits their own breaches; the two players' are not the same and nothing is shared (the host's strikes from a guest's puppet carry no `PlayerPuppet` instigator, so they never touch the host's breach). On proxies the breach component is the proxy's own (same record); a hit on it is computed here with its crit and weak spot and sent as a strike with those flags; the wear needs the damage event our proxy path queues (Combat.reds `gameTargetDamageEvent`, with the `BreachHit` flag and the damage): W. The explosion is our V's: strikes on proxies, `AreaHit` on a puppet if its attack type is an area one. Whether the finder picks a brain-off proxy at all is native: to verify | W, to verify |
| **Machine Learning** (`Espionage_Right_Perk_1_1`) | `CyberwareBreachStreak` on V when a breach is destroyed without its explosion (`204522`), stat `BreachStreakBuffBonus` (`GetBreachStreak`) | W | on our own V: W | W |

**Weak spots, in short.** Breaches are each V's own: shown only to the V
whose finder rolled them, on that V's game (on a guest, on the proxy). A
guest's breach hit reaches the host as a strike carrying the weak-spot and
crit flags and the damage the guest computed with the bonus; its explosion
is the guest's area attack (strikes, `AreaHit` on a V). The other player
neither sees nor can hit it, as two players' would be in any co-op game
where the marking is a personal implant.

**What stays degraded and why.** The launcher's round and the exact landing
of a Mantis leap at a running target are cosmetic (no projectile sync;
proxies are drawn a little late), and so is the fling of the host's
Gorilla punch on the guests' proxies (status effects of the host's own V
are not sent to proxies). A Gorilla Arms or Mantis Blades area does not hurt
another V (friendly, as any melee hit). A guest's Emergency Cloaking does
not wipe the host's NPCs' memory: per-player detection wins, the NPCs just
lose that V.

**To verify in game** (scenario `relic-guest` covers the forwarded mark,
`relic-host` the host's answer to a guest's second chance; none of this
was run): a breach shows on a proxy and wears down; the
monowire's quickhack lands on the host's NPC; the Gorilla mark ragdolls the
host's knocked-out NPC without slowing the host; a Mantis grand finale
beheads on both games and the host sees no blood on its screen; Emergency
Cloaking takes a guest out of combat while the host's V stays in it, and
the host's cloak leaves the guest in it; Sensory Protocol slows time for
the guest as a bubble and its dodge out of sight takes the guest out of
combat, not the host; whether `MemoryWipeOnDodge` and
`MantisBladesRelicDismemberment` carry the tags read above (TweakDB).

**First reading (2026-11-18)**, kept for its line references; the rules
it proposed are the ones the table above reports on.

<!-- prettier-ignore -->
| Perk (enum) | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Espionage_Central_Milestone_1` | unlocks the arm cyberware's relic attacks: mantis-blade buff on dismember and after finishers (`45572`, `250322`), launcher `relicAttack` on a charged shot (`ProjectileLauncherRound.SetAttackRecordBasedOnAction` `219860-219875`), monowire parts (`59798`, `224165`) | attacker side; new attack records others must see | relic attacks as `Shot`/`Throw` events with the attack record | C |
| `Espionage_Central_Perk_1_1..1_4` | per-arm "meleeware" perks: launcher (1_1), gorilla arms (1_2), monowire (1_3), mantis blades (1_4) (`PerkDisplayTooltipController.IsMeleewarePerk/MeleewarePerkToIcon` `369583-369623`); effects in TweakDB (`MantisBladesInvulnerableLeapRelicBuff`, `MantisBladesRelicDismemberment`, `GorillaArmsOnePunchNPCMark`) | see §5 (arm cyberware) | §5 | R |
| `Espionage_Right_Milestone_1`, `Espionage_Right_Perk_1_1` | **breaches** (vulnerabilities) on enemies: the V's `BreachFinderComponent` (`204383`) tracks a breach spot on an NPC; hitting it is a forced crit + weakspot (`TryProcessBreachHit` `204453-204475`, `ProcessBreachHit` `204478`); destroying it spawns a final electric attack (`SpawnFinalAttack`) or a streak buff (`OnBreachDestroyed` `204509-204533`) | the component is on the **player**, the breach on a proxy: per-player, all on the guest | fine; the final attack is a hit on a proxy. The breach VFX is the attacker's only (by design) | F |
| `Espionage_Left_Milestone_Perk`, `Espionage_Left_Perk_1_2` | relic optical camo / emergency camo (`BlockOpticalCamoRelicPerk`, `CanPlayerExitCombatWithOpticalCamo` `44021-44043`): camo drops every hostile threat on the player and exits combat | changes **host NPCs' trackers**; detection | §2.6 camo: the guest sends "camo on, exit combat"; the host drops the puppet's threats | B |

---

## 5. Cyberware abilities

Arm and leg cyberware, camo and the time-bending ones are introduced in
`coop-player-vehicles.md` §4.4–§4.7 and `coop-time.md` §2.1–§2.2; this
section adds what makes each one a co-op problem.

### 5.1 Sandevistan, Kerenzikov, Reflex Tuner

- **Time dilation**, global (`coop-time.md` §2.1, §2.2, §2.15): blocker
  with a rule already proposed there (owner-local, no world slow-down, the
  owner's V sped up instead). Additions:
  - kill-extended Sandevistans and their recharge use
    `SandevistanKillRechargeValue` (a stat) and on-kill prereqs: they need
    §2.3 [R stat name, I mechanism];
  - `sandevistanVersusSandevistan`: an NPC Sandevistan reads the **local**
    player's dilation (`coop-time.md` §2.15); on the host it must consider
    every V;
  - the Reflex Tuner's low-health slow-mo and Berserk's slow-mo are
    `SetTimeDilationEffector` records (TweakDB, not seen): the one hook on
    that effector (`307231`) covers them.
- Others see the puppet moving fast with the Sandevistan trail, and the
  Kerenzikov dodge (status-effect VFX on the puppet). **Cosmetic** once the
  time rule exists.

### 5.1a Time bubbles: is everything tied to Sandevistan and slowed time unchanged for its user? (2026-11-18)

Rule from the user: for the V using it, on its own game, a time ability
behaves exactly as in single player (same scale against enemies, same
duration, cooldown and extensions, same triggers and bonuses, same look);
the only co-op difference is that the other players, and what is outside
the ability's reach, are not slowed. Nothing here changes a perk, a stat or
a record: everything is in tandem's layer (`scripts/tandem/Time.reds`,
`Combat.reds`, `Npcs.reds`).

**How far to trust this.** Scripts only (`final.reds` lines). TweakDB was
not read on this machine, so each implant model's numbers, the mods and
fragments, iconic items and consumables are covered **by the mechanism they
use**, not item by item: each reaches the game through one of the rows
below (a stat on V, a status effect with a prereq, a stat pool, an
effector, or a script check). None of it has run in game: "identical"
below means identical by construction (the game's own code runs on the
user's game with the inputs single player gives it), "pending" names what
must be measured.

**How it is kept identical.**

- The user's own game keeps the game's own global dilation, guest and host
  alike (`time_host = full`, now the default): every check, formula, stat
  pool, effect and sound on that game is single player's. On the host the
  NPCs it shares are given their bubble's speed with the per-entity
  dilation, the global factor ignored, so its own slow motion never
  decides what the others get. (`time_host = bubble`, the earlier way,
  puts the host's world back to normal speed: kept as a fallback, with the
  differences it had.)
- Reach: a bubble holds every NPC within 150 m of its V, more than any
  game shows of NPCs (the host shares those within 60 m of its V), so the
  user sees every enemy slowed, as in single player. At the edge the other
  player sees an NPC walking in slow down and one already in stay slowed
  until the end; a V is slowed from 150 m in and freed 15 m farther out,
  so nothing flickers there.

<!-- prettier-ignore -->
| What | How the game does it [R] | For its user (guest and host) |
| --- | --- | --- |
| Time scale of each Sandevistan model, and anything that modifies it | stat `TimeDilationSandevistanTimeScale` on V (`524119-524126`), re-read every update (`524179-524195`) | identical: the bubble's factor is that stat, read every frame, a change mid-effect reaches the NPCs (unit test) |
| Duration, charges, cooldown and its reductions | stat pool `SandevistanCharge` (`523700-523726`), enter cost `524146-524153`, `BaseStatusEffect.SandevistanCooldown` (`494112-494126`) | identical pending in-game check: time a full Sandevistan as host and as guest against single player (`time report` logs the charge) |
| Kills that extend or recharge it (`SandevistanKillRechargeValue`), health on kill, on-kill perks | on-kill prereqs and effectors on V | identical pending in-game check: a guest's lethal hit now kills the proxy at once, by its V (`Npcs.PredictKill`); if the host says the NPC lives, the proxy is brought back 1.5 s later (what the kill gave stays given). Check a kill in the last second extends it |
| Crit, damage and other bonuses while active | status effects of type `Sandevistan` (`494085`), `TimeDilationPSMPrereq` (`311836`), set at `524134` | identical: the state machine and its effects are the game's own; attacker-side bonuses go through the guest's hit pipeline (§2.2) |
| Enemies slowed to the implant's factor | global dilation, V exempt (`522191-522212`) | identical pending in-game check: every shown NPC is in the bubble; on a guest the NPCs slow a network round trip after its own screen does (measure the delay; its proxies animate at its own factor from the first frame) |
| Everything else slowed (effects, physics, debris, sound, screen) | global dilation | identical: the user's own game is dilated. What a host's dilation still slows for the others: things nobody shares yet (its clock, physics props, traffic). To handle as each becomes shared |
| Enemy bullets slow enough to see and dodge | a dilation active on that game turns NPC shots into projectiles (`173366-173373`); a projectile is an `ItemObject`, so a `TimeDilatable` (`52793`, `269557`), slowed by the global factor | host: identical pending in-game check (the bullet gets its shooter's speed, `SlowBullet`: check the logged class and that it flies slowly). Guest: identical pending in-game check: its own game fires the shot from the proxy and decides the hit (`Shots.reds`); check the bullet is drawn, slow, and dodgeable, and that the proxy's shots are not dropped as friendly fire |
| V's own fire rate, reload and animations while active | `CalcCycleTimeDeltaFactor` (`286955-286967`) reads the global factor; items and V's animations follow the game's own time | identical: the formula reads single player's values on the user's game (`time report` logs them) |
| V's shots as time-dilated attacks | `PlayerTimeDilated` records while any dilation is active (`286770-286777`) | identical |
| Blade deflect free while time is slowed (`Reflexes_Right_Perk_2_3`) | `IsTimeDilationActive()` on the target's game (`87242`) | identical: a V is hit on its own game (§2.5) |
| Air dash hover during Kerenzikov (`Reflexes_Inbetween_Left_3`), dodge barred during Kerenzikov | state `kerenzikov`, reason `kereznikov` (`543278-543300`, `553463`, `557930-557941`) | identical |
| Kerenzikov: world and V factors, its buff, cooldown | stats `TimeDilationKerenzikovTimeScale` / `PlayerTimeScale` (`527831-527896`), `KerenzikovPlayerBuff` (`525573`) | identical: a bubble with its world factor |
| Focus (`Cool_Left_Milestone_2`, `Cool_Inbetween_Left_2`), Deadeye's reload (`Cool_Left_Perk_3_2`) | global dilations `focusedStatePerkDilation` (`542858`), `coolReloadPerkDilation` (`547583`) | identical: each makes a bubble with the perk's own factor |
| Implants and perks that slow time on a trigger (Reflex Tuner, Synaptic Accelerator, second chance, …) | `SetTimeDilationEffector` (`307231-307280`), reason, factor and duration in TweakDB | identical pending in-game check: the reason is learned as the game sets it (an observing wrap) and makes a bubble; check the log line `time: effector dilation` when one triggers |
| "No second dilation" rules | `IsTimeDilationActive()` with exceptions (`523680-523685`, `525481`, `307251`, `552914`, `554281`) | identical |
| An enemy with a Sandevistan answering V's | AI sub-action, reason `sandevistanVersusSandevistan`: the NPC speeds itself up by what V's slows the world, so it moves at real speed for as long (`427380-427503`) | host: identical (the game's own; the bubble never replaces a dilation an NPC set itself). Guest: identical pending in-game check: while that sub-action runs on the host the NPC is left out of every bubble (`Counter`); check the AI starts it against a guest's puppet (its condition is TweakDB) |
| Berserk, Overclock | status effects and stats; `berserk` is an effector reason (`307274`) | identical |
| Melee hit stop, deflect and one-punch slow motion | `meleeHit` (`45789`), `deflect` (`39679`, `552917`, `554284`) | identical on the user's game (local, a few frames, no bubble) |
| Skill XP for kills while active, the "time dilation" achievement | `StatusEffectType.Sandevistan` on the killer (`37134-37153`), `CheckTimeDilationSources` (`51487-51492`) | identical; a guest's XP itself is §2.4 |
| Scanner and quickhack panel slow motion | `focusMode` (`529159-529188`) | identical pending in-game check: a time bubble at the scanner's own factor (user, 2026-11-19; it was **off by decision** (user, 2026-11-18) while another V is in the session, on host and guest: the dilation was put back to normal speed under its own reason, the scanner itself untouched). No stat or perk changes that factor (`engine/time.md` §13.40). Alone or in single player: vanilla |

**Still open, for review** (nothing shipped blind):

- _NPC shots at a guest_: decided (the user, option b) and built: the
  guest's game simulates them (`Shots.reds`, ../architecture/time.md). To check in
  game: the proxy holds and fires the NPC's weapon (it is handed the
  weapon record of each shot); `AI_ShootAt` with no ammo cost fires at
  all. The proxy is made hostile to the guest's V when it fires at it (a
  friendly attacker's hits are dropped by `CheckForQuickExit`). Damage
  parity: same record, weapon and difficulty (the session plays at the
  host's, `Outcomes.reds`); the host puts no level or power modifier of
  its own on its NPCs today (the "mean level" rule is decided, not
  built), so there is nothing to copy yet: when it is built, the proxy
  must get the same modifier.
- _Projectiles crossing a bubble from outside_ (fired by an NPC farther
  than 150 m): not slowed. Nothing shown is that far.
- _The host's dilation and what is not shared yet_: its game clock, quest
  timers, physics props and traffic slow with it. Each needs its rule when
  it becomes shared (the clock: `World` already corrects guests).

**Other Vs in a bubble** (user's rule, 2026-11-18): a bubble slows the
other Vs inside it too, each on its own game, by the bubble's factor
divided by that V's own ability's factor, never above 1
(`TimeBubbles::PlayerFactor`, table-tested). No ability: as slow as the
NPCs, and the owner looks very fast. The same or a stronger ability: not
slowed, on an equal footing in the bubble. A weaker one: slowed by the
ratio. The owner's own mechanics are untouched by this; a V slowed by
someone else's bubble keeps its own rules in its own, slower, time.
Vanilla has nothing like it (an enemy Sandevistan speeds the NPC up, never
slows V, `427380-427400`), so there is no native sound or screen effect for
being slowed: open, see ../architecture/time.md.

### 5.2 Berserk, ground slam, cyberware explosion

- `UseBerserkAction` (`224021`) → PSM `OnBerserkActivated`; Berserk UI
  context (`44154`); `BerserkPlayerBuff` adds melee damage (`218734`),
  forces a superhero landing from lower heights (`281977`) and blocks
  workspot finishers (`22416`) [R].
- The landing (`SuperheroLandEvents` `558148-558300`), hard landings
  (`SpawnLandingFxGameEffect` `281377`) and pressure waves (`557591`,
  `557747`, `558082`) spawn **area attacks at V** (`Attacks.GroundSlamFar`,
  `GroundSlamNear`), an explosion stimulus and `stagger_effect` [R].
- `InitiateCyberwareExplosionEffector` (`302826`): an explosion centred on V
  whose radius grows with the share of filled cyberware slots, a physical
  impulse, `cyberware_explosion` VFX [R].
- Problem: area attacks on every NPC in range work on the lander's game
  (each proxy hit becomes an `NpcHit`); but the physical impulse moves
  props the host owns, the stimulus never reaches the host (§2.9), and
  whether these attacks skip friendly targets (the other V's puppet) is
  TweakDB [I].
- **Rule:** the lander's game runs the attack on proxies; the event (effect
  name, position, radius) goes to all games, which play the VFX and sound
  at the puppet; the host replays the impulse on its props and the stimulus
  from the puppet. Puppets are kept out of a V's area-attack targets
  (friendly attitude is enough for direct hits, `86455-86473`; area effects
  need a check). **Needs a rule.**

### 5.3 Arm cyberware

- **Mantis blades**: the leap writes `LeapedDistance` (`288771`) for
  `ModifyDamageWithLeapedDistance` (`305486`): attacker-local, fine. The
  relic finale puts `MantisBladesInstantDismembermentSpyBuff` on the NPC
  (`186229`): dismemberment of a host NPC (§2.2) [R]; handled in §4,
  Relic tree.
- **Gorilla arms**: the NPC's `OnePunchAttackLayer` interaction sets
  `CanOnePunch` on the local player's PSM blackboard (`22336-22355`);
  `GorillaArmsOnePunchNPCMark` (`128094`) makes the NPC ragdoll and sends
  the one-punch slow-mo to `GetPlayer()` (`126464-126472`,
  `186189-186234`) [R]. On the host the slow-mo would go to the host's V
  (§2.1). Rule: proxies keep the interaction for their guest; the mark is
  forwarded; the host ragdolls the real NPC with the puppet as instigator;
  the slow-mo is dropped (`coop-time.md` §2.6). Done (2026-11-19, not
  run): §4, Relic tree.
- **Monowire**: `MonoWireQuickHackApplyEffector` (`307374`) uploads V's
  equipped quickhack on a charged hit and spreads it on light hits
  (`MonoWireQuickhackContagiousHittableTarget`); the contagion cleanup
  writes **`GetPlayer`'s** PSM blackboard (`127087-127095`) [R]. Same path
  as a quickhack (§6); the cleanup must run on the caster's game.
- **Projectile launch system**: `ProjectileLauncherRound.ExecuteGameEffect`
  (`219650-219710`): explosion, perk radius bonus read from
  `GetData(m_user)` (null for a puppet), stimuli [R]. The shooter's game
  owns the real round; the others get a cosmetic round (no damage); area
  hits are forwarded, stimuli relayed. **Cosmetic** + §2.9.
- **Chain lightning** (`TriggerAttackOnNearbyEnemiesEffector` `253230`,
  hits and status effects `253396-253470`): picks the N nearest NPCs
  (`TSF_NPC`) with **no attitude check**, only a civilian filter [R]. On a
  guest it can chain into the other V's puppet or a friendly NPC. Rule:
  puppets are filtered out of targeting results on every game; hits on
  proxies are forwarded. **Needs a rule.**

### 5.4 Optical camo

- `AttachOpticalCamoVisionBlockerEffector` (`249803` → `249727`) registers
  a vision-blocking capsule on V with the sense manager. In combat, with
  `CanPlayerExitCombatWithOpticalCamo` (relic), the blocker becomes fully
  blocking (`PromoteOpticalCamoEffectorToCompletelyBlocking` `46520`),
  `SetInvisible(true)` toggles the visible object (`43505`), each hostile
  NPC's tracker drops V (`DeactivateThreat`, `44019-44056`; only the first
  threat, because of an `if j <` at `44040`), then a delayed
  `ExitCombatOnOpticalCamoActivatedEvent` follows (`39724`) [R].
- Problem: all of it runs on the camo'd V's game against proxies with no
  AI; the host's NPCs keep seeing and hunting the puppet. **Blocker** for
  stealth play.
- **Rule:** the owner sends camo on/off and "fully blocking"; the host
  disables the puppet's visible object (the puppet has a grafted
  `senseVisibleObjectComponent`, engine-knowledge §13), attaches the same
  blocker to the puppet and drops the puppet from its NPCs' trackers. The
  other V keeps seeing the puppet with the cloak shader (`cloak_on`), like
  NPCs with `Ability.CanSeeThroughOpticalCamos` (`190103`). [I]
- Status (2026-11-18, `scripts/tandem/Stealth.reds`, untested in game):
  `Stealth` carries camo, fully blocking, crouch and `Visibility`; the
  host turns the puppet's visible object off, applies the camo effect to
  it and drops it from its NPCs' trackers when blocking
  (engine-knowledge §13.12).

### 5.5 Death prevention and defensive cyberware

- Defensive effectors edit the hit as it lands on V:
  `NanoTechPlatesEffector` (`303383`, cancels hits by chance),
  `WeirdTankyPlatingEffector` (`304358`),
  `HardToKillDamageModificationEffector` (`300717`, damage below a health
  threshold becomes a DoT), `PowerBlockEffector` (`306004`),
  `SmartStorageEffector` (`304245`),
  `ModifyDamageWithCyberwareCountEffector` (`306038`, a health floor at
  `306096-306118`). Dodge, evasion and mitigation roll V's own stats
  (`84781`, `84795`, `84808`); one-shot protection applies only when the
  target `IsPlayer()` and the attacker is not (`85069`) [R]. All target
  side: **§2.5, blocker.**
- **Second Heart**: `ResurrectEvents` (`529013`) applies
  `BaseStatusEffect.SecondHeart`, `Vitals = 2`; `OnResurrected` revives at
  **20 % when `IsMultiplayer()`**, 100 % otherwise (`45083-45096`): CDPR
  had planned a co-op rule [R]. `BloodswellEffector` (`307729-307840`) sets
  a custom health limit, heals by Cold Blood stacks, then applies
  `PlayerInvulnerable` and a cooldown [R].
- **Rule:** V-local death prevention resolves first, on V's game (Second
  Heart, Bloodswell, health floors); the co-op downed state starts only
  when all of it has failed, so its threshold sits below every custom
  limit. Second Heart in co-op revives at 20 %, as the vanilla multiplayer
  branch does. The puppet shows resurrecting or downed. A revive by a
  partner is a request to the owner (authority table). **Needs a rule**
  (with `coop-npcs-combat.md` §6.4).
- Status (2026-11-18, `scripts/tandem/Revive.reds`, untested in game):
  the rules are decided (../architecture/revive.md "Down and out"); Second Heart is
  checked first by vanilla's own conditions and revives at 20 % in a
  session; co-op down starts only when it does not apply
  (engine-knowledge §13.20).
- **Engine leftovers worth reusing:** `PlayerPuppet.OnIncapacitated`
  (`45050-45058`) enables a `Revive` interaction and sets the sense type
  `Deadbody`; `RefreshCPOVisionAppearance` (`45061-45082`) shows another
  client's player through walls (`ForceVisionAppearance`: outline, and a
  fill when incapacitated) [R]. Apply the same to puppets.

### 5.6 Owner-only cyberware

Fine as is: Blood Pump (`Cyb_HealingAbility`, `515430`, `541422`),
overshield and its decay (`303021`, `304619`), `UseConsumableEffector`
(`308320`, auto-heal), `RestoreStatPoolEffector` (`306976`),
`CeaselessLeadAmmoEffector` (`305967`), time banks (`306740`, `305707`),
RAM regeneration, cyberware capacity, `PowerUpCyberwareEffector`
(`221622`, swaps an equipped item: the appearance sync must follow when it
is arm cyberware). Their visuals (`ApplyShaderOnEquipmentEffector`
`299933`, `ChangeAppearanceEffector` `300083`, `ApplyLightPresetEffector`
`299047`, `StartEffectEvent` names) replay on the puppet from the owner's
status-effect list: **cosmetic.**

Kiroshi scanning and highlights (`HighlightEffector` `261119`,
`KiroshiHighlightEffector` `303497`) are local UI; proxies must answer
`RevealObjectEvent` (`261212`). `SpawnSubCharacterEffector` (`304751` →
`SubCharacterSystem` `178208`, `Character.spiderbot_new`) spawns an AI
follower: an NPC, so the host spawns it, following that V's puppet
(**needs a rule**).

---

## 6. Quickhacks, cyberdeck and Intelligence

The upload flow and a first rule are in `coop-npcs-combat.md` §9.5. The
current code goes further: a guest's quickhack completes on the proxy, its
status effects stay there (look, damage ticks, which reach the host as
hits) and are forwarded (`NpcEffect`), and the host's own ticks for them
deal nothing (`Combat.reds:29-43`, `144-160`). So **quickhack damage is
already the caster's**, with its perks; `ProcessQuickHackModifiers`
returning early for a non-`PlayerPuppet` instigator (`85310-85316`) only
matters for damage the host computes.

### 6.1 Fine on the caster's game

RAM cost (`BaseScriptableAction.GetCost` `27100-27330`: perks
`Intelligence_Left_Perk_3_1`, `_Central_Perk_2_2`, `_Left_Milestone_1`,
`_Master_Perk_1`, `_Left_Perk_2_1`, `_Central_Perk_1_2`); the queue
(`PutActionInQuickhackQueue` `122261`), kept **on the target**, so each
player's queue on a shared NPC stays separate (the guest's on its proxy);
Overclock and paying with health (`122990-123130`, `MemoryListener`
`243740`); on-hit effectors `ModifyStatPoolValueQuickhackCostEffector`
(`303646`) and `ModifyDamagePerHackEffector` (`305353`) [R]. They read the
target's status effects and record: proxies need the real NPC's record
and its replicated status effects.

### 6.2 Host code that takes "the player" for the caster

<!-- prettier-ignore -->
| Code [R] | Wrong on the host | Rule |
| --- | --- | --- |
| `NPCPuppet.OnQuickHackEffectApplied` (`126593-126775`) | alert and reveal of the caster only if `instigator == GetPlayerID()` (guest hacks never alert: a stealth exploit); `RevealPlayerPositionIfNeeded` and `TryChangingAttitudeToHostile` cast to `PlayerPuppet`; Reboot Optics resets the host's detection; caster buffs (Weapon Glitch Lvl4, System Collapse cost) go to `GetPlayer` | wrap on the host: "player" = the instigator's puppet for the NPC half (alert, reveal, attitude, detection reset); skip the caster buffs there (the guest already ran them on its proxy, for its own V) |
| `SpreadInitEffector` (`316185-316215`, `m_player = GetPlayer`), `SpreadEffector` (`316386`), `EffectExecutor_Spread` (`316895`) | spread count, range and Overclock chance from the host's stats; spread map on the host's blackboard keyed by action only; executor the host's V | spread runs on the **caster's** game (its stats, its proxies); the host strips spread effectors from forwarded effects (§2.7); NPCs the caster has no proxy of are out of reach |
| `ForceMoveInCombatWhistleEffector` (`317768-317780`, local player's position), `UpdateWhistlePosition` (`317852`), `ApplyLegendaryWhistleEffector` (`317913`) | Bait/Whistle walks the NPC to the **host's** V | "player" = the caster's puppet, from the effect's instigator; Request Backup already uses `GetInstigator()` (`317890`) |
| `ICECounterHackEffector` (`300758-300780`), `AISubActionQuickHack.Hack` (`493744`), `ScriptedPuppet.OnHackTargetEvent` (`22119`; NPC-to-NPC path because `IsPlayer()` is false, `250960`) | **NPC netrunners cannot hack a guest**: the hack lands on the puppet; counterhacks always hit the host's V | the host turns a `HackTargetEvent` on a puppet into `NpcHack{netrunner, action}` for that guest, whose game runs `AIQuickHackAction` on its V with the proxy as executor (trace bar, upload HUD `UploadFromNPCToPlayerListener` `194812`); interrupts go through the host |
| `ScriptedPuppet.ProcessQuickHackQueueOnDefeat` (`21698-21770`) | heal (`Intelligence_Inbetween_Left_3`), RAM refund (`Intelligence_Left_Perk_2_2`) and `BlackWallUploadActive` removal for `GetPlayer` | the guest's proxy goes through the defeat path when the host's death arrives (its queue and `GetPlayer` are right there); the host runs it only for its own V's actions |
| `ScriptedPuppet.ActivateIntelligencePerks` (`24416-24430`, `Intelligence_Left_Perk_2_3`, squad spread) | needs the NPC's squad, which proxies lack | the request carries "spread to n squadmates"; the host picks them |
| `SetFriendlyEffector` (`315998`), `ApplyObjectActionEffector` (`315352`) | control hacks join the host's V's attitude group and squad; executor `GetPlayer` | friendly to every V; squad of the caster's puppet; the attitude change replicated to proxies |
| `PingSquadEffector` (`315552`), `EffectExecutor_PingNetwork` (`492950`), `RefreshPingEffector` (`315815`) | squads and networks exist on the host only; the highlights are information for one player | the host resolves members and devices and sends them back; the caster's game draws them; sharing with the partner is the "players linking up" feature |
| `SystemCollapseModifyRevealBarEffector` (`303884`), `SystemCollapseLifetimeEffector` (`303877`) | trace and cost buffs of `GetPlayer` | they belong to the caster: sent to the caster's game |
| `MadnessEffector` (`315462`, Cyberpsychosis), Memory Wipe Lvl4 (`TryReleaseFromReprimand`), `GrenadeLvl4HackEffector` (`303766`, Suicide / Detonate Grenade) | NPC AI, squads and security system: host state | applied on the host only; suppressed on proxies; kill credit to the caster's puppet |

### 6.3 Devices and world state

- **Breach Protocol** (`AccessPointControllerPS.ProcessMinigameNetworkActions`
  `119626`): reads the local minigame blackboard, runs each daemon
  (camera/turret shutdown, friendly, malfunction, lower ICE, vulnerability)
  on every device and NPC of the network with executor `GetPlayer`, gives
  loot to the local player, sets the persistent `SetIsBreached`, and sends
  `MinigameFailEvent` to the network's NPCs on failure [R]. The access
  point, its network and its NPCs are the host's. **Blocker** (shared world
  state). Rule: the minigame stays the guest's (its UI, its timer; the
  world does not slow, `coop-time.md` §2.7); it sends
  `Breach{access point, programs, traps, result}`; the host applies the
  daemons with the puppet as executor, sets the breached flag, computes
  the loot and sends it to the guest's inventory. Remote breach of an NPC
  (`AccessBreach` `205406` → `NPCBreachEvent` `119726`) takes the same path.
- **Upload awareness**: `BeingHacked` / `BeingHackedWithTraceableHack` go
  on the target at upload start (`24388`), so only on the proxy. Rule:
  `UploadStarted/Ended{target, traceable}` to the host, which applies them
  to the real NPC so its AI can trace (the trace then follows the `NpcHack`
  rule).
- **Camera control** (`Intelligence_Left_Milestone_1`, proximity reveal
  `166113`, `316830`): the camera is a host device; its controlled state
  waits for device sync.
- **Police**: `NotifyPoliceEffector` (`305855` →
  `PreventionSystem.NotifyPolice` `138772`),
  `RewardPlayerWithCrimeScoreEffector` (`306913-306921`),
  `TryDeactivatePreventionByMask` (`305783-305790`, a cyberware mask that
  drops heat): heat is the host's (../architecture/world.md "NCPD chases"), so crimes
  and heat reductions are requests to the host.

---

## 7. Vehicle combat

<!-- prettier-ignore -->
| Perk / mechanic | Mechanism [R] | Problem | Rule | Risk |
| --- | --- | --- | --- | --- |
| `Cool_Left_Milestone_1` (driver combat) | `RoadWarriorDamageMultiplier` with `DriverCombatVehicleManeuvers` (`87097-87105`); dilation allowed in a car (`240469`) | attacker side: fine; the dilation is global | `coop-time.md` | R |
| vehicle weapons (`GearheadDamageMultiplier`, `87088-87093`), `Tech_Right_Milestone_1` lock-on (`419897`) | mounted guns and missiles on NPC targets | hits on proxies: fine; others must see the missiles | launches as `Throw`-like events from the car's owner | C |
| `Body_Right_Milestone_1` (collisions) | impact damage × 0.5 / × 1.5 by the driver's perk (`85515-85530`, `86379-86388`); passenger collision damage only for `GetPlayer()` (`91305-91330`) | collisions are simulated on the driver's game; a guest riding in the host's car is a puppet there (no damage) and sits in a teleported copy on its own game (no collision) | the driver's game computes collision damage for every passenger and sends it to each passenger's game as a hit (§2.5) | R |
| running NPCs over | `VehicleImpact` hits on the NPC | a guest driver hits proxies (sent); the knockdown and ragdoll are the host's | §2.2 reaction | R |
| vehicle quickhacks (`VehicleComponentPS.GetQuickHackActions` `96329`, `Intelligence_Right_Milestone_1`) | take control, force brakes, accelerate, explode | brakes, accelerate, explode are requests to the car's owner (ARCHITECTURE); **take control** needs the car's ownership to move to the hacker | ownership hand-over while controlled | B |

---

## 8. Summary by risk

### Blockers (break the shared world or desync)

1. **Global time dilation** from Sandevistan, Kerenzikov, Focus, Deadeye,
   second chance, Reflex Tuner, Berserk, melee and one-punch slow-mo
   (`coop-time.md`). Rule: owner-local, the world never slows.
2. **A V's defensive side runs on the host for NPC hits** (§2.5, §5.5):
   blade block and deflect, damage-reduction cyberware and perks, dodge,
   armour, health floors, difficulty. Rule: the host sends the attack; the
   guest runs the hit on its own V.
3. **Optical camo and stealth visibility** (§2.6, §5.4): the host's NPCs
   keep seeing a camo'd or perked guest. Rule: visibility and camo state
   are sent; the host applies them to the puppet and drops its threats.
4. **NPC netrunners cannot hack a guest; counterhacks hit the host's V**
   (§6.2). Rule: `NpcHack` to the target's game.
5. **Host NPC code reacting to "the player"** (§2.1, §6.2): second chance,
   gag, finisher offers, Bait/Whistle, quickhack alerts and reveals (a
   stealth exploit), control hacks, spread stats. Rule: on the host,
   "player" = the instigator's puppet; player-side results go to that V's
   game.
6. **Carrying and throwing bodies, finishers, takedowns** (§3.1, §3.6): a
   player moves, kills and animates a host NPC. Rule: takedown-like
   request; the host validates and plays it on the real NPC.
7. **Breach Protocol daemons** (§6.3): they change host devices, NPCs and
   persistent breach state, and grant loot. Rule: a `Breach` request.
8. **Vehicle take-control hack** (§7): needs car ownership hand-over.

### Needs a rule (works, but wrong without one)

- Hit outcome beyond health: poise, cap at 1 HP, non-lethal,
  dismemberment, reaction (§2.2).
- On-kill perks evaluated at hit time: kill prediction on the guest (§2.3).
- No skill XP for guests: queue it on measured damage; assists (§2.4).
- Detection-driven perks and combat state come from the host (§2.6).
- Effects that spread or act in an area run on one game only (§2.7).
- One difficulty per session, the host's (§2.8).
- Guests' noise relayed to the host's NPCs (§2.9).
- Area attacks and chain lightning must not hit the other V (§5.2, §5.3).
- Gorilla-arms one punch, monowire contagion cleanup, launcher rounds
  (§5.3).
- Death prevention before downed; Second Heart at 20 % (§5.5).
- AI followers spawned by cyberware (§5.6).
- Quickhack queue-on-defeat, squad spread, control hacks, ping, System
  Collapse, upload awareness, police effectors (§6.2, §6.3).
- Passenger collision damage and run-over reactions (§7).

### Cosmetic (needs sync to be seen and heard)

- Status-effect VFX of every buff on the puppet (Sandevistan trail,
  Kerenzikov, Berserk, camo shimmer, overshield), shader and appearance
  effectors (§5.6).
- Ground slam, landings, cyberware explosion, dash: effect, sound,
  position (§3.2, §5.2).
- Thrown knives, launcher rounds, smart-weapon projectiles, vehicle
  missiles: `Throw`-like events (§3.4, §5.3, §7).
- Relic attack records, perfect-charge shots (§3.3, §4).
- Air-dash hover and other airborne placements of the puppet (§3.2).

### Fine as is

Most perks: attacker-side damage modifiers (they run in the guest's
pipeline against the proxy), the V's own stat pools, movement, reloads,
cooldowns, RAM, crafting, ripperdoc and perk menus, relic breaches
(`BreachFinderComponent`), Bloodlust and Sadism, owner-only cyberware
(§5.6), RAM cost and queue (§6.1). They need nothing beyond the hit
pipeline that exists.

---

## 9. What could not be seen

- **TweakDB.** Which effectors, prereqs and stat modifiers each perk
  record carries, and their numbers. In particular: which perks use
  `TargetKilledPrereq` / `TargetKilledHitPrereqCondition` (on-kill),
  `DismembermentTriggeredPrereq`, `WoundedTriggeredPrereq`,
  `EntityNoticedPlayerPrereq`, `DetectionMeterEffector`,
  `SetTimeDilationEffector`, `BroadcastStimEffector`, chain lightning
  (`TriggerAttackOnNearbyEnemiesEffector`), cyberware explosion
  (`InitiateCyberwareExplosionEffector`), `ModifyStatusEffectDurationEffector`
  (cooldown cuts); whether area attacks filter friendlies; the camo
  blocker's parameters.
- **The 110 perks with no script test** (of 189 in `gamedataNewPerkType`),
  for example `Body_Central_Milestone_1`, `Body_Left_Perk_2_*`,
  `Reflexes_Left_Perk_2_*` and `_3_*`, `Cool_Left_Perk_2_*`,
  `Cool_Master_Perk_1/2`, `Tech_Left_Perk_1_*`, `Tech_Right_Perk_3_*`,
  `Intelligence_Right_Perk_*`. Their risk is inferred from the effector
  classes they can use (§1.3): an attacker-side modifier is fine, anything
  that touches other entities or shared systems needs the rules of §2.
- **Cyberware items** whose behaviour is data only: Axolotl, Ex-Disk,
  Cogito Lattice, Camillo RAM manager, Adrenaline Converter, Pain Editor
  (only the stat `PainEditorDamageReduction`), Biomonitor, Cockatrice,
  Falcon and Dynalar Sandevistans, Lynx paws (silent-movement stat flags),
  Chrome Compressor.
- **Natives**: `RevealPlayerPositionEffector` (`317524`, empty body), what
  `AISubActionQuickHack.Target()` resolves to, how AI starts a trace, the
  noise of footsteps.
- **2.50-only script changes**: the dump predates 2.50 in places
  (`coop-time.md`, limits).

Next step to close most of these: a TweakDB dump (the `tools/re/` live
reader, or a WolvenKit export) searched for the effector classes above.

## 10. Tests owed

With the mock player as the other V where possible.

1. A guest kills a proxy: does an on-kill perk fire (a knife cooldown
   reset), does skill XP arrive (§2.3, §2.4)?
2. An NPC shoots a guest holding a blade with the deflect perks: blocked
   on the guest's game once §2.5 exists, not today.
3. A host NPC enters combat with a guest's puppet: does the host's V get
   the second-chance slow-mo (§2.1)?
4. A guest uses optical camo next to a host NPC hunting it (§5.4).
5. A guest's Contagion on a group: jumps on the host vs on the guest's
   proxies (§2.7, §6.2).
6. A guest's non-stealth quickhack on a host NPC: does the NPC react
   (§6.2)?
7. A host NPC netrunner targets a guest's puppet (§6.2).
8. A guest's ground slam among host NPCs and next to the host's V (§5.2).
9. A blunt non-lethal kill by a guest: unconscious or dead on the host
   (§2.2)?
10. Second Heart on a guest in a session: revive percentage, downed state
    (§5.5).

The scenarios that cover these, to run: 1 `outcomes-knockout` and a
knife kill by hand; 2 `npcs-shoot-guest` (a blade held), then
`combat-defend`; 3 `takedowns-host` (the cue); 4 `stealth-guest`; 5
`hacks-guest`; 6 `hacks-upload`; 7 `hacks-guest`; 8 by hand; 9
`outcomes-knockout`; 10 `revive-game`.

## 11. Leftovers, item by item (2026-11-19)

What §8 left as "needs a rule" and "cosmetic", and where each stands now.
Every way of fighting, by attacker and target:
[combat-coverage.md](../architecture/combat-coverage.md).

<!-- prettier-ignore -->
| §8 item | State |
| --- | --- |
| Hit outcome beyond health (§2.2) | built: `NpcStrike`, `NpcPoise`, `NpcWound` (combat.md) |
| On-kill perks at hit time (§2.3) | built: the guest predicts the kill (`PredictKill`) |
| Skill XP for guests, assists (§2.4) | built: queued per hit, paid at the death |
| Detection-driven perks (§2.6) | built: combat state and `UI_Stealth` follow the host; `OnBeingNoticed` not queued yet |
| One-game area and spread effects (§2.7) | built: a remote V's hits are dropped on every game but as strikes or `AreaHit` (`IsRemoteComputed`, 2026-11-19: on guests too) |
| One difficulty (§2.8) | built |
| Guests' noise (§2.9) | built for single broadcasts; a guest's grenade is now heard on the host through its copy's own stimuli (`Throw`) |
| Area attacks and chain lightning must not hit the other V (§5.2, §5.3) | built: friendly fire is the session's rule, off by default (`Rules`): every V hit on another V's puppet is dropped, explosions included; on, it goes as `AreaHit` |
| Gorilla-arms one punch (§5.3) | built: on the host, a guest's `OnePunchedMark` gives our V no slow motion (`SuppressPunchSlowMo`); the guest's own game plays it on its proxy |
| Monowire contagion cleanup (§5.3) | open: the cleanup writes the host's V's blackboard on the host (harmless: a flag the host's V does not read then) |
| Launcher rounds (§5.3) | built: the round's copy flies on the others' games (`Throw` Launcher); its explosion is the shooter's |
| Death prevention before downed, Second Heart (§5.5) | built (revive.md) |
| AI followers spawned by cyberware (§5.6) | open: a spawned follower is the host's NPC only when the host spawns it |
| Quickhack queue on defeat, squad spread, control hacks, ping, System Collapse, upload awareness, police (§6.2, §6.3) | partly built (combat.md "Quickhacks"); open: System Collapse's buffs, ping sharing, Breach Protocol |
| Passenger collision damage and run-overs (§7) | open (vehicles.md) |
| Status-effect VFX on the puppet (cosmetic) | open: the puppet shows its V's look, weapons and pose, not its buffs' effects |
| Ground slam, landings, dash effects (cosmetic) | open |
| Thrown knives, launcher rounds, smart rounds (cosmetic) | built: `Throw` copies (combat-coverage.md "Throws"); vehicle missiles open |
| Relic attack records, perfect-charge shots (cosmetic) | open: the shot shows, not the charge |
| Airborne puppet placement (cosmetic) | built (players.md "Every way a V moves") |
| `Cool_Right_Perk_3_1` knife pickup (§3.4) | built: only the owner can pick up its knife (the real one, in its world); the copies refuse |
| Blade bullet block and deflect for a guest (§2.5, §3.2) | built: NPC shots at a guest are fired and decided on its game, so its perks apply; the host's NPC hears of a block or parry (`Defended`). Until 2026-11-19 those hits were dropped as a proxy's own (`IsProxyComputed`) |
