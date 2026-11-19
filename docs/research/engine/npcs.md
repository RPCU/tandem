# NPCs: identity, proxies, attitudes, animation state

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.7 Naming NPCs across games (2026-11-18)

`tools/game.sh cmd npcs [range]` (`GameObject.GetNPCsAroundObject`, the
targeting system's search) on two loads of the same save:

- Crowd members (`Character.HomelessMan`, community spawns) have
  **dynamic** ids, small sequential numbers (10002155, 10002156, 10002957)
  that come back on the next load **for other NPCs**: the same id named an
  NPC with appearance `…_ma_18` at (-903.1, 1895.0) the first time and one
  with `…_ma_11` at (-906.4, 1894.6) the second. Spawn spots repeat (the
  same position under another id), appearances are random. Never match
  dynamic NPCs across games by id or by "nearest of the same record": the
  host announces them (record, appearance, transform, a session id) and
  guests spawn proxies, with their own community spawning off. [2.5-verified]
- Encounter spawns are different: a Tyger Claws group nearby (six NPCs,
  dynamic ids 9002400–9002405, plus a municipal worker) came back on the
  second load with **the same ids, records, appearances and positions**.
  So an encounter's members are deterministic per spawner while crowds are
  random. Still not a safe match across games (spawn order follows each
  player's approach and streaming): the host-announces rule stands, but an
  encounter member's (spawner id, record) is a useful cross-check. No placed
  (static-id) NPC was in range: to measure where a quest NPC stands (Judy,
  a vendor). [2.5-verified]

### 13.9 A guest's own NPCs off (2026-11-18)

- `GameInstance.GetCommunitySystem(game).ChangeDensityModifier(0.0)` stops
  the ambient crowd from spawning more; `ResetDensityModifier()` brings it
  back. NPCs already there stay. [2.5-verified]
- `gamePuppet.HideIrreversibly()` (vanilla uses it for disposed puppets)
  hides an NPC for good; with `TandemProxies.Pacify` first it neither acts
  nor shows. Our dynamic-entity-system spawns (puppets, proxies) are told
  apart with `DynamicEntitySystem.IsManaged(id)`. [2.5-verified]
- Deleting NPC proxies (`DynamicEntitySystem.DeleteEntity`) after
  `UnregisterPuppet` is safe: 13 at once, no freeze (unlike car copies that
  had an occupant, §13.6). [2.5-verified]

### 13.17 Attitude groups and a puppet's record traits (2026-11-18, scripts, then one run)

Read from the 2.50 scripts (`final.reds` line numbers) and the RTTI dump;
none of it run in game yet.

- **Groups are TweakDB records, relations are not part of them.**
  `AttitudeGroup_Record` (final.reds 174676) has `name` (CName, what
  `AttitudeAgent.GetAttitudeGroup` returns), `defaultAttitude`,
  `attitudeToSelf` (strings), `parent`, `isState`. Relations are separate
  `Attitude_Record`s (515340: `group1`, `group2`, `value`). V's group is
  `Attitudes.Group_Player`, name `player` (116087). A record copied with
  TweakXL (`Attitudes.Group_TandemPlayer1 : Attitudes.Group_Player`) gets
  the group's own fields only, none of its relations; whether the engine
  falls back to `parent`'s relations is unknown (not tried: a parent might
  also share hostility). [2.5-untested]
- **Relations at runtime**: `GameInstance.GetAttitudeSystem(game)`
  (14160) → `AttitudeSystem.GetAttitudeRelation(a, b)` /
  `SetAttitudeRelation(a, b, attitude)` by group name, with `Persistent`
  variants that are saved (174691–174704; RTTI `gameCAttitudeManager`).
  Quests and prevention change V's group relations with the persistent
  setter (141857–141868, police and gangs towards V's current group); a
  security system breached by someone turns its group hostile to the
  intruder's group (114508–114537). [2.5-untested]
- **A group added by TweakXL is accepted**: `SetAttitudeGroup(n"tandem_player_1")`
  reads back as `tandem_player_1` (scenario `puppet-attitude`, first run).
  But 10 s later `GetAttitudeRelation(n"civilian", n"tandem_player_1")`
  was friendly (0) while `civilian` to `player` was neutral (1): the
  relation copy had not taken, or not run, and civilians saw the puppet as
  friendly where they see V as neutral. A fresh group's relations with no
  copy read friendly. [2.5-verified] Second run: every copy logged
  `tandem_player_1 / civilian (puppet put in): V's 1/1, read back 0/0 (not
taken)`, and no NPC was within 60 m of V when the command ran (the
  scenario failed on `0 NPCs`). [2.5-verified]
- **Why the copy never shows: the setter and the getter use different
  tables** (Ghidra on the 2.50 exe, `tools/re/decompile`, 2026-11-18):
  - `GetAttitudeRelation` (native 0x140fcc234 → 0x14026981c) looks both
    names up in the manager's group table (manager +0x48; a name not
    found gives neutral, 1), then in an override map at +0xC8 keyed by a
    symmetric hash of the two names (0x14026a3f8), else the base relation
    from a packed 2-bit triangular matrix (0x14026a438): relations are
    **symmetric**. `tandem_player_1` is in the table (it read 0, not the
    unknown-name 1): TweakXL's groups exist before the world loads.
  - `SetAttitudeRelation` (native 0x140ca8650 → 0x140ca8730) **does not
    write either**: it walks the registered attitude agents (manager
    +0x158), matches each agent's group name (its PS's current group,
    else `baseAttitudeGroup` at +0xC8 of the agent), and for every agent
    of the first group and every agent of the second sets a per-agent-pair
    attitude both ways (0x14048b73c → 0x14048ac70, map at manager +0x190)
    and sends them an event. Agents that join either group later get
    nothing, and the group table is untouched. tandem copied while no
    agent was in `tandem_player_1` yet (before `SetAttitudeGroup`), so it
    did nothing.
  - `SetAttitudeGroupRelationPersistent` (native 0x14141b41c →
    0x14141b4fc) is the one that writes the group override map at +0xC8
    (for two names in the table), then updates agents. It is what quests
    use, and it is saved with the game.
    Read from the decompiled code, not run. [2.5-untested]
