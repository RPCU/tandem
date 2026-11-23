# NPCs

## Authority: enemy level

**Enemy level** (decided 2026-11-18): enemies the host spawns and runs are
scaled to the **mean of the character levels of the players in the
session**, not to the host's level. Each game reports its V's level to the
host when it changes (a field of a reliable message, alongside the look);
the host applies the mean where the game scales an NPC to the player's
level, and recomputes it when someone joins, leaves or levels up. In the
game an NPC's level is its `PowerLevel` stat (`gamedataStatType.PowerLevel`,
set from its record's `PowerLevelMod`; scripts compare it with the
target's, e.g. `GetPowerLevelDiff`): the likely lever is a stat modifier on
each NPC the host runs, moving its `PowerLevel` to the mean. To confirm in
game (whether health, damage and loot follow the stat). Built 2026-11-19:
`progression.md` ("Levels").

## Enemy scaling

**Enemies are tougher for each extra player near them** (asked
2026-11-19: "add multiplier to enemies stats based on how many players
joined"). Built and tested without the game (`core/include/tandem/scaling.hpp`,
`scripts/tandem/Scaling.reds`); **not yet run in game**. The engine facts:
`research/engine/combat.md` §13.50.

| Players near the NPC | Health | Poise, stagger and knockdown thresholds | Armour | Damage it deals |
| -------------------- | ------ | --------------------------------------- | ------ | --------------- |
| 1                    | ×1     | ×1                                      | ×1     | ×1              |
| 2                    | ×1.6   | ×1.25                                   | ×1.1   | ×1              |
| 3                    | ×2.2   | ×1.5                                    | ×1.2   | ×1              |
| 4                    | ×2.8   | ×1.75                                   | ×1.2   | ×1              |

Health ×(1 + h·(n−1)) with h = 0.6 (`enemy_scaling_health`), poise and the
hit reaction thresholds ×(1 + 0.25·(n−1)), armour +10 % per extra player
up to +20 % (`scaling::For`; the constants are in
`core/include/tandem/scaling.hpp`). Less than ×n health on purpose:
players share targets and revive each other, and a fight with three should
last about as long as one alone, not three times longer. Armour is capped
low because it divides every hit on top of the health. **The damage an NPC
deals is never scaled**: each V takes the hits it would alone (each game
applies them by its own rules, `combat.md`).

