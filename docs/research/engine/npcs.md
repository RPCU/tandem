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
- **Relations declared as records (2026-11-22, written, not run).**
  `Attitude_Record.Group1()` / `Group2()` return `AttitudeGroup_Record`s
  (flats `group1`, `group2`: foreign keys), `Value()` a String
  (final.reds 515340). No dump of the game's TweakDB is in the repository
  or the caches, so the player's records are read from the running game:
  TweakXL 1.12.2's script API (`red4ext/plugins/TweakXL/Scripts/TweakXL.Global.reds`:
  `TweakDBInterface.GetRecords(type: CName)`, `TweakDBManager.CloneRecord`,
  `SetFlat`, `UpdateRecord`, class `ScriptableTweak` with `OnApply`)
  clones each one that names `Attitudes.Group_Player` for our groups
  (`scripts/tandem/AttitudeTweak.reds`). Unknown, each settled by one
  start's `attitudes: declared relations …` line: the type name
  `GetRecords` wants (`Attitude`, `Attitude_Record` and
  `gamedataAttitude_Record` are tried: `R Attitude records read` is 0 if
  none is right); whether the packed matrix is built from every `Attitude`
  record in TweakDB when the attitude manager starts, TweakXL's included
  (`took`), or from a list baked before (`did NOT take`: the records
  exist, `C cloned` > 0, and the table still reads friendly). [2.5-untested]
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
- **The states' entry points** (2026-11-22; lines of
  `~/.cache/tandem/ref/gamescripts/game/final.reds`, the 2.50 dump of
  `tools/scripts-decompile.sh`). The three natives
  `AINetStateComponent.SetReplicatedHighLevelState`, `…UpperBodyState`,
  `…StanceState` (153534–153542) have one script caller each
  (`SetCurrent…State`, 153645–153690), called only by the private
  `NPCStatesComponent.ChangeHighLevelState` (153825),
  `ChangeUpperBodyState` (153858) and `ChangeStanceState` (153866), which
  also fill the blackboard (`Update…State`). Those run from
  `OnNPCStateChangeSignalReceived` (153795; `OnDead` 154125 calls the
  upper body's itself), reached from
  `ScriptedPuppet.OnSignalNPCStateChangeSignal` (23478). Every script
  that changes a state raises that signal (`NPCStateChangeSignal`, 10
  makers): the statics `NPCPuppet.ChangeHighLevelState` (107579; 31
  callers: status effects, reactions, death and defeat, the security
  system, quests' AI commands), `ChangeUpperBodyState` (107600; 3),
  `ChangeStanceState` (107621; 2), and the AI actions'
  `ChangeNPCState` / `ResetNPCState` (300361, 300400: a tweak action
  phase's states, most aim, shoot, reload, crouch and cover changes).
  Past them in scripts: `OnGameAttach` writes the blackboard from the
  replicated values (153635: the state an NPC spawns with, set by no
  script), and tandem's own `TandemFollow` on a proxy. Whether native
  code calls the three natives itself (a spawn's initial state, a mount)
  cannot be read from scripts: tandem's 2 s poll logs it
  (`poll caught a change the events missed`). The signal table holds one
  payload per signal: two changes raised before the handler runs keep
  the last one's. [2.5-untested]
- **Other events of what the poll read** (same dump). The right hand:
  `NPCPuppet.OnItemAddedToSlot(ItemAddedToSlot)` (107242) and
  `OnItemRemovedFromSlot(ItemRemovedFromSlot)` (107340), `GetSlotID()`
  on both. A workspot: `ScriptedPuppet.OnWorkspotStartedEvent` (22803)
  and `OnWorkspotFinishedEvent` (22816); `PlayerPuppet` overrides both
  without calling them (49175). The top threat: none. The tracker's
  events (`NewThreat`, `HostileThreatDetected`, `EnemyThreatDetected`,
  `ThreatRemoved`, 182729–183323; handled by `TargetTrackingExtension`
  151104–151263 and `AIHumanComponent` 111881–111893) say an entity
  entered or left the threat list, not that the top one changed.
  [2.5-untested]
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
  (`scripts/World/WorkspotSystem.reds`), which finds no instance on 2.50
  (§13.134); tandem reads the game's map itself
  (`TandemActivity.Workspot`). In scripts, `CResource.path`,
  `ResRef.resource` with `ResourceAsyncRef.GetHash` give a resource's
  depot path hash, the same in every copy of the game. `WorkspotStartedEvent.nodeId: GlobalNodeID`
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

### 13.134 Which workspot an actor plays: the game's instance map (2026-11-21, decompiled 2.50 exe, one run)

- `gameWorkspotGameSystem.IsActorInWorkspot` (native `0x1409ef4ac`) takes
  the `workWorkspotSystem` (runtime system 31: game instance `+0x40` →
  `+0x78` → `+0x1f0`), its shared spin lock at `+0x50`, and looks the
  actor's EntityID hash up with `0x1405dbbec` (address-library hash
  `2234523611`, `(system, uint64 entity) -> workWorkspotInstance*`): a
  `HashMap<uint64, instance*>` at `+0x70` whose hashed key is the low 32
  bits of the EntityID (`0x14022c920`: bucket `(uint32)key % capacity`,
  node key compared whole). True when the instance's `+0x60`, `+0x30` and
  `+0x90` are set (`0x140228508`). [2.5-verified]
- Codeware 1.21.5's `GetWorkspotInstance` reads the same map through
  RED4ext's `HashMap::Get`, which hashes a `uint64` key with FNV-1a: it
  never finds the instance. In game: 48 NPCs in workspots at
  kabuki-market, `IsActorInWorkspot` true, "no instance" for every one
  (npcs-anims-host, 2026-11-21). [2.5-verified]
- `workWorkspotInstance` (0x2E0 bytes) holds handles at `+0x60`
  (`workWorkspotTree`), `+0x70` (`workWorkspotResource`), `+0x80`, `+0x90`
  (its destructor releases those four), and, per Codeware, `animName`
  `+0x110`, `idleAnim` `+0x118`, `entryId` `+0x120`. `CResource.path` (the
  depot path hash) is at `+0x30`. Whether a community or crowd workspot's
  instance holds its resource is not yet seen (`npc seek: its workspot:`
  says). [2.5-untested]

### 13.154 Two crashes of a guest riding with the host, and the senses' meter on an NPC body (2026-11-22, game 2.50, two instances, exe read)

With spawns working from a seat (§13.141) a riding guest's game went down
within 20 s of every ride, five rides in five, at one of two places.

- **Mounting an NPC while our own V is mounted** (`MountingFacility.Mount`,
  instant, an NPC proxy into a proxy vehicle: a driver of the other game's
  traffic): `0xC0000005` at `+0x2AE4A10` on a job thread, 0.2 to 0.3 s
  after the mount, five times; never in the ten mounts of the night before
  made while V was on foot, never with the mounts switched off (one ride
  of 150 s). The stack: `0x1401F8210`, `0x14082355C` (a list of handlers
  run), `0x142C46940`, `0x142AE4A04`. `0x142C46940` is a virtual in a
  vtable at `0x143774898`, between `gameActionEquipItemState`'s and
  `gameActionRotateToObjectState`'s (not an RTTI class): its base
  (`0x1408AAD70`) stores the entity id of a weak object handle, null or
  not, and it then asks `0x142AE4A04` whether that object is the local
  player's, which dereferences it (`object + 0xC0`). Which action it is
  and why its object is null were not found then. A wait of 1 s after the
  body came, and the controller not held, changed nothing. **The action**
  (2026-11-23, exe read): **SlideToSimple** (factory type 13,
  `gameCActionsFactory` `0x140905000`; `0xC0` bytes, vtable `0x143774798`,
  constructor `0x142C46528`), whose move controller
  "CActionSlideToSimpleController" sits at `+0x58` (vtable `0x143774820`,
  base "Transition Controller" `0x143665AB8`); `0x142C46940` is that
  controller's slot 15. Only the behaviour-tree node
  `AIbehaviorActionSlideToWorldPositionNodeDefinition` with `useMovePlanner`
  false creates it (`0x142C36B18`, `0x142C36268`; setup `0x142C46BA8`, host
  the puppet's `moveComponent` at `+0x440`): a proxy's tree still runs a
  slide with its brain off. Its start (slot 6, `0x142C469C4`) adds the
  controller to the move component (`0x14022ED40`, `0x14022EDC4`), which
  hands it the entity in its weak handle `+0x270`, non-null (checked at
  `0x14022EE98`). Slot 15 casts that entity to `gamePuppet` (`0x1401BCA1C`)
  and, unlike the motion-table controller's override (`0x141932FF0`, a
  `gameObject` cast with a null test), sends the result unchecked to
  `0x142AE4A04` (is it the local player, through `gameIPlayerManager`): an
  entity that is not a puppet makes the cast null and the read of `+0xC0`
  faults. Who sets `moveComponent+0x270` was not found (the mount's vehicle
  is the guess). [2.5-static] The **slide guard** (`engine/guards.cpp`,
  feature `slide guard`) hooks `0x142C46940` (hash 2938119384) behind its
  first `0x36` bytes (its two first calls checked against hashes 1165500739,
  the base, and 2632196524, the cast) and answers through the base alone
  when the entity is null or not a `gamePuppet`, which is what the original
  returns for "not the local player". With our V on foot the same crash
  came twice on 2026-11-23, 3 to 13 s after drivers were seated in street
  cars (with the 100 m share, about ten a minute). tandem seats no
  proxy while our own V is mounted (`TandemNpcs.FollowSeat`: it waits out
  of sight). A proxy seated 1.5 s after our V got out of a seat died the
  same way (instance c, 20:41:31, a street car's driver): it now waits 5 s
  after our V was last mounted. [2.5-verified]
- **`gamePuppet.HideIrreversibly` on an NPC that holds a weapon**:
  `0xC0000005` at `+0x253048` (a read of `0x68`), game thread or a job,
  within a tick or two of the call, three times (a quest's armed "freaks",
  Tyger Claws), and the crash of the first two-game session; never on the
  unarmed crowd hidden the same way in the same rides (hundreds), never
  with the hide switched off. The stack: `0x141085E34` (hash 3584363503),
  `0x1408234A4`, `0x140823F24` (an entity's flags at `+0x158`, bit `0x20`
  of `+0x15C`), `0x1403D1BF8` (every component's slot `+0x198`),
  `0x14025302C`: a component whose `+0xA8` is null. A wait of 1 s after
  the NPC was first seen changed nothing. tandem fades such an NPC out the
  animation system's way, its weapon with it (`TandemVisibility.Fade`),
  and hides the unarmed irreversibly as before: one ride of 150 s through
  the same places, 279 NPCs held back, no crash. That the weapon is the
  cause is the pattern, not a reading of the component. [2.5-verified]
- **The senses fill their meter for any NPC body, whatever the attitude**:
  a neutral `Character.HomelessMan` had a guest's puppet at
  `GetDetection` 100 and the host's V at 0, both standing beside him on
  foot, his attitude to both neutral (1). For a V the meter counts for
  the hostile alone. tandem's "this NPC has detected that V" (and the
  guest's meter) now asks for a hostile attitude as well
  (`TandemNpcs.Detected`, `Notice`): a riding guest's combat that came
  and went was this. Changed after the measure, not yet run. [2.5-verified]