- Since then: a puppet is put in its group first, then gets
  `SetAttitudeRelation` from it to every group seen (reaching the agents
  around), and every 2 s each NPC within 60 m of V or a puppet gets its
  and V's attitudes on the puppet's agent (`AttitudeAgent.SetAttitudeTowards`,
  which reads back: §13.11). The persistent setter would give the real
  group copy (every NPC of the group, now and later) but writes the host's
  save; not used. [2.5-untested]
- **Why one group per V**: vanilla turns an NPC hostile to its target's
  whole group, `AttitudeAgent.SetAttitudeTowardsAgentGroup(target, owner,
hostile)` (`TryChangingAttitudeToHostile` 169933, companions 152568);
  `GameObject.ChangeAttitudeToHostile` is per agent (11816).
  `GameObject.GetAttitudeBetween` asks the first object's agent
  (`GetAttitudeTowards`, 11750). [2.5-untested]
- **A record's group**: `Character_Record.baseAttitudeGroup` (CName,
  19232); Panam's is `panam`. Her 2.02 record (moddocs,
  `changing-npcs-tweak-edit-redmod.md`): base
  `aldecaldos_grunt2_sniper2_sor22_wa_elite`, tag `Invulnerable`,
  `voiceTag` `panam`, `statModifiers` = burning immunity,
  `statModifierGroups` += VeryHighHealth, PanamSniperDamage,
  FollowerDamageAgainstBosses, a Grad sniper as primary equipment, a
  bounty draw table, `skipDisplayArchetype`. Her faction (`affiliation`,
  from the Aldecaldos base) gives the body and its items the faction's
  anim wrappers (24607, 126116) and is the scan's faction line (129227).
  [2.5-untested]
- **Companions**: `ScriptedPuppet.IsPlayerCompanion` needs the AI role
  `Follower` and the player as `FriendlyTarget` (160150), which quests
  give, not the record: a puppet is no companion, so
  `SenseComponent.ShouldIgnoreIfPlayerCompanion` (156168) does not make
  NPCs ignore it. [2.5-untested]
- **NPC UI**: `hideUIElements` is read as a character flat
  (`GetBoolFromCharacterTweak("hideUIElements")`): no sense mappin (the
  detection indicator) is created (155737) and no object marker drawn
  (406516, 406724). The stealth mappin's `HideUIDetection()` (398508,
  native) hides the detection meter; `Character_Record.hideUIDetection`
  exists (19260), presumably its source. [2.5-untested]