**Where it applies.** On the game that runs the NPC (the host, or the
NPC's owner under regions, `regions.md`), to hostile NPCs only: an NPC in
combat or hostile to any V, once seen so (sticky until it goes).
Civilians, vendors, companions and allies are never scaled. Bosses
(`NPCRarity` Boss: `ScriptedPuppet.IsBoss`), MaxTac, elites, officers and
named enemies follow the same rule (decided: one rule, no exceptions);
bosses and quest NPCs are pinned to their game (`TandemRegionRules.Pinned`),
and the boss health bar shows the pool's share, which the scaling keeps.
It is a runtime stat modifier on each NPC, the way the game's own
difficulty, level and rarity scaling put theirs: one `Multiplier` modifier
per stat (`Health`, `Poise`, `Armor`, `StaggerDamageThreshold`,
`StaggerDamageThresholdInCover`, `KnockdownDamageThreshold`,
`KnockdownDamageThresholdInCover`), which multiplies the stat's final value
after every additive term, so the difficulty's and the level's count once
and ours once on top. Never a record edit. A plain `AddModifier`: never
saved; taken off when the NPC is no longer shared and when the session
ends. The thresholds are percentages of the total health the NPC's
`HitReactionComponent` read when it was attached, so their modifier is
×poise × unscaled health ÷ that total (`scaling::ThresholdFactor`): in
points, one hit must take a quarter more per extra player to stagger it,
whatever that component read.

**Who counts** (`scaling::Counter`, run on the owner twice a second,
`TandemProgress.ScaleCount`). A V counts for an NPC from within 100 m of it,
and stops beyond 130 m; or while it fights it (in combat with it, its top
hostile threat, or it hit the NPC in the last 5 s), from anywhere, and for
5 s after. 100 m: past the range most fights are fought at, inside the
150 m at which players are linked (a counted V always sees the NPC as a
proxy) and beyond the 90 m to which an owner keeps sharing it; 130 m: the
hysteresis, still inside the link, so a V at the edge does not make the
count flicker; the engagement rule keeps a sniper beyond it counted while it
shoots. Only players of the session count (a player that left or vanished
stops counting at once); the owner's V is always one. Positions are each V's
here (ours, the puppets).

**When the count changes** mid-fight, the stats follow at the next pass
(within half a second) and the NPC keeps its share of health and poise: no
instant heal, no drop (`scaling::KeepFraction`; the plugin reads and sets
the pools in percent). An NPC that dies keeps nothing (its count, its word).

**The other games.** The owner says the count (`NpcScale`, to everyone who
sees the NPC, kept for late joiners until it goes, in the NPC digest when
it is more than one). Every proxy of it carries the same modifiers: a
guest's hit is computed against the proxy's armour (the attacker's game
runs the target's armour, `combat.md`), and its kill prediction against the
proxy's health. The health bar would be right without it (`NpcHealth` and
`NpcPoise` carry fractions; a strike carries points, which the owner takes
from its own scaled pool).

**The host's rule.** `tandem.ini` `enemy_scaling = on|off` (on by default)
and `enemy_scaling_health = 0.6` (advanced: ini only, 0 to 2.5), the host's
for the whole session: `Rules` carries them (`kRuleEnemyScaling`,
`enemyHealth` in whole percent; added before `Rules` was released, so no
protocol bump), kept for late joiners and in the world digest. The settings
page has it as a host setting (`Settings-EnemyScaling`), changed at once (the
host sends `Rules` again; every owner puts the modifiers on or off within a
second); a guest's page shows the host's.

**Hand-overs** (`regions.md`): the modifiers stay on the body. The count the
old owner said stays the NPC's word until the new owner counts; the new
owner puts its own count on from the share of health that came with the
NPC (`Restore`), which the modifiers' change keeps.

**Not scaled: XP, loot and rewards.** Each player already gets their own
(`progression.md`): loot is rolled per player, kill XP is the killer's,
quest rewards each V's. Skill XP for damage is a share of the NPC's health
pool, so a scaled NPC pays the same total as alone, split by who did the
damage: in a group each V earns less per enemy than alone. **The user may
want to revisit this** (e.g. an XP bonus per extra player near).

Tests and tools: `tests/scaling_test.cpp` (the formula, the rule, the share
kept, the thresholds, who counts and the hysteresis, the digest),
`tests/endpoint_test.cpp` and `tests/regions_endpoint_test.cpp` (`Rules`
carried to late joiners; `NpcScale` from an owner to everyone, said once,
kept, gone, early before its spawn, the heir's word after a hand-over),
`tests/protocol_test.cpp`; `tandem-peer --near-npc` (stand 4 m from a host
NPC), `host --enemy-scaling on|off --enemy-scaling-health F`; scenario
`enemy-scaling`; `tools/game.sh cmd npc scale` (the rule, each NPC's count,
health, poise, armour and thresholds).

To check in game: that the `Poise` stat is the poise pool's maximum (`npc
scale` shows both); the boss bar and the NPC health bar after a count
change; that the hit reaction component's total health is the unscaled one
(`npc scale` shows it); how a 3-player fight feels with the defaults.

Decided by the user 2026-11-19: the numbers as built (above; damage, XP
and loot unscaled); one rule for bosses (a lighter boss curve is the alternative if
bosses become sponges); the count radius 100/130 m (moved to
`enemy_scaling` keys only if a quest's arena is larger); XP revisited after
the first group session.

## Inside the plugin

- **NPCs** (`scripts/tandem/Npcs.reds`, milestone 5): once the world
  plays, the host lists the NPCs within 60 m of its V ten times a second
  (`GetNPCsAroundObject`, skipping our own puppets, copies and the dead),
  announces each new one (`NpcSpawn`: a session id, record, appearance,
  transform), sends where all of them are (`NpcStates`) and which left
  (`NpcGone`). A guest spawns a proxy of each with the dynamic entity
  system, switches its brain off at once (`TandemProxies.Pacify`), and two
  seconds later, once its rig has loaded, hands it to tandem's movement
  controller as puppet `0x10000 + id`, drawn like a remote player.
  Verified 2026-11-18 with the game as a guest of a replayed recording: 10
  proxies, 0.00–0.03 m from their tracks, walking when the host's NPC
  walks. A guest's own NPCs are never the world's: in a session its
  crowd density is 0 (`CommunitySystem.ChangeDensityModifier`) and what
  still spawns within 60 m (encounters, communities) has its brain off and
  is hidden for good (`HideIrreversibly`). When the session ends the
  proxies are deleted and the density reset. Verified 2026-11-18: 9–12 own
  NPCs hidden, 9–13 proxies shown, all proxies gone when the host left.
  **Per-player regions replace the host-only half of this**
  (`regions.md`): every game runs and announces the NPCs around its own V,
  shows the NPCs of the players linked with it as proxies, keeps its crowd
  on unless its V is deep in a higher-ranked linked player's region, and
  hides only new spawns in that overlap. Built and tested without the
  game; the verified runs above are of the host-only build.
  Open: police (`PreventionSpawnSystem`), quest NPCs the guest needs to
  talk to (milestone 6). Hit reactions and ragdolls from other players'
  hits: built, untested in game (`presence.md`, `combat.md`).
- **The same people on both screens** (`tools/npc-compare.py A.log B.log`,
  `npc list` on both games at once: each NPC we run, each proxy with or
  without a body, what a linked game shares that we show no proxy of, and
  our own unshared ones with why and whether they are in sight). Two
  instances at a Kabuki crossing, 2026-11-23: with the 60 m share, 14 to
  25 of 34 to 45 NPCs within 100 m of either V were on one screen only
  (each game's own crowd 60 to 100 m out, and the drivers below); with
  `regions::kShare` 100 m and 128 a message, on foot 84 of 85 and 88 of 98
  (the 10: the old 64 cap). What is still one-sided: the drivers and
  passengers of the others' cars, never seated here (`traffic.md`,
  "Occupants"; 42 of 52 on foot with traffic).
- **A kill seen first, and NPCs that cannot die** (`Npcs.reds`
  `PredictKill`, `Reconcile`): a guest's hit that takes the last of a
  proxy's health kills it there at once (on-kill perks, loot, XP), ahead of
  the owner's word. The owner says with the NPC's health whether its game
  can kill it now (`NpcHealth` `kNpcProtected`, protocol 11: god mode
  Immortal or Invulnerable, `GetImmortality`, or its `IsInvulnerable`
  stat; `TandemNpcs.Protected`, read every round): a protected NPC is never
  killed ahead (a quest's immortal, a boss between phases). A kill seen
  first with no death from the owner 1.5 s on, whatever its health says
  meanwhile (an immortal at its floor sends none), is undone: the proxy is
  spawned again where it fell. Scenario `npcs-protected`; not yet run in
  game.
- **What a proxy cannot do, its owner does** (`WeakSpotHit`, `HackTraced`,
  protocol 11; combat path, to the NPC's owner as `NpcStrike`, one-shots): a
  proxy's brain, senses and squad are off, so what the game runs on them
  happens on the owner's game. A boss's weak spot (`WeakspotObject`) a
  guest's fire damages breaks on its screen as alone, and the share of its
  health each hit took drains the owner's real one (`TandemWeakSpots`:
  its index and record), whose destruction sets its fact and moves the
  boss on (`npcs-weakspot`). The share is the `WeakspotHealth` pool's: read
  before and after the hit, else measured (`TandemWeakSpots.Measure`:
  what `StatPoolsManager.ApplyDamage` would drain of that pool through the
  hit's shapes; the spot's `Health` pool gave 0, 2026-11-21). An NPC netrunner's trace that runs to its end
  on the traced V's game sets the netrunner and its squad on that V's
  puppet on the owner's game, as the game does for its own V (the guest's
  wrap of `UploadFromNPCToPlayerListener.OnStatPoolMaxValueReached` skips
  the squad part: `hacks-trace-squad`). The guest sets where the trace
  began (`LastPlayerHackPosition`, as `RevealPlayerPositionIfNeeded` does):
  unset, the upload stopped at its first tick; the reasons the game stops
  one silently are logged (`hacks: npc N's trace of our V stops at …`), and
  the scenario reads the upload's pool fill (`query upload`). What stops a netrunner's upload
  (`HackingInterrupted`, `HackRevealInterrupted`, a `CyberwareMalfunction`
  effect, which its behaviour puts on it) goes as `NpcStatus`, and the
  proxy wears it where the upload runs (`hacks-interrupt`). Not yet run in
  game. Open: the stimuli a guest's V gives a proxy (aimed at, a weapon
  drawn, an illegal act: `NPCPuppet.OnAimedAt`, `OnDetectedEvent`) never
  reach its owner; only `TriggerSingleBroadcast` does (`Noise`).
- **NPC animation** (`scripts/tandem/NpcAnims.reds`, engine-knowledge
  §13.21): the pose stream is too heavy for dozens of NPCs, so the host
  sends what an NPC's animation graph reads and the proxy's own graph
  plays it. The owner reads (when: "When an NPC's animation is read",
  below) each shared NPC's
  high-level, upper-body and stance states (the `PuppetState` blackboard
  NPCStatesComponent fills), the item in its right hand, whom it is after
  (its top hostile threat: a player, or another shared NPC) and the
  workspot it plays in (`TandemActivity.Workspot`, the game's own instance
  map, engine-knowledge §13.134: the workspot resource's path hash, its tree
  entry, the animation; Codeware's `GetWorkspotInstance` finds none on
  2.50, so every NPC went with workspot 0 until 2026-11-21; not yet run in
  game), and sends the NPCs
  whose state changed as `NpcAnims` (reliable; the host keeps each NPC's
  latest for late joiners). Every NPC shot passes `AIWeapon.Fire` and goes
  as `NpcShot` (unreliable). A guest applies a state once the proxy is
  ready: the states as NPCStatesComponent applies its own (anim features
  under the component's own names, the `combatLocomotion`/`inCrouch`/
  `inCover` wrappers the controller unlocks, the blackboard, combat mode;
  not the brain's side effects: senses, voice, squads, the local V's
  combat state), the weapon in the proxy's hand, a look-at of head and
  chest at the target, the reload sounds when the upper body enters and
  leaves `Reload`, and a shot as the weapon's own effects (flash, report,
  shells). A workspot is played from a `WorkspotResourceComponent` every
  proxy gets while it is assembled: the host's resource is written into
  it (by RTTI name), it is placed where the proxy stands, the proxy is put
  in it (`PlayNpcInWorkspot`) and jumped to the host's entry; meanwhile
  the controller keeps still (`HoldPuppet`). When the host's NPC gets up
  the proxy plays the slow exit and the controller takes over again.
  Scenarios `npcs-anims` (guest of a replay with `--anims`),
  `npcs-anims-host` and `npcs-anims-events`. **Not yet verified in game**; open: tracers (the
  shot's target is sent, not drawn), cover poses beyond the stance, hit
  reactions, ragdolls.
- **How NPCs see a remote V** (`scripts/tandem/Attitudes.reds`,
  `tweaks/tandem/attitudes.tweak`, `tweaks/tandem/puppet.tweak`): every
  puppet is in an attitude group of its own, `tandem_player_1` to `_3`
  (one per puppet at a time; `tandem_player`, the records' own, beyond
  that), declared as copies of the `Attitudes.Group_Player` record. A
  group's relations are not in its record: they are `Attitude` records
  (`group1`, `group2`, `value`). `scripts/tandem/AttitudeTweak.reds`, a
  scripted tweak (TweakXL's `ScriptableTweak`, run as the `.tweak` files
  load), clones every `Attitude` record that names
  `Attitudes.Group_Player` once per group of ours, the group in the
  player's place (`Attitudes.TandemRelation<N>_<other group>`): the list
  is the running game's, a patch's or a mod's changes included. Whether
  the engine's relation table takes records TweakXL adds is **not yet
  known**: each start logs
  `attitudes: declared relations took|took in part|did NOT take: A of B
groups player is not friendly with read the same for tandem_player_1 …
(tweak: R Attitude records read, P with player, C cloned)`, read back
  through `GetAttitudeRelation` (`cmd puppet relations` logs it again).
  The runtime copy stays under it, for what the records cannot give and
  as the net if they do not take; the only non-saved setter,
  `SetAttitudeRelation`, sets attitudes between the agents in the two
  groups at that moment, not the group relation (engine-knowledge
  §13.17), so it works agent by agent: `SetAttitudeRelation` between a
  puppet's group and a group seen on an NPC near V or a puppet, when the
  declared relation is not V's (V's changed by a quest, or the records
  did not take); and each NPC gets the attitudes it and V have to each
  other on the puppet's agent where they differ, on the tick after it
  came into the world (`NPCPuppet.OnGameAttached`, queued:
  `TandemStreamKind.Npc`, attached; `attitudes: fresh npc … engine alone:
same|differs, on its first tick: same`, debug) and every 2 s within
  60 m of V or a puppet for what changed since (a quest), forgotten 30 s
  after it leaves range. What changed between an NPC and a puppet alone
  stays its own (that V provoked it). The police are left to `Police.reds`
  by the runtime copy; their declared relation is `player`'s base one.
  The persistent setter would set the real group relations but writes the
  host's save: not used.
  Vs are friends to each other. Companions (the host's V's followers:
  Jackie, Panam, Judy, Takemura) see a guest as V does, and on the host
  take on whoever fights a guest (`TandemReactions.RallyCompanions`: an
  NPC hitting a guest's puppet or hit by a guest becomes a threat to every
  companion hostile to it); they follow the host's V only
  (combat-coverage.md "Companions and allies"). The puppet record drops what made it Panam:
  her group and faction (Aldecaldos: anim wrappers, the scan's faction),
  her voice, her burning immunity and damage presets, and shows no NPC UI
  (`hideUIElements`, `hideUIDetection`): no detection indicator from the
  senses Body.reds grafts. `tools/game.sh cmd puppet attitude` logs each
  puppet's group and how the NPCs around V see it next to V; scenario
  `puppet-attitude` (it spawns a ganger near V so there is always an NPC
  to compare); `puppet-attitude-fresh` fails when an NPC new to the world
  does not see the puppet as it sees V on our first tick with it. Queries
  (`testing.md`): `attitude group NAME`, `attitude npc ID|fresh|nearest`,
  `attitude fresh`. Verified in game: the groups are accepted. Failed
  twice: civilians friendly to the puppet, neutral to V, and no NPC near V
  (fixed since, to run again; engine-knowledge §13.17). Not yet run: the
  declared relations, the copy at the spawn.

- **Whom NPCs attack** (`scripts/tandem/Threats.reds`, engine-knowledge
  §13.123): an NPC weighs each hostile threat in `AIActionTarget`, and only
  the engine's player-controlled flag gives a threat a player's weight; a
  puppet has it off, so the game that runs an NPC weighed a guest's V as
  one more NPC. On that game (the host, or a guest owning NPCs under
  regions: a proxy's brain is off, and the `Tandem.Proxy` tag is checked
  too) a puppet gets the player's treatment: `RegularThreatCalculation`
  adds the 0.99 between an NPC's base (0.01) and a player's (1.0), scaled
  as vanilla scales the whole (×0.1 grappled, ×0.5 unarmed);
  `BossThreatCalculation` the 1.99 between 0.01 and 2.0 (not for a
  companion dropped after 3 s as its target, as vanilla);
  `MadnessThreatCalculation` has no player term. `BlockTargetingPlayer` (a
  quest's "do not target the player") is honoured for a puppet: after
  `UpdateThreatsValue` a blocked puppet's threat base is -1, and
  `SetTopThreatToCombatTarget.SetCombatTarget` refuses it, as vanilla does
  for a player; blocked means the effect on the puppet, on the NPC, or on
  our V (a quest that spares the player on this game spares every V). These
  run on the AI's job threads: they read, call the wrapped method and change
  only their out value or, as the wrapped method does, the NPC's own target
  tracker. A puppet is known by its spawn tag (`Tandem.Puppet`, read from
  Codeware's `DynamicEntitySystem` under its lock), never from
  `TandemSystem`'s puppet list, which the game thread changes. Tools:
  `tools/game.sh cmd npc threats` (each NPC we run with hostile threats: its
  top one, what it holds on our V and on each puppet), `npc threats npc` /
  `npc threats player` (vanilla's weights or the player's, to compare;
  `test reset` puts the player's back), `npc threats spawn` (two hostile
  gangers 4 m beyond the first puppet), `test query threat puppet`;
  scenario `npcs-target-guest`. Not yet run in game.

  The other readers of the flag (§13.123), not given to puppets: security
  areas (`SecurityAreaControllerPS.AreaEntered`/`AreaExited`: one
  `m_isPlayerInside` per area in a persistent state, which a puppet leaving
  would clear while V is inside) and the senses' breach detection
  (`OnSecurityAreaCrossingPerimeter`, "breacher is not player"): a trespass
  is the security system's state (blacklist, alarm), which needs an owner
  of its own first; NPCs still see a trespassing puppet by their senses.
  `NPCPuppet.CheckNPCKilledThrowingGrenade`: the local player's "Denied"
  achievement, never a guest's kill. `MechanicalImpactComponent
.FilterOutVehicleImpact`: a car's hit on an NPC is its driver's game's
  (Wheels.reds), so a puppet's car copy must not knock it down twice.
  Proficiency level-up (`PlayerDevelopmentData`): telemetry only. The
  native `gameEffectObjectFilter_NoInstigatorIfPlayerControlled`: out of
  scripts' reach (the flag itself, row 2 of the research's ranked table).

- **Vendors** (`scripts/tandem/Vendors.reds`, `progression.md` "Vendors"):
  a vendor placed in the world is named by its owner with its static
  entity (`NpcVendor`, with the NPC's records). A player whose game shows
  it as a proxy gets the game's "Trade" prompt beside it, which opens its
  own game's vendor of that entity: its own copy, hidden behind the proxy
  as a twin, keeps its own stock and prices. A proxy never gets a vendor
  of its own in the save; a vendor that dies in the session is hidden in
  every game until it ends. Built and tested without the game; scenario
  `vendor-guest`.

- **Placed NPCs** (`regions.md` §3, §14): an owner names the static
  entity of each placed NPC it announces (`NpcPlaced`); the other games
  find their own copy by it (twins), and its death goes into the session's
  ledger: dead in every game until the session ends.

## When an NPC's animation is read

`scripts/tandem/NpcAnims.reds`, the owner's side (2026-11-22; before: every
shared NPC whole, four times a second). On the game's own change: wraps
queue the NPC (`TandemStreamKind.Npc`, not attached, `cls` what changed;
one taker, `TandemAttitudes.Drain`) and `TandemNpcAnims.Events` reads it
whole and sends it on the next frame's tick, at most one message per NPC
and frame. Same message and wire form; the receiving side is unchanged.

<!-- prettier-ignore -->
| What | The game's event (wrapped: reads, calls, queues) | Under it |
| --- | --- | --- |
| high-level, upper-body, stance | `NPCStatesComponent.ChangeHighLevelState`, `ChangeUpperBodyState`, `ChangeStanceState`: every script path ends there (engine-knowledge §13.21, the states' entry points) | the 2 s poll |
| item in the right hand | `NPCPuppet.OnItemAddedToSlot`, `OnItemRemovedFromSlot` (slot `WeaponRight`) | the 2 s poll |
| workspot (resource) | `ScriptedPuppet.OnWorkspotStartedEvent`, `OnWorkspotFinishedEvent`; read again 0.25 s later (the instance map may fill after the event) | the 2 s poll |
| workspot entry | none (the workspot's own sequence) | the 2 s poll only |
| whom it is after | none: `NewThreat`, `HostileThreatDetected`, `ThreatRemoved` say who is in the threat list, not which is on top (the tracker's own weighing, each AI update); read with every event of the NPC | 4 Hz, only for an NPC out of `Relaxed` |

- Gain: a state leaves on the frame after the change, not up to 250 ms
  later, and a state held for less than 250 ms is no longer skipped.
- Work left per shared NPC: four times a second, the lookup of what was
  sent and, out of `Relaxed`, one `GetTopHostileThreat`; every 2 s the
  whole read (three blackboard reads, the hand's slot,
  `IsActorInWorkspot`). Before: the whole read four times a second.
- The 2 s poll sends what it finds and logs what an event should have told
  (info, the first 10 then every 25th):
  `npc anims: poll caught a change the events missed: npc N stance 3 -> 1 (K missed so far)`.
- The command `npc anims` counts the changes sent by what found them:
  `first`, `by events` (with the events taken per kind), `settled`,
  `by the target read`, `polled`, `missed M (S of them states)`. Each
  event's change is a debug line:
  `npc anims: event (stance): npc N stance 3 -> 1`.
- Scenario `npcs-anims-events` (a fighting ganger: an event line for the
  upper body or the stance, no missed state). Not yet run in game: which
  thread the three wraps run on, and whether native code sets a state past
  them (the missed line says).

## Open problems

- **Suppressing guests' own world simulation** (crowds, traffic, encounters,
  quest phases) without breaking systems that expect them. With regions
  only the overlap of two regions is suppressed (`regions.md` §6: the
  crowd's density or a null area, new spawns hidden, own traffic held
  back); traffic follows `Endpoint::AreaOwner`. Open: removing a car of
  ours that is already in the overlap.
- **A second V is welcome wherever the first is.** In co-op every V
  starts with the attitudes the player has: friendly to friendly NPCs
  (fixers, allies, quest characters, vendors), neutral to the neutral,
  hostile to the hostile, so a quest never breaks because an ally sees a
  stranger. Puppets get an attitude group of their own that copies the
  `player` group's relations (a TweakXL record), not `player` itself: in
  `player`, hostility to one V spreads to every V (§13.11). What a single
  V does (provoking a gang, crimes) changes that NPC's attitude to that V
  only, per NPC and per player, by the game's own rules. Done (2026-11-18,
  to verify in game with scenario `puppet-attitude`): see Inside the
  plugin. Known limits: a group seen only on devices (a turret's security
  system with no NPC of its faction near) keeps the record's default
  attitude unless the declared relations took; a group relation V's own deeds change (if a provocation or a
  security breach by V changes a group relation, not a per-NPC attitude)
  reaches the puppets too; per-NPC attitudes a quest sets towards V are
  not copied.
