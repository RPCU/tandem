# Bosses

Every boss and mini-boss fight of the base game and Phantom Liberty in
co-op: what each is made of, the co-op design for each part, what is
built, the scenarios and the known limits. Built 2026-11-28, **nothing
run in game**: every row is read from the 2.50 scripts (engine facts
§13.181, `research/engine/npcs.md`) and the quests' known structure; its
in-game half is the scenarios below and the checklist. Records the scripts
do not name are found in game with `boss catalogue` and `boss find`
(scenario `boss-catalogue`).

## The catalogue

A boss is an NPC of rarity Boss (`ScriptedPuppet.IsBoss`; the boss bar,
`BossThreatCalculation`, pinning), or of NPC type Chimera or Cerberus
(Phantom Liberty's). Its phases are status effects its own component puts
on it, from its health (a listener on the Health pool) and from tags of
effects the quest, its AI or a hack apply; its weak spots are entities of
their own (`ScriptedWeakspotObject`). **Tandem before 2026-11-28**: the
boss pinned to its game, scaled, its threat of a guest's puppet a
player's (`Threats.reds`), a guest's fire on its weak spots taken by its
owner (`WeakSpotHit`), its proxy's health and death the owner's.

<!-- prettier-ignore -->
| Fight | Record (type) | Component, weak spots | Phases: what moves them | Scripted attacks, arena, cutscenes | Boss bar | Gaps before (closed here: ✓) |
| --- | --- | --- | --- | --- | --- | --- |
| Royce, The Pickup (q003) | `q003_royce_boss`, `main_boss_royce` (Human, an exo) | `RoyceComponent`; a weak spot (the exo's back) | `Royce.Phase1` at attach; suicide on `BossSuicide` or a lost `Madness`; death: the head explodes on a head hit | the Maelstrom den's arena; his talks before (S: own) | yes | ✓ phase effects on the proxy; ✓ a weak spot the host broke |
| Sasquatch, Pyramid Song area (q114) | `q114_main_boss_sasquatch`, `main_boss_sasquatch` (Human) | `SasquatchComponent`; a weak spot | `Phase1`, `PainInhibitors`, `Healing` at attach; `WeakspotDestruction`: drops her hammer, fights the local V | the Netwatch effect on V (`NetwatcherGeneral`, removed at her defeat) | yes | ✓ her weak spot broken on both; ✓ the V who broke it is the one she turns on |
| Oda, Play It Safe (q112) | `main_boss_oda`, `Cyberninja_Oda` (Human) | `OdaComponent`; a weak spot (his mask) | `Oda.Masked` at attach, its removal the fact `q112_oda_mask_destroyed`; `Oda.Emergency` at 70/55/40/25/10 %; cloak and heal by tags | his blades and cloak (AI events); the fight's end: C (watch a) | yes | ✓ mask, cloak, emergency on the proxy; the AI events' blade light (limit 3) |
| Adam Smasher, the endings (q113, q116) | `main_boss_adam_smasher`, `q113_boss_smasher`, `q116_boss_smasher` (Human) | `AdamSmasherComponent`; plate, torso, launcher and head weak spots (hit shapes switched per phase) | health 80 % plate off, 50 % phase 2 (arm off, every weak spot broken), 30 % `Invulnerable` emergency, 5 % phase 3; hacks jump phases (`Overload`, `SmasherCustomHack`) | teleports around his target (`SmasherFindTeleportPositionAroundTarget`); the rip finisher (`AdamSmasherRipInteraction`); the tower's cutscenes C | yes, dividers from his record | ✓ hack-driven phases on the proxy; ✓ his teleport in the targeted V's view |
| The Animals' boss, Grand Imperial Mall (q110) | `q110_animals_boss` (Human) | none of its own; a boss takedown (Sasquatch's) | its health | free fight | yes | ✓ the bar on both |
| Placide, the Voodoo Boys' line (q110: a fight if V turns on them) | `name:placide` (not read) | not read | its health | scenes around it (S, own) | if rarity Boss | to read: `boss find name:placide` |
| Woodman, Automatic Love (q105: a fight if V picks one) | `name:woodman` (not read) | not read | its health | the Clouds' back rooms | if rarity Boss | to read: `boss find name:woodman` |
| An exo boss of a side job (mq062) | `mq062_boss_darius` (Human, a Royce takedown) | not read | its health | | yes | ✓ the bar on both |
| Cyberpsychos (open world) | `ma_*_cyberpsycho*` and the rest (Human) | none; a Royce takedown for two of them | their health | MaxTac at some (`q305_maxtac`: the bar, MaxTac counts) | yes | ✓ the bar on both |
| The Chimera (q302) | `kind:chimera` (Chimera) | `ChimeraComponent`; heat sinks and plates (blocked while `ChimeraWeakSpotToggle`) | Immortal; health 70 % phase 2 (repair, healing), 40 % gas cloud, 1 % phase 3 (`InvulnerablePhase3`, weak spots broken); quest and AI tags (`ChimeraArenaMode`, `ChimeraForcePhase2/3`, `ChimeraSuicide`); `chimera_defeated` | Metalstorm (a shot at its combat target); `questUseWeapon_ChimeraMetalstorm_NodeType` (the quest's own target); stomps (camera shake on the V near); C at its phases | yes; hidden while `Chimera.HideBossHealthBar` | ✓ phase effects and blocked weak spots on the proxy; ✓ bar on both, hidden on both |
| Cerberus, Somewhat Damaged (q305) | `kind:cerberus` (Cerberus) | `CerberusComponent`, `ScannerControlComponent` | its detection of the player (`CerberusDetectionCombat`); scans and eye flares by AI events | the chase: R (own), its scripted kill (`CerberusRangedKillTask`: its target teleported away) | if rarity Boss | ✓ combat on any V's full detection; the kill and the camo (limits 1, 2) |
| Kurt Hansen, Firestarter (Kurt's stronghold) | `name:kurt` (not read; `kurt_replacer` is a replacer, not him) | `KurtzBossComponent` | `BossNoTakeDownInfinite`; knife and pistol looks by AI events | the stronghold assault; C in its cutscenes | yes | the looks the AI events switch (limit 3) |
| Phantom Liberty's exo mini-boss (world encounter) | `we_ep1_01_mini_boss_2nd_phase` (a Royce takedown) | not read | its health | | yes | ✓ the bar on both |
| Phantom Liberty's droid mini-boss (`sts_ep1_12`) | `sts_ep1_12_droid_miniboss` | not read | its health; a quickhack only while its scene is cinematic | | yes | ✓ the bar on both |
| Minotaur mechs (Militech, Dogtown) | Mech, not a boss | `MinotaurMechComponent` | `JamWeapon`, `SystemCollapse` (quickhacks: `NpcStatus` mirrors them) | its death blast | no | none new |
| MaxTac | rarity MaxTac | none | its health | | yes | ✓ the bar on both |

Every fight's quest side (its doors, its arena's walls, its phase
cutscenes, its fail state) is the host's quest, read in game with `quest
trace` (`scripted-moments.md`).

## The co-op design

The references: Destiny and Diablo run one boss on the server and every
client draws it; Elden Ring's co-op gives one host's boss with more health
for each phantom; Borderlands scales a boss's health with the players
present and lets any player draw its fire. Tandem's rule for every part:
the game that runs the boss decides, every other game draws its own copy
of the boss's own machinery from that word.

- **One authority.** A boss is pinned to its game
  (`TandemRegionRules.Pinned`: never handed over); the story's are the
  host's. Every other game shows its proxy.
- **The same phase, attack and telegraph at the same moment.** A proxy's
  own copy of the boss's component still runs (§13.181), so the boss's own
  code switches hit shapes, plates, appearances and blocked weak spots on
  it, if it gets the same input as the owner's: the owner's health
  (`NpcHealth`, its listener crosses the same thresholds) and the owner's
  effects (`BossState`, below). The attacks and their telegraphs are the
  owner's NPC's: its animation states (`npcs.md`, NPC animation), its shots
  (`NpcShot`), its hits on each V (`PlayerHit`). The proxy is one network
  trip behind; a phase effect waits for the proxy's health to reach the
  health it was said at (0.5 s at most), so the proxy's listener crosses
  the phase first, as the owner's did.
- **Either V is its target.** Its threats weigh a guest's puppet as a
  player (`Threats.reds`, `BossThreatCalculation`'s 2.0); what the bosses'
  own scripts give the local V goes to the V concerned (below, "Whom the
  boss's scripts take"). Every boss scenario counts which V the boss fought
  over the fight (`boss targets`).
- **Health scaled, not a sponge.** The scaling of `npcs.md` (×1.6 for two,
  ×2.2 for three) is checked for bosses: their phases are percentages of
  health, so each phase stretches by the same factor; two players of equal
  damage take 1.6 ÷ 2 = 0.8 of the solo fight's damage time, and the
  windows a timer holds (Smasher's emergency, the Chimera's repair, an
  invulnerable phase) do not scale. So a two-player boss lasts a little
  less than alone, never longer: no lighter boss curve is needed. Weak
  spots are not scaled (their `WeakspotHealth` pools): two players break
  them twice as fast, the Destiny way of rewarding aim. To revisit after
  the first group fight (`enemy_scaling_health`, the host's).
- **Weak spots break on both.** A guest's fire: its proxy's breaks on its
  screen and its share goes to the owner (`WeakSpotHit`); the owner's
  break (its own V's fire, a hack, a phase) reaches every proxy as the
  weak spot's health in `BossState`, the game's own way of breaking one
  (`DestroyWeakspot`, `Kill`).
- **One boss bar, the game's own, on both games.** Vanilla shows it for a
  boss whose threats hold the local player; each game queues the game's
  own `BossCombatNotifier` on its V for a boss in combat within 100 m of
  it and hostile to it, whoever it fights, and lets it go when the boss is
  out of the fight. Its dividers are the record's, its health the pool's
  share (which scaling keeps), `Chimera.HideBossHealthBar` (mirrored)
  hides it on both.
- **A phase cutscene for both.** A phase's scene is the host's quest's:
  a cinematic one (tier 5) is watched through the host's camera (`story.md`,
  "Spectating"; `scripted-moments.md` rows: Oda's end, the tower, the
  Chimera's phases, Kurt's stronghold), a tier 2-4 scene played from the
  guest's own V.
- **Arena doors and locks hold both.** Doors and gates are devices, the
  host's word (`devices.md`); a quest's lock of the area holds every V
  (`revive.md`, "No far respawn out of a locked quest area"; the fast
  travel lock); a quest teleport into the arena takes the guest along
  (`scripted-moments.md`, T).
- **Down in a boss fight.** `revive.md`: a V in a session goes down, not
  dead, and is dropped from the boss's threats while down; the fight is
  lost only when every V is down or out; a partner revives it.
- **Death, loot, credit.** The killer's game gets the kill (on-kill perks,
  XP); loot is rolled per player, quest items shared (`loot.md`); a boss's
  defeat and its quest outcome (spare, kill, the finisher) are the host's
  quest's; the boss finisher on a defeated proxy goes to the host
  (`Takedowns.reds`, kind Animate).
- **A guest who joins mid-fight.** The owner's `BossState` is kept for
  late joiners with the NPC's other records; the first word on a proxy
  takes off what its own component put on at its attach and the owner does
  not have (a latecomer's proxy starts in phase 1 whatever the fight's).
  Scenario `boss-late-join`.

## Inside the plugin

- **`BossState`** (protocol 27, `core/include/tandem/protocol.hpp`): the
  boss's health at the change, the status effects its scripts read
  (sorted, at most 24) and each weak spot's health (percent, at most 16,
  255 unread), as the cut multiplayer replicated a `gameWeakspotComponent`
  (§13.174). Its owner's word, once per change (the endpoint drops a
  repeat), kept for late joiners and in the NPC digest, gone with the NPC.
  `TandemFight.SendBossState`, `BossEffects`, `BossSpots` (event kind 16).
- **`scripts/tandem/Bosses.reds`** (`TandemBosses`, ticked by
  `TandemNpcs`): the bosses our game runs and shows, found once a second;
  the owner reads each boss's effects every tick (`Mirrors`: all but what
  `NpcStatus` and the hits carry: quickhacks, a breach's daemons, upload
  interrupts, damage ticks, a death) and its weak spots, and says them on
  change; a proxy wears them as above; both ask the bar; the owner counts
  whom each boss fights.
- **Whom the boss's scripts take** (§13.181, "What reads the local
  player"):

<!-- prettier-ignore -->
| Script | Vanilla takes | Here |
| --- | --- | --- |
| `BossStealthComponent`, a quickhack | combat with the local V | the hacker's puppet joins its threats (`TandemBosses.Reacted`) |
| `SasquatchComponent`, `WeakspotDestruction` | combat with the local V | the V whose fire last took a share of its weak spots, within 10 s |
| `CerberusDetectionCombat` | the local V's detection | any V fully detected (a wrap: every puppet's detection, read only) |
| `SmasherFindTeleportPositionAroundTarget.IsInPlayersFOV` | the active camera | the targeted puppet's view (a wrap) |
| `CerberusDetectionOpticalCamo` | the local V's cloak | the local V's (limit 2) |
| `CerberusRangedKillTask` | its combat target teleported | the same, a puppet's undone (limit 1) |
| footsteps, stomp shake, scan's full-screen effect | the local V | right on every game: each copy's component shakes its own V |
| Metalstorm, the boss's AI attacks | its combat target | either V (`Threats.reds`) |

- **Degradation** `boss state` (fallback, per boss): a proxy whose weak
  spots are not its owner's count, or an owner's effect record our game
  lacks: it shows its own for what differs.
- **Tests**: `tests/protocol_test.cpp`, `tests/referee_test.cpp` (the
  referee's rules for `BossState`: an NPC shown, values in range, never
  ours back), `tests/endpoint_test.cpp` (once per change, the late joiner,
  gone with the NPC). Commands (`tools/game.sh cmd`): `boss catalogue`,
  `boss find kind:K|name:S`, `boss spawn RECORD|kind:K|name:S`, `boss
kill`, `boss health P`, `boss effect RECORD`, `boss spot N`, `boss
report`; queries `boss bar|effects|spots|target|targets|phase|hits|health|state`.

## Scenarios

`tools/scenario.sh`'s `boss_fight RECORD NAME` (two games, `TANDEM_PARTNER`,
`two-instances.md`): our game hosts and spawns the boss hostile 15 m
ahead, the partner 3 m from it, our V 30 m back, god mode on both; checks
the proxy on the partner, the same phase on both, the game's boss bar on
both HUDs, the boss targeting the partner and its hits reaching the
partner's V, the partner's hits struck on the host's boss, a weak spot the
host breaks broken on the partner's proxy, the death on both and both bars
gone, with screenshot pairs (`NAME-bar`, `NAME-fight`, `NAME-dead`, ours
and `-partner`). `BOSS_RECORD` overrides each scenario's record. In this
order:

1. `boss-catalogue` (one game): the game's boss records, and those of the
   Chimera, Cerberus, Kurt, Woodman and Placide; fills the rows above.
2. `boss-smasher`, then `boss-late-join` (the partner joins at 45 %).
3. `boss-oda`, `boss-royce`, `boss-sasquatch`.
4. `boss-animals`, `boss-cyberpsycho`, `boss-darius`, `boss-smasher-tower`.
5. `boss-chimera`, `boss-cerberus`, `boss-pl-miniboss`, `boss-pl-droid`,
   `boss-kurt`.
6. `boss-woodman`, `boss-placide` (if `boss-catalogue` finds them bosses).
7. `npcs-weakspot` (by hand: a person's shots at a proxy's weak spot).

## Known limits

1. **Cerberus's scripted kill** teleports its combat target away
   (`CerberusRangedKillTask`); on a guest's puppet the guest's own stream
   puts it back: what kills the host's V in the chase leaves a guest's V
   alive. A death the quest scripts would need its own message to that V's
   game; q305's chase only.
2. **Cerberus and a cloaked guest**: `CerberusDetectionOpticalCamo` reads
   the local V's `Cloak` effect; a puppet wears a V's camo as a look only
   (`StatusVisuals`, never its gameplay), so a cloaked guest counts as not
   cloaked to this one condition.
3. **AI events to a boss's component** (`OnAIEvent`: Oda's blade light,
   Kurt's knife and pistol looks, Cerberus's scans and eye flares) come
   from the owner's behaviour tree; a proxy's is off, so its component
   never hears them. Next: forward the event names the bosses' components
   read with the NPC's animation states (`NpcAnims.reds`).
4. **A grab or paired attack on a guest** (a boss's synchronized animation
   on its target): its hit reaches the guest's V (`PlayerHit`); the paired
   animation is the owner's NPC's with the puppet. Not read.
5. **A quest's own target**: quest nodes that aim an attack at "the
   player" (`questUseWeapon_ChimeraMetalstorm_NodeType`) aim at the host's
   V, the quest's player; both games see it.
6. **Facts a proxy's component writes** (Oda's `q112_*`, a weak spot's
   `m_addFact`) land on the guest's game too; its own story is frozen in
   the host's (`story.md`), so they move nothing there.
7. **`boss hits`** counts our V's health drops near a fighting boss, not
   the boss's attacks one by one; `boss targets` is the owner's top threat,
   sampled twice a second.
8. **Weak spots are not scaled** (above: on purpose, to revisit with the
   first group fight).

## In-game checklist

- [ ] `boss-catalogue`: the records of the rows marked "not read".
- [ ] Each `boss-*` scenario passes on two instances; its screenshots
      show the same plates, phase and bar on both.
- [ ] A defeated boss (`BaseStatusEffect.Defeated`, mirrored): the guest
      gets the boss finisher on its proxy and the host's quest takes it.
- [ ] The Chimera's and Smasher's phase changes look the same moment on
      both screens (one network trip).
- [ ] A real fight through the quests (q003, q112, q113/q116, q302,
      q305): `quest trace` on the host, the guest's view of each phase
      cutscene.
