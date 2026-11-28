# The cut multiplayer's leftovers: what runs in single player

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Starts
from `coop-npcs-combat.md` §1, which lists the replicated-state classes,
the replication helpers and the session classes; not repeated here.
Read 2026-11-21 on game 2.50: exe decompiled with `tools/re/decompile`,
vtables from the exe, values read live with `tools/re/mem.py` on anya.

## 13.120 The runtime mode, and the gate every replication native has (2026-11-21, game 2.50, exe and live process read)

- `worldRuntimeInfo` `+0x40` is the world's mode byte: **1 single
  player, 2 client, 3 server, 5 game preview**; `IsMultiplayer()` is
  mode 2 or 3. `+0x41` is the local peer id. Natives:
  `IsSingleplayer` `0x142801be0` (hash 3536658472), `IsMultiplayer`
  `0x140ee4808` (3021086673), `IsClient` `0x1410790c8` (4158856600),
  `IsServer` `0x1411fab68` (4277345712), `IsGamePreview` `0x142801bcc`.
  Live: mode 1, local peer 0. An entity reaches it through
  `*(*(entity+0xb8)+0x10)`. [2.5-verified]
- An entity's network state is the pointer at `entEntity+0x148`
  (`IsReplicated()` is `+0x148 != 0`; its handler `0x1412265d4` is
  folded with `senseComponent.HasSenseMappin`'s). Live: null on V, NPCs
  and cars. [2.5-verified]
- **The replication natives do nothing in single player.**
  `gameObject.ReplicateAnimFeature` (`0x140d36680`, hash 3110150080),
  `ReplicateAnimEvent` (`0x140e3eb80`, 1943543542),
  `QueueReplicatedEvent` (`0x14114abc8`, 3242794970) and
  `ReplicateInputFloat/Bool/Int/Vector` (`0x140e1ad78`…) read their
  arguments, then act (vtable slots `0x238`…`0x250`) only when
  `+0x148 != 0` and the mode is 3. `gamePuppet.SetCPOMissionData`
  (`0x142ae5a68`) only in mode 3. `gameDeviceBase.GetServerState`
  (`0x141325ac8`) answers only in mode 3, `GetClientState`
  (`0x142b35e38`) only in mode 2: null otherwise, so the device
  `…ReplicatedState` classes (`devices.md`) have nothing behind them.
  `RequestWeaponEquipOnServer` (`0x142b233a8`) acts only in
  multiplayer for the local peer's puppet. [2.5-verified]
- What stays useful is the script side: `ApplyFeatureToReplicate`,
  `SetInput…ToReplicate`, `PushEventToReplicate` apply locally, then
  call these no-op natives, so their call sites remain the catalogue of
  animation-relevant state (`coop-npcs-combat.md` §1.4). [2.5-verified]
- **What those call sites carry, and what they do not** (2.50 scripts
  read 2026-11-22, `final.reds` line numbers; 130 call sites in 43
  classes). On an NPC: each AI action's own animation feature
  (`TweakAIActionAbstract.SendAnimData` 302068, on the owner and its
  weapons; `weaponOverride` 302365), `CoverStance`
  (`AISubActionCover_Record_Implementation` 505058), `NonCombatAim`
  (`AIAlertedStateDelegate` 451751), item handling (`AIActionHelper`
  170160), equip, unequip and reload sub-actions, the shot events
  (`AIWeapon` 173342–173345: `Shoot`, `Overheat`), hit, pain and death
  reactions (`HitReactionComponent`, `AIHitReactionTask`,
  `AIDeathReactionsTask`, `PainReactionTask`), `FacialReaction`,
  `CrowdRunningAway` and `Undead` (`ReactionManagerComponent`
  144649–147529). The rest is devices and vehicles (`VehicleComponent`
  20, `SensorDevice` 16, `SecurityTurret` 7). **None is in
  `NPCStatesComponent`**: the high-level, upper-body and stance states
  go through the plain `AnimationControllerComponent.ApplyFeature`
  (`UpdateHighLevelState` 168179, `UpdateUpperBodyState` 168458,
  `UpdateStanceState` 168843) and the anim wrappers through
  `SetAnimWrapperWeightOnOwnerAndItems` (168205). So wrapping the
  helpers sees none of what `NpcAnims.reds` polls (those three states,
  the weapon in hand, the top hostile target, the workspot: the last
  three are no animation input at all) and cannot replace that poll; it
  would add streams the poll cannot see (an action's animation, cover,
  reload), each an `AnimFeature` subclass to serialise in a new message.
  The states' own event is `NPCStatesComponent.ChangeHighLevelState` /
  `ChangeUpperBodyState` / `ChangeStanceState` (168077–168123, private
  final, a per-entity component: a wrap may only queue; wrapped
  2026-11-22, `NpcAnims.reds`; their callers: §13.21, the states' entry
  points). [2.5-untested]
- Stubs: `gameCpoPickableItem.EquipItem` (`0x142e21fd4`) reads its
  argument and returns. `cpPlayerSystem.IsCPOControlSchemeForced`
  (`0x14101b820`) returns a global byte (`0x143bc43a0`).
  `scnISceneSystem.GetPeerIdDialogChoiceFactName` (`0x142a339f0`,
  `scnSceneSystem` slot `0x220` → `0x142cef9e0`) returns a constant
  string, read live: `"SelectedForMultiplayerChoiceDialog"`. [2.5-verified]
- Global natives (`IsHost`, `IsMultiplayer`, `MPReplicateComboAttack`…):
  their `CGlobalFunction` (size 0x100) holds a `regIndex` at `+0xF0`
  (274 `IsMultiplayer`, 276 `IsClient`, 277 `IsServer`, 13175
  `MPReplicateComboAttack`), but the class handler table
  (`0x143b7bd30`) is empty at those indexes: globals dispatch through
  another table, not found. What `IsHost()` answers in single player is
  open. [2.5-verified: the offsets; unverified: the dispatch]

## 13.121 Who controls an entity: three per-object fields (2026-11-21, game 2.50, exe and live process read)

- `entEntity+0x155` (byte): the controlling peer, `0xff` for none.
  `GetControllingPeerID` (`0x1410b4c14`, hash 2589728621) returns it;
  `IsControlledByAnyPeer` (`0x14108c64c`, 3265011690) is `!= 0xff`;
  `IsControlledByLocalPeer` (`0x140ff79f0`, 136126637) compares it with
  the runtime info's `+0x41`; `IsControlledByAnotherClient`
  (`0x14039ca54` → `0x14039cc84`) also needs mode 2. Live: V 0; NPCs and
  cars `0xff`. [2.5-verified]
- `gameObject+0x1fc` bit `0x4`: **player-controlled**.
  `gameObject.IsPlayerControlled()` (`0x140e38910`, hash 2080251669)
  reads it. Live: V `0x05`, NPCs `0x00`. [2.5-verified]
- `gameObject.playerSocket` (`+0x1d0`, `gamePlayerSocket`): its `+0x20`
  (`gameObject+0x1f0`) points at the controlling `gamePlayer`. Live: V's
  points at a `cpPlayer` (`: gamePlayer`, 0xf0; `peerID` `+0xc0` = 0)
  whose `+0xb0` handle is V again. [2.5-verified]
- All three are written by one function, `gamePlayer` take control
  (`0x1409141e8`, hash 2235240413): the player's `+0xb0` = the entity,
  the entity's `+0x1f0` = the player, `+0x1fc |= 4`, the entity's
  vtable slot `0x1e0`, then slot `0x230` on each component. The player
  system's set control for a peer (`0x140af8258`, slot `0x1f8`, hash 1331962237) writes `+0x155` = peer first. Switching the local
  player's controlled object (`0x140a82c94`, hash 3287687122) is what
  `cpPlayerSystem.LocalPlayerControlExistingObject` (`0x142e689e8` →
  `0x142e68004`) runs: the camera and turret takeovers. [2.5-verified]

## 13.122 The player system has a per-peer table; the single-player manager has one player (2026-11-21, game 2.50, exe and live process read)

- `cpPlayerSystem` (0x605c0): local peer at `+0x61`; a table of players
  at `+0x130`, 0x40 per entry (byte peer at `+0x00`, main object handle
  at `+0x10`), count at `+0x2f0`: room for 7. The local main object's
  handle is at `+0x60580` (lock `+0x60590`). Live: local peer 0,
  1 entry (peer 0 → V), `+0x60580` = V. [2.5-verified]
- Its interface (vtable `0x1436cd188`): slot `0x1c0` adds a player and
  spawns its entity (`0x1408f4648`, hash 2813733872; the description's
  peer at `+0x52`, record at `+0x20`; spawn through `+0x308`), `0x1c8`
  removes a peer's (`0x140b46394`, 3809941156), `0x1d8` collects the
  player-controlled objects (`0x140989760`, 4067044415: entry peer ==
  local tests flag 1, else flag 2; flag 4 adds the entry's main object,
  flag 8 the peer's controlled object), `0x1e0` the local controlled
  object, `0x1e8` a peer's controlled object (`0x1402820b4`, 3874825563:
  the player manager's slot `0x1f0`(peer), then its `+0xb0`), `0x1f8`
  set a peer's control, `0x200` has peer (`0x142e67fcc`), `0x228` the
  local main object (`0x1402f6f44`, 1789467592), `0x230` a peer's main
  object (`0x142e67dcc`, 1237327419), `0x238`…`0x260` the attach and
  detach callbacks. Slot `0x180` (`0x14226d800`) removes the local peer
  and adds the description held at `+0x80`: the one player of a single
  player game. [2.5-verified]
- `FindPlayerControlledObjects(pos, radius, includeLocal,
includeRemote)` (`0x142e687dc`) is that slot `0x1d8`: "remote" means
  an entry whose peer is not the local one. [2.5-verified]
- **`gamePlayerManager`, the single-player manager** (0xe0, vtable
  `0x1430cf520`), holds **one** `gamePlayer` at `+0x58`. Its "player of
  peer N" (slot `0x1f0`, `0x1419543b0`, hash 704058176) returns `+0x58`
  whatever N is; slots `0x1f8`, `0x200`, `0x220` are the same function.
  Slot `0x1b8` (`0x1408f451c`) creates that player and adds it to the
  player system. Live: one `gamePlayerManager`, its `+0x58` V's `cpPlayer`.
  [2.5-verified]
- `mpPlayerManager` (0x128, vtable `0x14376d6e8`) is the multiplayer
  one: up to 7 `gamePlayer` pointers at `+0x88`, count `+0xc0`, looked
  up by `gamePlayer+0xc0` (`0x142bec574`, hash 116725490), under a
  critical section at `+0x60`. Only its class default object exists in
  a single player game. Its `PrespawnRequest`/`SpawnRequest` natives
  have no handler. [2.5-verified]
- **Consequence:** a second _player_ (an entry, a `gamePlayer`, a
  controlled puppet) cannot exist in a single player world. An entry
  for peer 1 added through slot `0x1c0` would spawn an entity, but every
  by-peer control call lands on V's one `gamePlayer`: control of peer 1
  is control taken from V. The multi-player structures need
  `mpPlayerManager`, which needs a multiplayer world (§13.120 mode 2/3,
  which flips the ~120 script branches, `coop-npcs-combat.md` §1.1).
  [2.5-verified: the structures; the conclusion is read, not run]

## 13.123 What vanilla reads to treat an object as a player (2026-11-21, game 2.50, scripts and exe read)

- `GetPlayer(game)` and `GetLocalPlayerMainGameObject()` are the one
  local main object (`+0x60580`): never a second one. [2.5-verified]
- `IsPlayer()` is a script virtual: `true` for `PlayerPuppet` and
  `Muppet` classes only. [2.5-untested]
- `IsPlayerControlled()` is the per-object flag of §13.121, and the
  **NPC threat weights** read it: `AIActionTarget.RegularThreatCalculation`
  starts a threat at 0.01 and sets **1.0 if `threat.IsPlayerControlled()`**
  (or a turret) (`final.reds:165497`); `UpdateThreatsValue` gives
  **2.0** to a player-controlled threat (`:165547`) and applies
  `BlockTargetingPlayer` only to such threats (`:165419`). A tandem
  puppet (flag 0) is weighted like any NPC. Other readers:
  `SecurityAreaControllerPS.AreaExited` (`:117776`), the sense
  component's breach detection (`:156102`, "breacher is not player"),
  `NPCPuppet.CheckNPCKilledThrowingGrenade` (`:129945`),
  `MechanicalImpactComponent` (`:321328`), XP gain (`:80643`); native:
  `gameEffectObjectFilter_NoInstigatorIfPlayerControlled`. [2.5-untested]
- `IsControlledByLocalPeer()` gates 27 script sites, mostly on
  `PlayerPuppet` (input, HUD); a spawned puppet has peer `0xff`, so
  they answer false for it. `PlayerPuppet.OnGameAttached` registers
  input listeners `if IsControlledByLocalPeer() || IsHost()`
  (`:40106`) and queues `PlayerAttachRequest`. [2.5-untested]
- `PlayerAttachRequest` handlers: per owner (`EquipmentSystem.m_ownerData`
  `:55330` and `PlayerDevelopmentSystem.m_playerData` `:79163`, both
  **persistent**: a second player would be saved), last one wins
  (`UIScriptableSystem` `:76097`, `UIInventoryScriptableSystem` `:67860`:
  a second `PlayerPuppet` would take V's inventory UI listeners), the
  local main object (`CraftingSystem` `:70202`). [2.5-untested]

## 13.124 The Muppet, the multiplayer's remote character (2026-11-21, game 2.50, RTTI and scripts read)

- Native class `gameMuppet` (script `Muppet`, `: gamePuppetBase`,
  0x298, vtable `0x1431a9228`, 19 slots of its own, small real bodies):
  `GetAttitude` `0x142e2c828`, `IsMuppetIncapacitated` `0x1410a6bd0`,
  `GetItemQuantity` `0x142e2c89c`; script `IsPlayer()` returns true
  (`final.reds:107821`). Driven by `gameMuppetReplicatedState` (0x320:
  `gameMuppetState` {position, yaw, velocity, look, high-level, upper
  body, inventory, abilities}) and components `gameMuppetLocomotion/
Physics/InputHandler/Inventory/Stats/…Component`. [2.5-untested]
- Scripts accept it as a player: `IsPlayerActivator` (device
  operation triggers, `:107800`), interaction-area triggers (`:104691`),
  hit representation (`:169889`). No entity template using it is known
  (`gamedataGenderEntity_Record.MultiplayerEntities`,
  `gamedataSpawnableObject_Record.MultiplayerTemplatePaths` not read);
  the `Character.Muppet` record NCO touched spawns a Panam-derived NPC
  (`puppets.md`). [unverified]

## 13.125 Smaller leftovers checked (2026-11-21, game 2.50, exe read)

- `AICTreeNodePlayerControlledDefinition`: its node (vtable
  `0x14368a558`) on activation (`0x142c2d0d8`, hash 2628723362) queues a
  PSM postponed Bool parameter (`gamestateMachineeventPostponedParameterBool`,
  true) on its owner and returns 2 every tick. A marker, not an AI-off
  switch (`coop-npcs-combat.md` §1.6, lead b). [2.5-verified]
- `SetIgnoreTimeDilationOnLocalPlayerZero` (`0x14131b2f8`, hash 752036011) asks the player manager for peer 0's player and toggles
  bit 0 of `+0x258` of its controlled entity: "LocalPlayerZero" is peer
  0, always V (`time.md`). [2.5-verified]
- `mpPlayerManager.GetPlayerNicknameByGameObject` (`0x142bed1c0`) reads
  the object's `+0x155` and looks the peer up in its own array: needs
  the multiplayer manager. [2.5-verified]

## 13.174 Every replicated state: where its fill and apply live (2026-11-28, game 2.50, RTTI, exe and Ghidra read; one live read on instance c)

The survey behind §13.175–13.179: every RTTI class named `*Replicated*`
(80, plus the `*SharedState` and `netEntityAttachmentInterface` kin),
each class's vtable compared slot by slot with its base's.
[2.5-verified] (RTTI, exe)

- **A component's pair is its vtable slots 52 (fill) and 53 (apply)**,
  empty (`0x1401ff2b0`, a `ret`) in `entIComponent`; both act on the
  component's state at **+0x58**. Twelve classes override one or both
  (table below). **An entity's pair is slots 47 (fill) and 48 (apply)**,
  on the state at `entEntity+0x148` (§13.120); `entEntity`'s own 48
  (`0x142993a50`, hash 1519128468) applies only the state's attachment
  (+0x48, §13.175). Four classes override 47/48: `gamePuppet` (NPCs and
  V), `gameDeviceBase`, `gameweaponGrenade`, `gameMuppet`.
  `vehicleBaseObject` and every item and weapon keep the base.
  [2.5-verified] (exe)
- **In single player every state pointer is null**: read on instance c's
  V: the entity's +0x148 and the +0x58 of its animation controller,
  attachment slots, state machine, status effect and FPP camera
  components are all 0. So no slot can be called as it is: like the
  ragdoll (§13.162), each pair is a map to the inner functions it calls,
  and those are what a caller would use. [2.5-verified] (live)
- Items in the states carry a `netTime` (milliseconds; × 0.001 at
  `0x1437b9c68`): an apply plays an item once its time has come, and the
  fills drop items older than **2 s** (`0x1437b9f18`). Positions are
  fixed point, × 2⁻¹⁷ m (`0x1437b97dc`), as the ragdoll's. [2.5-verified]
  (exe)

<!-- prettier-ignore -->
| Owner (state) | Fill (slot, address, hash) | Apply (slot, address, hash) | Gate in the apply | What the pair does |
| --- | --- | --- | --- | --- |
| `entRagdollComponent` | 52 `0x142992dcc` 317854601 | 53 `0x142992d84` 352523151 | none | §13.162: used for corpses |
| `gameAINetStateComponent` (`NPCStatesComponent`'s native base) | 52 `0x142a29f98` 1598821577 | 53 `0x142a29c58` 1635062991 | owner's +0x155 ≠ local peer (passes on every NPC, 0xff) | §13.176 |
| `entAnimationControllerComponent` | 52 `0x1429b6930` 756881544 | 53 `0x1429b68c4` 796268686 | none | 52 only prunes queued items; 53 applies anim wrapper variables (§13.176) |
| `gameAttachmentSlots` | 52 `0x142b4bf94` 324604802 | 53 `0x142b4bf28` 359273352 | mode 2 and owner ≠ local peer | the two hand slots' items (§13.177) |
| `gameStatusEffectComponent` | none | 53 `0x142ac0498` 2577208822 | state set, component +0x88 bit 1 | §13.177 |
| `gameWeakspotComponent` | 52 `0x142a89644` 1071191141 | 53 `0x142a89638` 1106646123 | none | each weak spot's health (stat pool 0x29) and last instigator (weak spot +0x278) |
| `gamestateMachineComponent` | 52 `0x142b3ffbc` 2894730797 | 53 `0x142b3fe04` 2932151859 | owner ≠ local peer | the whole PSM context (component +0xa0, 0x1a980 bytes) copied, then `0x142b3e5fc` (1249714614): the player's state machine, no NPC has one |
| `movePoliciesComponent` | 52 `0x142c16dc0` 1058280556 | 53 `0x142c16d88` 1093735538 | none | per policy `{key, policies}`, one field copied between the move system's object (its slots 0x1e8 / 0x1e0) and the policy (§13.179) |
| `gameScanningComponent` | none | 53 `0x142ad9e7c` 1053561926 | none | scanning state, percent scanned, controlling peers: each V's own scanner |
| `gameFPPCameraComponent` | none | 53 `0x142ad281c` 1218319436 | mode 2 | a player's look pitch and yaw |
| `gameTransformAnimatorComponent`, `gameRootTransformAnimatorComponent` | 52 `0x142a9bf98` 335681550, `0x142a9bf64` 2100894130 | none found | - | playback state, filled at most once a second (`0x1437b9d30`, 1.0) when changed |
| `gamePuppet` (`gamePuppetReplicatedState`, NPC and player subclasses) | 47 `0x142ae4c60` 1763643362 | 48 `0x142ae4bb4` 1794772968 | none | §13.175 |
| `gameDeviceBase` (`gameDeviceBaseReplicationProxy`) | 47 `0x142b35dfc` 2870153519 | 48 `0x142b35d14` 2902855989 | state set | §13.175 |
| `gameweaponGrenade` | 47 `0x142b1c7dc` 4204204780 | 48 `0x142b1c7b8` 4238480114 | state set | §13.175 |

`netDefaultComponentReplicatedState` and `netEntityReplicatedState` are
the plain states of every other component and entity (the RTTI dump lists
230 and 66 vtables under them): they hold only the base fields
(`componentName`, `enabled`; `persistentID`, `recordID`). [2.5-verified]
(RTTI)

## 13.175 The entity states: puppets, devices, grenades, attachments (2026-11-28, game 2.50, Ghidra read)

- **`gamePuppet`'s apply** (slot 48) runs `entEntity`'s (the attachment),
  then replays the state's actions buffer (`0x142b5b7e8`, hash
  1779244953: each `gameActionReplicatedState {replicationId, type,
start, stop}` newer than the last one played starts a game action of
  that type), then its anim events and entity events (`0x142b5b97c`: a
  `gameReplicatedAnimEvent {name}` or `gameReplicatedEntityEvent {value:
redEvent}` played through its own slot 0x80), then the state's own
  slots 0x60 and 0x68, then a bool on the puppet's blackboard from
  `hasCPOMissionData`. Its fill (47) only drops actions and events older
  than 2 s: the items are pushed by the dead natives of §13.120.
  [2.5-verified] (Ghidra)
  - State slot 0x60 (`0x142aea9e4`, hash 2018056393): health (+0xc8)
    and armor (+0xcc), each unless -1.0, set through the stat pools
    system (its slot 0x250, pool types 0x11 and 0x2d; by its arguments
    the native under `RequestSettingStatPoolValue`, [unverified]).
  - `gameNpcPuppetReplicatedState`'s 0x68 (`0x142af5b64`, hash
    3447069364): every weapon's shot history played (§13.178); its 0x70
    and 0x78 add and remove a weapon's history (the weapon's +0x530
    points at it only in mode 3).
- **`gameDeviceBase`**: the fill bumps `versionId` (+0x60) when the
  device marked itself changed (+0x244); the apply, when `versionId`
  differs from the one applied (+0x240), calls the script
  `ApplyReplicatedState(scriptState)` (CName `ApplyReplicatedState`,
  through `0x142b3593c`, hash 133182865). The three script overrides
  are thin: `Door` calls `MoveDoor(m_isOpen, m_wasImmediateChange)`
  (`final.reds:201645`), `WindowBlinders` `ApplyAnimState(m_isOpen,
m_isTilted)` (`:500488`), `SecurityTurret` `TurnOnDevice`/`TurnOffDevice`,
  `ShootStart`/`ShootStop`, its health through the stat pools and
  `OnDeath` (`:156666`; its state `{m_isOn, m_isShooting, m_isDead,
m_health}` `:157427`). It moves the client's look; the device's
  controller PS stayed the server's. [2.5-verified] (Ghidra, final.reds)
- **`gameweaponGrenade`**: the fill copies instigator, item id, the
  world transform, `exploded` (+0x2f4) and `launched` (+0x2f5); the
  apply, while the copy is not exploded, sets its transform from the
  state; a new instigator is queued as an event; on `launched` it
  switches a flag off on its components (`0x142b1c72c`) and queues a
  launch event at the state's position; on `exploded` it queues a
  `gameprojectileForceActivationEvent` (`0x142b1bab8`) and an event at
  the state's position. [2.5-verified] (Ghidra)
- **Attachments**: `gamecarryReplicatedEntitySetAttachmentToEntity`
  (`{entity, slot, localTransform}`), `…ToNode` and `…ToWorld` are
  written by the engine's own attach of an entity to another's slot
  (`0x1408a91f0`, hash 2115184978) after it has attached, only when the
  entity is replicated; their apply (vtable +0x18: `0x142ade69c`, hash
  491724460; `0x142ade854`, 3598458293) runs the same calls again
  (detach `0x1408a84e4` 2192777377, attach to a slot `0x140751db0`
  2876586092 or to the root `0x142993b48` 7220408, local transform
  `0x1404042c4` 4054718210). The record of an attach, nothing more.
  [2.5-verified] (Ghidra)

## 13.176 NPC animation and AI states: the cut pair is the path tandem already takes (2026-11-28, game 2.50, Ghidra and final.reds read)

- `NPCStatesComponent` derives from `AINetStateComponent`
  (`final.reds:153565`): its `SetCurrent…State` are the native
  `SetReplicated…State`, which keep each of the eight states as
  `{value, prev, time}` at component +0xb8…+0x114 in single player too.
  The fill copies them to the state; the apply (`0x142a2a134` per
  state) writes a changed triple whose time has come into the component
  and calls the component's script function by name:
  `UpdateHighLevelState`, `UpdateUpperBodyState`, `UpdateStanceState`,
  `UpdateHitReactionsExceptionState`, `UpdateBehaviorState`,
  `UpdatePhaseState`, `UpdateDefenseMode`, `UpdateLocomotionMode`
  (`UpdateHighLevelState` `:153919`: anim feature, blackboard, role
  callbacks). `NpcAnims.reds` sets the same states through the same
  setters and applies the same features: no gain. [2.5-verified]
  (Ghidra, final.reds)
- `entAnimationControllerReplicatedState`: anim features `{name, value:
animAnimFeature, invokeCallback}`, input setters, look-at add/remove
  (`entReplicatedLookAtAdd {bodyPart, request: animLookAtRequest,
targetPositionProvider, ref}`) and wrapper variables `{name, value,
applyServerTime}`, all queued by the dead natives. Slot 53 applies the
  wrapper variables only (each item's slot 0x20 on the animated
  component, +0x120); where the features and look-ats were applied was
  not traced. Each has a script path (`ApplyFeature`,
  `SetAnimWrapperWeight`, the look-at events). [2.5-verified] (Ghidra);
  [unverified]: the features' apply

## 13.177 Hands, status effects: thin over the transaction and status effect systems (2026-11-28, game 2.50, Ghidra read)

- `gameAttachmentSlots`' fill (`0x142b4b3e4`) records two slots only,
  the TweakDB ids at `0x14307c970` and `0x14307c950` (by their use the
  right and left weapon slots; [unverified]), as `{slotID, activeItemID,
hasItemObject}`. Its apply's body (`0x142b4c9d8`, hash 3383436206; no
  gate of its own) unequips (transaction system slot 0x3a8) or gives and
  equips (slots 0x2e0, 0x250, 0x398, 0x460) what differs.
  [2.5-verified] (Ghidra); by their arguments the natives under
  `TransactionSystem.RemoveItemFromSlot` / `GiveItem` / `AddItemToSlot`
  [unverified]
