# Cyberpunk 2077 co-op — knowledge harvested from Night City Online (NCO) and the campaign fork

Facts only, for a clean-room rebuild on game patch **2.50** (RED4ext C++ + redscript, Proton). No code
was copied; each fact names where it came from.

**Sources and abbreviations** (read on 2026-11-17; nothing below is copied code)

- `NCO` = Night City Online, https://github.com/ofmiceandcam98-eng/CyberpunkMP at `92474fc9e470`,
  a CyberpunkMP fork ported to patch 2.5. Paths written `cpmp/nco/...` or bare `code/...` are in
  that repository. Its own name for the game build: "2.5". Its history before 2026-11-01 ran on **2.31**.
- `CAMP` = the story co-op campaign fork (2.31 core + campaign plugin),
  https://github.com/ziomek123pl4-lang/CyberpunkMP at `f3b4df24435a` (`cpmp/campaign/...`).
- `CW` = Codeware (MIT), https://github.com/psiberx/cp2077-codeware at `60c4ab59035f`
  (2026-11-14) (`cpmp/codeware/src/...`) — the reference for what is known-good on 2.5.
- `addr` = the game's own `bin/x64/cyberpunk2077_addresses.json` from patch 2.50 (linker map
  2026-10-19). "in 2.50" means the hash exists in that list; most are unnamed there.
- `MAP` = NCO's `docs/MAP.md`.
- Commit hashes (8 hex) are NCO commits; their bodies are summarised in `nco-commits.md`; the
  campaign fork in `campaign-fork.md` (both next to this file).
- **Era**: game patch 2.5 shipped 2026-10-22 (`5e749fa6`); NCO's 2.5 port began 2026-11-01 (SDK
  vendored that day), so only commits from 2026-11-01 to 2026-11-10 (test.68…test.91) carry 2.5
  measurements. Everything in `MAP` dated August/September 2026 is 2.31 knowledge.

**Tags**: [2.5-verified] = NCO notes say it ran on 2.5, or CW uses it today; [2.31-only] = decoded/used
on 2.31 and not re-verified (or known different) on 2.5; [broken-on-2.5] = recorded as crashing/failing
on 2.5; [unverified] = built, never proven live; [2.5-untested] = read from the 2.50 game's own
scripts, not yet run in game.

**The 2.31→2.5 rule NCO learned the hard way** (`code/client/Game/CharacterCustomizationSystem.h:111-119`):
"Every raw offset, hand-numbered vtable slot and engine-struct mimic in the client was decoded on 2.31
and carried into the 2.5 SDK merge unverified - and on 2.5 they have overwritten a character, crashed
every client dressing a remote player, and crashed every client moving one." Address-library _hashes_
survived the patch (every hash both repos use is present in the 2.50 list); hand offsets and vtable
slots did not.

---

## 1. Networking stack and protocol

**Transport**

- Valve **GameNetworkingSockets** (open-source, not Steamworks), `GameNetworkingSockets_Init`,
  `SendMessageToConnection`; connection-oriented, per-connection encrypted
  (`code/common/Network/SteamInterface.cpp:14`, `Client.cpp:247`, `MAP:366-369`). [2.5-verified]
- Every packet body is **snappy**-compressed (`Client.cpp:235,414`, `Server.cpp:151,291`).
- Channels: reliable (`k_nSteamNetworkingSend_Reliable`/`ReliableNoNagle`) vs unreliable
  (`Unreliable`/`UnreliableNoDelay`); a message is unreliable iff its `.proto` has
  `bool unreliable = 1000;` as a tag (`code/protocol/client.proto:139`, `server.proto:351`).
  **Exactly four unreliable messages**: `MoveEntityRequest`/`NotifyEntityMove`,
  `VoiceFrameRequest`/`NotifyVoiceFrame`; everything that mutates persistent state is reliable+ordered
  (`MAP:374-381`). A CI check guards that list.
- Handshake: 17-byte transport handshake + protocol identifier check **before auth**; protocol id =
  FNV1a64 of each .proto's message surface (netpack generator), `kRefused` close on mismatch
  (`MAP:2392-2393`). Gotcha: `common.proto` is not hashed.
- netpack gotchas: emits C++ in **file order** (a message must be declared before use,
  `server.proto:13-19`); optional-field presence bitfield width = number of optional fields, so
  adding an optional field is a wire break (`MAP:1097-1107`); **enums are not range-validated** —
  `static_cast<Enum>(9999)` round-trips (`docs/NEW-SERVER-AUTHORITY-HANDOFF.md:96-102`).
- Server: C++ (flecs v4.0.3 ECS) native core hosted by a .NET 8 loader (C# plugins, CppSharp-generated
  bindings, web API) (`code/server/native`, `code/server/loader`). Server `TickRate{60}`,
  `UpdateRate{30}` (`code/server/native/Config.h:221,234`); `UpdateRate` is sent to clients in
  `server::Settings.update_rate` and is the client's send rate (`NetworkWorldSystem.cpp:4336`
  `.interval(1.f/update_rate)`). UpdateRate went 10→30 Hz on 2.31 (`124ce33b`: at 10 Hz players
  "teleported"). Drivers send 2 movement messages per tick (vehicle + character) (`606c8742`).
- Server gotchas: SIGTERM unhandled → .NET destroyed GameServer while the tick loop ran (`219eee3c`);
  flecs REST addon on by default (`06c82f02`); SIGSEGV belongs to the .NET runtime (managed null refs),
  only hook SIGABRT (`a289f888`); Discord HTTPS verify on the game thread froze every join →
  worker thread + task queue (`2cdbde70`).
- Ingress validation: drop non-finite/absurd positions (NaN from `eulerAngles()` of a degenerate
  quaternion disables every distance check and persists); real ccstate blobs are 6–9 KB, cap
  spawn payloads accordingly; chat 512 chars (`3615fadd`).
- World state: server-owned clock + weather in `NotifyWorldState` on join, change and every 30 s;
  clients apply via TimeSystem + Codeware weather setter; weather as CName hash of the
  `worldWeatherState` name, 0 = natural; forced weather is sticky — call release (`forceRestore`) once
  when going back to 0 (`653d2819`, `e58d3b9a`). [2.31-verified]
- Client flow on 2.5: the **main menu stands on a live world with a player object**, so "player
  exists" ≠ "in world" (`c4085596`). The interpolator (an ECS system created in `OnConnected`) was
  destroyed by the menu world detaching → restart it in `OnWorldAttached` when connected; connect
  handlers must be re-entrant (`5487d380`, test.74/75). Blackboard listeners fire only on change —
  push current state after attach (`9b154816`). [2.5-verified]
- Base-class dtor calling a virtual `OnDisconnected` → `_purecall` abort on every exit (`175a7f6a`).

**Movement messages** (`client.proto:138-162`, `server.proto:350-372`)

- Client→server `MoveEntityRequest{id, position(vec3), rotation(yaw f32), speed, full_rotation(vec3),
tick(u64), epoch(u32), locomotion(u32), upper_body(u32)}`.
- Server→client `NotifyEntityMove{id, position, rotation, tick, speed, full_rotation, locomotion,
upper_body}` + server-derived `{world_revision, cell_x, cell_y, sequence, authority_epoch,
correction}`.
- `locomotion`/`upper_body` = sender's `gamePSMLocomotionStates` (Default 0, Crouch 1, Sprint 2, Jump 5,
  Slide 10, CrouchSprint 12) / `gamePSMUpperBodyStates` (Aim 6), read from the **PlayerStateMachine
  blackboard** (local-instanced, keyed by player entity → `BlackboardSystem.GetLocalInstanced`, field
  ids via RTTI on `PlayerStateMachineDef.Locomotion/UpperBody`) (`NetworkWorldSystem.cpp:3492-3529`).
  Reason: at 30 Hz a sprinter measures 5-6 m/s and lands in the jog band; trust the sender's PSM for the
  band, measured speed for blend (`Game/Animation/States/Base.h:30-49`). [2.5-verified]
- Snapshot model: **full absolute state every packet** (no delta compression), per entity. Vehicle
  rows add full rotation + authority epoch.

**Clock / tick**

- `tick` = wall-clock **milliseconds since epoch** (u64), synchronized to the server
  (`SynchronizedClock::Synchronize(serverTick, ping)` adds ping/2) (`common/Network/SynchronizedClock.cpp`,
  `MAP:306-321`).
- **Trap: never put the absolute tick in a float.** At ~1.787e12 a float's step is ~131 s, so
  `float(tick) - 100` == `float(tick)`: the interpolation buffer had zero delay, drained every frame,
  remote players froze (`App/World/InterpolationSystem.cpp:602-628`). Keep absolute ticks integer;
  only differences become float.
- CAMP (2.31 core) still has the float bug: `const float tick = GetTick() - aSimulationDelay;`
  (`campaign/code/client/App/World/InterpolationSystem.cpp:72`) — do not take CAMP's interpolator as
  reference.
- `tick == 0` means "never moved" (server leaves it uninitialised until the first move): it is ~57
  years in the past and anchors the buffer forever → drop it (`InterpolationSystem.cpp:1119-1138`).
- A client whose wall clock steps back >30 s is re-baselined server-side (`MAP:315-321`).

**Interpolation (client)** — `App/World/InterpolationSystem.cpp`

- Base delay = `15 + 1.5 × (1000/update_rate)` ms = **65 ms at 30 Hz** (`:1035-1041`, `c193e801`).
  History: `50 + 1500/rate` = 100 ms session-wide, one 170 ms-ping player starved everyone's view of him
  (`2ccb7085`); with per-remote margin up to 200 ms the total was 150–350 ms = 1–2.5 m at sprint;
  test.85 "smoother but the lag is still there" → base 65, margin cap 100 (`c193e801`). [2.5-verified]
- Jitter = RFC 3550 smoothed transit variation (frame-to-frame change of `arrival − sender tick`, so the
  clock offset cancels) (`2ccb7085`).
- Per-remote widening: `jitterExtra = clamp(2 × ArrivalJitter, 0, 100 ms)`;
  `needed = AvgTransit + AvgInterval + jitterExtra`; `delay = min(max(base+jitter, needed),
max(400, base+jitter))` — cap 400 ms (`:636-654`, `0d1c86b0`). Jitter/transit/interval are EWMA with
  α = 1/16; arrival gaps ≥500 ms ignored (`:1245-1283`). test.91: transit 20–30 ms + gap 33 ms for
  close players (unchanged); a distant player (server thins to 7.5/s past 200 m) starved 236 frames in
  10 s before `needed` was added; a ~100 ms link would extrapolate every frame on a 65 ms base.
  test.86: 74 ms behind, 0 dry frames on a healthy link (`0662a27a`). [2.5-verified]
- Interpolate between the last sample ≤ renderTick and the first > renderTick (true interpolation, not
  "chase"); the old chase-lerp produced an acceleration+jump per segment (`:683-696`).
- Starvation: extrapolate along `(-sin yaw, cos yaw)` × speed for **≤250 ms players / ≤500 ms
  vehicles**, then hold; on recovery blend back over **300 ms players / 150 ms vehicles**, and cap the
  per-frame correction to `max(0.25 m, 3 × speed × dt)` (`:755-884`).
- Drop: out-of-order (`sequence <= last`, or tick older than buffer tail/anchor); vehicle samples with
  older `authority_epoch` (`:1210-1230`).
- 10-second stats line (avg delay, jitter, transit, gap, dry frames) per remote — the instrument that
  tuned all of the above (`:659-679`).
- Netlab (Python, `tools/netlab/`, `MAP:1520-1545`): on a 170 ms "rimtek" profile the shipped vehicle
  algorithm starved 75% of frames with 162 teleport-pops; **dead reckoning + projective blend
  (`vehicle_dr`) = 0 pops, 0.03% starvation**; adaptive delay ≈ 30 ms effective on LAN vs 80. Vehicles
  need 2 periods of look-ahead (they never extrapolate in the baseline), players 1. [unverified live]

**Server relevance / rates** (`code/server/native/Components/MovementComponent.cpp:27-36,74`, `MAP:263-298`)

- Full rate ≤200 m, every 4th update ≤600 m, every 16th beyond (decimated by _replicated_ sequence).
- **The cell grid culls nothing**: `kCellSize = 6000` while all observed positions span x −1760..673,
  y −1957..−1261 (2 cells); load radius 3 → 7×7 cells. Night City ≈ 5 km across. Effectively broadcast;
  bandwidth ∝ players² (`MAP:1202-1230`). Cell must be computed with `floor` on both sides (Night City
  coordinates are mostly negative; truncation put x = −1769 in cell 0 vs −1; "nobody could see anybody"
  for six days, `MAP:1131-1152`).
- Movement replication per received packet walked every player (O(packets × players)); the fix is
  coalescing: store newest state, replicate once per server tick (1000 pps flood: 31,000 → ~930
  relevance checks). Built behind `CoalesceMovement`, **default off, never 2-client tested** (`MAP:282-298`).
  Keep the received sequence separate from the replicated sequence.
- Do not reject movement by rate (rubber-banding); reject out-of-order by tick (`MAP:277-281,300-305`).
- Voice: Opus 1.5.2, 48 kHz, ~24 kbps, 20 ms frames ≈ 50/s ≈ 200 B, inbound cap 100 frames/s, 1 KB
  max frame; server relays undecoded; radius whisper 6 m / local 25 m / yell 60 m; 120 ms pre-roll;
  capture thread encodes, game thread sends (the transport isn't safe from arbitrary threads)
  (`xmake.lua:36-49`, `MAP:327-335`, `4358b8af`, `666eac9e`). WASAPI: fall back to eConsole when
  eCommunications is unset (`574eab9c`); Chromium device ids `default`/`communications` are sentinels,
  not endpoint ids (`0ca11ba2`). Chat: 20 lines / 5 s sliding window (`MAP:415-427`).
- Fire-rate check: a fixed floor on arrival time refused automatic fire (shots arrive batched, 0 ms
  apart) → GCRA leaky bucket, sustained 1500 RPM, burst ~12 (`214c7e9a`).
- Stale generated serializers linked under fresh headers (7 vs 6 optional fields) → 1 extra presence
  bit doubled every following varint (id 215 → 430) while the protocol id still matched: always
  regenerate (`d8e79f72`).

**Entity ids**: one flat u64 server-id namespace for players AND vehicles; the client's flecs entity id
_is_ the server id (`flecs::entity(world, serverId)`, `NetworkWorldSystem.cpp:240-243`). Movement can
arrive before the spawn (vehicles did, routinely) — log once, don't treat as a frozen player
(`InterpolationSystem.cpp:1081-1117`).

**Late join**: observers only reach players present at the time; everything a late joiner needs
(characters, then vehicles, then seat occupancy) must be replayed explicitly on join (`MAP:2395`).

---

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

---

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

---

## 4. Animation

- **AnimationDriver** = an engine struct NCO calls through three hashes: `Attach(Entity*)`
  **2624656810**, `Detach()` **2385973466**, `SendParameters(AnimationData&)` **2132808949**; plus
  `AnimationControllerComponent::GetAnimLength(CName)` **3750304414**
  (`Game/Animation/AnimationDriver.cpp`). Struct members: `Handle<anim::AnimFeature_Locomotion>`,
  `Handle<anim::AnimFeature_CrowdLocomotion>`, `WeakHandle<AnimationControllerComponent>`
  (`AnimationDriver.h:20-22`). Engine struct size **0x30** on 2.5 (`e3c5e455`). Hashes present in
  2.50, used on 2.5 via the MultiMovementController path (test.85+). [2.5-verified via the default path]
- `AnimationData` (passed to SendParameters / the idle controller): `{CName controller; bool debug;
Locomotion_Style style; MotionTableAction action; float path, slope, delta; bool stairs; float ground,
time, footScale(=1), startAngle; bool unk; float speed}` (`AnimationData.h`) — field order is the ABI;
  engine size **0x3C** on 2.5 (`e3c5e455`). `speed` is the graph's blend parameter — it was never
  written at first (`0d3a747e`). A missing clip stalls the motion pipeline (frozen puppet) → degrade to
  idle-glide; jog band falls back to `sprint_0` if no `jog_0` (`feb1e504`, `b4bfc201`).
- Enums: `Locomotion_Style {Idle, Rotation, Walk, Jog, Sprint, Any}`; `MotionTableAction`
  (None 0, Start 1, Stop 2, Move 3, TurnInPlace 4 … CrowdDirectionalStartFast 47); `Locomotion_AnimType`
  clip ids (idle_stand 1, walk_0 7, jog_0 10, sprint_0 13 …) (`Game/Animation/Base.h`).
- State machine (Spawning→Idling/Walking/Jogging/Sprinting): Walking sends `action=MTA_Move,
style=LS_Walk, time=fmod(t, len(walk_0)), speed`; if the clip length is 0 it degrades to idle
  (`States/Walking.cpp`). Band thresholds **walk 1.2, jog 4.0, sprint 6.5 m/s** (real speeds: walk
  ~1.8, jog ~5.5, sprint 7.5+; old walk threshold 3.0 made walkers glide) (`States/Base.h:24-29`).
- `AnimationControllerComponent::ApplyFeature(CName, Handle<AnimFeature>)` hash **316482401** declared
  but unused (`MultiMovementController.cpp:11`). [unverified]
- `PuppetDriver` (mod-owned, ticked on main thread, re-binds when the weak component handle expires —
  mounts rebuild components; rate-limited 1/s) is the transform-only path; idle controller then gets no
  vanilla writes for driver puppets on 2.31 (`PuppetDriver.h:10-19`). Off 2.31 driver puppets must still
  run the vanilla idle, or they have no idle pose (test.80: invisible body) (`InterpolationSystem.cpp:1329-1338`).
- AI-steered bodies (fallback) get the engine's own locomotion; a jump cannot be reproduced (AI walk
  can't jump) → steer to ground height.

---

## 5. Appearance / character customization

**Pipeline used for remote players (works on 2.5)** (`App/World/AppearanceSystem.cpp:272-643`)

1. ccstate bytes from the server → `CreateHandle_CharacterCustomizationState` (**2710710832**) →
   `CharacterCustomizationState::Serialize(state, stream)` (**108403442**, a CMPStream reader).
   Reject a blob that doesn't consume exactly its size (short blobs used to deserialize "successfully"
   into garbage) (`:352-367`).
2. Set `PuppetPS.unk72[0] = 1` (checked during item add for &TPP) (`:373-380`). [2.31 field, still used]
3. Clothes: on 2.5 via script `TransactionSystem` (GiveItem + AddItemToSlot); the native path used
   `ItemID_Create` (**3020231866**, seed −1), `GetItemRecord` (**3236045910**), `GetPlacementSlot`
   (**1934369954**), `EvalCName` (**3648983380**), `GetItemAppearanceName` (**3029088864**) then SDK
   virtuals → crashed on 2.5 at `exe+0x887DC4` reading 0xFFFFFFFF right after
   "[Appearance] holstered_default" (`929d924f`). Remote outfits may render with the item's default
   variant (no garment appearance name in the script path). Give and equip must be independent steps
   (muppets arrive with Panam's loadout, so a `!HasItem` guard skipped the equip, `394f941f`).
4. Keys: `GetHeadCustomization("TPP"|"face"|"hairs"|"beards" if male)`, `GetBodyCustomization("TPP_Body",
"breast", "lifted_feet", "flat_feet")`, `GetArmsCustomization("holstered_default","nails")` — the set
   that dresses remotes correctly on 2.5 (`Game/CharacterCustomizationSystem.h:72-104`). "genitals"
   pokes through clothes; "character_customization" does all.
5. `RuntimeSystemEntityAppearanceChanger` + **386815609** (NCO name
   `ScheduleSynchronizedAppearanceChanges`; CW name `AppearanceChangeSystem_ChangeAppearance2`)
   `(changer, WeakHandle<Object>&, Span<Key>* old, Span<Key>* new, std::function<void()> const&, u32
priority)`. **Async: keep the key array alive in the completion callback** (a stack DynArray was a
   use-after-free, more on female states) (`:557-628`). [2.5-verified, CW confirms hash]

- Head/body/arm group names seen: head `TPP, FPP, hairs, character_customization, TPP_proxy, FPP_proxy,
FPP_hairs, TPP_photomode, beards(ma), face, finalSceneBruises`; body `FPP_Body, TPP_Body,
character_creation, genitals, breast, lifted_feet, flat_feet`; arms `holstered_default(_tpp/_fpp),
holstered_strong…, character_customization, personal_link_*, *_launcher, *_mantis, nails`
  (`AppearanceSystem.cpp:414-498`).
- **Don't re-apply an identical ccstate**: re-scheduling an appearance the puppet already wears 15× in
  16 min crashed the observer (exit 0x80000003). Hash the ccstate per entity; same hash → clothes only
  (`MAP:1248-1264`, `AppearanceSystem.cpp:500-527`). Clothing and customization in one message plus a
  holstered weapon toggling the visual item count (4→5→4) caused the re-sends.
- **tandem (2026-11-18, [2.5-verified])**: no ccstate serialization is
  needed: the 15 keys collected from a state (resource path hash + CName
  hash, 16 bytes each) are portable; sent over the network and scheduled on
  another game's puppet they dress it with the sender's face, body and hair.
  Clothes go as the wardrobe's visual item ids, worn as preview items.
- Remote equipment must travel as **numeric TweakDBIDs**; `TDBID.ToStringDEBUG` returns empty strings
  in release builds (no debug name table) (`AppearanceSystem.cpp:81-87`).
- `AddItemToSlot failed` on remotes (~106/evening) — cosmetic, uninvestigated (`MAP:1517-1518`).

**The +0x78 problem / IsGame231Layout** (`Game/CharacterCustomizationSystem.h:111-160`)

- 2.31: `gameuiCharacterCustomizationSystem` (RTTI size **0x388**) holds the _creator's_ state handle
  at **+0x78**, null outside the creator — so "is it null?" told you "am I in the creator".
- 2.5: class is **0x3D8**; +0x78 is **never empty** in play and holds the template/default body. The
  90-second autosave serialized it and overwrote the player's stored face every 90 s (2026-11-01;
  6283-byte male → 6571-byte default woman, test.70, `463bc4d1`).