- Left on the puppet for now: Panam's primary equipment (an NPC brain
  entering combat could draw her Grad), her bounty draw table (a scan may
  show her bounty; a null foreign key's `.tweak` syntax is unchecked),
  `VeryHighHealth` (wanted: our game must not kill a remote V).

### 13.21 An NPC's animation state, for a brain-off proxy (2026-11-18, scripts only)

What tandem's NPC animation sync (`scripts/tandem/NpcAnims.reds`) relies
on. Read in `final.reds` (2.50) and Codeware's scripts; none of it run yet:
all [2.5-untested].

- **The states an NPC's graph reads** live in `NPCStatesComponent`
  (`final.reds:167844`) as the native replicated integers (§3.1 of
  coop-npcs-combat.md) and in the `PuppetState` blackboard, read with
  `ScriptedPuppet.GetHighLevelStateFromBlackboard`,
  `GetUpperBodyStateFromBlackboard`, `GetStanceStateFromBlackboard`
  (`:20698-20722`). Applying one (`ChangeHighLevelState` `:168077`,
  `UpdateHighLevelState` `:168168`, `UpdateUpperBodyState` `:168454`,
  `UpdateStanceState` `:168839`) is: an `AnimFeature_NPCState {state}`
  under the component's private feature names
  (`m_highLevelAnimFeatureName`, `m_upperBodyAnimFeatureName`,
  `m_stanceAnimFeatureName`), anim wrappers by state
  (`GetAnimWrapperNameBasedOnHighLevelState` `:167984`:
  `alertedLocomotion`, `combatLocomotion`, `stealthLocomotion`; by stance
  `:167998`: `inCrouch`, `inCover`, `inSwim`, `inVehicle`), the blackboard,
  and `AnimationSystem.EnterCombatMode/ExitCombatMode`
  (`HandleCombatStateAnimHint` `:168190`). The upper body's graph value is
  its own numbering (`GetUpperBodyStateForAnimGraph` `:168491`: Aim 1 …
  Shoot 9). The public path (`NPCPuppet.ChangeHighLevelState` `:126331`)
  goes through the AI signal table, and its high-level change also plays
  combat VO, changes senses presets and queues a
  `gameHighLevelStateDataEvent` **on the local player** (`:168088`): a
  proxy must not take that path. An `@addMethod(NPCStatesComponent)`
  reaches the private names and helpers.
- **Shots**: every NPC (and turret) shot is `AIWeapon.Fire` (static,
  `:173136`; callers `:155212`, `:171583`); `OnStopShooting` plays
  `gamedataFxAction.ExitShoot` (`:173113`). A guest plays a shot with
  `WeaponObject.TriggerWeaponEffects(weapon, Shoot)` as for players
  ([2.5-verified] on player puppets, §Weapons in ../../architecture/players.md).
- **Target**: `TargetTrackerComponent.GetTopHostileThreat(onlyVisible, out
TrackedLocation) -> Bool` (`:156469`), `TrackedLocation.entity`.
  An NPC turns its head and chest with a `LookAtAddEvent` (`:172727`;
  `SetEntityTarget(entity, n"pla_default_tgt", offset)`, `bodyPart
n"Eyes"`, extra parts Head/Chest, as `ReactionComponent` does at
  `:147434`), removed with `LookAtRemoveEvent.QueueRemoveLookatEvent`
  (`:174614`). No AI needed: it is an animation request.
- **Workspots**: `WorkspotGameSystem` (`:53211`): `IsActorInWorkspot`,
  `PlayNpcInWorkspot(npc, master, referenceEntity, actorDataCompName, opt
syncSlotName, opt slideTime)` (`:53330`; vanilla plays it on a dead or
  unconscious body for body disposal, `:92662`, `:210081`: no brain
  needed), `SendJumpCommandEnt(actor, entryId, instant)`,
  `SendJumpToAnimEnt`, `SendSlowExitSignal`, `StopNpcInWorkspot`.
  `ExtendedWorkspotInfo` (`:319157`) has only flags. Codeware adds
  `WorkspotGameSystem.GetWorkspotInstance(actorID) ->
workWorkspotInstance {resource, animName, idleAnim, entryId}`
  (`scripts/World/WorkspotSystem.reds`), `CResource.path`, `ResRef.resource`
  with `ResourceAsyncRef.GetHash`: the resource's depot path hash, the same
  in every copy of the game. `WorkspotStartedEvent.nodeId: GlobalNodeID`
  (`:250074`) names the workspot's world node, unused so far.
- **A workspot to play from**: `WorkspotResourceComponent` (`:105125`, an
  `IPlacedComponent`; Codeware exposes `workspotResource: ResourceAsyncRef`).
  Codeware's `Entity.AddComponent` only appends to the components array, so
  it must run before the entity initializes: tandem adds one in the
  `Entity/Assemble` callback of entities tagged `Tandem.Proxy`. Redscript
  cannot build a `ResourceAsyncRef` from a hash: the plugin writes it
  (`TandemNative.SetResourcePath`, by RTTI property name and type
  `ResourceAsyncReference`). To check in game: that the component added at
  assembly is initialized, that an unparented placed component's local
  transform is its world transform, that `PlayNpcInWorkspot` takes the
  proxy as its own master and reference entity, and that the movement
  controller (kept still meanwhile) does not fight the workspot.