- `gameStatusEffectComponent`'s apply body (`0x142abfd0c`, hash
  2823758648; (state, component)) diffs the state's
  `{statusEffectRecordID, stackCount, source}` list against the list it
  applied last, and calls the status effect system's slot 0x1b0 (apply a
  record, its stacks and source) and 0x1c0 (remove stacks). Tandem sends
  the same shape already (`NpcEffect`, `StatusVisuals`: record ids, the
  whole set on each change). [2.5-verified] (Ghidra)

## 13.178 The weapon's shot replay: a shot is an attack record, a target and an offset on it (2026-11-28, game 2.50, Ghidra read)

- `gameWeaponReplicationHistory` (0x1d0): a ring of 8
  `gameReplicatedShotData {timeStamp, attackId, target, targetLocalOffset}`
  (+0x20, newest index +0x1a8, count +0x1a0) and a
  `gameReplicatedContinuousAttack {start, stop, attackId}` (+0x1b0).
  Playing it (`0x142a87e38`) fires every shot newer than the last played
  (+0x10). [2.5-verified] (Ghidra)
- One shot (`0x142a87d70`, hash 1501568011: (history, shot)): the
  weapon's attack whose record is `attackId` is looked up in its list
  (+0x3e0, 16 bytes each) and made current (`0x140e6b320`); then
  `0x142af3d2c` (hash 3839172010: (weapon, shot)) aims from the muzzle
  at the target's world transform × `targetLocalOffset` (straight ahead
  when there is no target or it is not a puppet-like object, type byte
  +0x156 outside 2…5) and shoots through `0x140b6c814` (hash
  1097148015), **the shoot under `gameweaponObject.AI_ShootAt`**
  (`0x140b6bb28`). No network gate on either. A continuous attack starts
  through `0x142af47d0` (1249257224) and stops through `0x1407d1c58`
  (3689091666). [2.5-verified] (Ghidra)
