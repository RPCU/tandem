# Remote player puppets: spawning and moving

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

## 2. Spawning remote player puppets

**Records** (`code/assets/Tweaks/CyberpunkMP.tweak`)

- `Character.MaMuppet` / `Character.WaMuppet` **inherit from `Character.Panam`** (not `Character.Muppet`
  — editing `Muppet` does nothing; `MAP:2404`). Overrides: `appearanceName = "TPP_Body"`,
  `priority = SpawnableObjectPriority.Regular`, `reactionPreset = ReactionPresets.NoReaction`,
  `uiNameplate = UINameplate.MpPlayerSettings`, `abilities += [CanCrouch, CanSprint]`,
  `entityTemplatePath = mods\cyberpunkmp\player_{ma,wa}_tpp_cutscene.ent` (a copy of the player TPP
  cutscene template), one `genders` entry each.
- Must blank `displayName`, `fullDisplayName`, `alternativeDisplayName`, else every remote is labelled
  "Panam" (nameplate reads the record first) (`tweak:11-18`). Set the shown name at runtime via
  `displayName.unk08` on the object (`App/World/AppearanceSystem.cpp:332-333`). [2.5-verified]
- `tags -= ["Invulnerable","Immortal"]`: NPC god mode comes from record tags
  (`NPCManager.Init → SetNPCImmortalityMode`, npcManager.script:123, god-mode source `'Default'`);
  `Invulnerable` makes every hit DealNoDamage on the shooter's side, so PvP silently does nothing
  (`tweak:84-99`). Runtime backstop: remove god mode source `'Default'` and re-check after 1 s
  (NPCManager init can land after `OnEntityAttached`) (`9079c107`). [2.5-verified]
- Cutscene player rigs ship almost no locomotion clips ("the male glide"); story NPC records (Panam,
  Jackie) carry full locomotion sets. `Character.MaJackie`/`WaPanam` variants exist, unvalidated
  (`tweak:325-361`). The state machine logs `walk_0` clip length at attach; 0 = no walk clip
  (`Game/Animation/PuppetDriver.cpp:36-41`).
- `Character.Player_Puppet_Base` (real player rig): identity worked (real faces/bodies) but player
  records never enter the NPC idle-controller pipeline → frozen statues, clothes as painted fragments;
  `walk_0` length −1 (no NPC locomotion clips in the player graph) (`1845cfd3`, `bb738cad`,
  `0a6f2912`); round 2 with transform writes did not visibly move (`d3587258`,
  `App/Settings.h:126-131`). Reverted to mannequins. [2.31-only]
- The cutscene template is a PLAYER template: it customises itself from the **local** player, so without
  a working remote appearance apply every remote looks like you (`9e640e36`).
- Server-declared NPCs: `NotifyCharacterLoad.puppet_record` optional (absent for players), standing
  only, no AI (`c84e82d9`).
- Male cutscene template lacks most locomotion clips (male glide); female animates (`feb1e504`).

**Spawn call** (`code/assets/redscript/World/NetworkWorldSystem.reds:525-547`)

- `DynamicEntitySpec{recordID, alwaysSpawned=true, position, orientation, persistState=false,
persistSpawn=false, tags=[n"CyberpunkMP.Puppet"]}` → `GameInstance.GetDynamicEntitySystem().CreateEntity`.
  An invalid `TDBID` spawns nothing silently → fall back to the muppet. Lookup:
  `DynamicEntitySystem.GetEntity(id)`; delete via script. [2.5-verified]
- Vehicles use the same spec with tag `CyberpunkMP.Vehicle`, and readiness via Codeware's
  `CallbackSystem.RegisterCallback(n"Entity/Attached", …).AddTarget(DynamicEntityTarget.Tag(...))`
  (`World/VehicleSystem.reds:12-43`). [2.5-verified]
