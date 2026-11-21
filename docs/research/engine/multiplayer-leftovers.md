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

## What the leftovers could solve (ranked, 2026-11-21)

| #   | Leftover                                                                                                  | Could solve                                                                                                                                                   | Feasibility                                                                                                                                                                                     | Risk                                                                        | First experiment in game                                                                                                                                    |
| --- | --------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Threat weights keyed on `IsPlayerControlled()` (§13.123)                                                  | Host NPCs pick a guest's puppet as a player: 1.0/2.0 instead of 0.01; `BlockTargetingPlayer` applies to it                                                    | high (script `@wrapMethod` of `AIActionTarget.RegularThreatCalculation`, `UpdateThreatsValue`, Madness/Boss: raise `threatValue` when `threat` is a tandem puppet; reads only, job-thread safe) | low                                                                         | host + mock guest, two hostiles nearer the puppet than V: log each NPC's top hostile threat before/after the wrap                                           |
| 2   | The player-controlled flag itself, `+0x1fc` bit 4 (§13.121), set on guest puppets                         | All 11 script readers at once (security areas, breach detection, kill attribution) and the native `NoInstigatorIfPlayerControlled` filter                     | medium (plugin byte write behind a layout check: V's bit set, NPCs' clear)                                                                                                                      | medium-high: natives may expect `playerSocket` (`+0x1f0`) set with the flag | read-only first: `tools/re` the readers of `+0x1fc`; then one puppet, `game.sh launches 6`, a combat and a security-area scenario, `game.sh bt` on a freeze |
| 3   | Script side of the replication helpers (§13.120: natives dead, scripts alive)                             | Animations of NPCs: the call sites of `ApplyFeatureToReplicate` / `SetInput…ToReplicate` / `PushEventToReplicate` are the exact animation state a proxy needs | high (already used for `PushEventToReplicate`)                                                                                                                                                  | low                                                                         | count call sites per class; wrap the globals on the host, log name/value per NPC for 60 s of a fight                                                        |
| 4   | `RegisterRemotePlayerMappin` / `CPO_RemotePlayerVariant`                                                  | Player markers                                                                                                                                                | done                                                                                                                                                                                            | -                                                                           | -                                                                                                                                                           |
| 5   | Per-owner scriptable systems (`EquipmentSystem`, `PlayerDevelopmentSystem`, §13.123)                      | Vanilla equipment visuals / stats keyed by owner for a guest body                                                                                             | low                                                                                                                                                                                             | high: both persistent (save pollution); needs a `PlayerPuppet`              | none until a PlayerPuppet body is wanted                                                                                                                    |
| 6   | `PlayerPuppet`-class guest body (`IsPlayer()` true)                                                       | Every `IsPlayer()` check (targeting, aggro, devices)                                                                                                          | low: NCO froze it on 2.31 (`puppets.md`); UI systems retarget to the last attached player                                                                                                       | high                                                                        | read-only: list `IsPlayer()` sites that matter; no spawn yet                                                                                                |
| 7   | `Muppet` class (§13.124)                                                                                  | `IsPlayer()` true without `PlayerPuppet`'s systems; device triggers                                                                                           | low: no template known                                                                                                                                                                          | medium                                                                      | list the archives for an `.ent` with `gameMuppet*` components                                                                                               |
| 8   | cpPlayerSystem slot `0x1c0` add player, peer 1 (§13.122)                                                  | A real second player                                                                                                                                          | very low: the SP manager maps every peer to V's `gamePlayer`                                                                                                                                    | very high (control taken from V)                                            | none; stays read-only                                                                                                                                       |
| 9   | `entEntity+0x155` peer id = 1 on puppets                                                                  | `IsControlledByAnyPeer` (2 script users)                                                                                                                      | medium                                                                                                                                                                                          | low                                                                         | not worth it alone                                                                                                                                          |
| 10  | Device `GetServerState`/`GetClientState`, `questMultiplayer*` nodes, `SelectedForMultiplayerChoiceDialog` | Devices, quests, scene choices                                                                                                                                | none: null / unused in mode 1                                                                                                                                                                   | -                                                                           | -                                                                                                                                                           |
| 11  | Runtime mode byte to 2/3 (§13.120)                                                                        | Every MP branch                                                                                                                                               | no (the `IsMultiplayer()` warning stands; also feeds every native gate and device state)                                                                                                        | very high                                                                   | never                                                                                                                                                       |

### Top three, next steps

1. **Threat weights (row 1).** Wrap `AIActionTarget.RegularThreatCalculation`
   (and `MadnessThreatCalculation`, `BossThreatCalculation`) on the
   host: after the wrapped call, if `threat` is a guest's puppet
   (`TandemOwn`/puppet tag), set `threatValue` as for a player-controlled
   threat (1.0 + the same distance/visibility terms); in
   `UpdateThreatsValue`, give it 2.0 and honour `BlockTargetingPlayer`.
   Reads only, so `// job thread safe:`. Scenario: `npc-targets-guest`
   (V far, puppet near, two hostiles), assert the top threat.
2. **The flag (row 2), only if row 1 is not enough.** First a read-only
   pass: `tools/re/xref` the functions that test `+0x1fc & 4` and check
   which also read `+0x1f0`. Only then a plugin setter behind a startup
   check (V has the bit, a sample NPC does not; disable on mismatch).
3. **Animation catalogue (row 3).** Count and classify the
   `…ToReplicate` call sites (`final.reds`), wrap the five globals on the
   host, stream name→value changes per owned NPC; feeds the NPC proxy
   animation work without polling.

### Verdict: the second player object

No. In a single player world the engine's player chain is
`cpPlayerSystem` (a 7-slot per-peer table, used with one entry) →
`gamePlayerManager` (one `gamePlayer`, returned for every peer) → one
controlled object. The multiplayer manager that holds several players is
not instantiated, and only a multiplayer runtime mode would bring it.
`GetPlayer()` stays V. What vanilla AI and UI actually test on an object
is per-object: the class (`IsPlayer()`), the player-controlled flag
(threat weights, security), the peer id, and registered mappins. Those
can be given to a guest's puppet piecemeal (rows 1-2, 4), which buys
most of the targeting prize without a second player.