- Tandem fires a partner's or an NPC's shot through `AI_ShootAt` at a
  world point (`Presence.reds:1007`, `Combat.reds:580`): the same shoot.
  What the cut shape adds is the attack record (the weapon's own attack
  for that shot, a charged or a burst one) and a point fixed on the
  target, not in the world. [2.5-untested]

## 13.179 Dead ends, and the cut design against tandem's (2026-11-28, game 2.50, RTTI and exe read)

**No replicated state, no pair** (nothing to build on): vehicles (no
state class; `vehicleBaseObject` and every vehicle component keep the
base slots); physics props and destruction (`entPhysicalDestructionComponent`,
`entBakedDestructionComponent` and the world's destructible-mesh nodes:
no state); inventory, loot and drops (no state beyond the two hand
slots); weather (none). The cut multiplayer, a CPO test mode, never
replicated a car or a prop. [2.5-verified] (RTTI, exe)

**Game systems**: the shared states of the damage (`hitHistory`,
`killHistory`), god mode, time (`paused`, `gameTime`), ping
(`replicatedPingEntries`), mappin, journal and quests systems exist;
their systems' slots 38/39 are the save game's write and read, not the
replication. Their fill and apply were not found. [2.5-verified]
(exe); [unverified]: where they are

**Who ran what.** The cut model is one server for everything: it runs
every NPC, device and grenade, and each client applies the server's
state to its own copy, items timed by `netTime` and played when due.
Tandem's is one owner per piece of state (`ARCHITECTURE.md`), the
owner's game streaming it, the others applying it on arrival.