- `CreateEntity` returns an id immediately; the entity arrives later. Register the id in your own
  lock-free registry **before** it finishes assembling (animation-thread hooks look puppets up by id
  hash only) (`NetworkWorldSystem.cpp:145-160`). Spawn requests that arrive before the world is ready
  are queued (`:60-65`). A respawn for a known id replaces (despawn first), else frozen duplicates
  ("ghosts") accumulate (`:67-74`, `:193-201`).
- `id.IsDynamic()` false → spawn failed; garbage `/npc` records (`Character.<record>` pasted literally)
  produced half-spawned ghosts and were suspect #1 in a crash (`MAP:1331-1342`).
- Body gender is sent by the server next to the name; never re-derive it from the ccstate blob (a parse
  hiccup made a female spawn as the male mannequin, `NetworkWorldSystem.cpp:89-94`, `82c0ab9c`).
- ECS gotchas around spawn: a 4-field component constructed with 3 args shifted `usesDriver` into
  `AuthorityEpoch` for every puppet ever spawned (`832f3eb1`); after `emplace`/`add` an entity can
  change archetype, so references to its other components go stale (`162aca8c`); register every
  non-POD component before first use (else memcpy + double free) and keep components
  default-constructible (`98b12fa2`, `cc0629c2`).
- Registry the animation hook reads: fixed array of atomics + high-water mark (a mutex in a hook that
  fires for every animated entity cost frame rate) (`dd9c0744`).
- On world detach: first clear the registry (hook goes silent), destroy the interpolator, drop vehicle
  state, then forward (crash-on-quit, `d2e9ba53`).
- Entity readiness: Codeware's status byte at entity **+0x156** (2.31; CW still declares
  `Status = OffsetPtr<0x156>` in `Red/Entity.hpp:57`) (`01f92ab6`). [2.5: CW uses it]

**Making the puppet a target / hostile** (`MAP:2404`, `tweak:101-138`, `code/assets/redscript/Hackable.reds`)

