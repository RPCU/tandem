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
game (whether health, damage and loot follow the stat).

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
- **NPC animation** (`scripts/tandem/NpcAnims.reds`, engine-knowledge
  §13.21): the pose stream is too heavy for dozens of NPCs, so the host
  sends what an NPC's animation graph reads and the proxy's own graph
  plays it. Four times a second the host samples each shared NPC's
  high-level, upper-body and stance states (the `PuppetState` blackboard
  NPCStatesComponent fills), the item in its right hand, whom it is after
  (its top hostile threat: a player, or another shared NPC) and the
  workspot it plays in (Codeware's `GetWorkspotInstance`: the workspot
  resource's path hash, its tree entry, the animation), and sends the NPCs
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
  Scenarios `npcs-anims` (guest of a replay with `--anims`) and
  `npcs-anims-host`. **Not yet verified in game**; open: tracers (the
  shot's target is sent, not drawn), cover poses beyond the stance, hit
  reactions, ragdolls.
- **How NPCs see a remote V** (`scripts/tandem/Attitudes.reds`,
  `tweaks/tandem/attitudes.tweak`, `tweaks/tandem/puppet.tweak`): every
  puppet is in an attitude group of its own, `tandem_player_1` to `_3`
  (one per puppet at a time; `tandem_player`, the records' own, beyond
  that), declared as copies of the `Attitudes.Group_Player` record. A
  group's relations are not in its record, and the only non-saved way to
  set them, `SetAttitudeRelation`, sets attitudes between the agents in
  the two groups at that moment, not the group relation
  (engine-knowledge §13.17). So a puppet gets V's attitudes agent by
  agent: once in its group, `SetAttitudeRelation` from it to every group
  seen on an NPC near V or a puppet; then, every 2 s, each NPC within
  60 m of V or a puppet gets the attitudes it and V have to each other on
  the puppet's agent, once per NPC and puppet, again when the NPC's
  attitude to V changes (a quest), forgotten 30 s after it leaves range.
  What changed between an NPC and a puppet alone stays its own (that V
  provoked it). An NPC that appears near a puppet sees it as friendly
  (our groups' default) for up to 2 s. The persistent setter would set
  the real group relations but writes the host's save: not used.
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
  to compare). Verified in game: the groups are accepted. Failed twice:
  civilians friendly to the puppet, neutral to V, and no NPC near V
  (fixed since, to run again; engine-knowledge §13.17).

## Open problems

- **Suppressing guests' own world simulation** (crowds, traffic, encounters,
  quest phases) without breaking systems that expect them. With regions
  only the overlap of two regions is suppressed (`regions.md` §6); traffic
  follows `Endpoint::AreaOwner`.
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
  attitude; a group relation V's own deeds change (if a provocation or a
  security breach by V changes a group relation, not a per-NPC attitude)
  reaches the puppets too; per-NPC attitudes a quest sets towards V are
  not copied.