<!-- prettier-ignore -->
| Domain | Cut design | Tandem | For co-op | Better |
| --- | --- | --- | --- | --- |
| NPC states | the eight AI states as `{value, prev, time}`, applied when due through the component's own `Update…` | the same states, through the same setters, sent from their change (`NpcAnims.reds`) | same feel; the timestamp would hold a late state back rather than play it early | equal: keep ours |
| NPC motion | move policies replicated (`movePoliciesComponent`): each client's own navigation runs the server's intent, no positions | the owner's position at 10 Hz (`regions.md`), the proxy's brain off | intent is smoother and cheap, but two navmeshes and crowds diverge: a proxy off the owner's spot, a hit that misses it; positions cannot drift | ours: a co-op fight needs the proxy where the owner's NPC is |
| Shots | `{attack record, target, offset on target}`, the weapon's own attack fired at the target's current place | `AI_ShootAt` at a world point, hits dropped | theirs lands on the copy of a moving target and fires the right attack's projectile and effects; same bandwidth (a 4-byte record id against nothing, 12 bytes of offset against 12 of point) | theirs, as a shape inside ours: add the record and the target-relative point to our shot message |
| Devices | the server's PS; the client's look moved by `ApplyReplicatedState` (a door's `MoveDoor`) | the host's word, played on the guest's own PS by the device's quest actions (`devices.md`) | ours keeps the guest's PS, interactions and quests true; theirs only moves the look | ours |
| Grenades | the server's grenade; the client's copy placed and exploded by `gameprojectileForceActivationEvent` | a copy on the real one's path, exploded where and when it did (`Throws.reds`) | the same | equal |
| Status effects | record, stacks, source, diffed | record ids, the whole set on change (`NpcEffect`, `StatusVisuals`) | stacks and source would show a stacked bleed or burn as stacked | ours, adding stacks when a stack matters |
| Hands | two slots' items through the transaction system | the weapon in hand polled and set | the same calls | equal |
| Ragdoll | bodies' transforms | the same, through the inner physics calls (§13.162) | the only domain with no script path | theirs, built |

## What the leftovers could solve (ranked, 2026-11-28)

The survey of §13.174–13.179 adds two rows (1, 5) and confirms that,
besides the ragdoll, every cut apply is a thin layer over a path scripts
already reach: no other native-only sync exists.

<!-- prettier-ignore -->
| # | Leftover | Could solve | Gain over today | Feasibility | Risk | First experiment in game |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | The shot's shape (§13.178): attack record + a point on the target | Shots and tracers on a moving proxy land where the owner's landed; the right attack (charged, burst, its projectile and effects) | precision on moving targets (a 10 Hz copy is up to 0.5 m off a running NPC); bandwidth flat; no code removed | high: script only (the owner sends the target's id and the hit point in its local frame, the receiver turns it back with its copy's transform and keeps `AI_ShootAt`); the native replay (hash 1501568011) only if the record must be forced | low | two games, a ganger strafing: the guest's tracer impacts on the host's ganger copy, point against the owner's, before and after |
| 2 | Threat weights keyed on `IsPlayerControlled()` (§13.123) | Host NPCs pick a guest's puppet as a player: 1.0/2.0 instead of 0.01; `BlockTargetingPlayer` applies to it | built (`Threats.reds`) | high (script `@wrapMethod` of `AIActionTarget.RegularThreatCalculation`, `UpdateThreatsValue`, Madness/Boss; reads only, job-thread safe) | low | host + mock guest, two hostiles nearer the puppet than V: each NPC's top hostile threat before/after the wrap (`npcs-target-guest`, not yet run) |
| 3 | The player-controlled flag, `+0x1fc` bit 4 (§13.121), set on guest puppets | All 11 script readers at once (security areas, breach detection, kill attribution) and the native `NoInstigatorIfPlayerControlled` filter | removes per-reader wraps | medium (plugin byte write behind a layout check: V's bit set, NPCs' clear) | medium-high: natives may expect `playerSocket` (`+0x1f0`) set with the flag | read-only first: `tools/re` the readers of `+0x1fc`; then one puppet, a combat and a security-area scenario, `game.sh bt` on a freeze |
| 4 | Script side of the replication helpers (§13.120: natives dead, scripts alive) | Animations of NPCs the state poll cannot see (an AI action's animation feature, cover, reload, reactions); not the high-level, upper-body and stance states, which never go through the helpers | new streams, not a replacement | high (already used for `PushEventToReplicate`) | low | counted (§13.120; the name/value log of a fight was not run); the three states are sent from their own wraps (`NpcAnims.reds`, 2026-11-22); left: a two-game fight, proxies still while their NPC strikes or covers |
| 5 | Status effect stacks (§13.177) | A stacked effect shown as stacked on a proxy | a stack count beside each record id | high (script: `StatusEffectHelper`) | low | only if a stacked bleed or burn looks wrong |
| 6 | `RegisterRemotePlayerMappin` / `CPO_RemotePlayerVariant` | Player markers | done | - | - | - |
| 7 | Per-owner scriptable systems (`EquipmentSystem`, `PlayerDevelopmentSystem`, §13.123) | Vanilla equipment visuals and stats keyed by owner for a guest body | none until a `PlayerPuppet` body is wanted | low | high: both persistent (save pollution); needs a `PlayerPuppet` | none |
| 8 | `PlayerPuppet`-class guest body (`IsPlayer()` true) | Every `IsPlayer()` check (targeting, aggro, devices) | - | low: NCO froze it on 2.31 (`puppets.md`); UI systems retarget to the last attached player | high | read-only: list the `IsPlayer()` sites that matter; no spawn yet |
| 9 | `Muppet` class (§13.124) | `IsPlayer()` true without `PlayerPuppet`'s systems; device triggers | - | low: no template known | medium | list the archives for an `.ent` with `gameMuppet*` components |
| 10 | `entEntity+0x155` peer id = 1 on puppets | `IsControlledByAnyPeer` (2 script users) | - | medium | low | not worth it alone |
| 11 | Entity and component slots 47/48, 52/53 called as they are (§13.174) | - | none: their states are null in single player, and each body is thin over a public path | no | high (a state planted at +0x148 or +0x58 makes the entity replicated to every native gate) | never |
| 12 | cpPlayerSystem slot `0x1c0` add player, peer 1 (§13.122) | A real second player | - | very low: the SP manager maps every peer to V's `gamePlayer` | very high (control taken from V) | none; stays read-only |
| 13 | Device `GetServerState`/`GetClientState`, `questMultiplayer*` nodes, `SelectedForMultiplayerChoiceDialog` | Devices, quests, scene choices | - | none: null / unused in mode 1 | - | - |
| 14 | Runtime mode byte to 2/3 (§13.120) | Every MP branch | - | no (the `IsMultiplayer()` warning stands; also feeds every native gate and device state) | very high | never |

### Next steps

1. **The shot's shape (row 1).** Add the attack record and the hit point
   in the target's local frame (with the target's id) to the shot
   message; the receiver puts the point back on its copy of the target
   and fires `AI_ShootAt` there, its hits still dropped. Test: the
   experiment of row 1, the impact point's distance to the owner's.
2. **Threat weights (row 2).** Wrap `AIActionTarget.RegularThreatCalculation`
   (and `MadnessThreatCalculation`, `BossThreatCalculation`) on the
   host: after the wrapped call, if `threat` is a guest's puppet
   (`TandemOwn`/puppet tag), set `threatValue` as for a player-controlled
   threat (1.0 + the same distance/visibility terms); in
   `UpdateThreatsValue`, give it 2.0 and honour `BlockTargetingPlayer`.
   Reads only, so `// job thread safe:`. Scenario: `npc-targets-guest`
   (V far, puppet near, two hostiles), assert the top threat. Built
   2026-11-21 (`Threats.reds`, scenario `npcs-target-guest`; npcs.md,
   "Whom NPCs attack", also for the other readers); not yet run in game.
3. **The flag (row 3), only if row 2 is not enough.** First a read-only
   pass: `tools/re/xref` the functions that test `+0x1fc & 4` and check
   which also read `+0x1f0`. Only then a plugin setter behind a startup
   check (V has the bit, a sample NPC does not; disable on mismatch).
4. **Animation catalogue (row 4).** Counted and classified
   (§13.120, 2026-11-22): the helpers do not carry the states the 4 Hz
   poll samples, so wrapping them does not remove it. Done 2026-11-22
   (`NpcAnims.reds`; `architecture/npcs.md`, NPC animation):
   `NPCStatesComponent`'s three `Change…State` are wrapped, queue the
   NPC, and it is read and sent on the next tick; the poll stays under
   them every 2 s and logs what they missed. Not yet run in game
   (`npcs-anims-events`). Left: as new streams with their own messages,
   the AI actions' animation features, if a two-game fight shows proxies
   standing still while their NPC strikes or takes cover.

### Verdict: the second player object

No. In a single player world the engine's player chain is
`cpPlayerSystem` (a 7-slot per-peer table, used with one entry) →
`gamePlayerManager` (one `gamePlayer`, returned for every peer) → one
controlled object. The multiplayer manager that holds several players is
not instantiated, and only a multiplayer runtime mode would bring it.
`GetPlayer()` stays V. What vanilla AI and UI actually test on an object
is per-object: the class (`IsPlayer()`), the player-controlled flag
(threat weights, security), the peer id, and registered mappins. Those
can be given to a guest's puppet piecemeal (rows 2-3, 6), which buys
most of the targeting prize without a second player.