- `IsQuickHackAble()` needs `objectActions` with ≥1 `PuppetQuickHack`; Panam has none. Working list
  dumped live from `Character.rcr_sixthstreet_hooligan_melee1_knife_wa`: `QuickHack.BaseOverheatHack,
BrainMeltBaseHack, BaseContagionHack, OverloadBaseHack, SystemCollapseHackBase, MadnessHackBase,
SuicideHackBase, GrenadeHackBase, BaseBlindHack, BaseWeaponMalfunctionHack,
BaseLocomotionMalfunctionHack, BaseCyberwareMalfunctionHack, BaseMemoryWipeHack, BasePingHack,
BaseCommsCallInHack, BaseCommsNoiseHack, BaseWhistleHack`. Names in game scripts are wrong
  (`BlindHack`, `MadnessLvl3Hack` don't exist).
- Hostile attitude (`Att_Hostile`) toward the local player, per puppet (no faction change), satisfies
  both targeting gates: weapons use `TSF_EnemyNPC` = All(Obj_Puppet|Att_Hostile|St_Alive) AND
  Not(Obj_Player) (targetingSearchFilter.script:79); quickhacks need `IsQuickHackAble()`
  (scriptedPuppet.script:4045: active, not crowd, `IsAggressive()`, record objectActions incl.
  PuppetQuickHack, attacker has a cyberdeck). `GameplayRestriction.FistFight` did not work. Templates
  already have 16 `gameTargetingComponent`s (`6f431c54`, `4e16bb91`). [2.5-verified] Side-effect: NCPD/NPC AI treat players as threats — and on 2.5 the puppet's
  own Panam combat brain then joins fights and walks the body around (see §3).

**Stopping the puppet's own AI**

- `reactionPreset = NoReaction` alone is not enough on 2.5: the Panam behaviour tree owns movement;
  teleports lose to it; in combat it walks the body away (`InterpolationSystem.cpp:106-116`). [2.5-verified]
- What works: an engine movement controller attached to the move component (the MultiMovementController
  hijack, §3), or an always-active `AIMoveToCommand` with `finishWhenDestinationReached=false`,
  `ignoreInCombat=false`, `removeAfterCombat=false` (the tree yields movement to an active move command,
  tweakAIConditionChecks.script:3417) (`NetworkWorldSystem.reds:786-871`).
- Cancel `AIMoveToCommand` before mounting a puppet into a vehicle (mount is an AI event)
  (`World/VehicleSystem.reds:86-92`).

**What crashed on 2.5 during spawn/dress**

- Dressing remotes through hand-numbered `ITransactionSystem` vtable slots (`GiveItem`/`AddItemToSlot`,
  "0x250 and on") — 0xC0000005 reading 0xFFFFFFFF 16 s after join, every client that saw anyone
  (2026-11-01 23:05/23:08). Fix: call `TransactionSystem` from redscript (compiler-checked)
  (`AppearanceSystem.cpp:395-412`). [broken-on-2.5]
- Engine-attached `MultiMovementController` crashed an observer 2.2 s after attach on 2026-11-01
  (0xC0000005 writing into the exe image, test.74) — later re-enabled as default, see §3.
- 21 C++ exceptions at one address during world attach (2.31, not chased, `MAP:713-715`).

**Threading (the biggest crash class, 2.31 but engine-generic)** (`MAP:1289-1325`)

- Engine callbacks reached from redscript (hit hook, `OnVehicleEnter`, `OnVehicleReady`, mount events)
  are dispatched by the **job system on arbitrary threads**. "Called from redscript" ≠ main thread. Two
  threads inside flecs' unlocked stack allocator = the crash that looked like an entity-readiness bug.
  Fix: no ECS/world access off the main thread; lock-free id registries; marshal to main thread by id
  (never by component pointer — mounts rebuild components, `InterpolationSystem.cpp:1368-1377`).
- Never structurally modify an ECS entity inside an OnAdd observer dispatch (`:994-1019`).

## 3. Moving puppets — every method tried

| Method                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    | Where                                                                                 | Result                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **MultiMovementController (MMC)**: C++ object mimicking the engine's 19-slot idle/movement-controller vtable, attached via `AttachController` (hash **4018412273**, `float(move::Component*, Controller*)`) when the hooked `IdleController::SetAnimation` (hash **2268141838**) first fires for a registered puppet                                                                                                                                                                                                                                                                                      | `Game/Animation/MultiMovementController.{h,cpp}`, `InterpolationSystem.cpp:1296-1431` | 2.31 original. test.73: needed the 2.5 `IComponent::owner` fix first (`7bef6e17`). test.74: observer crashed 2.2 s after attach, AV writing `0x7FF7012DBCE8` (inside the exe image), RIP `exe+0x3E7863` inside function hash **2582323196** (`74de8dfd`, `b88e1040`) → retired. Static re-measure against the 2.5 exe (`e3c5e455`): IdleController vtable exactly 19 slots, same order; `AttachController` reads only `type` at **+0x8** and pushes the controller onto **move component +0xA8**; engine `AnimationDriver` size **0x30** (Detach zeroes +0x00..+0x28); `AnimationData` size **0x3C**, same field order. Crash-free on test.85/86; **default since test.87** (`0662a27a`, `App/Settings.h:140-146`). [2.5-verified; test.74 crash never explained] |
| Transform-only: write `IPlacedComponent::SetTransform` (hash **1828854026**, same native Codeware's `Entity.SetWorldTransform` uses) each frame + `PuppetDriver` feeding anim features                                                                                                                                                                                                                                                                                                                                                                                                                    | `InterpolationSystem.cpp:100-104,498-525`                                             | Used as the 2.5 fallback after test.74: bodies glide with no walk cycle (`74de8dfd`). test.75: **an NPC-record body's own movement component writes the position back every frame** — no visible movement (`fbade8f7`, `NetworkWorldSystem.reds:718-726`). [broken-on-2.5 for NPC records]                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `TeleportationFacility.Teleport` ≤10×/s, only after 2 cm                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  | `NetworkWorldSystem.reds:727-741`                                                     | Did not visibly move the body (test.79, `832f3eb1`); as a snap, every one failed and the gap stuck at 13.5/9.2/22 m for minutes (test.80, `ea776938`). [broken-on-2.5]                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| **AI steering**: `AIMoveToCommand` re-issued ≤4/s (retarget at 0.75 m, gait change, or ≥1/s); lead 0.6 s of velocity (≤4 m); target = lowest Z in last 0.8 s (ground, so jumps don't float the body); gait bands stand/walk/run/sprint at <0.3/<2.5/<5.5 m/s, +1 gait past 1.5 m behind, sprint past 3.5 m; straight line (`ignoreNavigation`) inside 3 m; "unstick" (straight line) if <0.5 m progress in 1.5 s while >2 m off; snap via `AITeleportCommand` (`doNavTest=false`) if >8 m for 1 s or >20 m, ≤1 per 2 s, cancel pending teleports before every move (a dead one froze the body at 12-15 m) | `InterpolationSystem.cpp:106-376`, `NetworkWorldSystem.reds:786-871`                  | Evolution: test.79 first walk cycle on 2.5 (`832f3eb1`); test.80 first 30 s good (0.3–2.5 m, 0 snaps) then lost (`ea776938`) and the body was **invisible** because the hook skipped vanilla idle (`56c559ce`); test.82 pathfinder parks ~2.2–2.5 m short (it treats ~2 m as arrived) and a dead `AITeleportCommand` blocks later moves (`fcb7ce5c`); test.83 jumps made the body float (`5487654f`). Final measure: **3–10 m behind (worst 17 m), 2–38 unsticks / 10 s** on a healthy link (test.86, `0662a27a`). Kept as per-machine fallback `mod-steered-movement.on`. [2.5-verified, poor quality]                                                                                                                                                           |
| Mod-spawned puppet _as the local player_ (`LocalPuppet.reds`, `-mod-local-puppet`)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        | `MAP:1403-1412`                                                                       | Ruled out by design decision, dead code.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |

**Final choice on 2.5** (`0662a27a`): engine-attached MultiMovementController (original 2.31 design),
default since test.87 — exact per-frame position, the engine's own locomotion animation, and it keeps the
Panam AI off the body (when it was removed on 2.5 the AI walked bodies around on its own, e.g. after the
local player fought police). Steering is the fallback. Remaining lag after test.85 was the
interpolation buffer, not the controller (`c193e801`).

**Measurement method worth copying**: a 10 s per-puppet summary line (commands, snaps, landed snaps,
unsticks, body vs target position, worst gap, engine view via `MpDescribePuppet` = attached / active /
appearance / AI LOD / high-level state) (`InterpolationSystem.cpp:334-375`, `56c559ce`).

**Upstream 2.31 traps**: `moveComponent->speed` at the hand offset read ~3e8 → use measured
distance/time (`88d58885`); read position from `worldTransform`, not `localTransform` (not updated
while walking) (`9732c461`).

**MultiMovementController facts** (layout is an **engine ABI** — no base class may be added, it would
reorder the vtable, `MultiMovementController.h:51-54`)

- Fields after vptr: `u32 type` (+8; `kMulti = 1<<10` marks ours), `bool attached`, `bool highPriority`,
  `move::Component*` (+0x10), `EntityID`, then own data. Allocated with the engine's
  `Memory::AI_MovementAllocator` / `PoolAI_Movement` (`Game/Entity.h:12-22`).
- Vtable: GetMemoryPool, dtor, PreTick, Tick(delta), **GetDeltaTransform(Vector4& posDelta,
  Quaternion& rotDelta)** (the engine moves the body by the returned delta = target − current placed
  position), sub_28(bool&), sub_30, sub_38, **SendAnimationParameters(delta, pos, orient)**, sub_48,
  sub_50, GetName (CName "Multiplayer Controller"), sub_60 (return true), sub_68, sub_70, Mount, Attach,
  Detach, GetAnimationParameters — 19 slots.
- `GetDeltaTransform` runs on the **animation thread**; null-check `component->owner->placedComponent`
  (mounting reparents the entity; unchecked derefs killed a driver ~25 s after a remote mounted). A
  frozen target while seated produced a 250 m "delta runaway" impulse (`MultiMovementController.cpp:43-95`).
- The hook: on the animation thread, match puppets by **entity id hash only** (reading `tags` raced the
  main thread's construction and crashed); skip driver puppets; skip during a 4 s vehicle-exit grace
  (attaching into the post-unmount component rebuild crashed an observer); attach on the main thread,
  re-resolving the entity and move component from the id (`InterpolationSystem.cpp:1308-1426`;
  UAF `1845cfd3`, exit grace `5a606f11`). On 2.5 the hook must still call the vanilla idle for puppets
  nothing else animates, or the body is invisible/unposed (`56c559ce`).
- **2.5 offset trap**: the 2.31 `Game::EntityPtr` = component+0x90 is past the end of `IComponent` on
  2.5; use the SDK's `IComponent::owner` at **0x50** (`InterpolationSystem.cpp:1310-1313`). With the
  wrong owner nothing matched and remotes stood still. [2.5-verified]
- `move::Component` 2.5 (vendored SDK `moveComponent.hpp:29-56`): size 0x2C0; `MovementRepresentation`
  (0x30, activeEntry 0x28) at **0x138**; worldTransform 0x1C0, position 0x1E8, speed 0x220,
  deltaFrame 0x22C. 2.31 stub had controller* at 0xA8 and a SpinLock at 0x128 (`Game/Movement.h`). [2.31-only for the stub]
- Old `ExPuppet` decode: `Teleport` virtual after 34 `sub_*` slots from 0x108, `m_moveComponent` at
  0x420 (`Game/Puppet.h`) — **retired**; SDK notes its moveComponent offset "was already wrong on 2.31"
  (`vendor/RED4ext.SDK/NCO-PROVENANCE.md:77-78`). [broken-on-2.5]

**Other movement facts**

- Local player placement: `TeleportationFacility.Teleport(player, pos, EulerAngles(deg))` (radians on
  the wire ×57.2957795); setting position directly drops people through the world. Even the facility
  lands before collision is ready on a fresh spawn → the game's out-of-bounds recovery parks you in a
  holding volume ("the box") in a loop; arm a 1.5 s watchdog and re-place up to 3× (`MAP:688-700`,
  `NetworkWorldSystem.reds:873-899`). Teleporting while mounted: tell the network you left, unmount
  (instant `UnmountingRequest` + `AIEvent "ExitVehicle"`), wait 0.5 s, retry ≤4, last resort
  `WorkspotSystem.UnmountFromVehicle(vehicle, player, true)` (`:616-715`).
- Don't build `EulerAngles`/native structs with constructor args in redscript (UB warning); set fields.
- While driving, the client sends the **vehicle's** transform as the player's position
  (`NetworkWorldSystem.reds:625-630`).
- A mounting event where the player is the **parent** (carrying a body, grappling) is not a vehicle
  entry — check `childId == player` and `parentId` resolves to `VehicleObject` (the bug sent no position
  until unmount: 237/349 "Couldn't find vehicle: 1", send rate fell to 12–17/s, observers dry on up to
  81/1038 frames) (`World/VehicleSystem.reds:169-198`, `0cfdaa0b`). [2.5-verified]
- Mounted passengers send no movement (parented to the car) → the server must measure them from the
  vehicle for range checks (`6336bd5d`). Clients ignore movement for attached entities.
- Teleport while mounted fires `OnVehicleExit` twice (0.115 s and 0.123 s); suppress the second
  (`e53b7b86`).
- 4 s vehicle-exit grace froze the target while the player moved → 250 m pop; fix was a 250 ms ease
  out of the frozen position (`MAP:1512-1515`, `InterpolationSystem.cpp:469-496`).

### 13.22 Every way a V moves: the signals, and NPC bodies in water (2026-11-18, scripts and exe read)

Read from the 2.50 scripts (`final.reds` line numbers) and the exe on
disk; none of it run in game yet. Used by `core/include/tandem/movement.hpp`
and `scripts/tandem/Movement.reds`.

- **`LocomotionDetailed` is the placement signal.** Every locomotion state
  writes it on entry through `LocomotionTransition.SetDetailedState`
  (281168: Stand 280901, Fall 285568, Crouch 289647, Climb 529545, Ladder
  530440, Slide 553244, Vault 556464, Jump 557425, DoubleJump 557518,
  Knockdown 558466); values are `gamePSMDetailedLocomotionStates`
  (6855, 0..31: fits 5 bits). `Fall` (282014–282037) and `Landing`
  (558037–558115) are written too. [2.5-untested]
- **Swimming** runs a separate high-level state (`gamePSMHighLevel.Swimming
= 6`), with `Swimming` = `gamePSMSwimming` (6965: Surface 1, Diving 2,
  Climbing 3 = getting out); vanilla treats `Swimming <= 1` as at the
  surface (21410). `LocomotionDetailed` is then stale: the swim value wins.
  [2.5-untested]
- **Elevators**: the elevator device sets `IsPlayerInsideElevator` and
  `IsPlayerInsideMovingElevator` on the player's PSM blackboard (198537,
  198545). `IsInWorkspot` is an Int set to 1/0 on workspot enter/exit
  (549017, 549035); `Felled` a Bool set from the felled status (44692).
  [2.5-untested]
- **`IsOnGround` has no script writer** (only listeners, 313710): if the
  engine fills it is unknown, so it is not sent; the PSM scripts ask
  `StateGameScriptInterface.IsOnGround()` instead (247352, with
  `IsOnMovingPlatform()`, only inside a state). [2.5-untested]
- **NPC bodies in water.** No NPC swimming exists in the scripts: an NPC
  in water is a ragdoll or a corpse. `NPCPuppet.KillIfUnderwater` (128196)
  force-kills an NPC whose head slot is below
  `AIGeneralSettings.underwaterDepthKillThreshold` under the water level
  (`AIScriptUtils.GetWaterLevel`, 158907); it runs from the AI ragdoll
  behaviour (`AIRagdollDelegate.DoCheckWaterLevel`, 471005). Puppets are
  spared it (wrapped in Movement.reds). With the pose stream on, a puppet
  gets V's own swim and dive pose (every body bone and the hips' height),
  so placement is the whole job; without it, no swim animation exists for
  the body (the controller plays idle). Whether the engine lets an
  NPC-record body sit below the water surface (buoyancy, a character
  controller) is what the `movement-course` scenario will show. [2.5-untested]
- **What the engine does with `GetDeltaTransform`** is not traced yet. The
  engine's idle controller (vtable `0x1430333b0`, 19 slots; slot 18 =
  `0x14053c970`, hash 2268141838, the `SetAnimation` NCO hooks) has slots
  2 and 3 (PreTick, Tick) as a bare `ret` (`0x1401ff2b0`); its slot 4 is
  `0x141956030` (hash 1662787369); slot 5 (`Unknown05(bool&)`) only writes
  false (`0x14020b000`), as ours does; slot 11 returns its CName
  (`0x14053c91c`). The move component (`moveComponent`,
  vtable `0x143033168`, constructor `0x14022f2fc`) keeps the controller
  stack at +0xA8 with the default controller inline at +0xB0
  (`AttachController` 0x14022ed40 compares types at +0x8, pushes with
  `0x14022edc4`). If the delta goes through a character controller
  (gravity, collisions, step height), a puppet in the air or on a ladder
  would be held back: the controller then teleports the body
  (`TeleportationFacility.Teleport`, 192802) and logs `teleport #N`.
