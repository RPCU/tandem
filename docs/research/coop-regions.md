# Players apart — per-player region authority (feasibility)

Feasibility study for the step after the first version in
[`ARCHITECTURE.md`](../ARCHITECTURE.md) ("Region limits: players anywhere on
the map, one world"): the world around each V is run by that V's own game
(its NPCs, fights, traffic, police) while the players are apart; the host's
game runs it when they are together; ownership hands over cleanly as they
move; lasting facts go to the host's record of the world.

**Status: desk research, 2026-11-18. Nothing here was run in game.** It
builds on what tandem already does and has measured (engine-knowledge §13,
milestone 5) and on [`coop-npcs-combat.md`](coop-npcs-combat.md) and
[`coop-quests-world.md`](coop-quests-world.md); it does not repeat them.

## Sources and tags

| Cited as          | What                                                                                          |
| ----------------- | --------------------------------------------------------------------------------------------- |
| `final.reds:LINE` | decompiled vanilla scripts of 2.50, `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`      |
| `CW path:LINE`    | Codeware source (MIT), `~/.cache/tandem/ref/codeware`, checkout `60c4ab5` (2026-11-14)        |
| RTTI              | `data/rtti-natives-2.50.tsv`, `data/rtti-vtables-2.50.tsv` (live dump of 2.50)                |
| `Npcs.reds:LINE`  | tandem's own scripts, `scripts/tandem/`; `protocol.hpp` is `core/include/tandem/protocol.hpp` |
| `§13.x`           | sections of [`engine-knowledge.md`](engine-knowledge.md)                                      |
| `NC §x`, `QW §x`  | sections of `coop-npcs-combat.md`, `coop-quests-world.md`                                     |

Tags: `[S]` read in the vanilla scripts, `[R]` read in the RTTI dump, `[C]`
Codeware's source, `[T]` measured by tandem in game (2.5-verified in
engine-knowledge), `[I]` inferred by me from those (not stated anywhere,
not tested).

## Contents