- CAMP's guest character restore also writes through `ccSystem+0x78`: `InitializeState()` → native
  `ImportCustomizationState(base64)` (Serialize **108403442** into the +0x78 state) → `SetLifePath` →
  `SetIsExpansionStandalone(true)` → `FinalizeState()` → `StartNewGame(state)`
  (`campaign/code/assets/redscript/Plugins/CampaignLobby.reds:995-1027`) — inside the creator flow, where
  +0x78 was valid on 2.31. [2.31-only; re-check on 2.5]
- `IsGame231Layout()` = `GetClass("gameuiCharacterCustomizationSystem")->size == 0x388`; used as the
  only available build fingerprint to gate every 2.31 decode. On 2.5 the +0x78 read is disabled
  (creator/ripperdoc appearance saves OFF). [2.31-only decode; fingerprint itself works on 2.5]
- Safe on 2.5: dispatch customization methods against the **interface**
  `gameuiICharacterCustomizationSystem` (24 functions: InitializeState, InitializeOptionsFromFinalizedState,
  FinalizeState, ReFinalizeState, CancelFinalizedStateUpdate, ClearState, GetState, Get{Head,Body,Arms,
  United}Options, RandomizeOptions, ApplyEditTag, ApplyUIPreset, ApplyChangeToOption, … ; redscript sees
  only `HasCharacterCustomizationComponent` + `GetState`; the concrete class has 0 functions)
  (`MAP:1391-1394`, `8558b7b1`, `78713d7a`); `GetState()` works in ordinary gameplay
  (equipmentSystem.script:4992) (`MAP:1395-1397`). **On 2.5 read the live state through `GetState`**
  (`17599f98`). [2.5-verified]
- **Capture timing on 2.5** (`ce05d986`): mid-creator autosave = 23-byte unpopulated state; the save at
  `OnBeforeWorldDetach` as the New Game world tears down = the real creator look (10263 bytes, female) —
  **the only moment that works**; after the MP world loads = template body. [2.5-verified]
- **Re-saving a restored look corrupts it** (`286c0312`): the restore writes the stored look _into_ a
  state still holding the template body; whatever the look doesn't mention survives: 6283 → 6571 bytes
  with `female_base`, `full_breast_big`, female genital options added while the body flag stays male.
  Restored characters must save possessions only; a clean write needs an emptied/fresh state.
  [2.5-verified]
- Body type must be chosen before world load: two template saves `MultiplayerStart` (female) and
  `MultiplayerStartMale`; a male look on the female template changes nothing on screen (`c05b8ba6`).
  Server refuses a save that flips `is_male` on an established character (`3903a8a3`).
- Blob sizes seen: 5435, 6283, 6518, 6586, 7138, 8588, 9141, 9206, 10263 bytes; a 23-byte half-built
  blob exists — require ≥1 KB on both ends (`8558b7b1`).
- Since 2.0 wardrobe outfits override slots and `GetPaperDollEquipAreas` doesn't report the outfit —
  read the Outfit slot explicitly (`32e6a486`). `GetItemList` = carried items only; equipped weapons
  and cyberware via `GetPaperDollEquipAreas` + `GetVisualItemInSlot`; unequip before RemoveItem
  (`642663dd`).
- **RED4ext SDK trap**: `ExecuteFunction` with a null out-slot returns false **without calling** a
  function that has a return type; `InitializeState`, `ReFinalizeState`, `FinalizeState` return Bool so
  never ran; void `InitializeOptionsFromFinalizedState` "worked" but does not rebuild the body
  (`MAP:1590-1599`, `Scripting/Utils-inl.hpp`).
- Own body: still open on 2.5 — "a player sees themself as the template body while everyone else sees
  them right". Candidate ("Route A"): stored look into a FRESH state, same key collection, scheduled on
  the local player (`MAP:1590-1599`). [unverified]
- SDK carries `CharacterCustomizationState` 0xF0→**0x110** and `Item_Record` 0x478→**0x488** on 2.5 as
  opaque growth, insertion point unknown (`NCO-PROVENANCE.md:73-75`). [2.31-only internals]
- Never hand an appearance blob to the engine while on the main menu: `GetState` returns nothing; keep
  the bytes until a live state exists (`MAP:1114-1126`).
- Morph targets: `SetTargetWeightWithNormalize` **633871992** (CW: `MorphTargetManager_ApplyMorphTarget`),
  `ResetWeights` **1054086242**, weight array at component+0x98 — declared, unused. [unverified]

**Session/character flow lessons**

- Load one fixed template save always and let the server own identity; picking "newest save" chose a
  random character by file timestamp (`MAP:1547-1576`). Template inventory/balance arrives ~88 s after
  load (save data arriving late), strip it once by detection, not timer (`MAP:1414-1429`).
- Lock saving with the engine's `SaveLocksManager` (`AddSaveLock(reason)`; covers F5/menu/autosave;
  saveLocksManager.script:29) — not by hiding menu entries (`code/assets/redscript/SaveLock.reds`).
- Save the character at the top of `OnBeforeWorldDetach` (last moment the body is the server's); a
  `Live` lifecycle state gates all saves (`MAP:1482-1493`, `MAP:1087-1096`).

---

## 6. Vehicles

**Sync model**: the occupant with authority (driver) simulates locally and sends the vehicle transform
as movement; everyone else runs the car **kinematic** and drives it with `ForceMoveTo` every frame.
Authority = parentage on the server + `authority_epoch` bumped on transfer (revoke before assign; late
packets from the previous simulator dropped) (`MAP:1166-1171`, `docs/NETCODE-AUTHORITY.md`).

**Calls** (`App/World/VehicleSystem.cpp:30-73`, `InterpolationSystem.cpp:95-98,891-943`)

| Hash / slot                                                                                                                               | What                                                                                                                                          | Status                                                                  |
| ----------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| **4039776020** `SetIsPlayerControlled(BaseObject*, bool)` (called from `vehicle::actions::DriveAction::OnStart`)                          | remote: false; local again: true                                                                                                              | in 2.50; used on 2.5 (vehicle rows in 2.5 logs) [2.5-verified, lightly] |
| vtable **+0x328** on `WheeledBaseObject` `(bool)`                                                                                         | "turn on engine"                                                                                                                              | hand slot from 2.31 [unverified on 2.5]                                 |
| vtable **+0x280** on `vehicle::BaseObject` `(WorldTransform const&)`                                                                      | `ResetWithNewTransform` — CAMP only, declared, never called (`campaign/code/client/App/World/InterpolationSystem.cpp:60-63`); absent from NCO | [2.31-only, unused]                                                     |
| `engineData->unk61 = 0`                                                                                                                   | after the engine call                                                                                                                         | engineData at **0xBD0** on 2.5 (jackhumbert `155d285a`) [unverified]    |
| **1620777158** `SetFlags(BaseObject*, u32)` with 0x10 then 0x80                                                                           | remote-driven flags                                                                                                                           | [unverified on 2.5]                                                     |
| **1585713002** `SetKinematic(BaseObject*, bool)`                                                                                          | true for every network-spawned car, false when we get authority                                                                               | [2.5-verified, lightly]                                                 |
| vtable **+0x1F0** on `vehicle::IMoveSystem` `(EntityID const&, bool)`                                                                     | `SetSimpleMovement(id, true)` before each ForceMoveTo                                                                                         | hand slot [unverified on 2.5]                                           |
| **2592348558** `EnablePhysics(BaseObject*, u32 flag, bool)` with flag 0x100 when `(physicsState & 0x100)==0`; `physicsState` at **0x260** | modelled on `DriveToPoint::OnUpdate_Internal` (**3713079867**)                                                                                | [unverified on 2.5]                                                     |
| **2933986803** `ForceMoveTo(BaseObject*, WorldTransform const&, float frameDeltaMs)`                                                      | per-frame remote drive                                                                                                                        | [unverified on 2.5]                                                     |
| **457775029** `ForceTransform(BaseObject*, WorldTransform const&, bool)`                                                                  | commented out (ForceMoveTo used instead)                                                                                                      | —                                                                       |
| **1160782872** `MountingFacility::IsMountedToObject`, **3120376212** `GetMountingInfo`                                                    | declared, unused                                                                                                                              | —                                                                       |

- 2.5 struct placements (vendored SDK, from jackhumbert `155d285a` 2026-10-25): `BaseObject` size
  0xBC0, isOnGround 0x25C, physicsState 0x260, acceleration 0x264, handbrake 0x26C, isReversing 0x2AB,
  burnout 0x2C4, `rigidBody` **0x2D8**, chassisType 0x340, `worldTransform` **0x350**, archetype 0x3B0,
  `runtimeData` **0x5F0**, isVehicleOnStateLocked 0x6F2, `engineData` **0xBD0** (WheeledBaseObject);
  `RigidBody::currentTransform` 0x150 (`NCO-PROVENANCE.md:71`, `vehicleBaseObject.hpp:204-254`).
  CW: vehicle `RecordID` at **0x658** (`CW Red/Vehicle.hpp:5`). [2.5-verified by CW for RecordID; rest from SDK asserts]
- No NCO commit says the vehicle-sync natives were exercised end-to-end on 2.5; `MAP` vehicle sections
  date from 2.31 (2026-08), and the commit bodies never name slots 0x328/0x1F0/0x280 (they exist only
  in source). Treat all vtable slots above as unverified.
- **2.5 made the game's `VehicleSystem` a NATIVE class** (`IVehicleSystem`, natives
  `SpawnActivePlayerVehicle`, `SpawnPlayerVehicle`, `ToggleSummonMode`…); a mod class registering RTTI
  name "VehicleSystem" → 9 validator errors, game closes. Never reuse a game class name (`84df9a48`).
  [2.5-verified]
- 2.5 P0 two-player crash ~90 s in: AV with RIP at a heap address (corrupt function pointer) after ~960
  `[OrphanVehicle] samples 0` lines — suspected the player's own summoned car replicated back as a second
  remote copy (`c92f26b1`). [unverified, open]
- Car radio + vanilla summon are gated by quest fact `unlock_car_hud_dpad` (→ blackboard
  `UIGameData.Popup_Radio_Enabled`, vehicleSystem.script:70); absent from template saves (`9ec6a206`).
- Downed while seated: instant unmount, then force the workspot out after 0.5 s, ≤4×, then teleport
  (`62e48492`). [unverified]
- 2.31 crash sequence (fixed by the threading fix, kept as a signature): quit in a car, rejoin → server
  replays the orphan car → `OnVehicleReady` → `MakeRemoteDriven` (SetIsPlayerControlled, engine vcall,
  engineData, SetKinematic) → dead 1.6 s later; a 9-player sweep: 20× 0xC0000005 + 8× 0x80000003, all
  2–10 s after a queued remote-vehicle mount (`a0553f70`, `a2709440`).
- Garage (2.31 script): `EnablePlayerVehicle(name: String, enable, despawnIfDisabling)`,
  `GetPlayerUnlockedVehicles(out array<PlayerVehicle{name, recordID, isUnlocked}>)`; takes a String,
  and TweakDBID→string is impossible in release → store names (`64951cc9`, `1a4174e1`).

**Mounting / seats**

- Remote puppet into seat: queue `MountAIEvent{name="Mount", data=MountEventData{slotName,
mountParentEntityId, isInstant=true, ignoreHLS=true}}` on the character (`World/VehicleSystem.reds:72-94`).
  Exit: `AIEvent "ExitVehicle"` (plays animation; "messes up orientation, sometimes crashes" note).
  `VehicleComponent.MountEntityToSlot` and `MountingFacility.Mount` variants were tried/commented.
- Local driver promotion: `MountingFacility.Unmount(current)` then mount driver seat after **0.35 s**
  delay (same-frame mount races the unmount) (`NetworkWorldSystem.reds:571-605`, `804e64c4`); driver
  slot name from `VehicleComponent.GetDriverSlotName()` (`seat_front_left`). Driver-exit handoff order:
  front right → back left → back right (`eaeaa792`). A car whose driver leaves is parked, not destroyed
  (`bec22350`).
- Seat ids on the wire = `CName` hash of the slot name (FNV1a64; `seat_front_left` =
  `0xb000b1d029d0cea0`) (`MAP:906-919`). Validate seat **identity**, not count.
- Boarding someone else's car: the engine's mount flow wakes the local copy's physics → re-assert
  kinematic on enter (else "he drives, I'm stationary") (`VehicleSystem.cpp:199-208`).
- A network copy spawning on top of the local world's parked instance of the same car → depenetration
  impulse "cars explode when someone gets in": make every network car kinematic before its first frame
  (`VehicleSystem.cpp:22-29`).
- Seat swap = exit+enter within 1 ms; process order bug emptied the car and destroyed it under the
  driver → exits must name vehicle+seat (`NEW-SERVER-NETCODE-PORTING-HANDOFF.md:236-239`).
- Entering a car must join the existing entity (remember your own car's server id), or a fresh entity is
  spawned per entry (7 copies stacked once) (`MAP:1179-1181`).
- Disconnect while driving then rejoin reproduced THE crash 1.6 s after `MakeRemoteDriven` (flecs race,
  fixed); clearing vehicles at zero players server-side (`MAP:1312-1318`).
- Remote vehicle-mount crash: **unresolved**, dominates crash reports (`NEW-SERVER-NETCODE-PORTING-HANDOFF.md:242-243`).

**Vehicle damage** (2.31 sources, `MAP:785-819`)

- Vehicle health = stat pool `Health` on the vehicle's EntityID; read `GetStatPoolValue(id, Health,
false)`, set `RequestSettingStatPoolValue`, observe `RequestRegisteringListener(id, Health, l)` →
  `OnStatPoolValueChanged`. `VehicleObject.DamagePipelineFinalized` fires **before** the pool settles.
  Street car ≈ **1050 HP**; one shotgun blast = **14 hits within 1 ms** (~8 each) → batch per shot.
  Destruction `gameVehicleDestructionEvent`, `m_damageLevel` 0-3. Whether a kinematic remote car runs
  the damage pipeline: unknown. [2.31-only]

---

## 7. Quests / facts / scenes (campaign fork + NCO)

Full detail: `cpmp/notes-campaign.md`. CAMP was built on **2.31** (RED4ext 1.30.0, Codeware 1.20.5,
ArchiveXL 1.27.1, TweakXL 1.11.4, redscript 0.5.31, Input Loader 0.2.3; `tools/campaign/BuildInstaller.ps1:21-31`)
and is **untested in game** — `docs/MISSIONS.md` test-notes table is empty and `docs/CAMPAIGN.md:249-252`
leaves open whether a scene node run outside its phase plays the scene. Paths below relative to `campaign/`.

**Architecture** (`docs/CAMPAIGN.md:34-60`): host-authoritative; first player in the lobby = host; the
host's game runs every quest; guests start a **quest-free world** ("a running quest cannot be stopped,
only prevented from starting", `:43-49`) and mirror journal + facts (script), "what you see/hear" quest
nodes (native QuestMirror), tracked objective, scene tier/position, NPC health/death; dialog choices are
voted, the winner applied by synthetic key presses. Guests start New Game with
`SetIsExpansionStandalone(true)` so the game loads `ep1\quest\ep1_standalone.gamedef`, replaced by a
quest-free gamedef (`zz_CampaignCoop_GuestStart.archive`, from NCO `9e1832f`)
(`code/assets/redscript/Plugins/CampaignLobby.reds:1021,1165`). [unverified on 2.5]

- "In game" detection: Codeware `Session/Ready` with `GameSessionEvent.IsPreGame() == false`; the main
  menu is a world with a player ~8 s after launch so `OnWorldAttached`/`OnPlayerAttach` fire there too
  (`CampaignLobby.reds:106-121`, `9a1e671`). Codeware 2.5 still has these. [2.5-verified API]
- Lobby state lives in a Codeware **ScriptableService** — ScriptableSystems don't exist at the main menu
  (`CampaignLobby.reds:97-99`).

**Running quest nodes on a guest (QuestMirror)** (`code/client/App/World/QuestMirror.cpp`, `7f88bec`)

| Hash           | Use                                                                                   | Signature                                                                                                                        | Tag                                                                                     |
| -------------- | ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| **3227858325** | `QuestPhaseInstance::ExecuteNode` — hooked on host (record), called on guest (replay) | `u8 (PhaseInstance*, questNodeDefinition*, QuestContext&, const QuestNodeSocket& in, DynArray<QuestNodeSocket>& out)` (`:67-72`) | [2.5-verified] = CW `QuestPhaseInstance_ExecuteNode`, `CW Red/QuestsSystem.hpp:286-292` |
| **3144298192** | `QuestsSystem::CreateContext`                                                         | `void* (QuestsSystem*, QuestContext*, i32, i8, i32, i32)`, called `(sys,&ctx,1,0,-1,-1)` (`:63-65,348`)                          | [2.5-verified] same call in `CW App/Quest/QuestPhaseExecutor.hpp:31`                    |

- Layouts (identical to CW, [2.5-verified]): `QuestsSystem+0x60` SharedSpinLock (hold while executing),
  `+0x68` `Handle<questPhaseInstance>` root phase, (CW: `+0x78` NodePathHash map, `+0xF8` `FactManager*`);
  `questPhaseInstance+0x30` `Handle<questQuestPhaseResource>`, `+0x40` `Handle<questGraphDefinition>`
  (CW: +0x70 NodePath, +0x80 NodePathHash, +0x88 children, +0x98 handlers; `ExecutePath` vfunc +0x120);
  `QuestContext` size **0x340**, `phaseStack` (DynArray<questPhaseInstance*>) @0xF8, `nodeID` u16 @0x108,
  `QuestPhaseContext` @0x110 (size 0x230: vtbl, game@08, prefabLoader@30); `QuestNodeSocket{CName name;
u8 unk08}` (`QuestMirror.cpp:21-61`).
- Replay recipe (= CW `QuestPhaseExecutor::ExecuteNode`): lock `sys+0x60`, zero a QuestContext,
  CreateContext, push root phase on `phaseStack`, ExecuteNode(root, node, ctx, socket, out), clear stack.
  CAMP passes the recorded input socket name (CW passes empty); a thread_local flag stops re-recording
  (`:335-359`).
- Node address recorded by the host: `<resource path hash>/<phase node id>.<…>/<node id>/<input socket
CName hash>`; resource from the phase, else nearest parent in `ctx.phaseStack`; phase chain by DFS from
  `resource->graph` through `questPhaseNodeDefinition.phaseGraph` (`:198-282`). Guest:
  `ResourceLoader::LoadAsync<graphGraphResource>(ResourcePath(hash))`, run strictly in order, skip a
  resource not loaded after 30 s (one stuck resource blocked every later scene, `2f3dfae`) (`:396-481`).
- 31 mirrored node classes: questScene*, questPhoneManager, questAudio*, questVoicesetManager,
  questSpawnManager, questDynamicSpawnSystem, questEntityManager, questCharacterManager, questCrowdManager,
  questPopulactionController, questWorldDataManager, questEnvironmentManager, questTimeManager,
  questFXManager, questRenderFxManager, questInteractiveObjectManager, questTeleportPuppet, questMovePuppet,
  questRotateTo, questUseWorkspot, questPuppetAIManager, questForcedBehaviour, questClearForcedBehaviours,
  questBehaviourManager, questSendAICommand, questVehicle, questTeleportVehicle, questMappinManager,
  questRewardManager (`:76-108`). NOT mirrored: facts (questSetVar), journal, control flow, conditions,
  phases, checkpoints. Scene and phone nodes are re-woken by the engine while they run → dedup 5 min per
  address, others 2 s (`:110-116`). Late joiners get no old nodes (`CampaignSession.cs:517-530`).

**Fact sync** (script only, `code/assets/redscript/Plugins/Campaign.reds`; server `CampaignSession.cs`)

- Host: one `QuestsSystem.RegisterListener(StringToName(name), watcher, n"OnFactChanged")` per fact, one
  watcher object per fact (the callback gets only the value) (`Campaign.reds:274-285,485-491`); read
  `GetFactStr`, guest writes `SetFactStr` only if different (`:495,580-589`). [unverified on 2.5]
- Watched list: 16,164 names (11,083 base + 5,081 PL) = every fact a `.quest/.questphase/.scene` graph
  **writes** via `questSetVar_NodeType` (`factName`, `factName1`, `factName2`), minus debug/test/demo
  facts; facts only _read_ by graphs are set by game code per player and deliberately excluded
  (`tools/campaign/extract_facts.py:20-40`). Regenerate from 2.5 archives (WolvenKit CLI `unbundle -r
"\.(quest|questphase|scene)$"` + `convert serialize`).
- Snapshot protocol (`docs/CAMPAIGN.md:62-75`): role first; host registers listeners, sends non-zero facts,
  journal tree, `SnapshotDone`; **only then** guests get journal, then every watched fact including unset
  ones as 0 (otherwise a guest further along keeps stale facts), then tracked objective; then deltas.
  Nothing goes to guests before the snapshot is complete. Batches = one `"k:v,k:v"` string ≤16,000 chars
  (RPC buffer 64 KB; the C# stub generator misnames arrays of primitives).
- Concern noted by the author: thousands of script listeners at once; native alternative =
  CW `FactManager*` at `QuestsSystem+0xF8`, vtable `+0x10 GetFact(store,id)`, `+0x18 SetFact(store,id,v)`,
  store 1 = named facts, 2..10 graph stores, `FactID = FNV1a32(name)` (`CW Red/QuestsSystem.hpp:114-192,223`).
  [2.5-verified layout per CW]

**Journal + tracked objective** (`Campaign.reds:411-578`, `1079f52`)

- Host: `JournalManager.RegisterScriptCallback(this, n"OnJournalEntryStateChanged",
gameJournalListenerType.State)` → `(entryHash: Uint32, className, notifyOption, changeType)`; snapshot via
  `GetQuests` (filter active/succeeded/failed) + recursive `GetChildren`. Guest: `GetEntry(hash)`, skip
  unknown/same, `ChangeEntryStateByHash(hash, state, JournalNotifyOption.Notify)`. Tracked: host polls
  `GetTrackedEntry()` every 0.25 s; guest `TrackEntry` only once the entry exists and is Active.
- CW 2.5 journal vfuncs: `GetEntries 0x210`, `GetEntryByHash 0x220`, `ChangeEntryState 0x268`,
  `GetEntryState 0x2B0`, `GetEntryTimestamp 0x2C8`; hashes `JournalManager_LoadJournal 1964512489`,
  `JournalManager_TrackQuest 3274382593` (`CW Red/JournalManager.hpp`). [2.5-verified per CW]

**Scenes** (`CampaignDialog.reds`, `CampaignExtras.reds`, `SceneSpots.cpp`)

- Wrap `PlayerPuppet.OnSceneTierChange(newState)`; host reports tier+position; guest ≥`Tier3_LimitedGameplay`
  and >25 m away → teleport next to host; ≥`Tier4_FPPCinematic` while the guest's own scene didn't start →
  `GameplayRestriction.NoMovement` until it ends (`CampaignDialog.reds:476-595`). Holds ref-counted per
  reason (`d652278`). Block saving while held with a ScriptableSystem `IsSavingLocked()` override;
  remove leftover god modes by source name on load (`2f3dfae`).
- Remote puppets within 1.5 m of V during a scene are pushed 1.2 m left/right; held puppets skip
  interpolation.
- Extras: V in a car → `MountAIEvent` puppets into free seats (`seat_front_right, seat_back_left,
seat_back_right, seat_front_left`) and pause vehicle sync (each game has its own scene car); V sitting
  → nearest free sit spot ≤8 m via `AIUseWorkspotCommand{workspotNode = NodeRef(globalNodeID)}` after
  teleporting the puppet there; standing → stand spot ≤5 m or a 1.6 m half circle facing the current
  speaker (wrap `BaseSubtitlesGameController.SpawnDialogLine`). Whether MaMuppet/WaMuppet accept
  `AIUseWorkspotCommand` is unchecked.
- `SceneSpots`: raw `worldStreamingSector + 0x40` DataBuffer of `worldCompiledNodeInstanceSetupInfo`,
  stride 0x90 (Transform@00, scale@20, node*@50, globalNodeID@58, proxyNodeID@60, nodeIndex@78), filter
  `worldAISpotNode`/`AIActionSpot` + workspot path whitelist (142 sit + 194 stand `generic` workspots).
  **[unverified/risky on 2.5]**: CW now reads node setups through buffer-version-dependent accessors
  (`CW App/World/NodeSetupWrapper.hpp:24-170`); use CW's script API
  `worldStreamingSector.GetNodeSetupCount/GetNodeSetup` instead. Sector source: CW `Resource/Ready`
  callback filtered to `worldStreamingSector`.
- Host modes: braindance (`BraindanceSystem.GetIsInBraindance()`) → guests held + Invulnerable; Johnny
  (`PlayerPuppet.IsJohnnyReplacer()`) → message only; regroup if host jumps >150 m between 0.5 s polls
  (fast travel) and isn't mounted.

**Dialog choice injection (ChoiceInput)** (`code/client/App/World/ChoiceInput.cpp`, technique from
VedalAI/neuro-cyberpunk via alphanin9/SharedPunk, MIT)

- Hook `Player::ProcessInput` **3535541945** `void (cpPlayer*, RawInputBuffer&)` (`:66-70`) — hash in
  2.50, not used by CW, signature [2.5-present, unverified]. Local cpPlayer via `gamePlayerManager`
  vtable **+0x200** (`:72-82`) [unverified].
- CAMP's `RawInputData` = **0x40** bytes {`EInputKey key@00`, `EInputAction action@04`, `float value@08`,
  `u32 mouseX@0C`, `u32 mouseY@10`, `unk14`, `u32 unk18@18` (must be 1), `u64 unk20..unk38`}; **CW on 2.5
  has the same fields plus `u64 unk40` → size 0x48** (`CW Red/Input.hpp:17-32`) → CAMP's stride is
  **[broken-on-2.5]**. `RawInputBuffer{vtbl; DynArray<RawInputData> inputs}`; vtbl +0x08 = GetInputs;
  CAMP's synthetic buffer fills +0x10..+0x88 with no-ops and +0x90 returning 0; allocator
  `Memory::InputInternalAllocator`. Alternative on 2.5: CW `InkSystem_ProcessInputEvents` **673454961**
  `void (InkSystem*, u64, RawInputBuffer&)` (`CW Red/InkSystem.hpp:42-44`). [2.5-verified]
- Mechanics: while voting strip confirm keys `IK_F`, `IK_Enter`, `IK_Pad_X_SQUARE`, `IK_LeftMouse` and count
  them as the host's vote; apply the winner as N× `IK_Up`/`IK_Down` then `IK_F`, one key per update,
  **120 ms apart**, each a Press (value 1, unk18 1) + Release buffer. Breaks with rebound keys.
- Script side: blackboard `UIInteractions.DialogChoiceHubs` (`{choiceHubs[{id, choices[ListChoiceData],
timeProvider}]}`), `ActiveChoiceHubID` (<0 = none), `SelectedIndex`; only the active hub counts;
  global index = choices of earlier hubs + SelectedIndex; `~` = unavailable
  (`gameinteractionsChoiceType.Inactive`/`CheckFailed`). Vote 15 s, **5 s for timed dialogs** (must end
  before the game's timer), only with ≥2 players in world. Counts drawn by wrapping
  `dialogWidgetGameController.UpdateDialogsData` and re-setting the variant with force=true. Guests with a
  different option count get a stand-in hub (id 77000) written to `UIInteractions`.

**Server (C#)** (`code/server/scripting/CampaignSystem/`): state = journal `Dictionary<uint,int>`, facts
`Dictionary<string,int>`, tracked, watched facts, hostId, snapshotReady, dialog vote, scene tier/pos
(`CampaignSession.cs:141-166`); host-only for journal/facts/tracked/nodes/scene/NPCs; 27 client→server
RPCs, ~25 server→client (`Campaign.reds:16-214`); timer ticks and join/leave are called from native
code **without a catch** — an exception kills the server (`:183-196`, `9a910b7`). RPC ids = FNV1a64 of
the script class name / function name (redscript mangled name for client funcs, e.g. `SetRole;Bool`).
Lifepath vote on `CharacterCreationBackstorySelectionMenu`, forced via
`m_characterCustomizationState.SetLifePath(TDBID)` + `SpawnEvent(n"OnAccept")`.

**NCO world-start facts** (`MAP:2418`)

- A game start is a `.gamedef` (root quests + spawn tag + world); 824 ship. NCO overrides
  `ep1\quest\ep1_standalone.gamedef` with `cyberpunk2077_ep1_standalone.quest` (base city, prologue
  skipped) + `ep1.quest` (non-standalone EP1 root: sets `ep1_installed=1`, enters `ep1.questphase` at
  `Base`) + `ep1_preorder.quest`, spawn tag `#q000_spwn_start` (a holding room; the server teleports
  arrivals out). Result: empty quest log, no Songbird, level 1. [2.31-verified; archive needs re-check on 2.5]
- A spawn tag is owned by its quest; keeping `#q301_spwn_ep1standalone_opener` without its quest made
  players fall through the map.
- `MainMenu → SpawnEvent(n"OnNewGame")` → preGameScenarios.script:308 routes to the EP1 branch whenever
  Phantom Liberty is installed.
- Dogtown gate = fact **`ep1_side_content >= 1`** (pause condition in
  `ep1\openworld\combat_zone_gate\combat_zone_gate.questphase`), not q301.
- `ep1_standalone` is read by 69 quest phases (metro, apartments, Dogtown community) — never touch it.
  Setting 8 `q301_*`/`holo_songbird_*` facts did NOT stop the Songbird holocall
  (`docs/songbird-holocall-hang.md:63-92`) — facts like `q301_failed` are written by the graph but never
  read. What does work (`08f1ed3b`): `songbird_holocall.questphase` gates every call variant on
  `holo_<call>_activate > 0 AND holo_setup_active < 1 AND holo_setup_started < 1`, so holding
  **`holo_setup_active = 1`** blocks Songbird and ~20 PL holocalls (Reed, Myers, Alex, Kurtz, courier);
  set it at connect, before world facts. The holocall on a new character produced a 120 s main-thread
  watchdog stall (`Watchdog timeout!`, engineWatchdog.cpp:198) with all mod threads idle — never
  root-caused; delivered by `HoloAudioCallLogicController` (scene `q301_00_holocall_hook`), refusing that
  controller's init froze the game outright (`902bfacf`, `2a11cb6b`). 2.5 mitigation
  `MpSilenceStoryHolocalls` (`3d7e8a23`).
- Fact `ep1_side_content`: game writes it =1 in 13 places, never 0; CDPR's `ep1_chicken_unlocks.questphase`
  opens Dogtown the same way (`06af8b5b`).
- The quest engine keeps running underneath; blocking the phone does not stop quest scenes; full PL
  quest stack is loaded (`ffe4c9c1`). Quest-scoped interiors (Konpeki, All Foods, Clouds, Arasaka
  Industrial Park) only stream during their quest; doors are gated on facts (`2ef73dcb`).
- 2.31 scripts expose only `LoadLastCheckpoint` (no LoadSave/LoadSaveInSlot/GetSaveSystem); the MP path
  `singleplayerMenu.script:1012` = `m_isModded ? LoadModdedSave(0) : LoadLastCheckpoint(false)` (= newest
  save) (`65cc869f`, `2b60289a`). Journal `ChangeEntryState` is import-only (`1610ed5e`). **On 2.50** `ChangeEntryState` and `ChangeEntryStateByHash` are
  ordinary script natives that vanilla code calls, and `LoadSavedGame(saveId)` / `ManualSave(saveName)`
  exist; the `SetScriptedQuest*` journal functions are stubs returning false (`coop-quests-world.md`).
  [2.5-static]
- Phone (2.31): `questPhoneCallPhase` (phoneSystem.script:9); answer = `PhoneInteract` BUTTON_RELEASED,
  decline = `PhoneReject` BUTTON_HOLD_COMPLETE, both require `callPhase == IncomingCall`
  (newHudPhoneGameController.script:512); `ui_phone_incoming_call` loops — call StopRingtone on every exit
  (`b6242c85`, `d58ad961`); `PhoneCallInformation{callPhase, contactName: CName, isAudioCall,
isRejectable, isPlayerCalling, visuals}` written to the `UI_ComDevice` blackboard presents a call
  (`1d5aec2a`).
- Phone: block calls at `PhoneSystem.OnTriggerCall` (blocking at `SetCallInfo` traps the player in an
  invisible call); phone phase enum `{Undefined, IncomingCall, StartCall, EndCall}`;
  `questTriggerCallRequest.isPlayerTriggered` means "the player triggered a quest call", not a
  multiplayer call (`MAP:927-944`, `NEW-SERVER-NETCODE-PORTING-HANDOFF.md:295-298`).
- Server world facts (`worldfacts.json`) applied client-side at join; facts open areas without faking
  quest completion.

---

## 8. Combat / NPC / damage

- **Detect, don't simulate**: wrap `ScriptedPuppet.OnHit(gameHitEvent)`, always chain
  `wrappedMethod`; report only when the target is one of our puppets **and** the instigator is the local
  player (else every observer reports the same shot) (`code/assets/redscript/Combat.reds:29-168`).
  Damage = `evt.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health)` (armour/resists
  already applied), type `GetDominatingDamageType()`, hit zone = `hitRepresentationResult.hitShapes[0]
.hitShapeName` (CName; vocabulary only readable from live hits — the `gameHitShapeBVH` is not in the
  .ent), weapon `attackData.GetWeapon().GetWeaponRecord().GetID()` (GetWeaponRecord is on WeaponObject,
  not AttackData). Compile-checked on 2.31. [2.31; script API, likely fine on 2.5]
- The hit hook runs on job threads → no ECS/world queries in it (§2 threading).
- Server owns health, magazine, RAM; client sends what the engine computed; server bounds it.
  **Quickhack damage must be 0 in the server rule table** — it already flows through the hit pipeline
  (double-count bug v0.3.104). Quickhack cost (`GetCost()`) includes perks and a random modifier
  (`MAP:2403`).
- Quickhack effects: wrap `OnStatusEffectApplied(ApplyStatusEffectEvent)`, report when
  `instigatorEntityID == player`; victim applies via `StatusEffectHelper.ApplyStatusEffect(player,
tdbid)`; upload telegraph via `OnUploadProgressStateChanged` filtered to
  `EProgressBarContext.QuickHack`/`UPLOAD`, replaying `AIQuickHackStatusEffect.BeingHacked`
  (`Combat.reds:180-340`). Quickhack id from `ScriptableDeviceAction.GetObjectActionID()`
  (`MAP:759-773`). Base vs leveled record ids on the wire: unproven.
- **No flatline**: `GodModeSystem.AddGodMode(player, gameGodModeType.Immortal, n"<source>")` — engine-level,
  damage still lands, dead state never entered; god mode is ref-counted per source CName; remove on
  disconnect. Script-level `OnDeath` wraps and stat-pool floors were bypassed by scripted kills,
  falls, drowning (`code/assets/redscript/Death.reds:52-80`). Revive grace = separate source with
  `Invulnerable` (no TTL; revoke by delayed callback) (`Death.reds:100-133`).
- Downed = `StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.Defeated")`, deliberately
  without `BlockAllMenu` (`Combat.reds:660-701`).
- Inbound effects drained by a 0.25 s `DelayCallback` chain armed per body (network thread → native
  queue → script poll) (`Combat.reds:229-309`). `DelayCallback(cb, t, false)` = real time, ticks while
  paused (`MAP:719-728`).
- **Time dilation must be blocked** (weapon wheel, phone, Sandevistan, Kerenzikov): wrap
  `TimeDilationHelper.SetTimeDilation/SetTimeDilationOnPlayer`; `IsMultiplayer()` gates 17 other files
  (damage etc.) so don't flip it. Suspected in a passenger + weapon-wheel crash
  (`code/assets/redscript/TimeDilation.reds`).
- Difficulty pinned to Very Hard for everyone, read by name not index (`MAP:2402`).
- Server combat (`4e16bb91`, `48d9d109`): any living player to any living target within 250 m, damage
  clamped to 100 per hit; apply server health by SET, not subtract, only on the target's machine
  (`4d8c279d`); LifeState 0 alive / 1 downed / 2 dead / 3 reviving; bleedout is a `DownedAt` deadline;
  stabilise 8 s, revive 12 s, revived at 15 %. Ammo: client polls `GetActiveWeapon` +
  `GetMagazineAmmoCount` (Uint32) — there is no script "weapon fired" event; reload ≥500 ms
  (`88d0a7f3`). Quickhack cooldown: `ObjectAction_Record.Cooldown()` → `Cooldown_Record{Duration(),
Modifiable()}` (`506ac5f1`).
- Clear wanted level on revive: `PreventionBlinkingStatusRequest` with telemetry string
  `'ResetOnPlayerChoice'` → Heat_0, police despawned (`f76877ea`).
- Scanner name: hook `CompileScannerChunks` (GetDisplayName is a native import, can't wrap)
  (`392b28f2`).
- Inbound-effect poll must be armed per body on player attach, not in OnConnected (menu connect means no
  body yet) (`92230cf6`). [2.5-verified]
- Death loop: immortality blocks death not damage → naked level-1 respawned into a fight gets farmed in
  20-30 s loops; add an Invulnerable window post-revive (`MAP:1379-1386`).
- No world-object sync at all (doors, containers, elevators) (`MAP:391-393`).

**CAMP's PvE model** (`CampaignCombat.reds`, `CampaignCombat.cs`, `3fc8462`, `7c6570f`; 2.31, untested)

- Every game has its own copy of each enemy (spawn nodes mirrored) with its own AI; only health and death
  sync. Host every 0.5 s (full list every 3 s, NPCs within 150 m of any player): `key;static;record;x;y;z;
health‰;state;hostile` — key `EntityID.ToHash`, `EntityID.IsStatic`, record `TDBID.ToNumber`, health
  `GetStatPoolValue(id, Health, perc=true)×10`, state alive / `IsDead()` / `IsDefeated`. Registry from
  wrapped `NPCPuppet.OnGameAttached/OnDetach` (skip crowd and our puppets).
- Guest binding: **static EntityIDs are identical in every game** (placed/community NPCs); else nearest
  unbound same-record alive NPC ≤12 m; hostile unmatched ≤80 m for ≥5 s → stand-in spawned, removed after
  15 s unreported. Health goes down immediately, up only 3 s after the guest's own hit; copies >30 m off
  are teleported. Host death → `Kill(localV)`, defeat → `SoftKill`.
- Guest hits: wrap `NPCPuppet.OnHit`, instigator == local V, damage as ‰ of max health (levels differ),
  batched every 0.5 s → host `RequestChangingStatPoolValue(-damage)` or `Kill`.
- Players: with ≥2 in world `GodMode Immortal`; ≤1 % health → downed (Invulnerable + NoWeapons/ForceCrouch/
  NoJump/NoMovement); others see `BaseStatusEffect.FollowerDefeatedStory` on the puppet; revive = stand
  ≤2.5 m for 4 s → 35 %; 45 s bleed-out.
- Money is an inventory item (`MarketSystem.Money()` ItemID; vendors move it client-side,
  vendor.script:1180); 17 vanilla paths change it; inventory is `(TweakDBID, qty)` so a restore loses
  mods/tier/attachments (`NEW-SERVER-AUTHORITY-HANDOFF.md:21-102`). Reconcile by difference, never add
  totals; protect `RightArm`, `LeftArm`, `BaseFists` slots.

---

## 9. HUD / UI / archives / tweaks / saves / Steam

- **inkhud override staleness** (`MAP:2407`): adding a HUD entry means replacing the whole
  `base\gameplay\gui\prototype_hud.inkhud` in your archive; every game patch makes it stale. NCO's 2024
  copy on 2.5 lacked `police`, `reaper_mq062`, `cyberware_mq062` and aimed the car HUD at a slot 2.5
  renamed. Procedure: unbundle from `basegame_1_engine.archive` (WolvenKit CLI), `convert serialize`,
  insert entry (`multiplayer_ui` after `new_phone`), `convert deserialize`, repack. 2.5's file has 72
  entries (73 with ours); the speedometer slot was renamed `BottomLeftSpeedometer`; 8 other entries
  changed contexts/margins (`1308b165`). [broken-on-2.5 if stale; rebuilt file unverified in play]
- `AsyncSpawnFromExternal` of a CLI-authored `.inkwidget` hard-crashed the client seconds after spawn;
  spawning library widgets from a game-authored file works (`6f6ed6b9`, 2.31). An `inkImage` binds an
  `.inkatlas`, never a raw xbm; ship extra content as a separate `zz_*.archive` (`1a4d06a8`).
- HUD widgets have no UI context: `navigate_up`/`proceed` never fire there; custom input actions
  appended to `UIBase` in the input_loader XML do fire in gameplay (`9fb1bfe1`). A modal on a HUD widget
  (`PushGameContext(ModalPopup)` + cursor visibility + `radialMenu` dilation) blocked typing but gave no
  cursor/freeze — a real cursor needs a menu controller (`6e1c52a6`). Never make a full-screen canvas
  interactive (soft-lock, `d6240d9d`).
- Ink coordinates: menus author at 1920×1080 (menu root is a fixed 3840×2160 virtual space on 1080p and
  1440p); `GetScreenSpacePosition` returns real display pixels (clicks were 1.333× off at 1440p);
  `GetWindowSpacePosition` is dead (`a90ef259`, `3082f066`). `ToString(inkActionName)` prints the type,
  not the value (`d22e0fe9`).
- `@addMethod/@addField/@wrapMethod` attach only to pre-compiled game classes (`d64ffe2f`); adding a field
  to a _native_ class = "constant pool error"; no `continue`; no Int64→Int32 cast; class params need
  `ref<>`/`wref<>`; a native struct event field needs a matching `public native let` (`8a50a9c1`).
- `FTLog` output reaches no collectable file and `LogChannel` needs CET → route script logs through a
  native function into your own log (`534ca258`, `4a41f34c`).
- Steam: a Steam copy launched while Steam is closed boots Steam and relaunches itself **bare**,
  dropping custom command-line args (`9788e383`). The exe's ProductVersion reads "2.5" (RED4ext says
  "2.50"); FileVersion is 3.0.80.x (`af2a3661`, `d3a04459`).