0. [Short answer](#0-short-answer)
1. [What each game does on its own when the players are apart](#1-what-each-game-does-on-its-own-when-the-players-are-apart)
2. [Hand-over: an NPC owned by game A becomes game B's](#2-hand-over-an-npc-owned-by-game-a-becomes-game-bs)
3. [Arbitration: who runs which part of the world](#3-arbitration-who-runs-which-part-of-the-world)
4. [Persistence: the lasting facts a region owner reports](#4-persistence-the-lasting-facts-a-region-owner-reports)
5. [Police, traffic, parked vehicles, gigs](#5-police-traffic-parked-vehicles-gigs)
6. [Cost estimate and stages](#6-cost-estimate-and-stages)
7. [Probes to run before stage 1](#7-probes-to-run-before-stage-1)

---

## 0. Short answer

- **Letting each game run its own surroundings while apart is mostly
  "stop suppressing".** Crowds, communities, encounters, police, traffic,
  vendors and devices are all per-game systems that already work for the
  local V. What has to be built is the decision of when a game is "apart"
  (groups of players, decided by the host), the switch between quiet and
  live world, and a ledger of deaths so a placed enemy killed by the guest
  stays dead in the host's world. **Medium: about 6–9 days.**
- **Hand-over of a live NPC is possible without despawning in one
  direction, and needs a respawn fallback in the other.** A real NPC
  becoming a proxy is what tandem already does to a guest's own NPCs
  (`TandemProxies.Pacify`, `[T]`). A proxy becoming a real NPC means
  switching its brain back on, which nobody has tested; the fallback is to
  spawn a fresh NPC of the same record at the same place with the
  transferred state. Health, status effects (with remaining time and
  stacks), attitude, combat target, high-level state and defeat state can
  be carried with vanilla calls; the behaviour tree's internal state,
  cover, squad and animation cannot. **Large: about 12–18 days**, the
  riskiest piece being NPC identity across two games (static ids are not
  yet measured on 2.50, and encounter members have dynamic ids).
- **Solo gigs are the hardest part.** A gig is a quest; a guest's game
  can run it natively, but only if the guest's quest suppression
  (milestone 6) is per quest phase rather than global, and the host must
  then take the gig's outcome into its own quest state without having run
  the graph. NCPD hustles are tractable (Codeware already models every
  piece of state one consists of); fixer gigs are an open research risk.
  **Medium for hustles (5–8 days), large and uncertain for fixer gigs
  (15–25 days).**
- **Recommendation: stage 1 first** (after two days of probes, §7). It
  fixes what the user sees today when the players split up (an empty
  bubble around the guest, §1.1), it needs no hand-over, and it settles
  two design rules that milestone 6 must respect anyway: guests' quest
  suppression must spare the open-world phases, and the guest's police
  lock must apply only while grouped.

---

## 1. What each game does on its own when the players are apart

### 1.1 What tandem does today when a guest goes far

- The host shares only the NPCs within 60 m of **its own** V
  (`Npcs.reds:53-55`, `:149`). A guest a kilometre away gets no
  proxies. `[T]`
- The guest, in a session, sets its crowd density to 0 once
  (`Npcs.reds:72-76`, `ChangeDensityModifier(0.0)`) and every 0.5 s
  pacifies and hides for good every NPC of its own within 60 m of its V
  (`HideOwn`, `Npcs.reds:115-130`, `HideIrreversibly`). `[T]`
- Result: a guest away from the host walks in a 60 m bubble with no
  pedestrians, no gangs, no vendors' NPCs, no encounter. Traffic is not
  switched off (no known switch: QW §10; whether density 0 also removes
  cars is untested, QW §14 q25). The host's puppet of the guest stands
  wherever the guest is, in a part of the map the host has not streamed.
  `[I]` (consequence of the above)
- ARCHITECTURE's first version avoids this with a soft tether. Per-player
  regions replace the tether.

### 1.2 Why the host cannot run the guest's surroundings

A game streams and simulates only around its own V: the host has no
sectors, navmesh, communities or AI near a guest who is far away. Nothing
script-visible moves or adds a streaming centre:

- `worldRuntimeSystemWorldStreaming`, `worldRuntimeSystemNodeStreaming`,
  `worldRuntimeSystemStreamingQuery` and `gameStreamingMonitorSystem` exist
  with no script functions; `AIAdjustableStreamingRangeTarget` is a
  `gameObject` subclass with no natives and no use in `final.reds`. `[R]`
- The only streaming queries are tests: `worldNavigationScriptInterface
.IsNavmeshStreamedInLocation(origin, tolerance, …)` (`final.reds:196013`)
  and Codeware's `Resource/Ready` callback on `worldStreamingSector`
  (campaign-fork.md:162). `[S]` `[C]`

So "the host runs everything" cannot extend to players far apart, as
ARCHITECTURE already says; a second streaming centre in the host's game
would need engine work nobody has located (and would double the host's
streaming and AI cost). `[I]`

### 1.3 What is free if the guest's game simply runs its region

Every row is a per-game system that already serves the local V; tandem
only has to stop switching it off while the guest is apart.

| System                         | Runs by itself for the local V because                                                                                                                                            | Free while apart?                                                    |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| ambient crowd                  | community/population systems spawn around the player (NC §7.2); `ResetDensityModifier()` restores it (`Npcs.reds:98-104`, `[T]`)                                                  | yes                                                                  |
| communities, placed spawners   | open-world quest phases drive them: `open_world_global_commuinity_phase`, `city_entities`, `open_world_vendors`, `open_world_fixers` (`CW src/App/Quest/QuestPhaseRegistry.cpp:10-20`) | yes, **if** the guest's quest phases are not suppressed (§1.4)       |
| encounters (gangs at a spot)   | same; members deterministic per spawner on one save (§13.7, `[T]`)                                                                                                                | yes                                                                  |
| NPC AI, senses, fights         | the guest's own NPCs with brains on, against its own V                                                                                                                            | yes: the full single-player combat, no network in the loop           |
| police                         | `PreventionSystem` chases `m_player`, the local V (`final.reds:138636`), spawns around it (QW §9.3)                                                                               | yes, **if** the guest's prevention lock is lifted while apart (§5.1) |
| traffic                        | `worldRuntimeSystemTraffic` (no switch, QW §10)                                                                                                                                   | yes (it is never off today)                                          |
| vendors, stash, loot rolls     | per game and per player already (QW §7.3–7.6)                                                                                                                                     | yes                                                                  |
| devices, doors, cameras        | each device's PS in the guest's game (QW §7.2)                                                                                                                                    | yes locally; lasting changes need reporting (§4)                     |
| time, weather                  | already the host's (`World` message)                                                                                                                                              | nothing to change                                                    |
| XP, kill rewards, loot pickups | the guest's own V, its own save                                                                                                                                                   | yes, and simpler than today (no forwarding)                          |

### 1.4 What breaks

1. **Two copies when the players meet.** Both games have spawned the
   world near the meeting point: two crowds, two copies of each encounter,
   two police forces. The host-announces rule (ARCHITECTURE, Identity)
   needs one of them to give way, and its NPCs may be in a fight. That is
   the hand-over problem (§2) and the merge policy (§3.3).
2. **NPC identity.** Only placed entities share an id across games
   (`EntityID.IsStatic`, NC §7.1). Crowd ids are reused for other people
   on the next load; encounter members have dynamic ids that repeat on the
   same save but are not a safe match across games (§13.7, `[T]`). **No
   static-id NPC has been measured on 2.50 yet** (§13.7, NC §11 q7), so
   how many NPCs a death ledger can name exactly is unknown: §7 probe 1.
3. **Deaths not shared.** A gang the guest wipes out alone lives on in
   the host's game and save. Vanilla remembers a dead placed NPC through
   its persistent state (`ScriptedPuppetPS.m_isDead`,
   `m_wasIncapacitated`, `final.reds:24687-24689`, set in `OnDied` and
   `OnIncapacitated`, `:21689`, `:21824`) and the community's spawner
   bookkeeping; both live in the game where it died. §4 is the fix.
4. **Quest-owned NPCs.** NPCs a quest phase spawns (`questSpawnManager`
   nodes, QW §6.1) and quest prefabs (QW §6.3) exist only in the game
   running that phase. Apart, a main-story NPC near the guest does not
   exist on the guest unless its own story phase spawned it — and the
   guest's story phases must follow the host (ARCHITECTURE, Saves). Rule:
   quest NPCs belong to the game running the quest and never hand over;
   the main story is played together.
5. **Quest suppression must be per phase.** Milestone 6 plans to stop a
   guest's quest phases. But the open world itself is quest phases
   (Codeware's list above: city scenes, global community phase, fixers,
   city entities, vendors, minor activities), so a guest that suppresses
   all phases would have no communities when apart. Story phases follow
   the host; open-world phases must keep running on a guest that runs its
   region. `[C]` `[I]` This is the main constraint stage 1 puts on
   milestone 6.
6. **The other V far away.** Each game keeps the other player's puppet
   (an always-spawned dynamic entity) at a position it has not streamed.
   Apart, the puppet should be disabled
   (`DynamicEntitySystem.DisableEntity`, NC §7.3) and the player shown on
   the map instead: the leftover remote-player mappin
   (`RegisterRemotePlayerMappin`, `final.reds:45502-45508`, QW §1). `[I]`
7. **World saving.** A guest's world-state saving is locked in a session
   (ARCHITECTURE, Saves); what the guest's game changes apart is lost from
   its own save, which is the rule anyway. Only the host's record must
   learn it (§4).

---

## 2. Hand-over: an NPC owned by game A becomes game B's

Hand-over happens in two cases (§3.2): a player joins a group (the joining
game's NPCs in a fight must go to the group's owner, or stay its own for a
while), and a player leaves one (the NPCs chasing them must come with
them, because the owner's game will stream them out).

### 2.1 The two directions

**Real → proxy, in place (the giving game).** The giving game keeps the
same entity on screen and only switches its brain off:
`TandemProxies.Pacify` (`Proxies.reds:15-39`: `AIHumanComponent.Toggle
(false)`, senses `ToggleComponent(false)` + `RemoveSenseMappin`, bump
`Toggle(false)`, `ClearThreats`), then tandem's movement controller drives
it from the new owner's `NpcStates` like any proxy (`Npcs.reds:398-415`).
Both steps are in use on 2.50: `Pacify` on a guest's own NPCs and on
proxies, the controller on proxies (§13.9, ARCHITECTURE NPCs). `[T]` No
pop on the screen of the player who is near the NPC.

**Proxy → real (the receiving game).** Two ways:

- **(a) Wake the proxy up**: the reverse of `Pacify`
  (`entIComponent.Toggle(true)` on the AI component, bump; senses
  `ToggleComponent(true)`), detach tandem's controller, then put back the
  state (§2.2). A guest's proxy is already a full NPC of the real record
  spawned through Codeware's dynamic entity system (`Npcs.reds:377-393`),
  so it has the record's inventory, stats and hit shapes. Whether a
  behaviour tree switched off by `Toggle(false)` resumes cleanly is
  **untested** (NC §2.1: there is no "disable AI" function, only the
  component switch; NC §11 q2). `[I]`
- **(b) Respawn**: spawn a new NPC of the same record and appearance at
  the proxy's transform (`DynamicEntitySpec` with `spawnInView = true`,
  as proxies are spawned), apply the state when it attaches, delete the
  proxy in the same frame. Deleting NPC proxies is safe (§13.9, `[T]`).
  Costs one visible frame of swap and the loss of the current animation;
  needs nothing untested. `[I]`

Do (a) when the probe (§7 probe 3) says it works, (b) otherwise and as the
fallback when (a) fails a check (the NPC does not reach a high-level state
of Combat within a second, say).

### 2.2 What state can be carried, and how

The snapshot is read on the giving game right before it pacifies the NPC
and sent in one reliable message (§3.4).

| State                                | Read on the giver                                                                                                                                                               | Applied on the receiver                                                                                                                                                                                                                                             | Notes                                                                                                                                                  |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| position, yaw                        | `GetWorldPosition`, `GetWorldOrientation` (as `NpcStates`)                                                                                                                      | (a) already there; (b) spawn transform; `TeleportationFacility.Teleport` for corrections (QW §6.5)                                                                                                                                                                  | the receiver must have navmesh there: `IsNavmeshStreamedInLocation` (`final.reds:196013`) as a precondition                                            |
| health                               | `GetStatPoolValue(id, Health, true)` (`Npcs.reds:169`)                                                                                                                          | `RequestSettingStatPoolValue(id, Health, pct, null, true)` (`Npcs.reds:435`, `[T]`)                                                                                                                                                                                 | percent, as today; also Armor/Poise if wanted (NC §5.1)                                                                                                |
| status effects                       | `StatusEffectSystem.GetAppliedEffects(id, out effects)` (`final.reds:51192`); per effect `GetRecord`, `GetRemainingDuration`, `GetStackCount`, `GetInstigatorEntityID` (`:83696-83712`) | `ApplyStatusEffect(id, record, instigatorRecord, instigatorEntity, applyCount)` (`:51169-51178`), then `SetStatusEffectRemainingDuration(id, record, remaining)` (`:51210`)                                                                                          | instigator mapped to the receiver's V or puppet; DoT ticks then come from the receiver's copy (the hit rules of ARCHITECTURE, Fights, still apply) `[I]` |
| attitude to each V                   | `GameObject.GetAttitudeBetween(npc, v)` per V (as `Npcs.reds:466`)                                                                                                              | `AttitudeAgent.SetAttitudeTowards(agentOfV, attitude)` (NC §3.2), `GameObject.ChangeAttitudeToHostile(npc, v)` (`final.reds:11816`)                                                                                                                                 | per player, never the `player` group for puppets (§13.11)                                                                                              |
| combat target                        | its `CombatTarget` behaviour argument (`GetThreats(false)` is not a usable measure, §13.11, `[T]`), or the top hostile threat (NC §3.3)                                         | vanilla's pattern: `ChangeAttitudeToHostile`, `TargetTrackingExtension.InjectThreat(npc, target)` (`final.reds:156710`), `SetThreatPersistence` (`:156770`) — as in `AIActionHelper.SetCommandCombatTarget` (`:169949`); this is how tandem's test ganger fights a puppet (§13.11, `[T]`) | the target is the receiver's own V or a puppet; puppets are seen and hit since 2026-11-18 (§13.11)                                                     |
| high-level state (combat, alerted…)  | `NPCStatesComponent` getters (NC §3.1)                                                                                                                                          | follows from the injected threat; `NPCPuppet.ChangeHighLevelState(obj, state)` (`final.reds:126331`) only for the proxy's look                                                                                                                                       | not forced on a real NPC: its tree decides                                                                                                             |
| defeated / unconscious / killer      | `IsDefeated`, `IsUnconscious`, `GetKiller` (NC §5.3)                                                                                                                            | `SetIsDefeatMechanicActive`, `ApplyStatusEffect(BaseStatusEffect.Defeated / Unconscious)` (NC §5.3)                                                                                                                                                                 | a dead NPC is never handed over: the corpse stays with its game, the death goes to the ledger                                                          |
| weapons in hand                      | `GetItemInSlot(WeaponRight/Left)` (NC §3.4)                                                                                                                                     | the record's loadout comes with the spawn (`InitializeBaseInventory`, `final.reds:19862`); `AIEquipCommand` if different                                                                                                                                            | `ScriptedPuppetPS` keeps the loadout (`final.reds:24700-24704`)                                                                                        |
| level (`PowerLevel`)                 | the stat                                                                                                                                                                        | the same stat modifier as the host's level rule (ARCHITECTURE, Enemy level)                                                                                                                                                                                         | otherwise the receiver rescales it to its own V                                                                                                        |
| inventory, loot                      | —                                                                                                                                                                               | —                                                                                                                                                                                                                                                                   | loot is rolled per game at death (NC §8.1); nothing to carry                                                                                           |

### 2.3 What cannot be carried

- The behaviour tree's internal state (which action, which cover, a
  grenade being thrown, a reinforcement call in progress): there is no
  script access to it (NC §2.1). The receiving NPC re-decides from its
  threat; expect it to re-pick cover. `[I]`
- Squad membership: a respawned NPC is in no community squad; an adopted
  static one is in the receiver's own squad. `AISquadHelper.LeaveAllSquads`
  (`final.reds:157474`) exists, joining one does not (no script function
  found). Squad tactics may differ after a hand-over. `[S]` `[I]`
- Animation in progress, ragdoll, dismemberment already done (a respawn
  loses severed limbs; `RequestDismemberment` can be replayed, NC §5.5).
- Quickhack uploads in progress and per-target cooldowns (cancel them at
  hand-over).
- Police units: they belong to the prevention system of the game that
  spawned them and are registered there (`PreventionSpawnSystem
.IsEntityRegistered`, `RegisterEntityDeathCallback`, QW §9.3). They are
  never handed over (§5.1).

### 2.4 Static ids help, and expose one change to make

For a **placed** NPC (static id), both games have their own copy. Then a
hand-over needs no spawn at all: the receiver wakes up its own copy, the
giver pacifies its own copy, and the snapshot moves the state (NC §7.4
design A, "adopt the local copy"). That is the cheapest and smoothest
case, and it is why identity matters so much (§1.4 point 2).

It requires one change in what tandem does today: a guest's own NPCs are
hidden with `HideIrreversibly` (`Npcs.reds:124-125`, native
`final.reds:19392`), which cannot be undone. A guest whose copy of a placed
NPC was hidden while grouped cannot later wake it up when it leaves the
group. Stage 2 therefore pacifies own static-id NPCs in place and uses
them as the proxy of the owner's NPC (design A) instead of hiding them and
spawning a second body. Dynamic ones (crowd, encounter members) keep
today's treatment. `[I]`

For a **dynamic** NPC, the receiver has no copy (or a different person
under that id), so it always goes through §2.1 (a) on its proxy or (b).

### 2.5 Recipe for one hand-over `[I]`

1. The host decides (§3.2) and sends `NpcHandover{npc, from, to}` to the
   giver.
2. The giver reads the snapshot (§2.2), pacifies its entity, keeps it as
   a proxy, and sends `NpcHandoverState{npc, to, snapshot}`; from then on
   it applies hits to that NPC as a non-owner (`NpcHit` to the new
   owner).
3. The receiver checks the precondition (navmesh streamed at the NPC's
   position, the NPC's record known), wakes or respawns it, applies the
   snapshot, and sends `NpcOwner{npc, owner = itself}`; it now sends the
   NPC's `NpcStates`, `NpcHealth`, `NpcGone` like the host does today.
4. If the receiver refuses or times out (2 s), the giver wakes its copy
   up again (it never deleted it) and stays owner. One owner at every
   moment: the giver's NPC is not acting between steps 2 and 3, which is
   a short freeze, not two brains.

---

## 3. Arbitration: who runs which part of the world

### 3.1 Groups of players, not map regions

Streaming sectors are the wrong unit: they are resources with levels and
categories (`worldStreamingSector{level, category}`, `CW
scripts/World/worldStreamingSector.reds`) loaded by each game around its
own V, NPCs walk across them, and what a game simulates is decided by
distance to its V, not by sector. `[C]` `[I]` The unit that matches the
engine is **a group of players close enough that one game's streaming
covers all of them**: inside a group, one game (the group's owner) runs
the world and the others show proxies, exactly as today with host and
guests; a player alone is a group of one and runs its own world.

Earlier projects chose spatial cells for network interest only (NCO: 3
cells of interest, rate tiers at 200 and 600 m, nco-commits.md:100, :135);
that is about bandwidth, not about who simulates, and tandem has no
server that could simulate a cell.

### 3.2 Rules (host decides)

The host has every V's position at 30 Hz (`PlayerState`), so it decides
and broadcasts. Distances are first guesses to tune after probe 2 (§7):
the NPC sharing radius is 60 m today (`Npcs.reds:53-55`) and the owner's
game must still stream and run full AI (`AICAgent.GetLOD`,
`final.reds:143681`) around every member.

1. **Grouping with hysteresis.** Two players are linked when closer than
   **100 m**, unlinked when farther than **200 m**; a group is a connected
   set of links (A near B near C makes one group even if A and C are
   250 m apart — the owner shares NPCs around every member, so the chain
   stays covered as long as each link is short). Fast travel and
   teleports unlink at once.
2. **The group's owner** is the host when the host is in the group, else
   the lowest player id in it. Every NPC a group owner spawns is that
   owner's.
3. **Per-NPC ownership is sticky.** An NPC keeps its owner until a rule
   moves it; a change of group membership does not move every NPC at
   once. This avoids a mass hand-over when the host walks into a guest's
   fight. Ownership moves when:
   - its owner's V is farther than **120 m** from it (about to be
     streamed out or LODed) and another player's V is within 60 m:
     hand-over to that player's game (§2.5);
   - its fight is over (no hostile threat for 10 s) and it is in a group
     whose owner is another game: hand-over to the group owner, or, for
     ambient NPCs out of combat, simply let it go (§3.3).
4. **Who spawns.** Only the group owner spawns ambient NPCs (crowd,
   communities, encounters) in a group; other members quiet their own
   world as today (density 0, own NPCs pacified). A game that leaves a
   group gets its world back (`ResetDensityModifier`, no hiding, its own
   prevention unlocked).
5. **Merges wait for fights.** A player linking to a group while in
   combat or wanted (`PlayerCombatController` state, NC §9.1;
   `PreventionSystem.IsChasingPlayer`, QW §9.1) joins for players and
   puppets at once, but its game keeps owning the NPCs it is fighting
   until rule 3 moves them; with stage 1 only (no hand-over), it keeps
   owning them until the fight ends, then quiets its world.

### 3.3 Both players inside the boundary

Inside a group everything is as in the first version. The delicate moment
is the merge: the joining game has a crowd and encounters of its own
around its V, the owner has its own. `[I]`

- NPCs of the joining game that are **in a fight** stay its own (rule 3);
  the others see them as proxies, as host-owned NPCs are seen today. The
  owner's game must then also show proxies of a guest-owned NPC, which
  only guests do today (stage 2).
- **Ambient** NPCs of the joining game (crowd, idle encounter members) go
  when out of the player's view, and the owner's version comes as
  proxies. Placed NPCs with static ids are the same person in both games
  (§2.4): the joining game pacifies its copy in place and follows the
  owner's; nobody sees a swap.
- Crowds have no identity: the people around the joining player change.
  Hiding them only out of view keeps it from being noticed; this is the
  visible cost of stage 1.

### 3.4 Messages (sketch)

Appended to the protocol's variant (never reordered, AGENTS.md); none
changes an existing message, so `kProtocolVersion` (5, `protocol.hpp:18`)
bumps once for the new set because guests must understand them.

```cpp
// Host → all (Event, reliable; kept for late joiners). Who runs whose world.
struct Groups {
  std::uint32_t revision{};
  std::vector<std::uint8_t> ownerOf; // index player - 1 → the player whose game runs its surroundings
};

// Any owner → host → others (Event). An NPC id is unique in the session:
// the owner's player id in the top 8 bits, its own counter below
// (today only the host allocates, protocol.hpp:225-229).
struct NpcOwner { NpcId npc{}; PlayerId owner{}; std::uint64_t staticId{}; }; // 0: dynamic

// Host → giver: give this NPC to that player. Giver → host → receiver: the state.
struct NpcHandover { NpcId npc{}; PlayerId to{}; };
struct NpcEffectState { std::uint64_t record{}; float remaining{}; std::uint8_t stacks{}; };
struct NpcHandoverState {
  NpcId npc{}; PlayerId to{};
  Vec3 position; float yaw{};
  float health{};                       // fraction
  std::uint8_t hostileTo{};             // bit (player - 1)
  PlayerId target{};                    // the V it fights (0: none)
  std::uint8_t highLevel{};             // gamedataNPCHighLevelState
  std::uint8_t flags{};                 // defeated, unconscious, …
  std::vector<NpcEffectState> effects;  // at most kMaxNpcEffects
};
struct NpcHandoverDone { NpcId npc{}; bool accepted{}; };

// Region owner → host (Event): a lasting fact for the host's record (§4).
struct WorldFact {
  std::uint8_t kind{};       // NPC dead, device action, fact, journal, activity done, car parked
  std::uint64_t key{};       // static entity hash, PersistentID hash, fact hash, journal hash
  std::uint64_t key2{};      // community/spawner id, component name, record…
  std::int32_t value{};
  Vec3 position; float yaw{};
};

// Each game → host → all (Event): its own police heat, for the HUD and the map.
struct Heat { PlayerId player{}; std::uint8_t stage{}; };
```

Existing messages keep their meaning, with "the host" read as "the NPC's
owner": `NpcSpawn`, `NpcStates`, `NpcGone`, `NpcHealth` come from each
owner and are relayed by the host; `NpcHit` and `NpcEffect` go to the
owner (through the host when the owner is a guest).

---

## 4. Persistence: the lasting facts a region owner reports

### 4.1 Where the host keeps them

The host's save is the world (ARCHITECTURE, Saves). Facts a guest's game
reports must reach it even if the host never visits the place before the
session ends. Two ways, both cheap: `[I]`

- **Applied at once in the host's game** when the vanilla system accepts
  changes for something not streamed in: quest facts (`SetFact`), journal
  (`ChangeEntryStateByHash`), device PS events (`QueuePSEvent` by
  `PersistentID` without an entity, QW §7.1, QW §14 q14). These end up in
  the host's save by themselves.
- **Kept in a tandem ledger** for what can only be applied to an entity
  when it exists (deaths): a file next to the DLL keyed by the host's save,
  or a scripted system's `persistent` fields (vanilla systems keep their
  saved state that way, e.g. `PreventionSystem`'s `persistent let`s,
  `final.reds:138625-138635`; whether a mod's scripted system round-trips
  through a save is a probe). Each game applies the ledger when a matching
  NPC attaches (Codeware `Entity/Attach` callbacks, NC §7.2). The host's
  game then also records it its own way (PS, community bookkeeping), so
  its save holds it.

This is the ARCHITECTURE "world ledger" option kept inside the host's game
instead of a server: the same facts, no accounts.

### 4.2 The facts

| Fact                                  | Observed by the region owner                                                                                                                  | Re-applied in another game                                                                                                                                                                                                          | Kept by vanilla?                                                                                                                 |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| death of a placed NPC (static id)     | the death wrap already used (`NPCPuppet.OnDeath`, NC §5.3) or `IsDead()` in the 0.1 s scan (`Npcs.reds:184`); key `EntityID.ToHash`           | on attach: `Kill(null, skipNPCDeathAnim, …)` (`final.reds:125843`) so it lies dead; or before it streams in, `SetPersistentBool(PersistentID, n"ScriptedPuppetPS", n"m_isDead", true)` (QW §7.1) — effect untested                    | yes: `m_isDead`, `m_wasIncapacitated` are `persistent` (`final.reds:24687-24689`); on re-attach a corpse regenerates its loot and skips AI set-up (`:19847`, `:125562-125568`) |
| death of an encounter member          | as above; key = (community or spawner id, entry name, record): `gameEntityStubComponentPS.GetSpawnerID`, `GetOwnerCommunityEntryName` `[R]`, Codeware `CommunityEntryWrapper.GetSpawnedEntityIDs` (QW §6.2) | on attach of a member of the same entry and record, as above, counted per entry                                                                                                                                                     | the community tracks it (Codeware resets activities with `DeactivateCommunity` + `ResetCommunity` + `ResetPersistentState`, `CW src/App/World/OpenWorldSystem.cpp:275-323`); no API to *mark* one dead |
| death of a crowd NPC                  | —                                                                                                                                             | —                                                                                                                                                                                                                                   | no (crowd is recycled, NC §2.5): not recorded                                                                                    |
| device state (door opened, camera off, terminal hacked) | hook on `QueuePSDeviceEvent` / `QueuePSEvent` (QW §7.2)                                                                    | rebuild the action, `QueuePSDeviceEvent`, or `SetPersistentBool/Int/Float` for plain fields (QW §7.2)                                                                                                                               | yes (device PS)                                                                                                                  |
| looted containers                     | —                                                                                                                                             | —                                                                                                                                                                                                                                   | per game; tandem's rule is "each player loots their own copy" (authority table, QW §7.3): nothing to send                        |
| quest items taken                     | `hasQuestItems` / `markAsQuest` containers (QW §7.3)                                                                                          | only matters to the quest's runner (§5.4)                                                                                                                                                                                           | —                                                                                                                                |
| destroyed objects                     | —                                                                                                                                             | —                                                                                                                                                                                                                                   | `gameDestructionPersistencySystem` has no script functions (QW §7.2): **cannot be**, without reverse engineering                 |
| police heat                           | `ChangeHeatStage` wrap / `UI_WantedBar` (QW §9.1)                                                                                             | not re-applied: a chase is not world state (§5.1)                                                                                                                                                                                   | no: saving is locked while chased (`final.reds:138868`, QW §9.4)                                                                  |
| quest facts of a solo gig or hustle   | the native `SetFact` hook or `RegisterEntity` listener (QW §2.1)                                                                              | `QuestsSystem.SetFact` on the host                                                                                                                                                                                                  | yes (save)                                                                                                                       |
| gig / hustle journal and map state    | journal callbacks (QW §3.2); a hustle's POI mappin phase (`CW src/App/World/OpenWorldTracker.cpp:4-26`)                                       | `ChangeEntryStateByHash`; the mappin phase only natively (`Raw::MappinSystem::SetPoiMappinPhase`, `CW src/Red/MappinSystem.hpp:17`)                                                                                                  | yes                                                                                                                              |
| fixer progress                        | the fixer's counter fact, named in TweakDB (`<fixer>.factCounterName`, read by `GetFixerVariantData`, `final.reds:276745-276746`)             | `SetFact`                                                                                                                                                                                                                           | yes                                                                                                                              |
| a car parked by a player              | the driver's `Seat` leaving, the car's last transform                                                                                         | §5.3                                                                                                                                                                                                                                | the player's own car is per save (`VehicleSystem`, `final.reds:189898-189945`)                                                    |
| time, weather                         | host's already                                                                                                                                | —                                                                                                                                                                                                                                   | —                                                                                                                                |

### 4.3 What cannot be kept one

- **Destroyed props and scenery** (no API, above). Most reset in vanilla
  on re-stream anyway. `[I]`
- **Corpses as they fell.** A ledger death makes the body lie where the
  NPC stands when it attaches, not where it fell in the other game.
- **Encounter deaths, exactly.** Matching by (entry, record) is a count,
  not a person; and if vanilla respawns an encounter after some time, the
  ledger should expire with it (unknown timing: probe).
- **What the guest's quest prefabs did** while apart, for quests the host
  does not run (§5.4).

---

## 5. Police, traffic, parked vehicles, gigs

### 5.1 Police

- **Apart: per-player heat for free.** `PreventionSystem` is a script
  system of each game around `m_player` (`final.reds:138636`), its spawn
  system is written around "the player" (QW §9.3). Each game chases its
  own V with its own units; no message is needed except `Heat` for the
  other's HUD and map. This also answers ARCHITECTURE's undecided "chases
  when players are far apart": one wanted level per player while apart.
  `[S]` `[I]`
- **The lock must follow the group, not the session.** The guest's lock
  (`TogglePreventionSystem{n"tandem", false}`, QW §9.4) is stored in the
  persistent `m_systemLockSources` (`final.reds:138634`) and clears
  everything when applied (`execInstructionOff`, QW §9.2). Apply it when
  a game becomes a non-owner member of a group, remove it when it leaves.
- **Merging while wanted.** Police units are never handed over (§2.3).
  With rule 5 of §3.2 the wanted player's game keeps its police until the
  chase ends; the others see them as proxies (stage 2) or not at all
  (stage 1: the merge of NPC ownership waits for heat 0). Forcing it
  (fast travel into a group) clears the joiner's chase: the owner can take
  the heat over with `SetWantedLevel` (QW §9.2) and point its police at the
  joiner with the public `PreventionSystem.SetLastKnownPlayerPosition
  (game, pos)` (`final.reds:142214`), but its units still hunt by its own
  V's rules. A shared wanted level inside a group stays the host's (QW
  §9.4). `[I]`
- `CanPreventionReactToInput` already gives up when `IsMultiplayer()`
  (`final.reds:139638`); tandem keeps that predicate false, so nothing
  changes there. `[S]`

### 5.2 Traffic

Traffic has no switch (QW §10) and is not suppressed today, so apart it is
already each game's own. Grouped, both games keep their own traffic (two
sets of cars) until a switch is found; per-player regions do not make this
worse. Moving traffic cars are never handed over or recorded. `[I]`

### 5.3 A car a player parks in another region

- The car a player drives is that player's game's (authority table). When
  they get out, ARCHITECTURE gives it to the host, which cannot simulate
  it far away. Per region: a parked car belongs to the game of its region
  owner; its record and transform go to the ledger (`WorldFact` kind car
  parked) so any game whose V comes near spawns a copy there.
- The copy can be registered with the engine to appear only when a player
  is around and to persist in the host's save: Codeware's
  `DynamicEntitySpec.alwaysSpawned = false` ("only when player is
  around") and `persistSpawn`/`persistState` (`CW
  scripts/World/DynamicEntitySpec.reds`, persistable ids at `CW
  src/App/World/DynamicEntitySystem.cpp:288`). A copy nobody sat in deletes
  safely; one that had an occupant does not (§13.6, `[T]`), so parked
  copies are only ever deleted before anyone mounts them. `[C]` `[I]`
- The player's own summoned car (`VehicleSystem.SpawnPlayerVehicle`,
  `final.reds:189903`) is spawned by the owner's game; the others show a
  copy as today.

### 5.4 Gigs: can a guest's game run one while the host runs the story?

What a gig is in the game:

- Journal quest types: `MainQuest`, `SideQuest`, `MinorQuest`,
  `StreetStory`, `CyberPsycho`, `Contract`, … (`final.reds:7102-7113`).
  Reward ids are prefixed by kind: `ma_` minor activities (NCPD hustles),
  `sts_` street stories (fixer gigs) (`final.reds:37544-37548`). A fixer's
  progress is a counter fact named in TweakDB
  (`final.reds:276745-276746`). `[S]`
- NCPD hustles live in one quest phase, the resource
  `base\open_world\minor_activities\open_world_minor_activities.questphase` (`CW
src/App/Quest/QuestPhaseRegistry.cpp:10`). Codeware enumerates each
  hustle's state: communities, spawners, named facts, graph facts, journal
  hashes, persistent-state refs, loot container, security system, POI
  mappin (`CW src/App/Quest/QuestPhaseRegistry.hpp:25-50`, collected at
  `QuestPhaseRegistry.cpp:298-364`), and can reset one completely
  (`OpenWorldSystem.cpp:160-375`). Completion is the POI mappin reaching
  `CompletedPhase` (`OpenWorldSystem.cpp:138-145`). `[C]`

So **yes for the simulation**: the guest's own game runs the gig's phase
natively, with its prefabs, spawners, scenes and phone calls, around its
own V — the very things a guest cannot get from the host (QW §6.3). What
it needs:

1. **Per-phase quest suppression** on guests (§1.4 point 5): story phases
   follow the host, open-world phases run, and the gig's own phase runs
   only for the player who claimed it. Codeware's phase registry shows
   phases can be told apart by resource path. `[C]` `[I]`
2. **A claim**, so one gig runs in one game: `GigClaim{quest}` to the
   host, granted if nobody (the host included) has it active; the other
   games keep that gig's phase stopped. How to keep the host's own phase
   from starting it (its trigger zones, the fixer's call) is open. `[I]`
3. **The outcome into the host's world**: the gig's facts, journal entry
   states, fixer counter, mappin phase, deaths (§4.2). Rewards stay the
   guest's own (ARCHITECTURE, Quests together, gigs apart).
4. **The host's quest graph agreeing with it.** This is the unknown. The
   host's copy of that quest never ran: its graph sits where it was (gig
   offered, say). Setting the gig's named facts and journal states may not
   move it. Quest-graph state is itself kept in fact stores 2..10 of the
   `FactManager` (QW §2.1), and Codeware resets hustles with
   `ResetGraphFact` and node execution (`OpenWorldSystem.cpp:339-371`), so
   copying a gig's graph facts might put the host's graph at the end too —
   a lead, not a result. `[C]` `[I]`

Verdict: **NCPD hustles apart are feasible** (their state is enumerable,
completion is a mappin phase, they are short and self-contained). **Fixer
gigs apart are a research project**: multi-phase quests with calls,
scenes, quest items and graph state; try one simple gig end to end before
committing. The main story stays the host's, played together.

---

## 6. Cost estimate and stages

Days are focused work for one developer who knows this codebase (the
existing NPC sharing, `Npcs.reds` + `Combat.reds`, is the yardstick).
Every stage keeps "one owner per NPC at every moment".

### Stage 0 — probes (small, 1–2 days)

The facts the later stages stand on, all on one PC with `tools/game.sh
cmd` (§7). No user-visible change.

### Stage 1 — apart, each game runs its own surroundings (medium, 6–9 days)

- `core/`: grouping with hysteresis, the `Groups` and `Heat` messages,
  `WorldFact` for deaths; unit and loopback tests (1.5 d).
- Guest scripts: quiet world only while a non-owner group member
  (density, own-NPC pacifying, prevention lock follow `Groups`); puppet of
  a far player disabled, map pin instead (2 d).
- Merge policy: ownership of the joiner's NPCs waits until its fights and
  chase are over; its ambient NPCs go out of view (1.5 d).
- Death ledger, placed NPCs only (static ids, plus encounter entries if
  probe 1 shows a stable key): reported, stored on the host, applied on
  attach in every game (2 d).
- Mock player: put its V anywhere (`--at X,Y,Z`, a path away and back) so
  the game sees a far player join and leave; scenarios (1 d).

Main risks: few NPCs have static ids (the ledger covers less than hoped);
the open-world/story phase split needed by milestone 6 (stage 1 is fine as
long as guests suppress no phases, which is the case today); crowd swaps at
merge are noticed.

**Test alone**: the game as guest of `tandem-peer host --at <1 km away>`:
expect "own world on" (density reset, nothing hidden, prevention unlocked)
in the log; the peer walks to the game's V: "grouped", own world quiet;
with `tools/game.sh cmd npc hostile` the game is in combat and the log must
show the merge deferred until the ganger dies. Ledger: as guest apart, kill
a placed NPC (scripted test hit, §13.10); the peer records the
`WorldFact`; then the game as host of a peer replaying it, `teleport`
there: the NPC attaches dead.

**What the user sees**: drive off alone and the city is alive around you
(people, gangs, vendors, your own police chase, full single-player fights);
the other player is a pin on the map; enemies you killed at fixed spots
are dead when the host comes; meeting up, the passers-by around you change
once, and a fight in progress finishes in your own world before you merge.

### Stage 2 — hand-over of NPCs (large, 12–18 days)

- Symmetric NPC sharing: any game can own NPCs; ids per owner; the host
  relays; proxies on the host of guest-owned NPCs; hits and effects routed
  to the owner (4–5 d). This is a refactor of today's host/guest split in
  `Npcs.reds`/`Combat.reds`.
- Placed NPCs adopted in place instead of hidden (§2.4) (2–3 d).
- Hand-over: snapshot, wake-up or respawn, timeouts and refusals (§2.5)
  (4–6 d).
- Edge cases: owner disconnects mid-hand-over, group owner changes, corpses,
  NPCs mid-takedown or mid-quickhack (2–3 d).
- Mock player as an NPC owner (replayed `NpcSpawn`/`NpcStates` with its
  own ids, scripted `NpcHandover` both ways) and scenarios (2 d).

Main risks: waking a pacified NPC (probe 3); combat re-acquisition on the
receiver (puppets are seen and hit since 2026-11-18, §13.11, but NPCs
fighting a puppet are young code); AI LOD and streaming at the moment of
hand-over; double-applied effects.

**Test alone**: the game as host, the peer owning NPCs (a replay with the
peer's ids): the game shows proxies of them; the peer sends
`NpcHandoverState` (health 0.4, burning, hostile to the game's V): expect
"handover in: npc N health 0.40 effects 1 target player 1" and the NPC
attacking V. Reverse: the peer asks for an NPC the game owns: expect
"handover out" and the NPC following the peer's states. Record both sides
once, replay them as regression scenarios.

**What the user sees**: meet up in the middle of a fight and the enemies
keep their wounds, burns and target; split up while chased and the
pursuers stay on you instead of vanishing.

### Stage 3a — NCPD hustles apart (medium, 5–8 days, after milestone 6's phase split)

Claim, outcome (facts, journal, mappin phase, deaths) to the host, applied
there; uses Codeware's activity model as the list of what a hustle is.

**Test alone**: the game as guest apart runs a hustle; the peer as host
records the `WorldFact`s and the claim; then the game as host of the
replay: the hustle shows completed on the map.

**What the user sees**: clear an NCPD hustle alone across town; it is done
on the host's map and stays done.

### Stage 3b — fixer gigs apart (large, 15–25 days, uncertain)

Per-gig claim, graph-state transfer (the open question of §5.4), phone
calls and scenes in the guest's game while the host's world says nothing.
Start with one gig as a spike (3–4 days) and decide from it.

**What the user sees**: take a gig from a fixer alone while the other plays
the story; both see it done and the fixer's progress counts it.

### Recommendation

**Do stage 0, then stage 1.** It removes the tether, gives the "players
anywhere" half of the goal with no network in any fight, and is mostly
switching existing per-game systems back on at the right time. Its two
rules (quest suppression per phase, prevention lock per group) should be
fixed now so milestone 6 is built to them. Stage 2 is worth doing once
fights between players together (milestone 5) are solid, because it reuses
all of it. Stage 3a after milestone 6; stage 3b only after a one-gig spike
says the host's graph can follow.

---

## 7. Probes to run before stage 1

Each is small and needs no second player.

1. **Identity classes.** For the NPCs around V at three places (a vendor,
   a gang encounter, a placed quest-giver), log `EntityID.ToHash`,
   `IsStatic`, record, `gameEntityStubComponentPS.GetSpawnerID` and
   `GetOwnerCommunityEntryName` on two different saves at the same place.
   Decides what the death ledger can name (§4.2) and how many hand-overs
   are adoptions in place (§2.4). (NC §11 q7.)
2. **Ranges.** At what distance from V do community and encounter NPCs
   attach and detach (Codeware `Entity/Attach`/`Detach` callbacks), and
   where does their AI LOD drop (`AICAgent.GetLOD`)? Codeware also reads a
   node's streaming distance (`WorldNodeSetupWrapper.GetStreamingDistance`,
   `CW scripts/World/WorldNodeSetupWrapper.reds:23`). Sets the 100/200/120 m
   guesses of §3.2.
3. **Waking a pacified NPC.** `tools/game.sh cmd npc hostile`, pacify it
   (`TandemProxies.Pacify`), wait, toggle the AI, senses and bump back on,
   re-inject the threat on V: does it fight again? (§2.1 a.)
4. **Status effects across.** Read `GetAppliedEffects` on a burning NPC,
   apply the same records with `SetStatusEffectRemainingDuration` on
   another NPC: same remaining time, stacks, VFX?
5. **A death applied on attach.** Kill a placed NPC with
   `Kill(null, true, false)` the moment it attaches: corpse on the ground,
   lootable, no death animation, and still dead after a save and load?
   Does `SetPersistentBool(…, n"ScriptedPuppetPS", n"m_isDead", true)`
   before it streams in do the same?
6. **A mod's persistent scripted fields** survive a save and load (for the
   ledger kept in the host's save, §4.1).