- CET was treated as incompatible: CET's ImGui overlay plus the mod's DX12 ImGui overlay on the same
  present path hard-locked the GPU (`46f61a5b`, `d014c945`). ImGui 1.92 stopped filling
  `ImDrawData::CmdListsCount` (use `CmdLists.Size`) (`75c5f080`).
- Pause: `PauseMenuBackgroundGameController.OnInitialize` queues SetMenuModeEvent then calls native
  `GetSystemRequestsHandler().PauseGame()` (can't wrap); unpause after a 0.25 s delay (`197d02ca`).
- Difficulty lives in UserSettings `/gameplay/difficulty` var `GameDifficulty`, list
  `["Story","Easy","Hard","VeryHard"]` (`858d723d`).
- TweakDBID = CRC32(name) | (length << 32) (not FNV); verify by hashing and searching
  `r6/cache/tweakdb.bin` (`31590288`). Lifepath enum {Corporate 0, Nomad 1, StreetKid 2}; a fresh save
  reads `Invalid` = 4294967295 / −1 (`9e37b8ff`).
- HUD canvas stays at authored `(0,0)` until a `to_vehicle`/`from_vehicle` animation moves it — play
  `from_vehicle` at connect or the custom HUD draws top-left (`MAP:1495-1510`).
- A HUD widget spawned on the menu root swallows all input (`MAP:189`).
- `inkWidget` parentWidget 0x168, layerProxy 0x148 (2.31 decode, `NetworkWorldSystem.cpp:769`). [2.31-only]
- WolvenKit JSON traps: entry names at `hudEntryName.'$value'`; library tree at
  `item.package.Data.File.RootChunk.rootWidget`; `HandleRefId` indirection (`MAP:2406`).
- redscript: ONE compilation unit — a single bad file (BOM, hex literal `0xFF`, Int32/Uint32 mismatch,
  missing `native func` for an RTTI method) boots the game with **no scripts**; game script dialect ≠
  redscript (`ref<T>` params, `script_ref<String>`); don't run `scc.exe` directly (pops a blocking dialog)
  (`MAP:2402,2415`, `CONTRIBUTING.md:96-123`). `import X.*` can shadow game classes.
- TweakXL skips an unresolvable record and logs it (doesn't kill the file) (`tweak:336-337`).
- Input contexts: custom `playerStateMachineInputContext.ui*Context` records with `alias
["allUIContexts"]`, `forceInputContext` names (`tweak:243-323`); Input Loader required.
- Plugin must not be symlinked (resolves its own path to find assets; failure in `Load` unloads it and
  all scripts silently vanish); first diagnostic `red4ext/logs/red4ext-*.log` (`MAP:446-464`). Install
  folder `red4ext/plugins/zzz…` to load after Codeware/ArchiveXL/TweakXL (`CONTRIBUTING.md:80-81`).
- Script validator hook: `ScriptValidator::Validate` **898639042** (CW same name) used to register RPC
  classes; 2.5 script-bundle layout from CW `b054150f`: `ScriptDefinition` 0x18→**0x40**, `ScriptClass`
  +0x20 before `functions` (parent 0x48, properties 0x50, functions 0x90, flags 0xD1), `ScriptFunction`
  0xF8 (params 0x58, flags 0xC0), `ScriptBundle` leading pointer (classes 0xD8, types 0xE8)
  (`Game/ScriptValidation.h`). [2.5-verified]
- Rendering hooks: `CRenderNode_Present_DoInternal` **2468877568** (addr: `GpuApi::Present`),
  `CRenderGlobal_Resize` **239671909** (addr: `GpuApi::ResizeBackbuffer`), `CRenderGlobal_Shutdown`
  **3192982283**; get swapchain/queue from `RED4ext::GpuApi::GetDeviceData()` + `swapChains.GetData(id)`
  (1-based id, check IsValidID/IsUsedID) — the 2.31 hand-located render context (displacement at
  present+0x1F; devices 0xC97F38, queue 0x13BC4D0) moved on 2.5 (swapchains 0xCEBF30, queue 0x14104D0)
  and crashed 1.5 s after load with nothing failing at compile time (`Game/Rendering.h`, `60a8ba47`).
  [2.5-verified]
- 2.5 `ScriptFunction` `isNative` bit at 0xC0 (unasserted); game shows at most 9 "Missing native
  function" errors (`bdeed85f`). Validator-hook crash signature on the old layout: AV reading 0x0
  313 ms after scc (`f42065f8`).
- Saves: `SaveLocksManager` lock; loading is not covered.
- Boot: `-skipStartScreen` (`MAP:200-201`). Launch args must be `--key=value` (`MAP:2399`).
- Pause menu unpause must be deferred (0.25 s) — `SetMenuModeEvent` is consumed a frame later (`MAP:719-728`).
- Steam networking: NCO uses GNS directly over UDP (port 11778), no Steam relay.

**CAMP SteamTunnel / CoopConnect** (`code/client/App/World/{SteamApi,SteamTunnel,CoopConnect,ServerHost}.cpp`,
`d639f03`, `179a6ec`; 2.31, untested)

- Uses the **game's own `steam_api64.dll`** via `GetModuleHandleW` + flat exports, no SDK link:
  `SteamAPI_SteamFriends_v017`, `SteamAPI_SteamNetworkingMessages_SteamAPI_v002`, `SteamAPI_SteamUser_v021`,
  `SteamAPI_SteamNetworkingUtils_SteamAPI_v004`, `SteamAPI_ISteamUser_GetSteamID`,
  `SteamAPI_RegisterCallback`, `…ActivateGameOverlayInviteDialogConnectString`, `…SetRichPresence`,
  `…ISteamNetworkingMessages_SendMessageToUser/ReceiveMessagesOnChannel/AcceptSessionWithUser/
CloseSessionWithUser`, `SteamAPI_SteamNetworkingMessage_t_Release`, `…InitRelayNetworkAccess`
  (`SteamApi.cpp:57-73`). [unverified on 2.5 — check the versioned export names in 2.5's DLL; no
  GOG/Epic path]
- Callbacks: a `CCallbackBase` mimic dispatched by the game's own `SteamAPI_RunCallbacks` on the main
  thread; `SteamNetworkingMessagesSessionRequest_t` id **1251**, `GameRichPresenceJoinRequested_t` id
  **337** (`{u64 friendId; char connect[256]}`, packed 8). `SteamNetworkingIdentity` (packed 8, 136 B,
  type 16 = SteamID); message_t `{data@00, size@08, conn@0C, peer@10}`. Send flags `1|32`
  (UnreliableNoNagle | AutoRestartBrokenSession).
- Tunnel: GNS UDP datagrams carried unchanged over `ISteamNetworkingMessages` (Valve relay when no direct
  path); channels guest→host **0x4350**, host→guest **0x4351**; max datagram 2048 B; guest binds a
  non-blocking UDP socket on 127.0.0.1 and the client connects to it; host keeps one loopback socket per
  guest SteamID so the real server sees distinct clients; idle 120 s → closed; worker thread detached
  (a joinable one `std::terminate`s at game exit), 1 ms sleep. Windows reports ICMP port-unreachable on
  the next `recvfrom` → `WSAIoctl(SIO_UDP_CONNRESET, FALSE)` (`2f3dfae`). Under Proton, Windows-socket
  semantics come from Wine — untested.
- Embedded server: `Server.Loader.exe` started outside `red4ext\plugins` (RED4ext would load its DLLs),
  suspended + `CREATE_NO_WINDOW`, in a Job object with `KILL_ON_JOB_CLOSE`, needs .NET 8 runtime.
- Invite: `--online --steamhost=<SteamID64> --coopjoin` or `--ip= --port=`; rich presence keys
  `connect`/`status`.

---

## 10. Build gotchas (NCO, Windows/MSVC)

- Prerequisite set for patch 2.5 (`MAP:2537-2540`, `5e749fa6`): **RED4ext 1.31.0, Codeware 1.21.2,
  ArchiveXL 1.28.6, TweakXL 1.12.0, redscript 0.5.32, input_loader 0.2.6** (needs RED4ext ≥1.31.0) —
  "the whole set moved together for game patch 2.5". 2.31-era set was RED4ext 1.29.1, Codeware 1.18.0,
  ArchiveXL 1.26.0, TweakXL 1.11.1. A 2.5 game with RED4ext 1.29.1 + ArchiveXL 1.26.0 died before any
  plugin loaded (`d06f33ad`). The 2.5 patch wiped winmm.dll, RED4ext.dll, the plugin folder and scc.exe.
  Codeware submodule at `a94441072`.
- RED4ext 1.30.0 removed the max-SDK-version check and added plugin API v1 (`RED4ext::v1`: PluginHandle,
  Sdk, EMainReason, PluginInfo, GameState); addresses come from `cyberpunk2077_addresses.json`
  (`8eca2871`). SDK 2.5 changes: callback inline buffer 32→64, some hashes changed, game-system virtual
  ABI changed; API renames `DynArray.size`→`Size()`, `->entries`→`Data()`,
  `Red::Detail::GetFunction`→`Red::GetFunction`, `CallFunctionWithArgs(func,ctx,args)`→
  `Red::CallFunction(ctx,func,args)` (`e797867b`).
- Install the crash reporter first thing in plugin Main (a startup AV ~300 ms in was otherwise
  unrecorded) (`b3580d49`). Crash dumps land in `REDEngine\ReportQueue`; `MINIDUMP_THREAD` stack
  descriptor at +24; `/OPT:ICF` folds helpers (`08ac5b15`). AMD TDRs masqueraded as mod crashes
  (`98ae4734`). In a vectored handler, a C++ throw's ExceptionAddress is the runtime throw site; ~20
  benign engine C++ exceptions happen in the first 250 ms (`0be76dc0`).
- RedLib type-name bug (2.31): a failing concept made the system register as base `gameIGameSystem` →
  script validation failed, RED4ext refused to start (`9732c461`). RED4ext loads every plugin
  subdirectory DLL, renamed folders included (`3c7904f8`).
- Toolchain: MSVC 14.44, Windows SDK 10.0.22621, xmake 3.1.0 (`02ab05a2`); xmake under Git Bash detects
  mingw → pass `--plat=windows` (`f12def17`); spdlog's fmt needs `/utf-8` (`fc8a950c`).
- RED4ext.SDK vendored: WopsS `5246e659` "Update types for patch 2.5 (#232)" (2026-10-24) + the
  CyberpunkMP team's decodes (tiltedphoques fork `f52af6a3`); RedLib `a88797f`; jackhumbert SDK
  `155d285a` for 2.5 vehicle offsets (`vendor/RED4ext.SDK/NCO-PROVENANCE.md`). Upstream WopsS alone lacks
  the MP decodes; tiltedphoques fork alone lacks 2.5.
- C++20 (`xmake.lua:8`); xmake with `--vs_sdkver=10.0.22621.0` (**10.0.26100.0 breaks**; switching SDKs
  needs `xmake global --clean` + delete `.xmake/` and `build/`); `-j 4` or lower (`C3859` PCH OOM);
  protobuf-cpp **29.3** forced on all transitive deps (3.19.4 too old, 35.x removed
  `FieldDescriptor::is_optional`); abseil **20250127.0**; flecs **v4.0.3**; mimalloc 2.1.7; libopus 1.5.2
  (`xmake.lua:22-73`, `CONTRIBUTING.md:36-50`).
- CppSharp 1.1.84.17100 on net9.0 for the C# SDK generator; the generator swallows exceptions and exits
  0 (`CONTRIBUTING.md:45,52-57`).
- `xmake install` without `-o distrib` writes elsewhere and prints ok (`CONTRIBUTING.md:61-76`).
- GCC/libstdc++ (Linux server) catches missing `std::` includes MSVC forgives — 34 found at once
  (`MAP:429-444`).
- flecs: `FLECS_DEBUG` changes `ecs_stack_cursor_t` layout; don't enable to debug the cursor
  (`InterpolationSystem.cpp:1017-1019`).
- No Proton/Wine-specific notes exist in either repo.

---

## 11. Address-hash table

All hashes found anywhere in either repo (source grep of every 7–10 digit decimal, filtered against
`addr.tsv`; commit bodies add one; two come from the vendored SDK). **Every one is present in the 2.50
list** — none are missing. Only four have symbol names in `addr.tsv`. Hash presence proves the symbol
still exists in 2.50, not that its signature or the struct layouts around it are unchanged. "CW name" = the constant in `codeware/src/Red/Shared/AddressLib.hpp`
(proof the hash means the same function on 2.5). "Used" = actually called/hooked in NCO HEAD (not just
declared). Repos: N = NCO, C = CAMP.

| Hash                    | addr.tsv symbol                      | CW name (2.5)                              | What the repos use it for (signature as used)                                                                                                                                                                                                                                                | Repo / file:line                                            | Used           | Status                                                            |
| ----------------------- | ------------------------------------ | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- | -------------- | ----------------------------------------------------------------- |
| 1828854026              | —                                    | `IPlacedComponent_SetTransform`            | write placed transform `(IPlacedComponent*, WorldTransform const&)`                                                                                                                                                                                                                          | N `InterpolationSystem.cpp:103`                             | yes            | [2.5-verified] call; doesn't move NPC-record bodies               |
| 3227858325              | —                                    | `QuestPhaseInstance_ExecuteNode`           | hook + call quest node `u8(PhaseInstance*, questNodeDefinition*, QuestContext&, Socket const&, DynArray<Socket>&)`                                                                                                                                                                           | C `QuestMirror.cpp:71`                                      | yes            | [2.5-verified]                                                    |
| 3144298192              | —                                    | `QuestsSystem_CreateContext`               | `(QuestsSystem*, QuestContext*, 1, 0, -1, -1)`                                                                                                                                                                                                                                               | C `QuestMirror.cpp:64`                                      | yes            | [2.5-verified]                                                    |
| 386815609               | —                                    | `AppearanceChangeSystem_ChangeAppearance2` | "ScheduleSynchronizedAppearanceChanges" `(changer, WeakHandle<Object>&, Span<Key>* old, Span<Key>* new, std::function const&, u32 prio)` — remote face/body                                                                                                                                  | N/C `CharacterCustomizationSystem.h:63`                     | yes            | [2.5-verified] (remotes dress correctly on 2.5)                   |
| 633871992               | —                                    | `MorphTargetManager_ApplyMorphTarget`      | SetTargetWeightWithNormalize `(comp, CName target, CName region, float, bool)`                                                                                                                                                                                                               | N/C `MorphTargetManagerComponent.h:11`                      | no             | [unverified]                                                      |
| 898639042               | —                                    | `ScriptValidator_Validate`                 | hook to register RPC classes during script validation                                                                                                                                                                                                                                        | N `RpcValidator.cpp:245`, C `:185`                          | yes            | [2.5-verified] (needs 2.5 ScriptBundle layout)                    |
| 2468877568              | `GpuApi::Present`                    | —                                          | `CRenderNode_Present_DoInternal` hook `void*(i32*, u8, UINT)` (ImGui overlay)                                                                                                                                                                                                                | N/C `Game/Rendering.h`                                      | yes            | [2.5-verified]                                                    |
| 239671909               | `GpuApi::ResizeBackbuffer`           | —                                          | `CRenderGlobal_Resize` hook                                                                                                                                                                                                                                                                  | N/C `Game/Rendering.h`                                      | yes            | [2.5-verified]                                                    |
| 3192982283              | —                                    | —                                          | `CRenderGlobal_Shutdown` hook                                                                                                                                                                                                                                                                | N/C `Game/Rendering.h`                                      | yes            | [2.5-verified] (part of the 2.5 overlay fix `60a8ba47`)           |
| 2268141838              | —                                    | —                                          | `IdleController::SetAnimation(Controller*, AnimationData&)` — hooked; attaches MMC to registered puppets                                                                                                                                                                                     | N `Game/Entity.h:25`                                        | yes            | [2.5-verified] (default path since test.87)                       |
| 4018412273              | —                                    | —                                          | `move::Component AttachController(Component*, Controller*) → float`; reads ctrl type @+0x8, pushes on component+0xA8                                                                                                                                                                         | N `InterpolationSystem.cpp:1296`, C `VehicleSystem.cpp:268` | yes            | [2.5-verified] (static check `e3c5e455` + test.85/86)             |
| 2624656810              | —                                    | —                                          | `AnimationDriver::Attach(Entity*)` (driver struct 0x30)                                                                                                                                                                                                                                      | N/C `AnimationDriver.cpp:11`                                | yes            | [2.5-verified] via MMC                                            |
| 2385973466              | —                                    | —                                          | `AnimationDriver::Detach()`                                                                                                                                                                                                                                                                  | N/C `AnimationDriver.cpp:17`                                | yes            | [2.5-verified] via MMC                                            |
| 2132808949              | —                                    | —                                          | `AnimationDriver::SendParameters(AnimationData&)` (0x3C)                                                                                                                                                                                                                                     | N/C `AnimationDriver.cpp:23`                                | yes            | [2.5-verified] via MMC                                            |
| 3750304414              | —                                    | —                                          | `AnimationControllerComponent` get anim length `float(comp*, CName)`                                                                                                                                                                                                                         | N/C `AnimationDriver.cpp:29`                                | yes            | [2.5-verified] via MMC                                            |
| 316482401               | —                                    | —                                          | `ApplyFeature(AnimationControllerComponent*, CName, Handle<AnimFeature>)`                                                                                                                                                                                                                    | N/C `MultiMovementController.cpp:11`                        | no             | [unverified]                                                      |
| 2710710832              | —                                    | —                                          | `CreateHandle_CharacterCustomizationState(Handle*)`                                                                                                                                                                                                                                          | N/C `CharacterCustomizationSystem.h:15`                     | yes            | [2.5-verified] (remote apply path)                                |
| 108403442               | —                                    | —                                          | `CharacterCustomizationState::Serialize(state*, CMPStream*)` (read & write)                                                                                                                                                                                                                  | N/C `CharacterCustomizationSystem.h:22`                     | yes            | [2.5-verified] for deserialize into a fresh state                 |
| 1993027039              | —                                    | —                                          | `CharacterCustomizationState_InitFromPreset(state*, Handle<Preset>*)`                                                                                                                                                                                                                        | N/C `:19`                                                   | no             | [unverified]                                                      |
| 56564470                | —                                    | —                                          | `gameTransactionSystem::AddItemToSlot(sys*, AddItemToSlotContext const*)` (commented out)                                                                                                                                                                                                    | N/C `:25`                                                   | no             | [unverified]                                                      |
| 394336209               | —                                    | —                                          | `GetGameSettings() → ink::GameSettingsResource const*`                                                                                                                                                                                                                                       | N/C `:28`                                                   | no             | [unverified]                                                      |
| 1713639304              | —                                    | —                                          | `ItemID::SetUniqueCounterValue` (commented out)                                                                                                                                                                                                                                              | N/C `:31`                                                   | no             | [unverified]                                                      |
| 3020231866              | —                                    | —                                          | `ItemID_Create(ItemID*, TweakDBID, i64 seed)`                                                                                                                                                                                                                                                | N/C `:33`                                                   | 2.31 path only | [2.31-only] (native dressing gated off on 2.5)                    |
| 3236045910              | —                                    | —                                          | `GetItemRecord` → `Handle<Item_Record>*` (member fn)                                                                                                                                                                                                                                         | N/C `:35`                                                   | 2.31 path only | [2.31-only]                                                       |
| 1934369954              | —                                    | —                                          | `Item_Record::GetPlacementSlot(Handle<AttachmentSlot_Record>*, i32)`                                                                                                                                                                                                                         | N/C `:38`                                                   | 2.31 path only | [2.31-only]                                                       |
| 523315084               | —                                    | —                                          | TweakDB `Val::EvalArrayTweakDBID`                                                                                                                                                                                                                                                            | N/C `:41`                                                   | no             | [unverified]                                                      |
| 3648983380              | —                                    | —                                          | TweakDB `Val::EvalCName`                                                                                                                                                                                                                                                                     | N/C `:44`                                                   | 2.31 path only | [2.31-only]                                                       |
| 4124714348              | —                                    | —                                          | `ResolveSuffix(void*, CString*, Handle<Item_Record>&, bool isMale)`                                                                                                                                                                                                                          | N/C `:47`                                                   | no             | [unverified]                                                      |
| 1337337421              | —                                    | —                                          | `ResolveVisualTags(CName, CString*, Handle<Item_Record>&)`                                                                                                                                                                                                                                   | N/C `:50`                                                   | no             | [unverified]                                                      |
| 3029088864              | —                                    | —                                          | `GetItemAppearanceName(CString*, Handle<Object>, Handle<Object>, Handle<Item_Record>, ItemID)`                                                                                                                                                                                               | N/C `:53`                                                   | 2.31 path only | [2.31-only]                                                       |
| 1054086242              | —                                    | —                                          | MorphTargetManager `ResetWeights(comp*)`                                                                                                                                                                                                                                                     | N/C `MorphTargetManagerComponent.h:14`                      | no             | [unverified]                                                      |
| 2624990479              | —                                    | —                                          | `RegisterRemotePlayerMappin(IMappinSystem*, NewMappinID*, MappinData const&, WeakHandle<Object>*)`                                                                                                                                                                                           | N/C `Game/MappinSystem.h:8`                                 | no             | [unverified]                                                      |
| 3577160229              | —                                    | —                                          | `RegisterMappin(IMappinSystem*, NewMappinID*, MappinData const&, Vector3*)`                                                                                                                                                                                                                  | N/C `Game/MappinSystem.h:12`                                | no             | [unverified]                                                      |
| 4039776020              | —                                    | —                                          | vehicle `SetIsPlayerControlled(BaseObject*, bool)` (from DriveAction::OnStart)                                                                                                                                                                                                               | N `VehicleSystem.cpp:36,68`, C `:269`                       | yes            | [unverified on 2.5]                                               |
| 1620777158              | —                                    | —                                          | vehicle `SetFlags(BaseObject*, u32)` (0x10, 0x80)                                                                                                                                                                                                                                            | N `VehicleSystem.cpp:37`, C `:270`                          | yes            | [unverified on 2.5]                                               |
| 1585713002              | —                                    | —                                          | vehicle `SetKinematic(BaseObject*, bool)`                                                                                                                                                                                                                                                    | N `VehicleSystem.cpp:38,69`, C `:271`                       | yes            | [unverified on 2.5]                                               |
| 2933986803              | —                                    | —                                          | vehicle `ForceMoveTo(BaseObject*, WorldTransform const&, float dtMs)`                                                                                                                                                                                                                        | N `InterpolationSystem.cpp:800,912`, C `:131`               | yes            | [unverified on 2.5]                                               |
| 457775029               | —                                    | —                                          | vehicle `ForceTransform(BaseObject*, WorldTransform const&, bool)`                                                                                                                                                                                                                           | N `:913`, C `:132`                                          | no (commented) | [unverified]                                                      |
| 2592348558              | —                                    | —                                          | vehicle `EnablePhysics(BaseObject*, u32 flag=0x100, bool)`                                                                                                                                                                                                                                   | N `:914`, C `:133`                                          | yes            | [unverified on 2.5]                                               |
| 3713079867              | —                                    | —                                          | `DriveToPoint::OnUpdate_Internal` — comment only (model for the vehicle drive sequence)                                                                                                                                                                                                      | N `:920`, C `:137`                                          | no             | reference                                                         |
| 1160782872              | —                                    | —                                          | `MountingFacility::IsMountedToObject(fac*, Entity const&, MountingSlotId const&, bool)`                                                                                                                                                                                                      | N `NetworkWorldSystem.cpp:3484`, C `:294`                   | no             | [unverified]                                                      |
| 3120376212              | —                                    | —                                          | `MountingFacility::GetMountingInfo(fac*, Entity const&, MountingSlotId const&, MountingInfo&)`                                                                                                                                                                                               | N `:3488`, C `:298`                                         | no             | [unverified]                                                      |
| 3535541945              | —                                    | —                                          | `Player::ProcessInput(cpPlayer*, RawInputBuffer&)` — hook + call                                                                                                                                                                                                                             | C `ChoiceInput.cpp:69`                                      | yes (CAMP)     | [2.5-present]; CAMP's 0x40 RawInputData is [broken-on-2.5] (0x48) |
| 2582323196              | —                                    | —                                          | function containing `exe+0x3E7863` (range 0x3E7848–0x3E789B), crash site of test.74 (MMC attach) and test.83 — named in commit `b88e1040` only                                                                                                                                               | commit                                                      | n/a            | crash locator                                                     |
| 404169501 (0x1817231D)  | — (present, unnamed)                 | —                                          | SDK `Detail::AddressHashes::CBaseFunction_InternalExecute`, used by RedLib call-with-frame `Red/TypeInfo/Invocation.hpp:18`. **Note**: CW's constant of the same name is **2744524784** (addr.tsv: `rtti::Function::InternalCallNative`) — the two disagree; check which RedLib path you use | N/C via vendored SDK `Detail/AddressHashes.hpp:14`          | yes            | used on 2.5 by NCO (mod runs), [check]                            |
| 1482886888 (0x58630EE8) | `rtti::ClassType::DeepCompare`       | —                                          | SDK `TTypedClass_IsEqual`, used in `Red/TypeInfo/Definition.hpp:730`                                                                                                                                                                                                                         | N/C via SDK `:240`                                          | yes            | [2.5-verified] (named in the 2.50 list)                           |
| 2744524784              | `rtti::Function::InternalCallNative` | `CBaseFunction_InternalExecute`            | not used by NCO/CAMP; Codeware's call-native entry                                                                                                                                                                                                                                           | CW `AddressLib.hpp`                                         | —              | [2.5-verified]                                                    |
| 673454961               | —                                    | `InkSystem_ProcessInputEvents`             | not used by either repo; suggested 2.5 alternative to ProcessInput for input injection                                                                                                                                                                                                       | CW `Red/InkSystem.hpp:42-44`                                | —              | [2.5-verified]                                                    |

**Non-hash offsets / vtable slots** (summary; details in the sections above)

| What                                                                                         | Value                                                                                                                                                                                                           | Version / status                                                                      | Source                                         |
| -------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------------------------------- |
| `gameuiCharacterCustomizationSystem` size                                                    | 0x388 (2.31) / **0x3D8** (2.5)                                                                                                                                                                                  | fingerprint for `IsGame231Layout`                                                     | `CharacterCustomizationSystem.h:117-126`       |
| creator state handle                                                                         | ccSystem **+0x78**                                                                                                                                                                                              | [2.31-only]; holds template body on 2.5                                               | `:131-160`, `463bc4d1`                         |
| `IComponent::owner`                                                                          | **0x50** (2.5 SDK assert); IComponent size 0x90; old hand offset +0x90                                                                                                                                          | [2.5-verified]                                                                        | `InterpolationSystem.cpp:1310`, `7bef6e17`     |
| `move::Component`                                                                            | size 0x2C0; representation @0x138; worldTransform 0x1C0; position 0x1E8; speed 0x220; controller list **+0xA8**                                                                                                 | 2.5 SDK + `e3c5e455`                                                                  | `moveComponent.hpp:29-56`                      |
| MMC controller layout                                                                        | vptr, `u32 type` @+0x8 (kMulti = 1<<10), bools, `move::Component*` @+0x10; 19 vfuncs                                                                                                                            | [2.5-verified]                                                                        | `MultiMovementController.h`, `e3c5e455`        |
| engine `AnimationDriver` / `AnimationData`                                                   | 0x30 / 0x3C                                                                                                                                                                                                     | [2.5-verified]                                                                        | `e3c5e455`                                     |
| `ITransactionSystem` vtable (GiveItem/AddItemToSlot)                                         | 0x250 and on (SDK virtuals)                                                                                                                                                                                     | [broken-on-2.5] crash `exe+0x887DC4`                                                  | `AppearanceSystem.cpp:395-412`, `929d924f`     |
| `WheeledBaseObject` vtable "engine on" `(bool)`                                              | **+0x328**                                                                                                                                                                                                      | **[broken-on-2.5]** (§13.5): the slot is another function; use `TurnEngineOn` by name | `VehicleSystem.cpp:46`                         |
| `vehicle::IMoveSystem` vtable SetSimpleMovement `(EntityID const&, bool)`                    | **+0x1F0**                                                                                                                                                                                                      | [2.5-static] plausible (§13.5), still to run                                          | `InterpolationSystem.cpp:95-98`                |
| `vehicle::BaseObject` vtable ResetWithNewTransform `(WorldTransform const&)`                 | **+0x280**                                                                                                                                                                                                      | **[broken-on-2.5]** (§13.5): the slot is `*(this+0x6B4) = 0`                          | `campaign/.../InterpolationSystem.cpp:60-63`   |
| `vehicle::BaseObject` fields (2.5 SDK)                                                       | physicsState 0x260 (flag 0x100), rigidBody 0x2D8, worldTransform 0x350, runtimeData 0x5F0, engineData 0xBD0 (`unk61`), size 0xBC0; CW RecordID 0x658                                                            | SDK asserts / CW                                                                      | `NCO-PROVENANCE.md:71`                         |
| `PuppetPS.unk72[0] = 1`                                                                      | "checked during item add for &TPP"                                                                                                                                                                              | used on 2.5, unexplained                                                              | `AppearanceSystem.cpp:373-380`                 |
| `MorphTargetManagerComponent` weights                                                        | +0x98                                                                                                                                                                                                           | unused [unverified]                                                                   | `MorphTargetManagerComponent.h:8`              |
| `game::Puppet` (old ExPuppet) Teleport vfunc after 34 slots from 0x108; moveComponent @0x420 | —                                                                                                                                                                                                               | [broken-on-2.5] retired                                                               | `Game/Puppet.h`                                |
| old move-component stub: controller* @0xA8, SpinLock @0x128                                  | —                                                                                                                                                                                                               | 2.31 stub; 0xA8 re-confirmed on 2.5                                                   | `Game/Movement.h`, `e3c5e455`                  |
| `inkWidget` parentWidget / layerProxy                                                        | 0x168 / 0x148                                                                                                                                                                                                   | [2.31-only]                                                                           | `NetworkWorldSystem.cpp:769`                   |
| entity status byte                                                                           | +0x156                                                                                                                                                                                                          | CW uses on 2.5                                                                        | `CW Red/Entity.hpp:57`                         |
| Script bundle (2.5)                                                                          | ScriptDefinition 0x40; ScriptClass parent 0x48 / properties 0x50 / functions 0x90 / flags 0xD1; ScriptFunction 0xF8 (params 0x58, flags 0xC0); ScriptBundle classes 0xD8 / types 0xE8; ScriptProperty type 0xB8 | [2.5-verified]                                                                        | `Game/ScriptValidation.h`                      |
| Render device (2.31 → 2.5)                                                                   | devices 0xC97F38 → swapchains 0xCEBF30; queue 0x13BC4D0 → 0x14104D0                                                                                                                                             | use `GpuApi::GetDeviceData()`                                                         | `60a8ba47`                                     |
| `QuestsSystem`                                                                               | +0x60 lock, +0x68 root phase, +0xF8 FactManager*                                                                                                                                                                | [2.5-verified] CW                                                                     | `QuestMirror.cpp:57-58`                        |
| `QuestContext`                                                                               | size 0x340, phaseStack 0xF8, nodeID 0x108, PhaseContext 0x110                                                                                                                                                   | [2.5-verified] CW                                                                     | `QuestMirror.cpp:35-43`                        |
| `RawInputData`                                                                               | 0x40 (CAMP) vs **0x48** (CW 2.5)                                                                                                                                                                                | [broken-on-2.5] in CAMP                                                               | `ChoiceInput.cpp:12-26`, `CW Red/Input.hpp:32` |
| `gamePlayerManager` vtable → local cpPlayer                                                  | +0x200                                                                                                                                                                                                          | [2.5-static] a getter of `this+0x58` (§13.5), still to run                            | `ChoiceInput.cpp:72-82`                        |
| `worldStreamingSector` node setups                                                           | +0x40, stride 0x90                                                                                                                                                                                              | [unverified/risky on 2.5]                                                             | `SceneSpots.cpp:67-111`                        |
| Seat CName                                                                                   | FNV1a64("seat_front_left") = 0xb000b1d029d0cea0                                                                                                                                                                 | engine CName hash                                                                     | `MAP:913-915`                                  |
| TweakDBID                                                                                    | CRC32(name) \| (len << 32)                                                                                                                                                                                      | engine                                                                                | `MAP:2403`, `31590288`                         |

---

## Top traps (one line each)

1. Hand offsets/vtable slots from 2.31 broke silently on 2.5 (ccSystem+0x78, ITransactionSystem 0x250+,
   component+0x90 owner, render context); hashes survived. Fingerprint the build (class sizes) and gate.
2. Engine callbacks reached from redscript run on job threads — never touch your own world/ECS there.
3. Never put an epoch-ms tick in a float (131 s resolution); drop tick 0.
4. NPC-record puppets on 2.5 are owned by their AI/move component: transform writes and
   TeleportationFacility don't move them; use an engine move controller (MMC) or AI move commands.
5. `ExecuteFunction` with a null out-slot silently skips functions that return a value.
6. Replacing `prototype_hud.inkhud` goes stale every patch; one bad `.reds` disables every script.
7. Panam-derived records bring god-mode tags, name and AI — strip/blank all three.
8. Re-applying an identical appearance repeatedly crashes the observer; key by ccstate hash.

---

## 12. tandem's own measurements (game 2.50, Proton, anya)

Measured in this project; each line says how.

- **Movement-controller footprint**: right after `AttachController` (4018412273)
  the engine writes the 32-bit value `1` at controller **+0x20** and nothing
  else in +0x20..+0x7F (filled with zeros before attach, dumped at the first
  `SendAnimationParameters`, same on every run). Anything stored at +0x20 is
  lost: an id placed there read back as 1. NCO's layout had its target
  position there, masked by rewriting it from the main thread every frame.
  [2.5-verified]
- **The engine-attached controller works on 2.5** with the 19-slot layout of
  §3, a `kType` marker at +0x08 and the `IdleController::SetAnimation` hook
  skipping our own controllers: the body is moved by `GetDeltaTransform`'s
  delta to within **0.01 m** of the target at ~60 frames/s, with the engine's
  walk animation (`MotionAction::Move`, `LocomotionStyle::Walk`, clip time,
  speed). No crash in ~10 minutes over 5 runs. [2.5-verified]
- **Clip lengths** (`GetAnimLength` 3750304414): `Character.Panam`'s rig:
  `walk_0` 2.67 s, `sprint_0` 0.57 s, **no `jog_0`** (-1). The player's
  `player_wa_tpp_cutscene.ent`: all three **-1** (no NPC locomotion clips, the
  "glide"). Missing clips answer -1, not 0. [2.5-verified]
- **Templates present in 2.50** (`basegame_4_appearance.archive`, found by
  FNV1a64 path hash in the archive indexes): `player_{wa,ma}_tpp.ent`,
  `player_{wa,ma}_tpp_cutscene.ent` under `base\characters\entities\player\`;
  no repacked copy is needed to reference them. [2.5-verified]
- **Main menu world**: its background scene (V's arm in the junkyard) attaches
  a player: `ScriptableSystem.OnPlayerAttach` fires for it, at positions near
  the world origin, with a **female** body whatever the save's V is.
  `GameInstance.GetSystemRequestsHandler().IsPreGame()` (Codeware's
  accessor, the engine's flag) is **true** there and **false** in a loaded
  save: the reliable test. [2.5-verified]
- **Script systems are per world**: a loaded save gets a new instance of each
  `ScriptableSystem` (`OnAttach` runs again). [2.5-verified]
- **Codeware session events don't reach a script system after a save loads**:
  `Session/Start` and `Session/Ready` arrive for the main menu (pre-game),
  none for the loaded game, sticky or not. Codeware also drops every
  non-sticky callback on world detach. Use `OnPlayerAttach` + `IsPreGame()`.
  [2.5-verified]
- **Script callbacks run on several threads** (thread ids differ between
  `OnAttach`, `OnPlayerAttach`, delay callbacks): natives called from scripts
  must be thread-safe. [2.5-verified]
- **redscript has no `continue`**; a compile error opens a modal dialog that
  blocks the main thread, and on a headless display the engine watchdog then
  fires after 120 s. [2.5-verified]
- **Startup hangs happen without tandem's involvement too**: 2 of ~8 launches
  hung on the logo or behind a dialog (watchdog dump in
  `pfx/.../REDEngine/ReportQueue/`), the player's mod list being the other
  suspect (see the anya notes). Not yet attributed. [open]

### 12.1 V's body for a remote player (2026-11-17, probe runs on anya)

Measured with the probe (`tandem.ini probe = on`, scripts/tandem/Probe.reds),
which spawns bodies in rows and logs their components.

- **V's templates carry no meshes**: `player_{wa,ma}_tpp(_cutscene).ent`
  extract with character-customization controllers and no mesh component.
  Without a look the puppet is invisible. [2.5-verified]
- **The look**: the live state of `GameInstance.GetCharacterCustomizationSystem(game).GetState()`,
  its keys collected through `gameuiICharacterCustomizationState` vtable
  slots 0x190 / 0x1A0 / 0x1B0 (head / body / arms; slot 0x120
  IsBodyGenderMale answers right, the check used before calling them), groups
  TPP, face, hairs, (beards), TPP_Body, breast, lifted_feet, flat_feet,
  holstered_default, nails: 15 keys for a male V, scheduled with 386815609 on
  `GetRuntimeSystem<RuntimeSystemEntityAppearanceChanger>()` (RedLib
  mapping 34); the completion callback runs, the body shows face, hair,
  tattoos. [2.5-verified]
- **`player_*_tpp.ent` draws only a shadow** (it is the first-person player's
  third-person body); `player_*_tpp_cutscene.ent` draws. [2.5-verified]
- **NPC locomotion lives in `entAnimationSetupExtensionComponent`s**
  ("Gameplay Animation Setup" 125 sets, "Special Locomotion Setup",
  "Fear Extension", "Character Entity Animation Setup"…), not in the root
  `entAnimatedComponent`'s own setup nor in the `AnimationControllerComponent`
  (identical on every body: actions db 4960121789495904435). V's templates
  have " Player TPP Animation Setup" (4 sets) and an empty "Character Entity
  Animation Setup". `GetAnimLength` finds `walk_0` once the donor's extension
  sets are added to V's "Character Entity Animation Setup" (bound to V's
  root) and the root runs the donor's graph (11281804297281181379, shared by
  NPCs): male V 3.10 / 1.73 s (walk / sprint, from Kerry), female 2.67 /
  0.57 (Panam). No `jog_0` on any. [2.5-verified]
- **Edits in Codeware's `Entity/Extract` reach the live entity** (read back
  from `entity.GetComponents()`); edits in `Entity/Assemble` came too late or
  never ran. Setup hashes change with the edit (`hash` + constant) since the
  engine may key setups by them. [2.5-verified]
- **Skeletons** (root rig hash): female V = Panam = 10037276762472186253;
  male V = Kerry = Saul = Takemura = Mitch = 14264052059588999261; Jackie
  1707357870198610305 (man_big). Records `Character.Kerry`, `Saul`, `Takemura`,
  `Mitch`, `Jackie` exist; `Kerry_Eurodyne`, `River_Ward`, `Viktor_Vektor`,
  `Goro_Takemura`, `Johnny`, `Dex`, `Viktor`, `River` do not. [2.5-verified]
- **Unregistering a Codeware callback while a job thread dispatches it hung
  the game** (watchdog dump, no exception): register once per world, gate the
  handler with a flag. [2.5-verified]
- **Clothes: solved 2026-11-18, see §13.4 (the record's `genders`).** `GiveItem` + `AddItemToSlot` (script) return true for the
  local V's 4 worn items, before or after the look, with the PuppetPS byte
  at +0x72 set (`gamePuppetPS` RTTI size 0x78; `gameObject.persistentState`
  found by RTTI property offset) and with clothing `attachmentSlots` added to
  the record; afterwards `GetItemInSlot` is empty for every clothing slot and
  nothing is drawn. [2.5-verified, unsolved]
- **Dev loop**: RedHotTools v1.3.0-beta.13 hot-reloads scripts (`.hot-scripts`
  next to its DLL, ~1.7 s) and tweak values, but **not a record's
  `entityTemplatePath`**, and after a script compile error it stops
  compiling until the game restarts. Load Begone (Nexus 8144) loads the last
  save at launch with Mod Settings' `user.ini` section
  `[LoadBegone.LoadBegoneConfig]` (`enabled`, `skipRecapVideos`): the section
  is the script class's full name, module included. [2.5-verified]
- **NPC bodies can't show a player's moves.** Applying `stanceState`
  (AnimFeature_NPCState) = Crouch, the `inCrouch` anim wrapper (what
  `NPCStatesComponent.OnStanceStateChanged` does), `GamePuppetPS.SetCrouch`,
  high-level Stealth, and `AnimationData.areAnimWrappersUnlocked` (+0x34 of
  the locomotion data) leave a controller-driven NPC (pure Panam too)
  upright: no animation set of Panam, Kerry or the 6th Street gang records
  (`Character.rcr_sixthstreet_hooligan_melee1_knife_{wa,ma}`, V's skeletons,
  292/309 sets) is gated by `inCrouch` or `stealthLocomotion`. NPCs crouch in
  cover only. [2.5-verified]
- **Anim features the engine applies** (trace: hook on 316482401
  `ApplyFeature(component, CName, Handle<AnimFeature>*)`, works on 2.5): to
  NPCs `locomotion` (animAnimFeature_Locomotion), `crowd_locomotion`,
  `stanceState` / `upperBodyState` / `highLevelState` (animAnimFeature_NPCState);
  to the local `PlayerPuppet` `locomotion`, `LocomotionStateMachine`
  (AnimFeature_PlayerLocomotionStateMachine), `playerLocomotion`
  (animAnimFeature_PlayerMovement)… Crouching V adds no new name: the
  state travels as values. `NPCStatesComponent`'s feature names are
  `stanceState`, `upperBodyState`, `highLevelState` (read through an added
  method; V's template leaves them empty). `GamePuppetPS.GetTpp()` is the
  byte at PuppetPS+0x72. [2.5-verified]
- **Design consequence**: the remote V keeps V's own third-person graph and
  gets the local player's anim features mirrored (generic RTTI field copy,
  sent when they change); the movement controller only places the body.
- **The player's gameplay graph can't go on a puppet.** Mirroring every anim
  feature applied to the local `PlayerPuppet` onto a puppet of V's cutscene
  template (no controller, no NPC stance code) changes nothing when V
  crouches: that template's graph (14299736328643609073) is not the gameplay
  one. The live player's root runs graph 8975379029519062281 on rig
  7304775167351189356 (122 root sets + 115 from extensions); putting that
  graph and its sets on the third-person body (rig 14264052059588999261)
  crashed the game at spawn (2026-11-17 19:53). [2.5-verified]

## 13. Reverse engineering of the running game (2026-11-17, game 2.50, read-only)

Measured with `tools/re/` (see `tools/re/README.md`): the live process read
through `/proc/PID/mem` on the game machine (nothing written, nothing
paused), the exe disassembled with capstone and decompiled with Ghidra.
Tags here: [2.5-verified] = read from the live process and consistent over
every object checked; [2.5-static] = read from the exe's code, not yet
exercised by a hook of ours: confirm in game before relying on it.

The image loads at its preferred base `0x140000000` (Proton, no relocation):
addresses below are the exe's own. Every function has an address-library
hash (`tools/re/addr.py`): use the hash, never the address, in the plugin.

### 13.1 RTTI, as the game holds it

- `CRTTISystem` singleton `0x144763320`; name pool `0x144334B80` (entry
  pointers at +0x08, count at +0x14; entry: hash, next, length byte, text at
  +0x14; 580 240 names); native handler table `0x143B7BD30`
  (`handlers[regIndex]`, `regIndex` at +0xF8 of a class function, +0xF0 of a
  global one). [2.5-verified]
- 16 329 classes, 1 606 enums, 4 898 global functions, 13 727 native class
  functions with a handler. Dump: `tools/re/rtti_dump.py` →
  `.cache/re/rtti.json`; query: `tools/re/rtti.py`. Kept in the repository:
  `data/rtti-vtables-2.50.tsv` (7 053 classes with their **instance
  vtable**) and `data/rtti-natives-2.50.tsv` (native function → handler
  address). [2.5-verified]
- A type's kind: `IType::GetType` (type vtable slot 0x20) is `mov al, N; ret`.
  An instance vtable is found from slot 0: `ISerializable::GetNativeType` is
  `mov rax, [rip+g]; ret` with `g` a global holding the class. [2.5-verified]
- A native handler `h(context, frame, out, outType)` reads its parameters
  from the script frame, then calls the real function: the real
  `entSlotComponent::GetSlotTransform` is `0x140CA22E4`, behind its handler
  `0x140CA2200`. Handlers are the way into any engine function a script can
  call. [2.5-static]
- An `IScriptable`'s class is the pointer at +0x30 (the script class for
  scripted types: `PlayerPuppet` on an object whose vtable is `gamePuppet`'s);
  +0x08 is its weak reference to itself (a cheap "is this a live object"
  test when scanning for a vtable). [2.5-verified]

### 13.2 Animation: from an entity to its pose

All offsets measured on the local V and checked on every live
`entAnimatedComponent` (≈760 at the time, ≈357 of them root components).

- **`entAnimatedComponent` unmapped fields** (RTTI size 0x2B0, instance
  vtable `0x1430320F8`): +0x168 the `worldAnimationSystem`, +0x170 the
  `workWorkspotSystem` (the two "global objects" of pose-streaming.md),
  **+0x178 the entity's animated object** (null on non-root components:
  deformations, face rig, shadow…), +0x250/+0x258 a weak handle to itself,
  **+0x270 its graph instance**, +0x288 `controlBinding`. [2.5-verified]
- **Graph instance** (`GI = [component+0x270]`, one per animated component,
  ≥ 0x25C8 bytes): +0x20 the `animAnimGraph`, +0x28 the instance data (what
  a node's sample function receives as third argument), +0x40 the `animRig`,
  +0x2510 root motion of the last sample (48-byte transform), +0x2540 /
  +0x2544 update / sample counters (≈ frames). `[GI+0x40]` is the
  component's rig on 757 of 758 components, `[GI+0x20]` its graph likewise.
  [2.5-verified]
- **Animated object** (`obj = [root component+0x178]`, one per entity, stable
  while the entity lives; no vtable): +0x00 a 32-bit id (-1 = unusable),
  **+0x08 the merged skeleton**, **+0x18 the pose**, +0x60 the root
  component's graph instance (equal to `[component+0x270]` on 356 of 356).
  Without +0x178 the engine finds it through the animation system:
  `0x140286A3C(system, component)` (hash 1353065690). [2.5-verified]
- **Merged skeleton** (`[obj+0x08]`): the root rig's bones first, then the
  bones of every other animated component of the entity (deformations, face
  rig, shadow, dangles). +0x00 reference transforms (count at +0x08), +0x10
  parent indices (int16), +0x20 bone names (CName), +0x40/+0x50 float track
  defaults and names (count at +0x48), +0x60 a signature that changes when
  the skeleton is rebuilt (appearance or equipment change: 677 → 619
  transforms on V during the session, new skeleton and pose objects, same
  animated object). Lists for V and for NPCs on V's two body rigs:
  `data/skeletons-2.50.txt`. [2.5-verified]
- **Pose** (`[obj+0x18]`): +0x00 → transforms, 48 bytes each (translation,
  rotation quaternion, scale: 3 × 4 floats), one per skeleton entry, count at
  +0x08; +0x18 → float tracks (count at +0x20); +0x28 / +0x38 extra named
  tracks and transforms (64-byte entries, name first). The arrays stay at the
  same address from frame to frame and are rewritten in place. They hold
  **model space** except for a short window each frame (≈3 % of 700
  samples): the graph's local-space result is copied in, then converted in
  place, parents first. Hips of a standing male V: z = 0.95–0.98 m.
  [2.5-verified]
- **Reading one bone** as the engine does: `0x14032AA4C(component, boneRef,
out WorldTransform)` (hash 3930267764; `boneRef` = index, name, skeleton
  signature: re-resolved by name when the signature differs) →
  `0x14032B1E8(obj, boneRef, out)` (hash 444473555), which is
  `pose.transforms[boneRef.index]`. Slots use it. [2.5-static]
- **V's own entity** has four animated components (`root` on rig
  7304775167351189356 with 152 bones, `deformations`, `shadow`,
  `face_rig`) and one animated object of 619 transforms. Its skeleton has
  `shadow_Hips … shadow_WeaponRight` (indices 236–261), a 26-bone copy of
  the body that follows the main bones. [2.5-verified]

### 13.3 Animation: where a frame's pose is produced

From the code (decompiled), consistent with the live counters and buffers:

1. `0x1401B25DC(GI, params)` (hash 4286257933) runs once per graph instance
   and frame, on a job thread: it updates the graph
   (`0x1403A9D90(graph, ctx)`), samples it (`0x1403A98D0(graph, ctx)` →
   `0x1403A9C80` → the root node's virtual at vtable +0x128), stores the root
   motion at GI+0x2510, then commits. `params[7]` (+0x38) is the entity's
   pose (13.2), `params[6]` (+0x30) the bone mapping from this component's
   rig to the merged skeleton. [2.5-static]
2. **The commit: `0x1401B3C08(GI, params, pose, lod)`** (hash 2074943978,
   hooked and written to by tandem in game 2026-11-18: **[2.5-verified]**;
   also called from `0x1401B5AF4` and `0x142430540`). It copies
   `[pose+0x30][src]` into the entity's pose at `[dst]` for each (src, dst)
   pair of the mapping, and the float tracks from `[pose+0x38]`. `pose` is
   the graph's output: bone count at +0x00, extra transform and track counts
   at +0x08 / +0x0C, **a pointer to the transforms at +0x30** (local space,
   in the rig's bone order, 48 bytes each), a pointer to the float tracks at
   +0x38. [2.5-verified: count 71 for a body rig, LeftHand 0.275 m from its
   parent = the reference forearm]
3. The entity's pose is then converted to model space in place (seen live,
   13.2); the function was not looked for.

**What this gives tandem** (pose streaming): one hook on the commit sees
every character's final local-space pose with its identity:
`GI == [component+0x270]`. Read V's pose there (copy `pose` when GI is the
local V's root component's); write a puppet's there (overwrite the
transforms before calling the original). No need to tell characters apart
by bone count, as the CTO mod does at the output node.

- **Node sample function**: `animAnimNode_Output` (instance vtable
  `0x1430C1160`) has it at vtable **+0x128** (`0x1403AACCC`), as CTO found:
  `Sample(node, ctx, instanceData, out)`. `out[0]` is the pose, `out[1]` a
  root-motion transform. `ctx` is a **per-call struct on the stack** (bone
  counts by LOD first, a "reference pose only" byte at +0x34, a pointer to
  the rig at +0x40): it does not identify the entity; `instanceData` does
  (`== [GI+0x28]`). With no input the node writes the rig's reference pose
  (`0x1403B1D5C(pose, rig)`). 255 node classes share the slot.
  [2.5-static; the +0x30 pointer contradicts "bones at +0x30" in
  pose-streaming.md's fourth pass: check which when the hook runs]

### 13.3b The whole per-entity animation job (second pass, 2026-11-18)

- **`0x1401B3E00(obj, entityParams)`** (hash 2251364700, hooked by tandem in
  game 2026-11-18: [2.5-verified]; the pose is complete model space when it
  returns) is the job for one entity: `obj` is the animated object of 13.2 (`[root component+0x178]`).
  It walks the entity's graph instances, a linked list from `[obj+0x60]`
  through `[GI+0x08]` (parent instance at `[GI+0x10]`), builds each one's
  parameters (skeleton at +0x28, bone mapping at +0x30 =
  `[skeleton+0x68] + index * 0x80`, pose at +0x38) and calls
  **`0x1401B5AF4(GI, params)`** (hash 1991448876), which runs
  `0x1401B25DC` (update, sample, commit). When its loop ends it calls
  **`0x140582B3C(pose, skeleton)`** (hash 1343032985): the local → model
  space conversion, in place, parents first (the short local-space window
  seen live in 13.2). [2.5-static]
- So three hook points, by need: the commit (local space, per graph
  instance, before conversion); **after `0x1401B3E00` returns** (the
  entity's complete model-space pose, identity = `obj`, nothing half
  written: the simplest place to _read_ V's pose and a candidate to _write_
  a puppet's, if nothing reads the pose in between); the conversion itself.
  [2.5-static]
- **`GI+0x25C8` (byte)**: when set, `0x1401B5AF4` does not run the graph: it
  commits the rig's reference pose (`0x142741E44(pose, rig)`). A way to
  silence a puppet's own graph while its pose is driven from outside; who
  sets it was not looked for. Other fields: `GI+0x25CA` (int16) the bone of
  the merged skeleton this instance hangs from (-1: none), `GI+0x25B1` a
  "skipped, needs re-init" flag, `GI+0x2490 / +0x24C0 / +0x24F0` transforms
  copied from the parameters each tick. [2.5-static]
- **V's first-person body is a complete body.** Model-space read of the
  local V standing (z up, metres): Hips 0.95, Spine3 1.33, Head 1.64,
  hands 0.96, knees 0.52, feet 0.09. The `shadow_*` copies equal the main
  bones for limbs and differ by ≈5 cm in the spine, neck, head and
  shoulders (the first-person camera pull): for other players, take
  `shadow_*` where it exists, the main bone elsewhere. [2.5-verified]
- A component's world transform (+0xE0) stores its position as three
  32-bit fixed-point numbers, 1/131072 m (`WorldPosition`), then the
  rotation quaternion at +0xF0. [2.5-verified]

### 13.4 What V's entity is made of

`tools/re/live.py player` lists it: about 150 components on the local V
(`PlayerPuppet`, native vtable `gamePuppet`). Worn clothes are
**components of V's own entity**, not item entities:
`entGarmentSkinnedMeshComponent` named after the mesh with a 4-digit suffix
(`t2_206_pma_jacket__tw3_coat1761`, `t0_000_pma_base__full`, …), next to
`entSkinnedMeshComponent`s `legs`, `shoes`, `torso` and
`entMorphTargetSkinnedMeshComponent`s for head and nails. The entity also
carries `gameAttachmentSlots` "AttachmentSlots", `gameInventory`
"Inventory", `entVisualControllerComponent`, `gameTPPRepresentationComponent`
and the customization controllers. For the open clothes problem (12.1): a
puppet that shows clothes needs these garment components on its entity,
whatever its slots answer. [2.5-verified]

**Clothes on tandem's puppet, measured 2026-11-18** (live slot data read with
`tools/re`, slot events from a script `AttachmentSlotsScriptCallback` kept
alive on the puppet — a listener nobody holds is dropped):

- `gameAttachmentSlots` (RTTI 0x118): slot table at **+0xB0** (count +0xB8),
  entries `gameAttachmentSlotData` of 0x90 bytes: +0x00 slot TweakDBID,
  **+0x08 the item object handle** (a `gameGarmentItemObject` for V's
  clothes), +0x18 the item id being placed, +0x58 appearance item id, +0x68
  the slot record. V has 23 slots; the puppet (record based on
  `Character.Panam`) **33 of its own, Head, Chest, Torso, Legs, Feet and
  Outfit included**: `puppet.tweak` used to add them again, making
  duplicates (removed). [2.5-verified]
- On the puppet, clothing placed either way — the paper doll's
  `GivePreviewItemByItemID` + `AddItemToSlot(CreatePreviewItemID(item))`, or
  `GiveItem` + `AddItemToSlot(item)` — is accepted (both calls true, the id
  written at +0x18/+0x58) but **no item object ever spawns and no slot event
  fires**. A pistol (`GiveItem` + `AddItemToSlot(WeaponRight)`) on the same
  puppet spawns at once (`OnItemEquipped`, `OnItemEquippedVisual`, object
  at +0x08). The refusal is specific to garments on this body. [2.5-verified]
- **The cause: the record's `genders`.** `Character.Panam`'s single entry
  is female with `panam.ent`; the puppet records inherited it. Giving each
  puppet record its own entry (`genders = [{ initial = true; gender =
"Gender.Male"; entity = <its template>; }]`, as NCO's records did) makes
  the same preview items equip at once: `OnItemEquipped` and
  `OnItemEquippedVisual` for coat, shirt, pants and boots, and the puppet
  is drawn dressed in V's clothes, face and hair from the look, pose from
  the stream. (The items are placed twice in the first 2 s: once by our
  call, once again by the engine's refresh; harmless.) [2.5-verified]

**How the game itself dresses a puppet of V** (vanilla scripts, decompiled
reference `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`; natives and
their handlers in `data/rtti-natives-2.50.tsv`) — not what 12.1 tried
(`GiveItem` + `AddItemToSlot` with the real item id): [unverified on a
tandem puppet]

- The inventory's paper doll (`OnPuppetReady(sceneName, puppet)`, line
  347417): for each area of `EquipmentSystem.GetData(player).GetPaperDollEquipAreas()`,
  the item is `GetVisualItemInSlot(area)` (the wardrobe's choice, not the
  stat item; skipped when `IsSlotHidden(area)`); clothing gets
  `GivePreviewItemByItemID(puppet, item)`, other items
  `GivePreviewItemByItemData(puppet, data)` then
  `AddItemToSlot(puppet, EquipmentSystem.GetPlacementSlot(item), CreatePreviewItemID(item))`.
  Then the head: `GiveItem` + `AddItemToSlot` of `Items.PlayerMaTppHead` /
  `Items.PlayerWaTppHead` by `puppet.GetResolvedGenderName()`.
- On the player (`EquipmentSystemPlayerData`, lines 58600–58836): a visual
  override is `GivePreviewItemByItemID(owner, item)` +
  `AddItemToSlot(owner, slot, CreatePreviewItemID(item), true)`; an equipped
  item with another look is `AddItemToSlot(owner, slot, item, visualItem)`;
  a hidden slot `AddItemToSlot(owner, slot, item, n"empty_appearance_default")`;
  and `OnItemAddedToEquipmentSlot(owner, item, opt garmentAppearance, opt appearanceItem)` /
  `OnItemRemovedFromEquipmentSlot` tell the engine about equipment changes.

### 13.5 NCO's hand-numbered slots, checked against 2.50

Sizes from the live RTTI match the 2.5 SDK: `gameuiCharacterCustomizationSystem`
0x3D8, `moveComponent` 0x2C0, `vehicleBaseObject` 0xBC0 (`vehicleWheeledBaseObject`
0xC10), `gamePuppetPS` 0x78, `entIComponent` 0x90, `gamePlayerManager` 0xE0,
`inkWidget` 0x210, `questQuestsSystem` 0x300. [2.5-verified]

Vtable slots (instance vtables from `data/rtti-vtables-2.50.tsv`, the slot's
function decompiled): [2.5-static]

| Slot                                          | Function on 2.50 (hash)    | What it does there                                                                                      | Verdict                                      |
| --------------------------------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| `vehicleWheeledBaseObject` +0x328             | `0x1421BBE60` (3954780831) | `(this, out handle)`: resolves a weak handle through its own slot +0x318                                | **not** "engine on (bool)": do not call      |
| `vehicleBaseObject` +0x280                    | `0x1421BBAA0` (1485377386) | `*(this + 0x6B4) = 0`                                                                                   | **not** `ResetWithNewTransform`: do not call |
| `vehicleMoveSystem` +0x1F0 (vt `0x1432803D8`) | `0x142BA87C8` (2599754135) | `(this, EntityID*, bool)`: finds the vehicle's record and sets its field +0x9D4 to 2 (true) / 3 (false) | consistent with `SetSimpleMovement`          |
| `gamePlayerManager` +0x200 (vt `0x1430CF520`) | `0x1419543B0` (1062672294) | returns `[this + 0x58]`                                                                                 | consistent with "local player" getter        |

By name instead of the first slot: `vehicleBaseObject.TurnEngineOn(on: Bool)`
and `TurnVehicleOn(on: Bool)` are script natives (handlers `0x1414CE088`,
`0x14059A934`), with `IsEngineTurnedOn`, `GetLinearVelocity`,
`ForceBrakesFor`… on the same class (`tools/re/rtti.py class vehicleBaseObject`).

### 13.6 Moving a vehicle we do not simulate (2026-11-18)

`scripts/tandem/VehicleProbe.reds` (`tools/game.sh cmd vehicle probe
[record]`): a dynamic `Vehicle.v_standard2_archer_hella_player` spawned
ahead of V and moved every frame (≈85 frames/s) with the teleportation
facility (`Teleport` with the vehicle, a position and
`EulerAngles(0, 0, yaw)`) along a 6 m circle at 5 m/s.

- The car is exactly where each teleport puts it: **zero drift between
  frames** over 20 s, and `GetLinearVelocity()` reads 0, so its own
  physics does not fight the teleports (or is asleep). [2.5-verified]
- Seen on screen: the car follows the circle; height and orientation
  against the ground were not checked closely (the test was cut short: the
  player was playing). A teleport sets the body's origin at the given
  position: a remote car must be placed at the driver's car's own
  transform, not at a ground point.
- Consequence for tandem: a puppet vehicle can be driven by teleporting it
  every frame to the interpolated `VehicleState`. Still to check: wheels
  turning and suspension (velocity 0 may leave them still), engine sound
  without RPM, collisions of other things against it, a passenger seated in
  it, and whether one teleport per frame is cheap enough with several cars.

**Deleting a car copy freezes the game** (2026-11-18, three engine hang
dumps in `REDEngine/ReportQueue`, all threads waiting in `ntdll`, no stacks
in the dump): `DynamicEntitySystem.DeleteEntity` on a dynamic vehicle that
has had a puppet mounted in it (mounting facility, `MountingRequest`,
instant) froze the whole game at once, whether the body was still seated
or had been unmounted 0.3 s before (`IsMountedToAnything` false). The probe
car, never mounted, deletes fine. Deleting the mounted _body_ after
unmounting it is fine. tandem now leaves car copies parked and never deletes
them in a running game. Not yet found: what the vehicle keeps of a past
occupant (seat reservation in `VehicleComponentPS`? the workspot system?)
and a safe way to remove such a car. [2.5-verified]

**It is a crash, not a hang** (2026-11-18, `tools/game.sh cmd vehicle clear`
on a parked copy whose driver had got out, then `tools/game.sh bt`): a job
thread faults at `sub_14071ca98+0x15`, a virtual call (`[obj]+8`) on an
object that is already gone. It is reached from `sub_140dbb958+0x75`, which
walks a table of objects (`*param_1`, 0x38-byte entries, `ushort` indices)
and calls `sub_140206a14` on each, under the job at `sub_1402e0400`. So
some engine list keeps a raw pointer to the deleted car, most likely
because of its past occupant, and the next pass uses it. The hang dumps
were the game's report queue waiting on that crash. Next: name that table
(decompile `sub_140dbb958`'s caller and the job's owner), then either take
the car out of it before deleting, or reuse parked copies (a pool) rather
than delete them. [2.5-verified]

**The full vehicle loop runs** (mock player `--mode drive`): copy spawned
from the record, the driver's puppet mounted in `seat_front_left`
(`MountingFacility.Mount`, no AI), the copy teleported every frame to the
interpolated `VehicleState`, the puppet unmounted when the driver gets out
(`MountingFacility.Unmount`), our movement controller re-attached 0.5 s
later (mounting detaches it), the driver leaving the session while seated.
[2.5-verified]

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

### 13.8 Crashes that look like freezes, and three ways to cause them (2026-11-18)

- **Under Proton a crash freezes the game instead of closing it.** The
  game's own exception handler (the frames at `+0x2f3a523`…`+0x2f3ae6b` on
  top of `ntdll`'s exception dispatch) waits forever: 0% CPU, every thread
  asleep, the screen as it was (or black with the loading spinner). Before
  calling it a deadlock, read the stacks: `tools/game.sh bt` runs the
  game's own Proton `winedbg` inside its pressure-vessel namespaces
  (`nsenter -U -m -t PID`, the game's `WINEPREFIX`, `LD_LIBRARY_PATH`,
  `WINEDLLPATH`), attaches, dumps `bt all`, detaches (the game keeps
  running), and names tandem's frames from `tandem.pdb`. [2.5-verified]
- **Native structs must be at most 8-byte aligned.** The script VM places
  a function's struct locals (the `event` of `for event in
TakeNpcEvents()`) 8-byte aligned and runs the type's constructor there.
  A RedLib struct with a `Red::Vector4` or `Red::Quaternion` field is
  16-byte aligned, clang zeroes it with `movaps`, and that faults. The
  constructor is `ClassDescriptorDefaultImpl<T>::ConstructCls`, called from
  the VM's frame set-up `sub_14027887c`. `TandemNpcEvent` now carries plain
  floats; `tools/check-natives.py` (`task natives:check`, run before every
  deploy) rejects such fields. Vector4 and Quaternion **parameters and
  return values** of native functions are fine. [2.5-verified]
- **A native declared with other types than the DLL's breaks the VM.**
  redscript never sees the DLL: `SendNpcSpawn(…, orientation: Quaternion)`
  against a C++ `float yaw` compiled with 0 errors and stopped the game on
  the first call. `tools/check-natives.py` compares every function and
  struct. [2.5-verified]
- **The clip-length query blocks while the rig's animations stream.**
  `GetAnimLength` (hash 3750304414, `0x1402d4a18`, (animation controller
  component, CName) → seconds) on an NPC spawned a frame earlier never
  returned: it waits on the game thread for the animations to finish
  loading, and they load on that thread. V's rig (our puppets) is loaded
  with V, so asking at once is fine there. For NPC proxies the controller
  asks after 60 animation updates, and the script attaches it 2 s after
  the entity first exists. [2.5-verified]
- The first freeze of the day came behind the loading screen, right after
  the NPC sharing's first tick. That was most likely the native type
  mismatch above, not the loading itself. Sharing is now also held until
  the world plays (`m_playing`). [2.5-tested]

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

### 13.10 Damage to an NPC another game owns (2026-11-18)

- `DamageSystem.DealDamages(hitEvent)` is where a hit takes health away:
  `StatPoolsManager.ApplyDamage(hitEvent, forReal, out lost)` with
  `forReal = !IsClient()`. With `forReal` false it measures the loss (the
  `SDamageDealt` list, per stat pool) without applying it; vanilla's own
  client path. Wrapping it (`@wrapMethod`, private is fine) catches every
  ordinary hit after armour, mitigation and the ±10 % variance: 40
  physical became 9.65 on a ganger proxy. [2.5-verified]
- Hits with `hitFlag.Kill` never reach `DealDamages`:
  `ProcessInstantKill` sets `DealNoDamage` and calls
  `RequestSettingStatPoolMinValue(target, Health, instigator)`, so they
  need their own wrap. [2.5-verified]
- A scripted hit for tests: a `gameHitEvent` with a new `AttackData`
  (`SetSource`, `SetInstigator`, `SetAttackDefinition(IAttack.Create(ctx))`
  with `ctx.record = TweakDBInterface.GetAttackRecord(t"Attacks.VehicleImpact")`),
  `attackComputed.SetAttackValue(N, Physical)` on a new
  `gameAttackComputed` (kept by the pipeline), queued with
  `DamageSystem.QueueHitEvent(hit, target)`. [2.5-verified]
- On the host, `RequestChangingStatPoolValue(id, Health, -damage,
instigator, false, false)` kills an NPC at 0 like any death: the next
  `NpcStates` round sees `IsDead()`. No hit reaction plays (no hit event
  reached it). [2.5-verified]
- `ScriptedPuppet.Kill(instigator, skipAnim, noRagdoll)` sets health with
  `RequestSettingStatPoolValueIgnoreChangeMode`, outside the damage
  pipeline: safe on a proxy from inside a damage hook. [2.5-tested]
- Status effects a hit carries (`AttackData.GetStatusEffects()`, the
  attack record's `ApplyImmediately` effects) are queued by
  `DamageSystem.ProcessStatusEffects` and applied **a frame later**: they
  are not in `StatusEffectHelper.GetAppliedEffects` right after the call.
  The target's `NPCPuppet.OnStatusEffectApplied(ApplyStatusEffectEvent)`
  sees each one (`isNewApplication`, `instigatorEntityID`, `staticData`),
  whatever applied it (hits, buildup, quickhacks). [2.5-verified]
- A status effect's damage ticks are DoT hits (`AttackData.IsDoT`, attack
  type 2) through the whole pipeline, with the effect's instigator as
  instigator and source. Applied with no instigator
  (`ApplyStatusEffect(target, id, EntityID())`), the ticks name the
  target itself. Burning on a ganger: about 5 health a tick, ~20 ticks.
  [2.5-verified]
- `AttackData.AddStatusEffect(id, stacks)` on a scripted hit applies
  that effect through the normal rules (immunity checks): a test hit can
  set an NPC on fire. [2.5-verified]

### 13.11 Host NPCs against a guest's puppet (2026-11-18, first look)

`tools/game.sh cmd npc hostile [puppets]` spawns a Tyger Claws ranged
ganger 12 m ahead with its brain on, made hostile to the puppets (and to V
unless `puppets`), and reports every 5 s; `puppet group NAME` sets the
puppets' attitude group. With the mock player in front mode:

- The puppet's attitude group is `panam` (inherited from its Panam-based
  record). `AttitudeAgent.SetAttitudeTowards(puppetAgent, AIA_Hostile)`
  reads back as hostile (`GameObject.GetAttitudeBetween` = 2). In the
  `player` group, hostility to the puppet made the NPC hostile to V as
  well (it shot V although set neutral to it): never put puppets in the
  `player` group. [2.5-verified] Puppets now get groups of their own
  (§13.12).
- **The NPC's senses never detect the puppet**
  (`SenseComponent.GetDetection(puppetId)` stays 0 at 3–12 m in plain
  view), and `TargetTrackingExtension.InjectThreat(npc, puppet)` puts it
  in combat (high-level state 2) without adding a threat
  (`TargetTrackerComponent.GetThreats(false)` empty). It then attacks
  (seen in game: it closes in and swings at the puppet) but **no hit on
  the puppet ever reaches the damage system**, not even with the puppet's
  invulnerability check skipped (`InvulnerabilityCheck` wrap, needed
  anyway: the puppet record is invulnerable, so every hit on it would end
  as "no damage" before `DealDamages`). And it still hurts V, even set
  neutral to V. Likely cause: the puppet's entity (V's TPP cutscene
  template) lacks what NPC senses and hits need (visible object,
  targeting, hit representation shapes). Next: compare the components of a puppet and of an NPC
  (`tools/game.sh cmd inspect`), then spawn puppets from a template that
  has them, or add them at assembly (Codeware). [2.5-verified]
- Injected into combat while hostile to V, the ganger shot V: ranged
  hits of type 10, 29.6 physical each. [2.5-verified]
- `GodModeSystem.AddGodMode(V, Invulnerable|Immortal, n"Default")`
  returns true but `HasGodMode` is false right after and V still died:
  script god mode does not protect V this way. Tests that fight must not
  depend on it. [2.5-verified]
- V's death screen ("Flatlined") stops the game thread: the watchdog
  reports it as silent (no crash frames in `tools/game.sh bt`). [2.5-verified]
- **Fixed: the puppet is seen.** A puppet built from V's TPP cutscene
  template lacks the components NPC senses work with. Grafting the donor
  NPC's `senseVisibleObjectComponent` **and** `senseComponent` into the
  puppet as it is built (Codeware `Entity/Extract` callback,
  `EntityBuilderTemplateWrapper.AddComponent(component.Clone())`) makes a
  hostile NPC detect it (`GetDetection` 100, `IsAgentVisible` true when in
  sight) and keep it as its `CombatTarget`. The visible object alone was
  not enough (an NPC is seen through its senses' own visible object; its
  separate `VisibleObject` component is the dead-body one, off while it
  lives). [2.5-verified]
- **Fixed: the puppet is hit.** NPCs have an `entColliderComponent`
  named `HitPhysicalQueryMesh`, the shape bullets and blows are tested
  against; V's TPP template has none, so the ganger's shots and swings
  passed through. Grafted from the donor too, its shots land: hits of
  type 10, 31.9 physical each, reach `DamageSystem.ProcessPipeline` for
  the puppet (measured, never applied there). [2.5-verified]
- Vanilla's inject-combat-target pattern: `GameObject.ChangeAttitudeToHostile`,
  `TargetTrackingExtension.InjectThreat`, then `SetThreatPersistence`
  with `AIThreatPersistenceSource.CommandInjectCombatTarget` (final.reds
  169949). `GetThreats(false)` still read 0 while the NPC fought the
  puppet: not a usable measure of who it fights; its `CombatTarget`
  behaviour argument is. [2.5-verified]

### 13.13 Calling a script static function from the plugin (2026-11-18)

A redscript class's **static** functions are neither in the class's
member list (`CClass::funcs`) nor its static list (`staticFuncs`): the
compiler registers them as **global** functions with a qualified, typed
name, e.g. `TandemPause::Frame;GameInstance`. RedLib's `CallStatic` and
`GetFunction(class, name)` therefore never find them; scan
`CRTTISystem::GetGlobalFunctions` for the qualified name (as
`plugin/src/main.cpp` does) and `Red::CallFunction` it. Its game-instance
parameter arrives empty from native code: the script takes the game from
the global `GetGameInstance()` instead. A RED4ext game-state update
(`RED4ext::v1::GameState`, running state) calls it every frame, also
while the world is paused (scripts' delayed callbacks stand still then).
[2.5-verified]

### 13.12 The car radio and the pocket radio (2026-11-18, 2.50 scripts read)

Read from the 2.50 scripts (`final.reds` line numbers), used by
`scripts/tandem/Radio.reds`; none of it has run in a session yet.

- **Stations** are the `ERadioStationList` index 0..13 (`final.reds`
  9476: AGGRO_INDUSTRIAL 0 … DOWNTEMPO 4 … IMPULSE_FM 13); their audio
  names (`radio_station_09_downtempo`…) come from
  `RadioStationDataProvider.GetStationName` (198877), or
  `GetStationNameByIndex(index, isUI)` (`station_none` for -1). The radio
  popup lists them in another order (`GetRadioStationUIIndex` /
  `GetRadioStationByUIIndex`, 198811/198844) and converts before sending.
  [2.5-untested]
- **The car's radio**, `VehicleObject` natives (87773–87839):
  `ToggleRadioReceiver(Bool)`, `SetRadioReceiverStation(Uint32)` (an
  `ERadioStationList` index: the pocket radio hands its own index to it,
  244342), `NextRadioReceiverStation()`, `SetRadioTier(Uint32, Bool)`,
  `IsRadioReceiverActive()`, `WasRadioReceiverPlaying()`,
  `GetCurrentRadioIndex() -> Uint32` (read back as that index, 244354),
  `GetRadioReceiverStationName()`, `GetRadioReceiverTrackName() -> CName`
  (a localisation key: the popup shows it with `SetLocalizedText`,
  392189). [2.5-untested]
- **Setting it the game's way**: a `VehicleRadioEvent { toggle;
setStation; station }` (215564) queued on the car reaches
  `VehicleComponent.OnVehicleRadioEvent` (93566): `toggle` turns the
  receiver on (or, already on, steps to the next station), `setStation`
  then sets `station`; both update the vehicle blackboard
  (`VehRadioState`, `VehRadioStationName`) and the HUD
  (`UIVehicleRadioEvent`). The popup's choice
  (`VehicleRadioPopupGameController.Activate`, 392196) goes through
  `QuickSlotsManager.SendRadioEvent(toggle, setStation, index)` (240038):
  `(true, true, station)` to tune, `(false, false, -1)` for off. The
  short press of the radio button is a `RadioToggleEvent` (93543).
  [2.5-untested]
- **No playback position**: nothing in the scripts reads or sets where a
  song is. `GetRadioStationCurrentTrackName(stationName)` (244526) names
  the current song of a station; `AudioSystem.RequestSongOnRadioStation(
stationName, songName)` (149608, no script caller) asks a station for a
  song, presumably from its start; whether it takes the track name the
  receiver reports, and whether it cuts in at once, is unknown. So two
  games can be on the same station but not at the same point of a song;
  tandem asks the passenger's station for the driver's song, which lines
  up when the driver's song has just begun. [2.5-untested]
- **The pocket radio** is `PlayerPuppet.GetPocketRadio()` (40614), a
  `PocketRadio` (244010) playing through audio switches on the player's
  `pocket_radio_emitter` (`radio_port_station`, `radio_port_on`,
  `radio_port_off`). Restrictions (`PocketRadioRestrictions`, 1002: scene
  tier, phone call, `VehicleBlockPocketRadio`…) go through
  `HandleRestriction(restriction, Bool)` (244196): while any is set it
  turns off; once all are lifted it waits for the status effect
  `BaseStatusEffect.PocketRadioRestrictionUnlockDelay` (244165), then
  plays its selected station again (or stays off if it was off).
  [2.5-untested]
- **Pocket radio and cars in vanilla**: only the **driver's seat**
  (`seat_front_left`) links the two (`PlayerPuppet.OnMountingEvent` /
  `OnUnmountingEvent`, 40460/40485): with the setting
  `/gameplay/radioport` `radioport_sync_to_car` on, getting in hands the
  pocket station to the car (`HandleVehicleMounted`, 244340) and getting
  out takes the car's back (`HandleVehicleUnmounted`, 244346). In a car
  with the setting off, the pocket radio ignores radio events
  (`ShouldIgnoreEvents`, 244077). Nothing in the scripts turns it off for
  a passenger seat, hence tandem's restriction. [2.5-untested]
- Volumes are each player's own settings (`/audio/volume`
  `CarRadioVolume`, `RadioportVolume`, read by the popup): not synced.
  [2.5-untested]

### 13.12 Attitude groups and a puppet's record traits (2026-11-18, scripts only)

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
  intruder's group (114508–114537). Whether the attitude system knows a
  group added by TweakXL (it may build its table from TweakDB at start) is
  what `attitudes: N of 4 puppet groups declared` and the read-back
  group in `player N: puppet in the G attitude group` will show.
  [2.5-untested]
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

### 13.12 Splitting a hit between the attacker's game and the target's (2026-11-18)

Read in the 2.50 scripts (`final.reds`), for NPC hits on a guest's puppet
(the host runs the attacker's side, the guest's game the target's) and
guests' skill XP. None of it run in game yet.

- The pipeline (`ProcessPipeline` 84290): `PreProcess` 84558
  (`ConvertDPSToHitDamage`, `CalculateDamageVariants`, quick exit,
  invulnerability, immortality, death check, `ModifyHitData`), then
  `Process` 84914 (returns at once on `DealNoDamage`, else the modifier
  stages and `DealDamages` 85771), `ProcessHitReaction`, `PostProcess`
  85801 (`ProcessStatusEffects` 85883). [2.5-untested]
- Attacker's side, for an NPC instigator: `ConvertDPSToHitDamage` 84575
  (divides by the weapon's `ProjectilesPerShot`; needs a weapon),
  `CalculateSourceModifiers` 86219 (bonus vs. target type, charge, pierce,
  ricochet, crit, damage multipliers, cyberware, stealth, spreading),
  `ProcessEffectiveRange` 86554 (weapon range falloff, explosion distance).
  `CalculateDamageVariants`, `ProcessLocalizedDamage`, `ProcessCriticalHit`
  and `ProcessQuickHackModifiers` only act for a player instigator.
  [2.5-untested]
- Target's side: `ProcessDamageReduction` 84618 (resistances per damage
  type, explosion/melee/DoT/boss/mech resistances), `ProcessDodge` 84781
  (`IsDodging` stat → no damage), `ProcessEvasion` 84795, `ProcessMitigation`
  84808, `ProcessPlayerIncomingDamageMultiplier` 84831 (target a
  `PlayerPuppet`), `ProcessPlayerFixedPercentageOverride` 85107 and
  `ProcessGrenadeExplosionDamageToPlayer` 85250 (difficulty),
  `CalculateTargetModifiers` 86213 (damage reductions, `ProcessArmor` 86621,
  on-vehicle mitigation), `ProcessBlockAndDeflect` 87114,
  `ProcessOneShotProtection` 85069 (per-hit cap by difficulty), and in
  `StatPoolsManager` overshield, armour pool, `CannotKillPlayer`, god-mode
  immortality and minimum health (`ApplyDamageSingle` 219067,
  `DrainStatPool` 219283). [2.5-untested]
- **No flag skips the attacker's side.** `DeterministicDamage` is read only
  by `ProcessCriticalHit` (86769), which already returns for a player
  target; `CannotModifyDamage` by `ProcessCriticalHit`,
  `ProcessOnVehicleMitigation` (86704: a target-side stage, wrongly skipped
  for a forwarded hit) and the no-op `ClearDamage` (52652). So tandem wraps
  the stages instead: on the host the target's stages return for a hit on a
  puppet; on the guest the three attacker stages return for a forwarded
  hit (`scripts/tandem/Combat.reds`). [2.5-untested]
- `ProcessArmor`, `ProcessOneShotProtection`, `ConvertDPSToHitDamage` and
  the melee block all need `attackData.GetWeapon()`; armour penetration
  comes from `GetArmorPenetrationValue(weapon, hitEvent)` 87006 (protected,
  wrappable). A forwarded hit carries the host's value. [2.5-untested]
- `CheckForQuickExit` 86416 drops a hit with no source, and turns a hit
  from an attacker friendly to the target into no damage unless the hit has
  `hitFlag.FriendlyFire` (86453; the flag is read nowhere else, and only
  removed for player instigators, 86413). [2.5-untested]
- The attack record's status effects are applied by `ProcessStatusEffects`
  from `GetAttackDefinition().GetRecord().StatusEffects()` (85930), and the
  target's application-rate rolls (`ProcessStatusEffectApplicationStats` 85983) use the instigator's stats: a hit rebuilt from the same record and
  the proxy as instigator brings them along. [2.5-untested]
- Skill XP: `DrainStatPool` (219283-219327) calls
  `RPGManager.AwardExperienceFromDamage(hitEvent, percentDrained)` 36969;
  it requires an active, XP-awarding (`AwardsExperience`: not crowd, not
  police) puppet target, a player instigator, and reads the weapon's item
  type for the skill. The request is queued per target entity and paid by
  `ProcessQueuedCombatExperience(entity)` (80726) when the player gets
  `TargetNeutraliziedEvent` (45336), sent by `ScriptedPuppet.RewardKiller`
  (21851) only to a `PlayerPuppet` killer; `FindAndRewardKiller` (12550)
  uses the damage history, else the `Kill` instigator. No other kill XP is
  given in scripts. [2.5-untested]

### 13.12 Combat state, detection and stealth for a guest (2026-11-18, scripts only)

Read in the 2.50 scripts (`final.reds` line numbers); none of it tried in
game yet. Used by `scripts/tandem/Stealth.reds`.

- **What puts V in combat.** `PlayerCombatController` (242633) keeps flags
  (crouch, `m_isTracked`, quickhack, Johnny) and `VerifyActivation`
  (242719) picks a state: tracked → `ActivateCombat` (242876: PSM
  blackboard `PlayerStateMachine.Combat` = 1, `AnimFeature_CombatState`,
  `FastTravelSystem.AddFastTravelLock(n"InCombat")`, combat chatter,
  `NotifyGameTone(n"EnterCombat")`, `HandleCombatMix`); crouched and not
  tracked → stealth (3); neither → out of combat (2, `LeaveCombat` tone,
  lock removed). `m_isTracked` is set only by
  `OnStartedBeingTrackedAsHostile` / `OnStoppedBeingTrackedAsHostile`
  (242808-242816), public, the event argument unused; `PlayerPuppet`
  forwards the native AI events `StartedBeingTrackedAsHostile` /
  `StoppedBeingTrackedAsHostile` (importonly, 315706-315709) to them
  (46292-46298). `PlayerPuppet.OnCombatStateChanged` (43634) listens to
  the PSM value and sets `m_inCombat` (`IsInCombat()`, 45377), the perk
  blackboard's `CombatStateTime`, `UpdateVisibility`.
  `AttackData.IsPlayerInCombat` (52593) reads the PSM value directly.
  Tandem's guest calls the two controller functions from an `@addMethod`
  on `PlayerPuppet` (the controller is the private `m_combatController`),
  with `null` for the event, and wraps the `PlayerPuppet` callbacks so a
  local "stopped" does not end a combat the host's NPCs still hold.
  [2.5-untested]
- **NPC side of "in combat with V".** `NPCPuppet.IsInCombatWithTarget(npc,
target)` (130026): high-level state Combat and the target tracker's
  threat for the target is `Hostile`. The host ORs it with
  `GetDetection >= 100` for `NpcEntry::detected`. [2.5-untested]
- **The stealth meter.** The HUD meter and the detection perks read the
  `UI_Stealth` blackboard (`highestDetectionOnPlayer`,
  `highestHostileDetectionOnPlayer`, 243024; `DetectionMeterEffector`
  300783 stacks an effect per `detectionStep`), written by the engine.
  The indicator over an NPC is its sense component's `StealthMappin`
  (`CreateSenseMappin`, 155542); `StealthMappinController.OnUpdate`
  (406522) takes the level from the native `GetDetectionProgress()`, not
  from script. `NPCPuppet.SetDetectionPercentage` (125823) writes
  `PuppetState.DetectionPercentage`, which some effectors compare
  (313954). Tandem writes the blackboard (scale assumed 0..100, as
  `GetDetection`) and each proxy's percentage, and creates the proxy's
  sense mappin while it notices us. To check in game: whether the engine
  overwrites the blackboard every frame, and what a proxy's mappin shows
  with its senses off (likely no progress: then the per-proxy meter needs
  native work). [2.5-untested]
- **What NPC senses see of V.** V's visible object is the
  `senseVisibleObject` component (ScriptedPuppet 19809;
  `GetVisibleObjectComponent`). `PlayerPuppet.SetInvisible` (43505)
  toggles it (`IsInvisible`). `UpdateVisibilityModifier` (40819) queues
  `VisibleObjectDetectionMultEvent{multiplier = Visibility stat}` on V;
  `UpdateSecondaryVisibilityOffset` (43599) queues
  `VisibleObjectSecondaryPositionEvent` with Z
  `player.stealth.crouchVisibilityZOffset` (0.6) crouched or
  `chestVisibilityZOffset` (1.3). Tandem queues the same events on a
  puppet and toggles its grafted `senseVisibleObjectComponent` (§13.11)
  for camo. [2.5-untested]
- **Optical camo.** Status effects tagged `CamoActiveOnPlayer` (269230)
  or `ActiveCamo` (44019). Their `AttachOpticalCamoVisionBlockerEffector`
  (249803, an `AttachCapsuleVisionBlockerEffector` 249727) registers a
  capsule vision blocker on the owner with the sense manager's
  `VisionBlockersRegistrar`. With the relic
  (`CanPlayerExitCombatWithOpticalCamo`, 44019-44056) in combat:
  `SetInvisible(true)`, each hostile threat's tracker
  `DeactivateThreat(V)`, `ExitCombatOnOpticalCamoActivatedEvent` later.
  Tandem applies the V's camo effect to its puppet (owner: the puppet) for
  the look and the blocker; whether a player camo effect runs on an NPC
  body is to check. [2.5-untested]
- **Stimuli.** `StimBroadcasterComponent.TriggerSingleBroadcast(contextOwner,
type, radius, investigateData, propagationChange)` (149948) is how V's
  shots are heard (`ShootEvents.OnEnter` 548102: `Gunshot` at 25 m indoors
  or `GetGunshotRange()`, `SilencedGunshot` + `IllegalAction` when
  silenced; muzzle brake: none). With no broadcaster on an entity,
  `StimBroadcasterComponentHelper.CreateStimEvent` +
  `ProcessSingleStimuliBroadcast` (150609) do the same. `gamedataStimType`
  values: `Explosion` 24, `Gunshot` 28, `SilencedGunshot` 49. Grenades
  broadcast from the projectile (`Explosion`, 127920), not from V.
  [2.5-untested]

### 13.14 Quickhacks between games (2026-11-18, scripts only)

Read in `final.reds`; none of it seen in a session yet.

- An NPC's upload is a `QuickHackUpload` stat pool **on the target**
  (`ScriptableDeviceAction.StartUpload` 28078, `QuickHackUploadListener`
  194694): its `OnStatPoolAdded` queues `UploadProgramProgressEvent`
  (`STARTED`, `EProgressBarType.UPLOAD`, `duration` = the action's
  activation time) on the target, and at 100 % `CompleteAction` then
  `COMPLETED`. `ScriptedPuppet.OnUploadProgressStateChanged` (24388) puts
  `AIQuickHackStatusEffect.BeingHacked` (and `BeingHackedWithTraceableHack`
  when `QuickHackableHelper.WillHackRevealPlayer`, 123133) on the target at
  `STARTED` and removes both at `COMPLETED`. One pool per target: two
  uploads by the vanilla path on one NPC would share it. [2.5-untested]
- The queue (`QuickHackableQueueHelper.PutActionInQuickhackQueue` 122261)
  and cost skipping (`CanSkipPayCost` 26943) work only for a `PlayerPuppet`
  executor; a puppet executor on the host gets none of it.
  `GetActivationTime` (26793) takes the executor's stats. [2.5-untested]
- A quickhack effect on an NPC (gameplay tag `Quickhack`,
  `NPCPuppet.OnStatusEffectApplied` 126476 → `OnQuickHackEffectApplied` 126593) alerts and traces only when `instigatorEntityID ==
GetPlayerID()` and the effect lacks the `Stealth` tag:
  `NonStealthQuickHackVictimEvent` (126787) → `RevealPlayerPositionIfNeeded`
  (126798: a `HackTargetEvent` with `AIQuickHack.HackRevealPosition`
  queued on the player, unless in combat or already revealed) or
  `NPCStatesComponent.AlertPuppet` (168014), then
  `AIActionHelper.TryChangingAttitudeToHostile` (169916). [2.5-untested]
- NPC hacks: `AISubActionQuickHack.Hack` (493744) queues a `HackTargetEvent`
  (195237: netrunner, target, action record, settings) on the target;
  `ScriptedPuppet.OnHackTargetEvent` (22119) runs it as an
  `AIQuickHackAction` with the netrunner as executor; `PlayerPuppet`
  (45832) adds the reveal state. Queued on our V with a proxy as
  `netrunnerID`, it should run the hack from the proxy. [2.5-untested]
- `ExecutePuppetActionEvent{actionID}` (201075) queued on an NPC runs a
  quickhack from the local player on it, by the normal path (22452). [2.5-untested]
- Spread (`SpreadInitEffector` 316185, `SpreadEffector` 316386) reads
  `GetPlayer(game)` at initialisation for stats, spread map
  (`HackingDataDef.AddItemToSpreadMap`) and the spread effect's
  instigator; a `SpreadEffector` with no map entry for its action does not
  spread. [2.5-untested]
- Vehicle quickhacks reach `VehicleComponentPS.OnVehicleOverrideForceBrakes`
  / `…Explode` / `…Accelerate` (96426-96451) and call
  `VehicleObject.ActivateNetrunnerQuickhack` (87829); the police hacker
  loop stops a player's car with `ForceBrakesUntilStoppedOrFor` (87873,
  194573). Whether `ActivateNetrunnerQuickhack(Accelerate)` acts on a car a
  player drives is not known. [2.5-untested]

### 13.15 Quest phases: making a node wait (2026-11-18, offline, spike S1)

Read in the 2.50 exe with `tools/re/decompile` (offline, no game). Feeds
`plugin/src/engine/quests.cpp`, the guest's quest freeze
(`coop-quests-design.md` §1.3).

- `QuestPhaseInstance::ExecuteNode` (hash 3227858325, `0x1402b3aa0`):
  returns 1 at once when the node is a disableable node in the phase's
  disabled set (`phase+0xC8`, by `node+0x40` id) or when the node's
  pre-check vfunc `+0x150(node, ctx+0x110, socket)` returns 1; otherwise
  notifies the context's listener (`ctx+0x70`, vfuncs `+0x1B8` before,
  `+0x1C0` after), runs the node (`0x14034fa80`: `ctx+0x110` vfunc `+0xE0`
  with the node id, clears the outputs, node vfunc `+0x158`), and maps the
  result through node vfunc `+0x160`; a context state byte `ctx+1 == 7`
  forces 3. [2.5-verified offline]
- Its only direct caller, `0x1402b393c`, runs one entry of
  `ExecuteGraph`'s work list (`0x140352984`, entered through
  `QuestPhaseInstance_ExecuteGraph` 779107884 = `0x1403528a4`, which
  pushes the phase on `ctx.phaseStack` at `+0xF8` around the call):
  result **0**: the node finished; a `questOutputNodeDefinition` (size
  0x58: socket name `+0x48`, type `+0x50`) ends the pass with its socket,
  any other node queues its output sockets; **2**: finished, outputs
  queued; **1 or 3**: nothing queued, the pass goes on with the other
  queued nodes. [2.5-verified offline]
- So a hook that returns **1 without calling the original** makes a node
  do what a disabled node does: nothing, no outputs, the phase stays
  active. The engine keeps no trace of that signal: tandem holds it
  (phase `NodePath` `+0x70`, node id, input socket CName) and gives it
  back at the end of the freeze with the root phase's `ExecutePath`
  (vfunc `+0x120`, under the quests system's lock `+0x60`, after
  `QuestsSystem::CreateContext` 3144298192 `(sys, ctx, 1, 0, -1, -1)`:
  Codeware's `QuestPhaseExecutor::ExecutePath`, used by its
  `OpenWorldSystem`). [I] — whether every node resumes cleanly that way
  is the in-game check.
- Per-phase filter: a phase is open-world when its resource
  (`phase+0x30` → `CResource.path` `+0x30`) or an ancestor's on
  `ctx.phaseStack` is one of Codeware's open-world phases
  (`QuestPhaseRegistry.cpp:12-20`), or its `NodePath` extends one of
  theirs; the root phase (empty path) always runs. [C][I]
- To check in game (`quest_freeze = on`, `tools/game.sh cmd quest freeze
on`, `quest report`, `quest freeze off`): refused-node counts by phase;
  no pending call rings and the tracker does not move while frozen; on
  `off`, the held signals resume (a call queued during the freeze rings);
  vendors, metro, apartment, car summon, NCPD hustles still work; no
  crash in the 1 s after `off`. [2.5-untested]

### 13.16 Story levers in the scripts (2026-11-18, read in final.reds, not run)

What `scripts/tandem/Story.reds`, `Phone.reds` and `Dialogue.reds` rely
on; every line is [2.5-untested] until `story-guest.sh` / `story-host.sh`
pass.

- A guest presents the host's call through its own
  `PhoneSystem.TriggerCall` (`final.reds:240618`, private: reached from an
  `@addMethod`), which writes `UI_ComDevice.PhoneCallInformation` and the
  `phonecall_*` fact; the fact is held back by a wrap of `SetPhoneFact`
  (`:240841`) while a mirrored call is up. The HUD's answer goes through
  `PickupPhoneRequest` → `OnPickupPhone` (`:240662`), wrapped to send it to
  the host. The incoming widget finds the contact by journal id
  (`GetIncomingContact`, `:409000`): a contact the guest's journal lacks
  shows no name.
- Subtitles: `UIGameData.ShowDialogLine` carries `array<scnDialogLineData>`,
  `HideDialogLine` `array<CRUID>` (`:268284`, `:268327`). Scripts cannot
  build a `CRUID`: `TandemStory.LineId` / `LineValue` convert it.
- Choice hubs: `UIInteractions.DialogChoiceHubs` (`DialogChoiceHubs{
choiceHubs: array<ListChoiceHubData>}`, `:243020`, `:389618`),
  `ActiveChoiceHubID`, `SelectedIndex`, `LastAttemptedChoice`
  (`InteractionAttemptedChoice`, `:199455`). The guest's stand-in hub has
  no native backing: its cursor moves on the input actions
  `ChoiceScrollUp` / `ChoiceScrollDown` / `ChoiceApply` (names from the
  input contexts, not seen in the scripts: to confirm).
- Holds: `GameplayRestriction.NoCombat` and `BaseStatusEffect.Invulnerable`
  status effects (`:128602`); script god mode does not protect V (§13.11).
- Saving: a `ScriptableSystem` overriding `IsSavingLocked` (`:48411`) and
  `SaveLocksManager.RequestSaveLockAdd(game, n"tandem")` (`:195179`, not
  saved: re-added after a load); `AutoSaveSystem.InvalidateAutoSaveRequests`
  (`:203134`). Whether the pause menu greys out Save with only these: to
  see.
- The co-op reward ledger is a `persistent` array on `TandemStorySystem`:
  it reaches the guest's save with its next save after the session.

### 13.17 The main menu and the pause menu, for a co-op entry (2026-11-18, read in final.reds, not run)

Line numbers are `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` (2.50).
All [2.5-untested]: written into `scripts/tandem/Coop.reds`, not yet seen
in game.

- Both menus are `gameuiMenuItemListGameController`s (`:275489`): a
  `ListController` (`m_menuListController`) fed `PauseMenuListItemData`
  (`:275579`: `label`, `eventName`, `action: PauseMenuAction`, `:7937`).
  `ShowActionsList` (`:275561`) clears the list, calls the subclass's
  private `PopulateMenuItemList`, appends Quit (`UI-Labels-CloseGame`) when
  `CanExitGame`, and refreshes. `PopulateMenuItemList` does not clear by
  itself (NCO 7b4c9531). A subclass of `PauseMenuListItemData` pushed into
  the list shows its `label` like the vanilla items.
- Main menu: `SingleplayerMenuGameController` (`:385861`):
  `PopulateMenuItemList` (`:386507`: Continue as `PauseMenuAction.QuickLoad`
  when `m_savesCount > 0`, New game `OnNewGame`, Load game `OnLoadGame`,
  Settings, Credits), `HandleMenuItemActivate` (`:386636`: QuickLoad →
  `LoadModdedSave(0)` or `LoadLastCheckpoint(false)`; other items spawn
  their `eventName` on `m_menuEventDispatcher` in the base, `:275520`),
  `OnGlobalRelease` (`:386533`: `back` → `OnBack`, the quit prompt),
  `ShowActionsList` (`:386483`: the save preview tooltip hangs on the
  list's index 0, `OnContinueButtonEnter`). NCO wrapped the same two
  methods with `ref<PauseMenuListItemData>` (facts only).
- Pause menu: `PauseMenuGameController` (`:340290`): `PopulateMenuItemList`
  (`:340378`), and its own `OnMenuItemActivated` (`:340451`, a switch on
  the action; it does not call `HandleMenuItemActivate`); Exit to main menu
  is `PauseMenuAction.ExitToMainMenu` → `GotoMainMenu`. Its back key is not
  handled in `OnGlobalRelease` (the menu scenario's `OnBack`): whether
  handling `back` there keeps the pause menu open is to see.
- Text input: the game has no script text field; Codeware's
  `GenericMessageNotification.ShowInput(controller, title, message, type)`
  is the game's message popup (`:273144`, `generic_fullscreen_message_notification`)
  with Codeware's `HubTextInput` in it, upper case; the token's
  `GenericMessageNotificationCloseData` carries `result` and Codeware's
  `input`. The vanilla benchmark results screen shows the same popup from a
  pre-game menu (`:328896`), so it should work in the main menu. To check:
  that typing a letter bound to `cancel` (C) does not close it.
- A menu's own clock: delayed callbacks stand still while the world is
  paused (§ Pause), and our system does not tick in the main menu; a
  looping `inkAnimProxy` (`loopInfinite`, `inkanimLoopType.Cycle`,
  `OnStartLoop` callback, the way Codeware's text input ticks) runs in
  menus whether the world is paused or not.
- The menu's ink space is 3840 × 2160 whatever the screen (NCO a90ef259):
  the status panel is placed top right with margins in that space; a
  full-screen interactive widget would take the menu's input (NCO d6240d9d),
  so the panel is never interactive.
