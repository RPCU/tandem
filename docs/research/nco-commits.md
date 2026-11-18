# Night City Online — facts mined from git commit bodies

Source: NCO repo (CyberpunkMP fork), 1310 commits 2024-11-21..2026-11-10; 1050 non-launcher/selector/release/deploy/merge commits read. Only facts, no code.

Tags: [2.5-verified] measured on game 2.5; [2.31-only] measured on 2.31 only; [broken-on-2.5] known to fail on 2.5; [unverified] built/compiled but not proven in game. Each section lists findings newest era first; within each era bullets carry the commit short hash.

Era note: NCO ran on 2.31 until early Sept 2026; the 2.5 port (port/2.5, test.NN builds) is in the c1 era. Treat anything not in c1 as 2.31 unless tagged otherwise.

## At a glance: what matters for a 2.5 co-op mod

- **Coverage.** Commit bodies say nothing about Proton or Wine. They never mention DynamicEntitySystem, and they give no vehicle vtable slots 0x280, 0x1F0 or 0x328. Those facts live only in NCO's source code, not in its commit history. The only vtable slot range named is ITransactionSystem 0x250+ (2.31, crashed on 2.5).
- **Address-library hashes.** The bodies give exactly one: **2582323196**, the function holding `exe+0x3E7863` on 2.5 (b88e1040). Everything else is raw offsets (section 11).
- **Patch history.** 2.5 shipped 22 Oct 2026. NCO ran on 2.31 until then. Every bullet outside the "2.5 port era (c1)" blocks is 2.31 knowledge.
- **Puppet movement on 2.5, final choice (0662a27a).** The C++ MultiMovementController mimic attached through the IdleController hook:
  - It gives the exact per-frame position and the engine's own locomotion animation, and it keeps NPC AI off the body.
  - AI steering (AIMoveToCommand / AITeleportCommand) trailed by 3–10 m (worst 17 m) and is only a per-machine fallback.
  - Transform-only writes and TeleportationFacility did not visibly move an NPC-record puppet.
  - The layout was re-measured statically on 2.5 (e3c5e455): IdleController vtable 19 slots, controller list at component+0xA8, AnimationDriver 0x30, AnimationData 0x3C.
- **Remote bodies.**
  - Records `Character.MaMuppet` / `Character.WaMuppet` inherit `Character.Panam`.
  - Strip the `Invulnerable`/`Immortal` tags and the 'Default' god mode, or no hit ever registers.
  - Make the puppet hostile per puppet for PvP targeting.
  - Run vanilla idle on the animation thread, or the body is invisible on 2.5.
- **Interpolation (0d1c86b0, c193e801).**
  - Per-remote render delay = max(base + jitter margin, transit + arrival gap + jitter margin), capped at 400 ms.
  - Base = 1.5 send intervals + 15 ms (65 ms at 30 Hz).
  - Jitter is RFC 3550, margin 2× jitter, capped at 100 ms.
  - The server sends 30/s, thinned to 7.5/s past 200 m.
- **Appearance on 2.5.**
  - CharacterCustomizationSystem is 0x388 bytes on 2.31 and 0x3D8 on 2.5. The raw +0x78 handle returns the template's default body on 2.5. `IsGame231Layout()` is a class-size fingerprint.
  - Read the live state through the `GetState` RTTI accessor instead.
  - Capture a new character's look only in the save that runs at `OnBeforeWorldDetach` (10263 bytes).
  - Re-saving a restored look corrupts it (6283 → 6571 bytes, female tokens added).
  - `ExecuteFunction` with a null out-slot silently skips any function that has a return value.
- **2.5 build set (5e749fa6, e797867b, f42065f8):**
  - RED4ext 1.31.0, redscript 0.5.32, TweakXL 1.12.0, input_loader 0.2.6, Codeware 1.21.2, ArchiveXL 1.28.6.
  - RED4ext.SDK `5246e659`, RedLib `a88797f`.
  - Script structs moved: ScriptDefinition 0x40, ScriptClass functions at 0x90, ScriptBundle classes at 0xD8.
  - `IComponent::owner` is at 0x50.
  - 2.5 added a native `VehicleSystem` class, so a custom class with that name kills the game.
  - Render device data must come from `GpuApi::GetDeviceData()`, not fixed offsets.
- **2.5 crashes.** The 2.31-decoded ITransactionSystem vtable crashed at `exe+0x887DC4`. The test.74 crash at `exe+0x3E7863` after the controller attached was never explained, and the controller ran crash-free afterwards on test.85 and test.86.
- **Every patch.** The HUD `prototype_hud.inkhud` override replaces the whole file, so it must be rebuilt from the patch's own file plus one entry (73 entries on 2.5).

## Supplement: facts from launcher/selector/release commits (excluded from the chunk pass, spot-read)

- af2a3661: the launcher compares the game exe's ProductVersion **exactly**, and on 2.5 it reads "2.5", not RED4ext's "2.50". Runtime check `kSupportedGameMinor` 31→5; the runtime reports 2.5.0. Known gaps at the v0.3.126 2.5 release: own look not visible, HUD/voice not loading, two new boot screens. [2.5-verified]
- d06f33ad: a 2.5 game with RED4ext 1.29.1 + ArchiveXL 1.26.0 died before the plugin DLL loaded (measured 2026-11-01). Framework pins must be upgraded together with the mod. [2.5-verified]
- 9788e383 (Steam): a Steam copy launched while Steam is closed boots Steam and relaunches itself **bare**. Custom command-line args (server address, Discord token) are lost, so the mod dialled 127.0.0.1. Check that steam.exe is running before launching.
- 6f6ed6b9: `AsyncSpawnFromExternal` of a CLI-authored `.inkwidget` hard-crashes the client a few seconds after spawn (2 crash logs, 2026-09-13, 2.31). Spawning the library `multiplayer_ui` widgets (chat/emotes) works. [2.31-only]
- a90ef259 (ink coordinates): the menu root is a fixed virtual space, 3840×2160 on both 1080p and 1440p monitors. `GetScreenSpacePosition` returns real display pixels, so clicks were 1.333× off on 1440p. Fix: read `/video/display` Resolution, divide its height by 1080, and convert at the handler. 1a4d06a8: a controller root lays out at about double the assumed resolution, so measure the root and scale.
- d22e0fe9: `ToString(inkActionName)` compiles but prints the type name, not the value. Match `e.IsAction()` against a list of candidate names; ESC arrives as 'back'/'cancel'.
- 534ca258: redscript `FTLog` output is not collectable at runtime: the redscript log is scc's compile log, and `LogChannel` needs CET. Route logs through a native `ScriptLog` into the plugin's own log.
- 1a4d06a8 (archives): an inkImage binds an **inkatlas**, never a raw xbm. NCO repointed a stock loading-screen atlas from basegame_4_gamedata. The extra content ships as a separate `zz_*.archive`, which ArchiveXL loads, instead of repacking the main archive.

## 1. Networking stack & protocol

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 1. Networking stack & protocol)_

- 2ccb7085: old interpolation delay was session-wide `50 + 1500/update_rate` ms; one 170 ms-ping player starved everyone's view of him. Replaced with per-remote jitter margin: RFC 3550 smoothed transit variation measured at sample ingestion (frame-to-frame change only, so client/server clock offset cancels), margin = 2x jitter capped 200 ms. Client-only, no server ping. [unverified at the time]
- c193e801: measured remote players drawn 50 + 1.5 send intervals = 100 ms behind at server 30 Hz, + up to 200 ms jitter margin => 150-350 ms with network = 1-2.5 m at sprint. New: base = 1.5 send intervals + 15 ms (= 65 ms at 30 Hz), jitter margin cap 100 ms; 10 s log of avg delay, jitter, dry-buffer frames. [2.5-verified]
- 0d1c86b0: final formula per remote: `render = now - max(base + jitterMargin, transit + arrivalGap + jitterMargin)`, capped 400 ms. test.91: transit 20-30 ms + gap 33 ms (close players unchanged). Server thins updates past 200 m to 7.5/s; test.91 logged 236 dry frames/10 s at 8/s vs buffer sized for 30/s. Slow links (~100 ms) would have extrapolated every frame under 65 ms base. [2.5-verified measurements]
- Server send rate to clients: 30/s nominal (c193e801); distance thinning to 7.5/s past 200 m (0d1c86b0).
- 5487d380: interpolator is a flecs system created in `OnConnected` and destroyed by every world-detach stand-down. Once CONNECT moved to the main menu (c4085596), loading into the world detached the menu scene and killed it => nobody saw anyone move (test.74/75). Fix: restart in `NetworkWorldSystem::OnWorldAttached` when connected; `OnConnected` must be re-entrant. [2.5-verified]
- 9b154816: blackboard listeners fire only on CHANGE; connected value was already true before the HUD controller existed => call `OnConnectedToServer(true)` directly after EnterWorld.
- b613591a: decide "hold spawn vs spawn now" at the moment of the CONNECT press (`CaptureSpawnIntent`), not in the auth reply ~1 s later (world could finish loading in between). Selector drew its first card 7 s after the spawn otherwise.
- c4085596: on 2.5 the main menu stands on a live world with a player object, so "player exists" no longer means "in world"; menu connects use `ConnectFromMenu()` (never spawns on auth). [2.5-verified]
- 9878e1d5: LeaveWorld (server destroys puppet, connection kept) must re-arm a held spawn; movement must stop until a spawn answers (~770 "Attempt to move invalid entity" otherwise).
- 46916a05/5cd8c70b: server held Puppet from spawn until disconnect; select/delete/appearance were refused on `Puppet.is_alive()`. Added `LeaveWorldRequest` (client.proto oneof 22): save position, `Level::Remove` (unload broadcast), destruct, clear handle. Marked do-not-undo (looped ~5 times).
- a5fd7363: the netpack proto generator emits C++ in FILE ORDER — a message must be declared above anything using it (MSVC C2065 / GCC "not declared"). Any proto change changes the protocol hash => mismatched clients refused at handshake ("flag day"); symptom is a menu button that seems to do nothing.
- 5fe41827: `CharacterSummary` gained `repeated Attribute attributes = 8`; 074d91de added `is_male` (flag day).
- 175a7f6a: `Client::~Client` calling `Close()` invoked virtual OnDisconnected in a base dtor => `_purecall` SIGABRT on every exit while connected (test.68); also cancel the pending libuv request before freeing the loop.
- 214c7e9a: server fire-rate limiter used a 40 ms floor on ARRIVAL time; auto fire arrives batched in one flush (0 ms apart) and was refused. Replaced with GCRA leaky bucket, sustained 1500 RPM, burst ~12 shots (`WeaponComponent.ShotTatMs`).
- Server stack facts: .NET loader hosts native tick loop (Program.cs, ServerAPI::Exit); ECS = flecs v4.0.3 with REST addon on by default (06c82f02). SIGTERM unhandled => .NET exited on its own thread destroying GameServer while the tick loop ran (219eee3c). flecs `EcsRest` dtor `ecs_os_free(ipaddr)` + C++ `set()` shallow copy of a `std::string::c_str()` => `free(): invalid pointer`, exit 139 on every stop; fix `ecs_os_strdup` (06c82f02/d9551034). SIGABRT handler only (SIGSEGV owned by .NET runtime for managed null refs) (a289f888).
- Persisted data: `config/players.json` (all characters) + world/respawn/startpoint/vehicle state (83bb762d). Server autosave every 90 s (04c90b06).

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 1. Networking stack & protocol)_

- 91b66e15: the transport is **GameNetworkingSockets** (connection-oriented, encrypted per connection). Only **4 messages are unreliable**: MoveEntityRequest/NotifyEntityMove and VoiceFrameRequest/NotifyVoiceFrame. Everything that changes persistent state is kReliable and ordered. There is no session epoch. `AuthorityComponent::Epoch` only orders vehicle hand-offs: player puppets never change owner. All 16 mutating handlers resolve through GetByConnectionId and stop when it is missing. A Verify check stops any new unreliable mutating message. [2.31-only, protocol design]
- 04fee2f0: movement `tick` is **wall-clock ms since the Unix epoch** (~1.787e12, NetworkWorldSystem::GetTick). It is globally monotonic and never resets on respawn or reconnect. The server now drops a packet with tick <= the last one (the client already did this, InterpolationSystem.cpp:721). If the client clock goes back more than **30 s**, it is treated as a clock reset and the baseline is set again. A fresh component has Tick 0 and accepts anything. Bug found while doing this: ReplicatedSequence/ReplicationPending were not carried across the component replace, so the LOD counter was reset on every packet (`seq % 4` was always true). 20 tests. [unverified live]
- d6c58c81: one movement packet walked every player with a distance test (flecs observer on OnSet). 1000 pkt/s × 32 players ≈ **32,000 relevance checks/s**. Fix (behind a flag, **OFF by default**): store the newest state, set ReplicationPending, and replicate once per server tick at the **UpdateRate{30} = 30 Hz** already in the config (it was sent to clients but unused). Sequence is split into received vs ReplicatedSequence; the wire and the LOD (`% 4`, `% 16`) use ReplicatedSequence. Measured cost: 1000 pps drops from 31,000 checks to ~930; 10,000 pps costs the same as 30. The coalescing was never tested with two clients (listed as a blocker in 032ca69d/829d239b). [unverified]
- 666eac9e: voice frames are capped at 1 KB. Inbound rate is capped at **100 frames/s** (real Opus: 20 ms frames ≈ 50/s, ~200 B each). Worst case: 100 × 31 recipients = 3,100 relays/s ≈ 620 KB/s, and 3.1 MB/s at the 1 KB cap. The voice radius is decided by the server from the client's intent.
- d342fdc6: **the cell grid culls nothing.** kCellSize = 6000 and kLoadRadius = 3 (42,000 units per side). 11 stored live positions span x −1759.7..672.8, y −1956.5..−1261.0, z 27.9..69.7 and fall in 2 cells. So everyone receives everyone, and bandwidth grows with N². Earlier, a cell-size bug dropped loads entirely ("nobody could see anybody" for 6 days). The client works the cell out again and drops loads that disagree, so changing the size is a flag day.
- 3b05ab9b: server ids are one flat namespace; `GetEntityByServerId` = `flecs::entity(*this, id)` (server id IS the flecs entity id, no kind tag). A "movement for id N but no puppet" warning was always a VEHICLE whose movement arrived before its spawn ("OnVehicleReady: mounting queued character ... into vehicle N").
- 5159dc45: RPC table bug. HandleRpcDefinitions sized the table by the definition count and indexed it by id, so any id >= count was an out-of-bounds heap write. The Call() guard `if (Klass != 0 && !pContext)` let a null pContext through. Fixed: size = highest id + 1, null check always. RPC ids are negotiated per connection, so adding a ClientRpc pair is not a flag day (3e2c06c6).
- Protocol identifiers (netpack hashes the .proto text): client.proto `0xc67c52a1b6c5f096`; server.proto `0xa14513f4653e80f`, then `0xb5c99e27f949fe83` after adding CharacterSummary.lifepath=7 (dab55bea, flag day). The hash covers the .proto text, so any field change is a flag day (17d6d0a9).
- b6fc19c8: netpack enum codegen never worked. The generator segfaulted (exit 139: TYPE_ENUM used message_type(), which is null), and a repeated enum cast to Vector<>. netpack does **not** check enum ranges (`static_cast<Enum>(9999)` round-trips), so handlers must switch on the known values. `value < COUNT` is not a check.
- 99895894/f75ce65d: each text rewrote the whole message store. It now writes at most once per 2 s (PlayerStore positions: 30 s). Chat flood limit: sliding window of **20 messages / 5 s**; 10/5 s caught fast typing at 2.5/s.
- b7468a27: RequestLedger idempotency: TTL 5 min, 64 per owner, 4096 global, recorded on success only.
- 80bc5b56: upstream ServerListSystem was hard-coded to `https://cyberpunk.skyrim-together.com` (no DNS any more), and it called `GServer->Kill()` on a 403, which was a remote kill switch. Now an empty config entry by default.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 1. Networking stack & protocol)_

- f087d2ff/ec2858d5 (08-30): "nobody could see anybody". Since f66a40a (08-24) the client re-derives each load's cell from its position and drops loads that disagree with the server ("[World] dropped map-invalid character load 38654705884", "[VehicleSystem] dropped map-invalid vehicle load 229"). Two server faults: Level binned with sCellSize = 6000 while GameServer advertised 60000 (10x); server ToCell used float→int cast (truncate toward 0) while client uses std::floor — disagree for negative coords (x = −1769: server cell 0, client −1; Night City is mostly negative). Truncation also made the two cells around origin double width. Fix server-side: floor, advertise Level's constants. Movement/appearance (separate messages) still arrived so the player list showed and /tp worked, but NetworkWorldSystem::Spawn was never called in 21 session logs. Interest radius = 3 cells. [protocol, version-independent]
- 9f553a5c/f2d2a55f (09-03): client world-contract check had a third literal cell_size == 60000 (kCellSize = 60*100 = 6000) → every connection refused since 08-30 ("incompatible world contract (world 'night-city', coordinate 1, cell 6000, radius 3)"). Fix: strictly check only world_id and coordinate_version; grid numbers sanity-bounded (0 → div-by-zero) and adopted from server. Lesson: never duplicate grid constants; InterpolationSystem, VehicleSystem, NetworkWorldSystem read settings.get_cell_size().
- 68d34b42: protocol kIdentifier = FNV1a64 of a string enumerating every struct+field → any protocol change refuses every un-rebuilt client (no backward-compatible change possible; every change is a "flag day"). NotifyMoney carries int32 while record is int64_t.
- 9cdb62e8/a0346ced: codegen (netpack) now emits kXPresenceBits per generated struct (39 structs). Presence bitfield width = number of optional fields; reader with different width misreads all later fields. SpawnCharacterResponse presence = 4 bits (author wrongly said 6 three times).
- Wire IDs: server NotifyCall = 33; client CallRequest = 20, CallControlRequest = 21 (1d5aec2a). CreateCharacterRequest removed, field 19 deliberately never reused (6ac90d13). Flag day 9af9e8d2: CharacterSummary +slot,+is_active; AuthenticationResponse +character_slots; SelectCharacterRequest; DeleteCharacterRequest +slot (falls back to active slot if absent).
- c0ca2c7f: voice = proximity chat, server loop runs 50 frames/s per speaker over all online players; a connected phone call routes frames regardless of distance (resolved once per frame, not per listener).
- 1115f5b5/0ca11ba2: voice dead because launcher (Chromium enumerateDevices) passed sentinel id "communications" as --voicein (only "default" filtered); mod treated it as WASAPI endpoint id ({0.0.1.00000000}.{guid}), GetDevice failed, capture branch has no fallback (output branch does). StartCapture returns at thread spawn so errors were invisible; stats line every 10 s: "[Voice] mic NOT CAPTURING / speakers ok - encoded 0, sent 0, received 0, decoded 0". Standalone WASAPI test: 48 kHz, 10 ch, works. Fix: both sentinels = "follow Windows default". [worked]
- 10a68b6a (08-30, subject only): netlab adaptive-delay (interpolation delay) strategy starved vehicles — needed a 2-period margin, not 1. [unverified detail]
- 3cde2718 / ce498b1a: THE crash = data race in flecs (ECS): GetServerIdByEntity ran a flecs query from the game's job system — 16 distinct threads in one session, none the one running progress(); dump shows two threads at the same instruction in the same ms holding the same ecs_stack_t* with different cursors. OnVehicleEnter was a second off-thread caller. Rule: never touch the ECS world off the main/progress thread. Disconnect-in-a-car repro left open. 77971ee9: after race fixes no crash firing a weapon or mounting a bike; crash while riding was a GPU dump ("Gpu Crash for unknown reasons", gpuApiDX12Error.cpp), no AV, no flecs signature. [2.31, race is engine-independent]

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 1. Networking stack & protocol)_

- afad4cc9: remote players froze at spawn point. Clock = ms since epoch as uint64 (~1.787e12). `float tick = GetTick() - 100ms delay` loses everything below 131072 ms (float 24-bit mantissa) → delay vanished, buffer drained every frame, extrapolation delta 0 → puppet never moved. Fix: absolute ticks int64, only differences to float (render time, drain compare, extrapolation window, segment ratio, LastRenderTick). Simulation delay = 100 ms; ~30 packets/s; clocks agreed within 20 ms. [version-independent]
- b2507a2b: MovementComponent::Tick uninitialised → Tick 0 replicated for players who hadn't moved → buffer anchored 57 years in the past, remote never moved. Fix: drop tick 0 client-side + server initialisers. [version-independent]
- e15f6f5b: tools/netlab (Python, replays movement traces through interpolation candidates). Profile "rimtek" = 85 ms one-way, 30 ms jitter, 1 % loss: baseline vehicle path starved 75 % of frames, 162 teleport-pops per 1-minute drive; `vehicle_dr` (dead reckoning through starvation + projective blend on recovery) → 0 pops, 0.03 % starvation, lower error, identical on clean links. Client dev flag `-sync-trace` writes NDJSON per received sample/applied pose. 7f0d386a: banked real drive traces replayed; vehicle_dr wins on real data too. [unverified in game]
- 2026-08-24 commits with empty bodies: f60c8e16 "stabilize vehicle dead reckoning", 45cc81ca "smooth player snapshot recovery", a332f90d/09065676 map-aware snapshot metadata, 14c11a66 netlab vehicle authority handoffs, 846f639c netlab corrections, f66a40a4 "enforce map contract and cell interest sync". (No details.)
- 6336bd5d: interest management throttles by distance: >200 m → every 4th update, >600 m → every 16th. Mounted passengers send no movement (puppet parented to car) so their server position froze at mount point; fix measures from the vehicle in AttachmentComponent. [version-independent]
- 2cdbde70: Discord verify (up to 2 blocking HTTPS calls × 5 s timeout) ran on the game thread and froze everyone's relay on each cold join; moved to a detached worker posting back via task queue. [version-independent]
- 54d9a856: netpack (their message generator) prefixes each message with a presence bitfield sized to its optional fields; SpawnCharacterResponse = 6 bits; adding 2 fields → 8 bits → misaligns every later field on non-rebuilt clients. Workaround: reuse an existing message (NotifyAppearanceUpdate). Repeated fields are `set_X(vector)`, no `add_X` (a0ac01dd, 18a6d7ae). [version-independent]
- 9b7b9923: UpdateAppearanceRequest = client tag 10, NotifyAppearanceUpdate = server tag 22. Requests carry no player id (identity from connection).
- 048554dc: handshake AuthenticationRequest carries build_stamp, manifest_version, install_digest, unmanaged findings, password; denial code + reason; one-byte kRefused before kick; client drains pending messages before acting on close (same-tick race ate denial reasons). Any new message changes handshake hash (kIdentifiers) → flag day.
- 4358b8af voice: Opus 48 kHz; capture thread encodes, game thread sends (transport not safe from arbitrary thread), network thread queues, render thread decodes/mixes. Server relays undecoded, radius by mode: whisper 6 m, local 25 m, yell 60 m. 120 ms pre-roll. 574eab9c: WASAPI eRender/eCommunications often unset → fall back to eConsole.
- 5804d946: AuditLog::Open locked a non-recursive mutex then called Record (locks again) → deadlock on every Linux boot; Windows let it slide.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 1. Networking stack & protocol)_

- Stack (from build commits): GameNetworkingSockets (gns) + protobuf-generated "netpack" serializers (`.proto` → `.gen.cpp/.gen.h`), flecs ECS on server, C# Server.Loader/plugins (net8.0) with CppSharp-generated SDK bindings. [2.31-only, engine-independent]
- **Protocol identifier = hash of the generated proto surface**; any `.proto` change bumps it and old clients are refused at auth (handshake shows two raw hashes). Outage 2bbd078a: server rebuilt, client never `xmake install`ed → everyone refused. Ship gates added (8be3c694: FullInstall DLL hash must equal release DLL; 70d5a954: carry artifacts only from `releases/latest`). [engine-independent]
- netpack generator emits in file order → a message must be defined before the one using it (beea6208). Adding a field to a `oneof` message: keep retired messages in proto so oneof numbering holds (eaeaa792).
- d8e79f72: stale `.gen.cpp` linked under fresh headers (7 vs 6 optional fields) → client wrote 7-bit presence field, server read 6 → one extra 0 bit **doubled every entity id in the following varint (215→430)** while protocol id matched. Fix: always regenerate; compare generated files against the .obj mtime. [build gotcha]
- 124ce33b: **UpdateRate 10 → 30 Hz** (movement send rate; also feeds client simulation delay). Interpolation rewritten: hold last sample behind render time, constant-speed lerp to next; on starvation **extrapolate along last heading up to 250 ms**. Old code overwrote PreviousFrame each frame (exponential chase = "teleporting"). [2.31-verified]
- 606c8742 interest management: old 4-player cap = 360 sends/s; 16 players = 7,200/s (drivers send 2 per tick: vehicle + character). Mounted entities (AttachedComponent) movement no longer relayed (client discarded it anyway; server still records position for chat range/jail). **Distance tiers: full rate ≤200 m, every 4th update ≤600 m, every 16th beyond**, chosen from a per-entity counter (same for every recipient); counter must be carried forward since component is replaced per packet. [2.31-verified]
- 3615fadd ingress validation: drop movement with non-finite/absurd pos/rot/speed (NaN from `eulerAngles()` on degenerate quat; NaN disables all distance checks and persists to store). Spawns refused if appearance blob/equipment absurdly large — **real CC blobs 6–9 KB**. Chat capped 512 chars, control chars stripped.
- eaeaa792 authority: authority = entity parentage; added `NotifyAuthorityRevoked`/`NotifyAuthorityAssigned`; **MoveEntityRequest carries an authority epoch**, server drops stale-epoch packets silently (movement is unreliable/unordered across handoff). `NotifyVehicleControlAssigned` subsumed.
- 1b20a407: spawns arriving before world attach were silently dropped (`!m_ready`) → queue and replay on OnWorldAttached; unload cancels queued spawn; spawn for an existing id replaces the puppet (reconnect re-announce). 0d3a747e/3b9f4271: dead session id kept after disconnect with world attached → streamed at the new session = invisible player; cleared.
- 4f0f0394: every on-foot movement update carries sender's **PlayerStateMachine blackboard Locomotion + UpperBody states**; relayed on MovementComponent, carried per-sample. Reason: speed sampled at 30 Hz measures 5–6 m/s while sprinting → jog forever. [2.31-verified]
- 653d2819 world state: server-owned persistent clock + weather, `NotifyWorldState` on join, change, **every 30 s**; clients apply via TimeSystem + Codeware weather setter, re-assert every 30 s; weather on wire as CName hash of worldWeatherState name, 0 = natural. e58d3b9a: forced weather is sticky engine-side; going to 0 must call weather release (`forceRestore`, blended) once. d88fccde: `/time real` = wall clock 1:1.
- 9088176b: time dilation/slow-mo blocked while connected (TimeDilationHelper two entry points + `PlayerPuppet.SetSlowMo`); time skip blocked at `TimeskipGameController.Apply`. `IsMultiplayer()` native exists and gates 17 script files (damage replication, combat state, carried objects, equipment, grenades, muppet record) — deliberately NOT flipped. Suspected cause of weapon-wheel-as-passenger crash (19 Aug). f32cc32c: time dilation calls with a listener must pass through at 1.0 (returning false hangs the caller).
- cdd5b455: measured direct WireGuard (tailnet) RTT 73 ms for the remote host leg. Earlier "node-to-node" era: position sync worked on same interpolation math; animation sync never worked; driving first worked after the server-hosted swap.
- Reliable messages are ordered (ca0f5f55 relies on it).
- 2024 upstream: 5c3ffc4e flat vector for RPC lookup (ids sequential); df30e3d9 RPC registration idempotent so plugin hot-reload keeps ids sent to clients.

## 2. Spawning remote puppets

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 2. Spawning remote puppets)_

- 832f3eb1: remote body record = `Character.MaMuppet` / `Character.WaMuppet`, both inheriting `Character.Panam` (her full combat AI). `--hackable-puppets` makes it hostile to the local player so the brain joins fights. Entities come from `mods\cyberpunkmp\*.ent` inside CyberpunkMP.archive (070e9c33). Experiments also used MaJackie, WaPanam puppet records (9079c107).
- 832f3eb1: bug — spawn passed 3 of SpawningComponent's 4 fields, so `usesDriver` landed in `AuthorityEpoch` and UsesDriver was false for every puppet.
- 162aca8c: flecs gotcha — after `emplace<EntityComponent>`/`add<InterpolationComponent>` the entity can move archetype, so a reference to another component (`aSpawning`) is stale; copy fields to locals first.
- 35edc677: DriverComponent arrives via deferred command one frame after EntityComponent.
- 5487d380: previous body survived a reconnect as a "ghost" (puppet spawned 5 s before connection ended); DeSpawn now logs outcome.
- 9079c107: MaMuppet/WaMuppet inherited Panam's `Invulnerable`/`Immortal` tags; NPC god mode is granted from record tags via `NPCManager.SetNPCImmortalityMode` (source `'Default'`); invulnerable target => every hit `DealNoDamage` shooter-side => no combat event to server. Fix: tweak `tags -= ["Invulnerable","Immortal"]` on MaMuppet/WaMuppet/MaJackie/WaPanam + script strips 'Default' god mode and re-checks after 1 s (NPCManager init can land after OnEntityAttached).
- 0662a27a/832f3eb1: stopping the puppet's AI — on 2.31 the controller swap (MultiMovementController attach) kept the Panam brain off the body; when 74de8dfd removed it on 2.5, the AI walked bodies on its own (e.g. once the local player fought police). Restoring MMC on 2.5 again keeps AI off.
- 1a737738: brand-new characters spawn in the base-game `q000` holding box; server relocates to an arrivals point, gated on SpawnedBefore (RelocatedAwaitingRecord flag).
- c05b8ba6: two world-template saves `MultiplayerStart` (female) and `MultiplayerStartMale`; a male look on the female template changes nothing on screen, so body type must be chosen before world load. [2.5-verified]

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 2. Spawning remote puppets)_

- f60fad5f: start point (−5.2, 20.9, 0.0) is under the terrain, so new characters fell through. Start points within a few metres of the origin are now refused, both when setting and when getting.
- 9c42677c (refers to 8401ec3): "box" bug. The arrival teleport lands before the destination's collision is ready. The player falls through, and the game's own out-of-bounds recovery puts them in a holding volume and loops. `q000_spwn_start` is the template's holding room by design. Also seen: 21 C++ exceptions at one address during world attach, and "no customization state to save". [2.31]
- 9d4daeaa: OwnSave used to load "the newest save that is not MultiplayerStart", which picked a stray AutoSave. Now it always loads the template **by name** (renamed MpLoadOwnCharacterSave → MpLoadMultiplayerWorld), through LoadLastCheckpoint. The identity comes from the server. Side effect: every client starts as the template's "Phantom Veronica" until the appearance restore lands. "[MONEY] 4 restore: first spawn, stripped 121694 template eddies" proves the template world was loaded.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 2. Spawning remote puppets)_

- 2356232f (08-28, experiment, off unless -mod-local-puppet / flag file mod-local-puppet.on (148e4075)): remote puppets are built record-first: `record = isBodyGenderMale ? puppetRecordMale : puppetRecordFemale` then CreatePuppet → AddEntity(ccstate) → ApplyAppearance; never touches CharacterCustomizationSystem, which is why it works in gameplay. CreateEntity returns an id long before the entity exists — must wait. Tested PlayerSystem.LocalPlayerControlExistingObject(entityID) to make a puppet the LOCAL player (542 game script call sites ask who the local player is: 278 GetLocalPlayerControlledGameObject, 264 GetLocalPlayerMainGameObject). Result: approach RULED OUT by owner 08-30 (db9eff8a) — not pursued. [2.31-only]
- c79eab62: "invisible players" was never the puppet system — it was the grid bug (see §1).
- 681e3af8 (subject): /npc refuses placeholder-paste records.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 2. Spawning remote puppets)_

- 6f431c54 / 67b53546 / 392b28f2: spawn records are Character.MaMuppet / WaMuppet; they inherit from Character.Panam (via Character.TPP_Player chain), NOT Character.Muppet. Inherited Panam display name/affiliation (blanked 3 display-name fields → game then invents random per-client generic NPC name). Entity template player_ma_tpp_cutscene.ent has 16 gameTargetingComponents + weakspot, stats, scanning components (confirmed by WolvenKit CLI CR2W conversion). [2.31-only]
- 98b12fa2 / 03afef8a / cc0629c2 / c06b65cc (flecs v4.0.3): every component with non-trivial members must be explicitly registered (`world.component<T>()`) before first use, else flecs registers as POD → memcpy on archetype move, double free. Components must be default-constructible (gsl::not_null member → ecs_ctor_illegal at Level::Add on join). Don't mutate (add component) inside an OnAdd observer — add InterpolationComponent explicitly where EntityComponent is created. [version-independent]
- 407c0538 / 662e054d / cec1343d / a9fbcaf2: `.run()` systems — `while(it.next()){}` hung the client (no-component systems never return false) → REVERTED cec1343d; `it.fini()` segfaulted the server (exit 139, production down) → REVERTED a9fbcaf2: flecs finalises run() iterators itself; fini = double restore. [version-independent]
- 559828f6 / 0da3c9b1 / 0fa2bb94: ROOT CAUSE of 27-Aug crash loop: game hooks (hit hook, OnVehicleEnter called from redscript) arrive on engine JOB threads (16 distinct threads seen); flecs query from them races the stage stack allocator (flecs_stack_restore_cursor, two threads same RCX). Fix: lock-free EntityID→serverId table (ServerIdRegistry) written on main thread, read anywhere; never touch flecs off-thread. Redscript-called RTTI methods are NOT guaranteed main thread. [version-independent, engine threading applies to 2.5 too]
- 58c80f04: removed 0xDD poison + per-frame thread census; kept flecs on mimalloc and FlecsThread::Assert tripwires keyed (call site, thread).
- 01f92ab6: no entity-readiness gate on the puppet path; readiness primitive = Codeware's status byte at entity +0x156 (2.31). Pattern: OnVehicleReady + pending queue. [2.31-only offset]
- d2e9ba53: crash-on-quit: on OnBeforeWorldDetach first stand down (clear PuppetRegistry so anim hook goes silent, destroy interpolator, drop vehicle state), then forward. [2.31]
- 82c0ab9c: receivers parsed body gender from blob with silent fallback to male → female shown as male; NotifyCharacterLoad now carries is_male from server record.
- 1ad3bdfd: server log "Puppet da spawned" (ids hex), possessions 130 stacks.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 2. Spawning remote puppets)_

- Upstream remote players built from **`Character.MaMuppet` / `Character.WaMuppet`** (muppet/mannequin records) which **inherit from `Character.Panam`** → inherited her name, loadout, scanner data (PANAM PALMER, Aldecaldos). Fix: tweak blanking her name on muppet records (88dc8269, shipped f058cc48). Nameplate reads the character record before entity display name. Upstream hardcoded remote display name "Test". [2.31-only]
- Muppet records are mannequins: no health/hit detection, cannot be aimed at (88d58885). Puppet record made a launch flag (`-puppet-record`, `-puppet-driver-all`).
- 9e640e36: remote puppet entity template is **`player_ma_tpp_cutscene.ent`** (PLAYER template, customises itself from local player → everyone looks like you).
- 1845cfd3: switched remote players to **`Character.Player_Puppet_Base`** (full moveset, body gender from CC state). bb738cad/d3587258 verdict: identity works (real faces/bodies) but player records never enter the NPC idle-controller pipeline → frozen statues, clothes as painted fragments → **reverted to mannequins as baseline**; real-rig work behind flag. 0a6f2912: `walk_0` length -1 on Player_Puppet_Base — player graph ships no NPC locomotion clips. [2.31-only]
- 7684bc4a `/dummy`: server fabricates entity with MovementComponent → broadcasts same NotifyCharacterLoad → reproduces client spawn path solo. 9088176b: dummy now spawns 5 m away, walks a circle, wears summoner's appearance.
- 38a06bea spawn-crash bisect: crash at AddEntity's first log line; suspects `make_alive()` with server ids below configured entity range, and unsynchronised maps. a5f60d4f: three appearance maps (equipment, ccstate, names) written on spawn path and read from redscript callbacks on other threads → one mutex, copy-out; `find` not `operator[]`; FNV-1a fingerprint of ccstate+equipment at AddEntity and ApplyAppearance to locate "players render as each other" bug (suspect `ScheduleSynchronizedAppearanceChanges`).
- 9732c461 (first working 2.31 snapshot): PuppetRegistry hook walked heap DynArray off-thread → mutex set; dd9c0744: then a **fixed array of atomics with high-water mark** (lock in animation-thread hook fired for every animated entity from several worker threads = frame-rate cost).
- 1845cfd3: **use-after-free**: controller re-attach queued from animation thread captured raw component pointers, deref'd later on main thread; vehicle mounts rebuild those components → crash seconds after remote car materialised. Fix: only entity id crosses threads, re-resolve live. Ported to main 3b9f4271.
- c84e82d9: server-declared NPCs: `NotifyCharacterLoad` gained optional `puppet_record` (absent for players; specific record e.g. bartender/Judy for NPCs); persisted config/npcs.json. Standing only, no AI.
- 3a6f22f7 PuppetDriver: idle-controller hijack only runs for NPC records and stops when a mount rebuilds components. Driver = direct placed-transform writes (what Codeware SetWorldTransform uses) + AnimationDriver feature writes; self-heal re-attach of expired anim component within 1 s. 0a6f2912: driver puppets frozen because legacy idle-controller hook also attached and interpolation preferred any Controller → registry checked first, DriverComponent preferred. d3587258: placed-transform writes "still did not visibly move the puppet" in round 2. [2.31, inconclusive]
- 394f941f: give and equip must be independent tests (muppets arrive with Panam's loadout so `!HasItem` skipped AddItemToSlot). TDBID.ToStringDEBUG returns "" in release builds.

## 3. Moving puppets

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 3. Moving puppets (chronological attempts on 2.5))_

- 7bef6e17 (test.73): 2.31 hand offset `move::Component + 0x90` → owning entity is past the end of IComponent on 2.5 (asserted size 0x90). Use `IComponent::owner`, 2.5 SDK asserts at 0x50. Puppet only moves after `HookIdleController_SetAnimation` matches it in PuppetRegistry and attaches the multi controller. [2.5-verified]
- 74de8dfd (test.74): MultiMovementController (C++ mimic of a 2.31 engine movement controller) attached; observer crashed 2.2 s later: `0xC0000005` writing `0x7FF7012DBCE8` (inside exe image), RIP `exe+0x3E7863`. Response: off 2.31 every puppet uses transform-only driver (`PlacedComponent_SetTransform`, what Codeware `Entity.SetWorldTransform` calls), no PuppetDriver/AnimationDriver mimic. Result: glide, no walk cycle. [later superseded]
- fbade8f7 (test.75): a placed-transform write does not visibly move an NPC-record puppet — its movement component writes position back every frame. Added `TeleportationFacility` teleport (max 10/s per puppet, only after 2 cm). [broken-on-2.5: 832f3eb1 found 10/s TeleportationFacility did not visibly move it either]
- 832f3eb1 (test.79): AI steering — `AIMoveToCommand` to the network position, `ignoreInCombat=false` (tree yields movement to it in combat, tweakAIConditionChecks.script:3417), re-issued max 4/s min 1/s, never finishes on arrival, gait from speed, face yaw when standing, teleport if >6 m behind. EnterVehicle cancels the command before the mount event. First 2.5 path with a walk cycle.
- ea776938 (test.80): steering worked first ~30 s (10-25 commands/10 s, 0 snaps, 0.3-2.5 m from real). Then >6 m behind and every `TeleportationFacility` snap failed; gap stuck at 13.5/9.2/22.0 m for minutes. Switched snaps to the AI's own `AITeleportCommand` (behaviour tree executes, aiTeleportCommand.script) max once/2 s, 0.5 s settle; pathfind (`ignoreNavigation=false`) unless within 2 m.
- 56c559ce (test.80): steered body was within 0.3-2.5 m but INVISIBLE to both players (test.79 unmoved body visible). Cause: our animation-thread idle hook skipped vanilla idle for driver puppets (fine on 2.31 where PuppetDriver writes anim). Fix: run vanilla idle for them off 2.31. Probe `MpDescribePuppet` (attached/active/appearance/AI LOD/high-level state).
- fcb7ce5c (test.82, first 2.5 session where both saw movement): one side 0-3 m but parked 2.2-2.5 m short when target stopped (pathfinder treats ~2 m as arrived; old straight-line cutover 2.0 m); other side 3-6 snaps/10 s and froze >1 min at 12-15 m — a dead `AITeleportCommand` left in the queue blocks later moves. Tuning: lead 0.4 s of horizontal velocity (cap 3 m); +1 gait past 2.5 m, sprint past 5 m; snap only after 8 m for 1 s or 20 m at once, max every 2 s; straight line inside 3 m or if gap not shrunk 0.5 m in 1.5 s; every move cancels pending teleport. Modes: 0 pathfind / 1 straight / 2 snap.
- 5487654f (test.83): jump made the steered body float (AI walk cannot jump). Steer to ground = lowest height in last 0.8 s; gaps horizontal, height counts only past 2.5 m. Lead 0.6 s (cap 4 m), run past 1.5 m, sprint past 3.5 m.
- e3c5e455: static re-measure of MMC against the 2.5 exe (capstone): IdleController vtable exactly 19 slots in the same order; `AttachController` reads only type at `+0x8` and pushes onto `component+0xA8`; engine AnimationDriver size 0x30 (Detach zeroes +0x00..+0x28); AnimationData size 0x3C, same field order as engine idle controller. test.74 crash unexplained (log rotated off). Returned as per-machine opt-in (`mod-original-movement.on` / `-original-movement`). [2.5-verified static]
- c193e801 (test.85): MMC on 2.5 ran crash-free; "a bit smoother but lag still there" => remaining lag = buffer delay (see topic 1).
- 0662a27a (test.86) FINAL CHOICE: steering measured trailing 3-10 m while moving (worst 17 m), 2-38 unsticks/10 s, teleport snaps, with healthy network (74 ms behind, 0 dry frames). MMC crash-free on test.85 and test.86. => original MultiMovementController is the 2.5 default (exact per-frame position + engine locomotion animation + keeps NPC AI off); AI steering is a per-machine fallback (`mod-steered-movement.on` / `-steered-movement`). [2.5-verified]
- 0cfdaa0b: our `PlayerPuppet.OnMountingEvent` wrap treated EVERY mount as vehicle entry, including when the player is the PARENT (carrying a body / grappling; vanilla carriedObject reads `childId`) => parentId = player entity id 1 => "vehicle 1" => UpdatePlayerLocation took vehicle branch, failed lookup, sent no position. Logs: 237/349 "Couldn't find vehicle: 1"; send rate fell to 12-17/s; observer buffer dry up to 81/1038 frames. Rule: vehicle entry only when local player is child and parentId resolves to VehicleObject; unresolved vehicle falls back to on-foot stream.
- c384b130/8d8e3b11: remote exiting a moving car is frozen for a 2 s stand-down grace (guards the 2026-08-19/20 exit crashes during component rebuild — do-not-undo), then snapped; added 250 ms ease after `SuppressUntil`.
- 162aca8c/35edc677: DriveEntity early-outs: no engine entity, no placedComponent, vehicle-exit grace.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 3. Moving puppets)_

- Only the server-side ordering and coalescing work is in this chunk (see 1). The client already drops states with seq <= the last one applied (InterpolationSystem.cpp:721). The per-connection interpolation-delay work matters more because the grid culls nothing (d342fdc6).

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 3. Moving puppets / teleport / placement)_

- 8401ec3a (09-04): fresh spawn and respawn (RevivePlayer) both go through DoTeleport (streaming-aware facility), but collision at destination is not always streamed in → player falls into game's out-of-bounds holding volume ("the box"). Guard: 1.5 s after placement, if player ≥ 20 m below or ≥ 30 m from server target, re-place; max 3 retries then log and stop. Redscript-only, not compile-checked. [unverified]
- d20e1769: log via ScriptLog (FTLog only reaches game log, not collected).
- b6e4b3da: stored start point (−5.2, 20.9, 0.0) is under terrain → every new char fell. Server now refuses start points within a few metres of origin in both SetStartPoint and GetStartPoint; fallback to player's remembered position. Log: "[NetworkWorldSystem] teleport to (-5.2, 20.9, 0.0)".
- ffe8f689: arrivals-point teleport fire-once flag lives on the character record; with no record it re-fired every join → requires record.
- 3100c342/9cdb62e8: "placement is not a teleport" item deliberately NOT built (experiment only).
- c28bd042 (subject): brief post-revive invulnerability so respawn in a firefight can't loop.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 3. Moving puppets)_

- afad4cc9/b2507a2b above (interpolation fixes). Spawn race: movement before puppet registered under id self-heals ~25 ms.
- a0553f70 / 8a10d689 / 0e00c775: crash reproduced: quit while in a car, rejoin → server replays orphan car → OnVehicleReady → MakeRemoteDriven (SetIsPlayerControlled, engine vcall, engineData set, SetKinematic, done) → client dies 1.6 s later (driver id resolves to nothing). Native ForceMoveTo, SetSimpleMovement, EnablePhysics in InterpolateEntity; gate checks physicsState / entity stub. Mitigation: server clears cars when player count hits 0. Open: A disconnects in car while B stays. [2.31-only]
- a2709440: 9-player sweep, 76 launches: 20× 0xC0000005 + 8× 0x80000003; all die 2–10 s after "frozen remote" warning then queued remote-vehicle mount (OnVehicleReady → DoMount → MakeRemoteDriven). [2.31]
- 5a606f11: engine gives puppet a fresh idle anim controller mid component rebuild after vehicle exit; attaching their "multi controller" there crashed. Fix: 4 s grace after exit (lock-free table readable from anim threads) where hook forwards to vanilla idle, then re-attach. [2.31-only]
- e53b7b86: teleport while mounted fires OnVehicleExit twice (.115 and .123 s); second has no vehicle_id/sit_id → server unconditional detach. Client suppresses using m_mountedSlot.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 3. Moving puppets)_

- Upstream speed from `moveComponent->speed` at hand-mapped offset → **moved on 2.31, returned ~3e8**; anim SM thresholds 3 m/s walk / 5 m/s run → pinned. Fix 88d58885: speed = measured distance/time between updates, teleports clamped. [2.31-verified]
- 9732c461: position must be read from **worldTransform not localTransform** (local not updated while walking) — fixed puppets at origin.
- 0d3a747e: kWalkSpeed 3.0 was above real walk ~1.8 m/s → glide; **bands: walk ≥1.2, jog ≥4, sprint ≥6.5 m/s**; added Jogging state; `AnimationData.speed` (graph blend param) never written → now interpolated speed. fmodf by zero anim duration = NaN guarded. b4bfc201: jog band [4.0, 6.5) falls back to sprint_0 clip if no jog_0. feb1e504: missing clip → motion pipeline stalls (frozen puppet) → degrade to idle-glide. 4f0f0394: sender's Sprint state overrides speed band.
- Movement while mounted: client ignores movement for entities with AttachedComponent (InterpolateEntity returns early; game moves the parented puppet). ad04782d: GetDeltaTransform deref'd `owner->placedComponent` unchecked on anim thread; mount reparents entity → guarded; controller target frozen at mount spot while seated (delta grows unbounded; logs past 100 m). 3a6f22f7: dismount resets interpolation anchor.
- Teleport: client must teleport itself via game teleportation facility (streams destination, moves camera); writing position directly drops through world; server position edits are overwritten by client updates (8a50a9c1, d99494c4). 1c8e9239: while driving, UpdatePlayerLocation sends the VEHICLE transform as player position → order: tell network you left, force exit (MountingFacility unmount + ExitVehicle AI event — facility negotiates and may refuse mid-animation), then move. 32e6a486: /return lands a few metres short.
- 4f0f0394: driver puppets stand down 2 s after remote vehicle exit (4/4 crashes 0–15 s after exit handling).

## 4. Animation

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 4. Animation)_

- 56c559ce: without a PuppetDriver writing animation, the vanilla idle (animation-thread idle controller) must run or the body is invisible/unposed. [2.5-verified]
- e3c5e455: MMC logs Attach, first Tick, GetDeltaTransform, SendAnimationParameters; engine AnimationData 0x3C, AnimationDriver 0x30 (see topic 3). With MMC the body gets the engine's own locomotion animation (0662a27a).
- 74de8dfd: PuppetDriver's AnimationDriver hands a 2.31 engine-struct mimic to engine functions (the reason it was disabled on 2.5 initially).

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 4. Animation)_

- Nothing in this chunk.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 4. Animation)_

- Nothing in this chunk.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 4. Animation)_

- PuppetRegistry: mutex-guarded/lock-free registry used by the animation hook (anim hook runs on animation threads, marshals to main via ThreadService); mutex on read path cost frame rate (559828f6). "multi controller" = their replacement anim controller attached via hook (5a606f11). [2.31]

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 4. Animation)_

- See §3 bands/state machine. Locomotion states: Idling, Walking, Jogging (added), Sprinting; clips walk_0/jog_0/sprint_0. Male template lacks most locomotion clips; females animate (feb1e504). [2.31-only]
- ILocomotionHost adapter: legacy controller's vtable is an engine ABI and must not change (3a6f22f7).
- PlayerStateMachine blackboard Locomotion/UpperBody states replicated for crouch/jump/aim (4f0f0394), consumers pending.
- d2935a2e: /push (3 m reach, 1.5 m stumble) removed — puppet cannot animate the stumble.

## 5. Appearance / character customization

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 5. Appearance / character customization)_

- 463bc4d1: `GetCustomizationState` read raw handle at `CharacterCustomizationSystem+0x78` (decoded on 2.31, class size 0x388). 2.5 class size 0x3D8 and that slot is non-empty in ordinary play (template default body) => 90 s autosave + disconnect save stored the default body: Pajeet 6283 bytes → 6571-byte default woman (test.70). Fix: check class size, answer "no state" on undecoded layout. [broken-on-2.5 for raw +0x78]
- 929d924f / 74de8dfd: `IsGame231Layout()` (CharacterCustomizationSystem.h) = class-size 0x388 fingerprint used by every 2.31-decoded path.
- 712817b6 / be695136: `CallCustomizationFunction` passes no out-slot; `ExecuteFunction` returns false WITHOUT calling any function that has a return type when the out slot is null. `ReFinalizeState`, `FinalizeState`, `InitializeState` return Bool => the earlier "engine refuses them in gameplay" conclusion was wrong: they never ran. [2.5-verified reasoning]
- 17599f98: on 2.5 nothing was ever saved (every save reads customization state). Fix (off 2.31): read LIVE state via engine's `GetState` RTTI accessor (what restore writes through), only after this session's restore was Applied or while a new character is being made; refuse if body type changed; log size, body type, FNV.
- 286c0312: re-saving a RESTORED character contaminates it — restore writes stored look INTO a state that still holds the template body; engine keeps whatever the look doesn't mention: 6283 → 6571 bytes with `female_base`, `full_breast_big`, female genital options added; body flag stays male. Fix: restored char saves possessions only; server keeps stored look. Clean conversion needs the state emptied first. [2.5-verified]
- ce05d986: three capture moments on 2.5: mid-creator autosave = 23-byte unpopulated state (server size floor refuses); `OnBeforeWorldDetach` save as the New Game world tears down = real creator look (10263 bytes, female, fnv `074ece6197c293a3`) — the only working one (`m_savingBeforeDetach`); after MP world loads = template body (too late). [2.5-verified]
- b3dd44d5 (2.31): customization state instance exists only while creator or mirror is open; null in ordinary gameplay.
- be695136: 2.5 symptom: each player sees others correctly and themselves as the template body. Experiment "Dress my body": stored look into a FRESH state, keys via `CollectCustomizationKeys`, passed to the appearance changer. [unverified]
- 929d924f: dressing remote players called `GiveItem`/`AddItemToSlot` via hand-numbered `ITransactionSystem` vtable slots (0x250 onward) decoded on 2.31 => 2.5 crash 16 s after join: `0xC0000005` reading `0xFFFFFFFF` at `exe+0x887DC4` after "[Appearance] holstered_default". Fix: redscript `AddItems` via TransactionSystem's declared API; cost: no garment appearance name (default variant). [broken-on-2.5 vtable; fix 2.5-verified]
- 04c90b06: "worn" items recorded from live puppet each save and used at spawn; possessions-only save had copied an empty look onto the live puppet so later arrivals got no look.
- 44500137: perk restore buys perks with points; template at a lower level couldn't afford (35 asked, 19 landed); equipment reports wait 6 s for re-dress.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 5. Appearance / character customization)_

- fbb7b33f: **crash.** 15 remote appearance applies in 16 min, all with the identical ccstate (hash `78a96dec…`), 7 of them exactly 90 s apart (the possessions autosave). Clothing and customization share one message, and the visual item count changed on its own (4↔5 items, e.g. a holstered weapon), so the server rebroadcast. Each receiver ran the full customization apply again, and the repeated apply killed the observer. Fix: remember the ccstate hash scheduled on each entity. On a match, apply only the clothing (cheap, idempotent). The hash is recorded only after the schedule is accepted. (A use-after-free had already been fixed in c346f5a.) [2.31]
- 3903a8a3: server guard. A save that flips `is_male` on an established character (one with stored appearance that has been played) is refused whole. Cause: a client captured the template's default female corpo V instead of the male character the player made. 13 tests.
- 171b04ba: stored appearance blob for one character: **5435 bytes**.
- 32056122: the server's only C++ TransactionSystem use is AppearanceSystem giving clothing to REMOTE puppets.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 5. Appearance / character customization)_

- e795a522 (08-28): GetCustomizationState() reads (ccSystem + 0x78); that pointer is NULL during ordinary play; InitializeState builds one but engine refuses InitializeState outside the creator. But equipmentSystem.script:4992 calls GetState() in gameplay (hair suffix) → live state IS reachable via RTTI GetState on interface gameuiICharacterCustomizationSystem (concrete class has empty function list). [2.31-only; the +0x78 offset is 2.31]
- 99355161 / 2b60289a / 2356232f — commit calls tried on live V during gameplay: InitializeState refused; ReFinalizeState refused (body-morph menu's); FinalizeState refused (creator summary's); InitializeOptionsFromFinalizedState ACCEPTED but body did not change (inert). Conclusion: appearance lives in the save; cannot overwrite live V. [2.31-only, failed]
- 99355161: deserialising stored ccstate into live state set isBodyGenderMale, refused commit left it → template body with male anatomy, no arms, hair missing on motorcycle. Fix: snapshot live state before write, restore if no commit; no write if snapshot fails.
- 2b60289a (08-29): root cause of "wrong character": MP path called LoadLastCheckpoint(false) = save index 0 (newest); launcher installs world template as save "MultiplayerStart"; whichever is newest wins (singleplayerMenu.script:1012: `if (m_isModded) LoadModdedSave(0) else LoadLastCheckpoint(false)`). Fix: list saves, skip template by name, load newest own save; template only for players with no save. 3b88952d later: "newest non-template" is not an identity (loaded a throwaway 'AutoSave-12'); proposal: ManualSave(saveName) per character.
- 3b88952d (09-02): server sent stored appearance (9206 bytes) while player on main menu; restore failed "GetState returned nothing" (no live state before world); bytes std::move'd before attempt → lost. Timeline 00:13:34.156 sent → 00:13:35.164 FAILED → 00:13:36.063 OwnSave 'AutoSave-12' loads. Fix: test engine can accept before consuming; clear buffer only after accepted commit.
- 5fe82f0f: stored blob contaminated (9141 bytes), re-uploads every save, commit inert.
- 9cdb62e8: character lifecycle states Connected / Selected / Restoring / Live / Detached; saving legal only in Live; Live→Detached logged as WARNING (the overwrite happened 70 s after world detach).
- d926ca9a (08-28): engine grants the vanilla PL corpo loadout ~88 s after spawn (04:51:08 kit queued 5 items, money 121994→20000; 04:52:36 capture 124 stacks, 14 cyberware) — not fixed timing; MpStarterSettlement polls stack count until it grows then settles, acts once (max 90 polls); armed only at character creation; keeps body slots (else "floating torso"); removes engine-installed cyberware; never money. b42baedb: counts vary per session → not a fixed vanilla grant.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 5. Appearance / character customization)_

- 78713d7a: RTTI dump: gameuiCharacterCustomizationSystem has 0 functions; gameuiICharacterCustomizationSystem has 24 (InitializeState, ReFinalizeState, GetState…). Red::CallVirtual resolves on object's own type → must look up on the interface type explicitly. [2.31-only, likely holds]
- 54d9a856 / 47b6a01c: local restore = InitializeState (no live CustomizationState during gameplay — "CustomizationState was null") → deserialize stored ccstate (same call AppearanceSystem uses for remote puppets) → ReFinalizeState. Run on settle path before inventory restore. Stages logged; state machine NotStarted/Applying/Applied/Failed; stale reject keyed on remote player id (2000000dc→3000000dc across spawns). Not proven working in this chunk. isBodyGenderMale lives inside the blob. [2.31, unverified]
- Sizes: appearance blob 7138 / 9141 bytes (~9 KB); "under 1 KB = 23-byte half-built case" ignored (9b7b9923). Empty ccstate in update = clothing-only.
- 9b7b9923: clothing sync via PollEquipmentChanges on 1 s watch (PollAppearanceChanges only fires in creator); ApplyAppearance is script-only → native calls AppearanceSystem.ReapplyAppearance.
- c346f5a5 (no body): "Fix appearance-apply UAF and silent ccstate corruption". 01f92ab6: "female-biased appearance apply", span-lifetime candidate, silent deserializer.
- 642663dd / 75548ca4 / 7ff11767: TransactionSystem.GetItemList enumerates CARRIED items only; equipped weapons + installed cyberware via GetPaperDollEquipAreas + GetVisualItemInSlot; unequip before RemoveItem. Equip area enum 33 = RightArm; must not strip RightArm, LeftArm, BaseFists (else arms vanish, floating pistol). Body is NOT inventory items: Items.PlayerMaHead/WaHead/MaArms/WaArms/MaTorso/MaLegs/MaFeet/MaBody/MaSkin/MaFace all absent from 2.31 tweakdb. [2.31-only]
- 96da4bf2 / f61b1b6f (HUD, see §9).

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 5. Appearance / character customization)_

- **`GetCustomizationState()` returns `(ccSystem + 0x78)`** — never null; the _instance_ behind it is null during normal play and non-null only while editing (mirror/ripperdoc/creator). Old null check was a no-op; serialising the null instance crashed on connect (9732c461). Used as "editor closed" signal: non-null→null; serialise during session, poll 1/s (134f572f). [2.31-only]
- SDK generated `gameuiCharacterCustomizationSystem` is opaque: **`uint8_t unk48[0x388 - 0x48]` → class size 0x388** on the 2.31 SDK (d93a8fd1). [2.31-only]
- RTTI dump (8558b7b1): `gameuiICharacterCustomizationSystem` has **24 functions**: InitializeState, InitializeOptionsFromFinalizedState, FinalizeState, ReFinalizeState, CancelFinalizedStateUpdate, ClearState, GetState, GetHeadOptions, GetBodyOptions, GetArmsOptions, GetUnitedOptions, RandomizeOptions, ApplyEditTag, ApplyUIPreset, ApplyChangeToOption, TriggerVoiceToneSample, HasCharacterCustomizationComponent, Get/SetRandomizationParameters, Register/Unregister/GetPuppetPreview, IsTransgenderAllowed, IsNudityAllowed. Redscript (91016b8b) sees only HasCharacterCustomizationComponent + GetState; concrete class is `importonly` and empty. No show-creator call, no mirror class, no creator controller in 2.31 type dump. [2.31-verified]
- Appearance stored as game's own CharacterCustomizationState blob, base64 in JSON, opaque to server (0912346c). Sizes seen: 6518/6586/8588 bytes; **23-byte half-built blob** caught by 1 s poll → both ends require ≥1 KB, client holds previous good bytes (8558b7b1). Body gender not readable from blob → sent separately.
- 9e640e36: correct remote blob arrives and deserialises; clothes apply; but head/body customization doesn't take (template customises from local player).
- Equipment: **TDBID.ToStringDEBUG empty in release builds (debug name table stripped)** → hashed "" matched nothing ("ItemID creation failed" ×8). Wire changed `repeated string` → `repeated uint64` via `TDBID.ToNumber`; C++ reconstructs TweakDBID from value (e9b6a4ef). [2.31-verified]
- 32e6a486: since 2.0 wardrobe outfits override slots and `GetPaperDollEquipAreas` doesn't report the outfit → read Outfit slot explicitly.
- Body gender baked in save (no ripperdoc/mirror change) → choose in launcher / NEW CHARACTER runs real New Game creator via menu OnNewGame (RequestNewGame doesn't exist) (0d5ed5ce, 7035a88d).

## 6. Vehicles

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 6. Vehicles)_

- 84df9a48: 2.5 made the game's `VehicleSystem` a NATIVE class (base `IVehicleSystem`, natives `SpawnActivePlayerVehicle`, `SpawnPlayerVehicle`, `ToggleSummonMode`, ...). Our class registered native RTTI name "VehicleSystem" => 9 validator errors, game closed. Renamed `CyberpunkMPVehicleSystem`; script alias kept. [2.5-verified]
- 9ec6a206: car radio gated by blackboard `UIGameData.Popup_Radio_Enabled`, only writer `HotkeyConsumableWidgetController.UpdateBlackboard`, false when quest fact `unlock_car_hud_dpad` = 0. Same fact keeps vanilla vehicle summoning shut (vehicleSystem.script:70). Fix wraps `IsRadioEnabled`. MP cars are real vehicle records; mount is a real `MountingRequest` into the driver slot.
- 62e48492: downed while seated: polite unmount made instant; if seat still holds after 0.5 s force the workspot out (as the game's own exit), up to 4 times, then teleport. [unverified]
- c92f26b1: P0 two-player crash ~90 s in; AV with RIP at a HEAP address (corrupt function pointer); ~960 identical `[OrphanVehicle] samples 0` lines (prevTick frozen, renderTick advancing). Hypothesis: own summoned car replicated back as a second remote copy missed by server-id dedup. Added diagnostics only. [unverified]
- 0cfdaa0b: 55 ms pre-authority window on fresh own-car entry (left alone).
- 1f4f2e57: ownership is per vehicle instance (id/plate/owner/price/sale-lock).
- 832f3eb1: EnterVehicle must cancel AI move command before the mount event.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 6. Vehicles)_

- 032ca69d (solved-bug list): the seat-swap exit emptied a moving car. A **remote-vehicle-mount crash** is still open. Movement for a vehicle can arrive before its spawn, so mounts are queued (3b05ab9b).
- 564d094f/ffe4c9c1: the vanilla in-game vehicle shop is a second purchase path. The proposal is to refuse at the purchase, not hide the UI.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 6. Vehicles)_

- f34e1462/eff82e05: seat ids are CName = FNV1a64(name). 0xb000b1d029d0cea0 = FNV1a64("seat_front_left") (verified offline). Server validates seat IDENTITY against the set of known seat names (not a count; server can't know per-car seat count) — fixed a 5th person fitting in a 4-seat car via an invented 64-bit seat id. Already present: sit_id on wire for enter/exit/NotifyVehicleEnter; server seat state AttachmentComponent{Parent, SlotId}; one per seat, refused not reassigned; stale exits from seat swaps dropped; late joiners get NotifyVehicleEnter replayed per occupant; driver disconnect hands simulation authority to a passenger (NextOccupant priority list); ReleaseVehicleIfEmpty counts all attachments and parks (not destroy). /vehseats debug. 13 tests.
- 3cde2718: OnVehicleEnter was an off-thread flecs caller (crash cause, see §1).

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 6. Vehicles)_

- 37559eb2: HandleVehicleLoadMessage spawned unconditionally; server sends load with every enter → stacked duplicates (7 copies seen). Fix: one local copy per server id.
- bec22350: car whose driver leaves is PARKED (TransferAuthority to nobody), not destroyed; re-entering client sends remote_vehicle_id to join existing entity. 2 s release delay removed.
- 804e64c4: driver exits with passenger → server PromoteToDriver (authority + seat) via NotifyVehicleControlAssigned; client leaves seat then takes driver seat 350 ms later (mounting facility negotiates; same-frame mount races unmount); seat from VehicleComponent.GetDriverSlotName() (seat_front_left). [unverified]
- 885f252e: observers only reach players present → late joiners got no vehicles; AddPlayer replays NotifyVehicleLoad + NotifyVehicleEnter.
- 64951cc9: garage APIs (vehicleSystem.script): EnablePlayerVehicle(vehicle: String, enable: Bool, optional despawnIfDisabling), GetPlayerUnlockedVehicles(out array<PlayerVehicle>), PlayerVehicle {name: CName, recordID: TweakDBID, isUnlocked: Bool}. Lock before unlock. [2.31]
- 3bf24463 / 3a388257: vehicle damage probe: shotgun on parked car = 14 hits in 1 ms each ~8 dmg; health 1050.32→932.89 (117.434) vs sum of GetTotalAttackValue(Health) 117.434 (diff 0.000088). Street car ≈ 1050 HP. DamagePipelineFinalized fires BEFORE pool settles → read back after 0.15 s. Damage arrives per hit not per shot → batch. GetWeaponRecord is on WeaponObject (AttackData.GetWeapon()). [2.31-only]
- c13231af reverted by 5b097d9e: adding RecordId (std::string) to VehicleComponent crashed — not because strings are forbidden but because components must be registered before first use (cc0629c2). /givecar stored Model as std::hash<string>, not TweakDBID.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 6. Vehicles)_

- c121c9ec: each car entry created a new flecs entity never destroyed → 7 stacked cars in 7 min; now destroyed when occupant count 0. b9558d22: 2 s empty-vehicle teardown grace; ExitVehicleRequest names vehicle+seat so stale exits dropped (seat swap = exit+enter 1 ms apart); destroying owned entity sends owner revoke + unload; queued mounts keyed by server id (engine EntityID is 0 until mirror exists); owner remembers its own car's id pairing (server never replicates a spawn to its owner); passenger-seat spawn-and-enter refused.
- d13c76ab: network copy spawned at same transform as local parked instance with live physics → depenetration impulse = "cars explode". `MakeRemoteDriven` (kinematic) called from OnVehicleReady so copy never simulates; local player's own car never kinematic.
- eaeaa792: new driver of network car: physics asleep from MakeRemoteDriven and `m_vehicleGameId` empty → `NotifyAuthorityAssigned` wakes physics + resolves entity. Passenger without authority re-asserts remote-driven (b9558d22). Driver exit handoff order: front right → back left → back right. **Seat names hashed as CNames = FNV-1a 64**; verified front-left constant reproduces from seat name. 27a14c3c: no seat check existed; two clients could both claim seat_front_left.
- ad04782d: passenger crash ~25 s after remote puppet mounted moving car (see §3 GetDeltaTransform).
- Vehicle unlock: EnablePlayerVehicle takes String; TweakDBID→string impossible on 2.31 release → stored by name (1a4174e1). Mod's own `VehicleSystem` + `import CyberpunkMP.World.*` shadows game's VehicleSystem → UNRESOLVED_METHOD on GetPlayerUnlockedVehicles/EnablePlayerVehicle.
- Phone summon used as interface; server pushes EnablePlayerVehicle per owned model (e9b67c37).

## 7. Quests / facts / scenes / journal

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 7. Quests / facts / scenes / journal)_

- 9ec6a206: quest fact `unlock_car_hud_dpad` (radio + vanilla summon gate); absent from template saves.
- 3d7e8a23: Songbird holocall hang: 8 failed attempts reverted 2026-08-20; mitigation `MpSilenceStoryHolocalls` blocks Songbird from reaching a character; without it a 120 s watchdog freeze returns. Phantom Liberty prologue skip shipped as `zz_NightCityOnline_CleanStart.archive`.
- 1a737738: new characters start in base-game `q000` holding box.
- 1afab4c6/f9d5bca5: `DelayCallback` third arg `isAffectedByTimeDilation=false` (delaySystem.script:59) runs on real time, so it ticks while paused; pause menu unpauses the world behind it 0.25 s later.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 7. Quests / facts / scenes / journal)_

- ffe4c9c1: NCO's Quests.reds "does not disable the quest engine... quests still technically progress in the background". The phone block does not stop quest scenes. The full PL standalone quest stack is loaded. 12 of 14 RPGManager.GiveReward callers (rpgManager.script:2693) are devices or NPCs; giveRewardEffector is the quest-graph path.
- b6242c85: phone. `questPhoneCallPhase { Undefined, IncomingCall, StartCall, EndCall }` (phoneSystem.script:9); there is no RejectCall. Answer and decline are input actions on the HUD phone controller (newHudPhoneGameController.script:512): `PhoneInteract` BUTTON_RELEASED = answer, `PhoneReject` BUTTON_HOLD_COMPLETE = decline. Both check `callPhase == IncomingCall`. Calls shown as StartCall could never be answered. OnAction is wrapped (vanilla PickupPhoneRequest stops the ringtone and sets the talking fact). Shown through the UI_ComDevice blackboard. [2.31]
- d58ad961: `ui_phone_incoming_call` is a looping audio event. Setting callPhase=EndCall on the blackboard only controls the widget, so StopRingtone must be called on every exit from ringing. [2.31]

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 7. Quests / facts / scenes / journal)_

- 9e1832f8/c1518c4a (08-30): MainMenu.reds SpawnEvent(n"OnNewGame") → preGameScenarios.script:308 always takes the EP1 branch when Phantom Liberty is installed → every mod char was a PL standalone start (Songbird call, Dog Eat Dog). ep1\quest\ep1_standalone.gamedef lists three root quests + spawn tag: ep1_standalone.quest (PL story, q301) DROPPED; cyberpunk2077_ep1_standalone.quest (base NC, prologue skipped) kept; ep1_preorder.quest (Quadra) kept; ep1.quest ADDED (sets ep1_installed=1, enters ep1.questphase at socket "Base"). Spawn tag = base game's #q000_spwn_start (empty holding room; server then teleports). Earlier attempt kept #q301_spwn_ep1standalone_opener with its quest dropped → players fell through map (a spawn tag dies with its quest). Shipped as a 2nd .archive; ArchiveXL given the directory. Proven: ep1_standalone=0, ep1_active=0, q301_active=0, empty quest log; d3425258 confirmed ep1_installed=1, Dogtown reachable, no Songbird. [2.31-verified]
- 06af8b5b / 34e68aa8: Dogtown gate = fact ep1_side_content ≥ 1 (combat_zone_gate.questphase pause condition; no q301 fact read); same fact roots communities, vendors, encounters. Game writes it =1 in 13 places, never 0 → safe to set; CDPR's ep1_chicken_unlocks.questphase opens Dogtown this way. Seeded only on creation of worldfacts.json. [2.31-verified in game 08-29]
- 08f1ed3b (08-30): ep1\quest\holocalls\songbird\songbird_holocall.questphase gates every call variant behind pause condition holo_<call>_activate > 0 AND holo_setup_active < 1 AND holo_setup_started < 1. Holding fact holo_setup_active = 1 blocks Songbird and ~20 PL holocalls (Reed, Myers, Alex, Kurtz, courier). Set at CONNECT (before world facts), read back after write. Facts like q301_failed are written by graph but never read (setting them did nothing).
- 51756fca: PhoneSystem.OnTriggerCall (phoneSystem.script:155) is the single quest→phone entry (private TriggerCall called nowhere else); refusing there = no sound, no blackboard, no ApplyPhoneCallRestriction(true). Blocking at IncomingCallLogicController.SetCallInfo was too late (player restricted in invisible call). Zero calls presented in a session. Does not stop scenes. QuestDiagnostics reads ep1_side_content, ep1_installed, ep1_active, gate blockers, on_foot/by_car crossing flags.
- c0ca2c7f / 526c56ba: questTriggerCallRequest.isPlayerTriggered = "player triggered this QUEST call" (journal callback), not an MP player — never gate on it. Player calls never go through OnTriggerCall.
- 1d5aec2a (2.31 probes): enums are quest-prefixed: questPhoneCallPhase {StartCall, EndCall, RejectCall} (PhoneCallPhase doesn't exist); questPhoneCallVisuals {Default}; PhoneCallInformation fields: callPhase, contactName (CName), isAudioCall, isRejectable, isPlayerCalling, visuals (NOT contact/contactHash/contactId/isVideoCall). Widget OnAccept/OnReject/Hide/Show/AcceptCall/RejectCall/EndCall absent from script API. Present a call by writing PhoneCallInformation to UI_ComDevice blackboard. Whether a runtime CName renders on phone: unverified. [2.31-only]

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 7. Quests / facts / scenes / journal)_

- 902bfacf: Songbird holocall freeze — 8 attempts all failed, reverted to 697b4ec; v0.3.77 hook refusing to build holocall UI controller froze game outright. Kept: level-15 clamp on new character first capture, phone crash fix. Investigation in docs/songbird-holocall-hang.md. [2.31]
- 64951cc9/5ecd95ed: quest progression, world state, street cred still come from the shared world-template save (not server).
- 1ad3bdfd: connecting during character creator drew player list over creator and froze on Phantom Liberty start prompt → REVERTED; connect only after creator.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 7. Quests / facts / scenes / journal)_

- 65cc869f: scripts expose only **LoadLastCheckpoint** (no LoadSaveInSlot/LoadSave/RequestLoadGame/GetSaveSystem on 2.31) → launcher installs template save and stamps its timestamps 1 min in future so it is newest. 4b249fd4: applied only until the player has a newer save of their own.
- Templates: AutoSave-0 female (L15, SC15, 63 quests, PL start, unmodded 2.31); ManualSave-121 male via PL quick start (997a6dbb). 218a92c6: template at `get_to_combat_zone` → Songbird calls; `ep1_standalone` fact read by 69 quest phases (would depopulate Dogtown); `q301_border` owns Dogtown gate devices while active; a 2.3 save loads clean on 2.31. Server clamps new character to level 15 on first capture (697b4ec2).
- 2a11cb6b: Songbird holocall = scene `q301_00_holocall_hook`, delivered by **HoloAudioCallLogicController** (not IncomingCallLogicController); refused at initialise. Tracked objective untracked on notification-queue event. f32cc32c: Songbird holocall hang — engine watchdog kills at **120 s** ("Watchdog timeout!"); 100/101 threads stuck, one game thread spinning on a clock in render path; six hypotheses wrong. HangWatchdog: 8 s tick-stall → suspend all threads, log.
- 1610ed5e: calls refused at `SetCallInfo`; quest phase graphs native; journal `ChangeEntryState` is imported (no script body). 8d85114f: five guesses at JournalRequestStateFilter values missed.
- 2ef73dcb: doors gated on quest facts → server-pushed facts list (config/worldfacts.json, `/fact`). Quest-scoped interiors (Konpeki, All Foods, Clouds, Arasaka Industrial Park) only stream during their quest.
- Menu: AddMenuItem wrap removes Load Game / Save while connected; REGULAR START lives in `ExpansionNewGame` (two bound buttons, never via AddMenuItem) (f058cc48).

## 8. Combat / NPC / damage / health / kill attribution

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 8. Combat / NPC / damage / health / kill)_

- 4e16bb91/df115221: PvP targeting requires remote puppets to have a hostile attitude toward the local player (per puppet, no faction change); weapon filter `TSF_EnemyNPC` = All(Obj_Puppet | Att_Hostile | St_Alive) AND Not(Obj_Player); quickhacks need `IsAggressive()`. `GameplayRestriction.FistFight` status effect tried and discarded. Cost: NCPD/bystanders may react. Default now on (`-no-hackable-puppets` to disable).
- 4e16bb91: server `Level::HandleCombatEventRequest`: no PvP check; damage from any living player to any living target within 250 m, clamped 100 per hit.
- 9079c107: god-mode via inherited Panam tags (topic 2).
- 92230cf6: server-decided downs/health/incoming quickhacks poll was armed only in OnConnected; with menu connect `/kill` arrived (state 1, 2) but `MpApplyDownedState` never ran. Now armed per body on the player's own attach. Weapon poll deliberately left off: reviving it before the reload-refusal fix makes the server refuse every shot after the first magazine. [unverified]
- Starter kit: pistol + 100 `Ammo.HandgunAmmo` + 20000 eddies (4e16bb91); shared world template holds 121994 eddies (63ad233b).

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 8. Combat / NPC / damage / health / kill)_

- 16173215: /kill did nothing because nothing called IsDowned(). The fix applies `BaseStatusEffect.Defeated` on the edge (as vehicles.script:825 does). It does not use BlockAllMenu: that locked the pause menu for the rest of the session. The server owns LifeState and sends NotifyCombatState.life_state. Revive checks LifeState and the server-side distance. [2.31]
- 764db459: the server checks ownership in the combat, weapon, quickhack, status-effect, vehicle and appearance handlers. No doors, containers or interactables are synced.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 8. Combat / NPC / damage / health / kill)_

- 48d9d109 (09-02): server-owned HealthComponent {Health, LifeState: 0 alive, 1 downed, 2 dead, 3 reviving}; lethal damage downs instead of killing; NotifyCombatState broadcasts. Bleedout = DownedAt timestamp deadline (set only on transition); stabilise 8 s, revive 12 s (requires stabilised), revived at 15% health; one medic per patient (TreatedBy); /assess /stabilize /revive distance-checked vs server positions. Dying cancels trades.
- c28bd042 (subject): post-revive invulnerability.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 8. Combat / NPC / damage / health / kill)_

- 6f431c54: weapon target filter TSF_EnemyNPC (targetingSearchFilter.script:79) = All(Obj_Puppet|Att_Hostile|St_Alive) AND Not(Obj_Player). Hackable = ScriptedPuppet.IsQuickHackAble (scriptedPuppet.script:4045, 7 conditions: active, not crowd, IsAggressive, record objectActions incl. PuppetQuickHack, attacker has cyberdeck). Setting attitude Hostile toward local player → IsQuickHackAble true, IsAggressive true, can aim. FistFight status effect did NOT work (IsAggressive stayed false). [2.31]
- 67b53546: quickhack objectActions copied from Character.rcr_sixthstreet_hooligan_melee1_knife_wa (dumped via CET); correct names e.g. BaseBlindHack (not QuickHack.BlindHack), MadnessHackBase (not MadnessLvl3Hack), SuicideHackBase.
- c4bf2797: hook ScriptedPuppet.OnHit; gameHitEvent has instigator, target, hit pos, dir, attackComputed (armour applied). Always call wrappedMethod. Guards: target is our puppet, instigator is local player.
- ccd4cb3c: hit zone = hitRepresentationResult.hitShapes[0].hitShapeName (CName from gameHitShapeBVH, only readable on live hit). Quickhack damage goes through ordinary hit pipeline → don't add server damage (v0.3.104 bug doubled hack damage).
- 4d8c279d: apply server health by SET not subtract, only target applies; clamp to MpDeathFloor (Death.reds sets Immortal so death menu never opens; "downed" is server state).
- 88d0a7f3 / 80dac149: server ammo: fire floor 40 ms (1500 rpm), reload ≥ 500 ms. Client polls GetActiveWeapon + WeaponObject.GetMagazineAmmoCount (returns Uint32; mixing Int32 → NO_MATCHING_OVERLOAD). No script "weapon fired" event.
- d7b3b8a7 / 506ac5f1: RAM cost computed per use (ScriptableDeviceAction.GetCost via RPGManager.CalculateStatModifiers) → server clamps reported cost and owns pool. ObjectAction_Record.Cooldown() → Cooldown_Record {Duration(), Modifiable()}; GetCooldownsCount is on AIAction_Record (NPC AI), not ObjectAction_Record.
- 89d8c2dd: UploadProgramProgressEvent.action is ScriptableDeviceAction; GetObjectActionID() gives id; send TDBID.ToNumber.
- ee33ce6a / 7b0a403d: ScriptedPuppet.OnUploadProgressStateChanged; AIQuickHackStatusEffect.BeingHacked applied on STARTED, removed on COMPLETED. Status effects relayed via OnStatusEffectApplied; applied to local PLAYER on victim's machine; only instigator reports.
- f76877ea: clear wanted level on revive: PreventionBlinkingStatusRequest with telemetry string 'ResetOnPlayerChoice' (DespawnAllPolice checks it) → Heat_0, police despawned (preventionSystem.script).
- 392b28f2: scanner name: hook CompileScannerChunks (GetDisplayName is native import, can't wrap); std::string doesn't marshal over RTTI → GetNetworkPlayerName.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 8. Combat / NPC / damage / health / kill)_

- 27a14c3c: death screen blocked with **`gameGodModeType.Immortal`** (engine checks before dead state; damage and health still drop, stat-pool listener still fires) applied on connect, keyed on own source name. Earlier tries (OnDeath wrap, stat-pool custom limit floor — spent once reached, 99a537c8) bypassed by scripted kills/falling/drowning. Death-menu backstop: hide root, close via m_menuEventDispatcher, retry 5×, restore after 8 s.
- 88dc8269: death → heal + teleport to server respawn (config/respawn.json). Loading a save desyncs (world rebuilt, server keeps old puppet).
- IsMultiplayer() gates damage replication etc. (see §1).

## 9. HUD / UI / archives / tweaks / saves / Steam

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 9. HUD / UI / archives / tweaks / saves)_

- 1308b165: `prototype_hud.inkhud` override REPLACES the whole file; a 2024 copy on 2.5 dropped police, `reaper_mq062`, `cyberware_mq062` HUD entries, aimed car HUD at a slot 2.5 renamed `BottomLeftSpeedometer`, and kept old contexts/margins on 8 others. Correct: 2.5's own file + `multiplayer_ui` inserted after `new_phone` (72 game entries + 1 = 73); repack with other 14 files byte-identical. Must be rebuilt every game patch. [unverified in play]
- 9fb1bfe1/44a0ba46: HUD widgets have no UI context, so `navigate_up`/`proceed` menu actions never fire there. Working: custom input actions appended to `UIBase` in the input_loader XML (CyberpunkMP.xml) fire in gameplay (push-to-talk, chat key).
- 6e1c52a6 REVERT of 166d5a42/889d4831: modal on a HUD widget (`PushGameContext(ModalPopup)` + `inkMenuLayer_SetCursorVisibility` + TimeDilationHelper `radialMenu`) blocked chat typing but did NOT freeze movement or show a cursor. A real cursor needs a menu controller (like ServerListController). `PopupStateUtils.SetBackgroundBlur` works standalone.
- 3082f066: ink coordinates: author at 1920x1080, scale by rootWidth/1920 from top-left pivot; `GetScreenSpacePosition` returns real display pixels; `GetWindowSpacePosition` is dead; convert to authored units once at the input boundary.
- d64ffe2f: `@addMethod/@addField/@wrapMethod` only attach to pre-compiled game classes; runtime ink tree reparented onto controller's root compound widget; authored .inkwidget blocked on those controllers. Item names resolved client-side from TweakDBID.
- d6240d9d: never set the full-screen canvas interactive (test.27 soft-lock). dfeecaf0: a HUD widget spawned on the menu root swallows all input.
- 30fe2958: input hints via `UpdateInputHintMultipleEvent` draw in game style with current binding.
- 90b76408: `AsyncSpawnFromExternal` probe of library `multiplayer_ui/server_list` vs CLI-authored `nightcityonline/character_select`.
- 75c5f080/83c49c97/ee4a41a1: ImGui DX12 overlay: ImGui 1.92 stopped filling `ImDrawData::CmdListsCount` (use `CmdLists.Size`); overlay refuses to run with CET loaded (both drawing hard-locked a GPU); build pipeline for the real back-buffer format.
- 60a8ba47: render hook — 2.31 read `Rendering::Context` via displacement at present-fn+0x1F then fixed offsets devices `0xC97F38`, direct queue `0x13BC4D0`; 2.5 `SDeviceData` swapchains at `0xCEBF30`, queue `0x14104D0`. Fix: `RED4ext::GpuApi::GetDeviceData()` + `swapChains.GetData(id)` (1-based id) with IsValidID/IsUsedID. Crash signature: died 1.5 s after load before game init. [2.5-verified]
- af83913e: on cold start NetworkWorldSystem can be undefined when `PopulateMenuItemList` runs => vanilla menu; bounded poll (~5 s of 0.25 s ticks) then `MpRefreshMenu`.
- 835ab034/d3a04459: redscript aborts the WHOLE mod on one bad file (e.g. SYM_REDEFINITION); 113 game-class hooks compile as one unit => vanilla menu without CONNECT after a patch.
- 9e37b8ff: first save out of creator reads `gamedataLifePath::Invalid` (PlayerDevelopmentSystem not populated) = 4294967295 (reaches script as -1; declared last); compare enum members.
- 8d2d08a8: restore settle latch `m_restoreReadyAt` set once and never cleared: first restore waits 180 ticks (~3 s), later ones fire next tick and silently don't land ("call returned success while script never ran"). Fix: reset every restore.
- 5943a8f1: strip after world reload saw "no equipment system"; template equipped gear stayed; retry at settlement. Template inventory strip took back 18 stacks, 4 equipped, 16 engine-granted stacks.
- 82d20c8a: unequip is queued (async); backpack/strip/money passes immediate.
- 30e1fcf8: 17 vanilla paths bypass the mod's Economy (money not server-authoritative).
- 1050d2d9: slots: public 1, staff 4.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 9. HUD / UI / archives / tweaks / saves / Steam)_

- 68faf1c0/028ab8ab/045f0e10/57c8febc: **one bad .reds, or the same class/struct/enum defined in two .reds files, aborts ALL redscript compilation.** The game then starts with no mod scripts and looks like the stock game. v0.3.115–.117 shipped 12 duplicate Ink controllers (stale flat copies plus Ink/ copies). Copy-Item -Force never deletes, so the shipped tree must be mirrored. scc compiles every .reds as one unit; 73 definitions.
- f1c9d718: when redscript rejects the mod, the C++ half still connects and the server works (1 ms ping), but all scripts are dead. "Empty map" means a client script failure until proven otherwise.
- 819740bf: the plugin finds `assets/redscript` next to its own DLL path. If the DLL is a symlink, the path resolves to the link target ("canonical: The system cannot find the file specified"), RED4ext unloads the plugin, and the 46 .reds files never register. Copy the DLL, don't symlink it. PowerShell shows reparse points as 0 bytes.
- 343b912c: block local saves with the engine's own gate. `SaveLocksManager` / `IsSavingLocked` cover quicksave, manual save and autosave; AddSaveLock refuses duplicate reasons (saveLocksManager.script:29). Arm it at the main menu, because character creation runs before connect. Loading is not blocked: the mod loads the template itself through LoadLastCheckpoint.
- 848a4000/197d02ca: the pause menu pauses the world. `PauseMenuBackgroundGameController.OnInitialize` queues SetMenuModeEvent, then calls the native `GetSystemRequestsHandler().PauseGame()` (cannot be wrapped). Calling UnpauseGame inline removed the menu. Unpausing after a **0.25 s DelaySystem delay** works. GetSystemRequestsHandler exists only as a class method (widgetController.script:84, menuDefinitions.script:28); there is no GameInstance version. Reverted in 848a4000: removing the Save/Load entries (9f46bbef), which the user did not want.
- 9f46bbef: pause menu items are built in pauseMenu.script PopulateMenuItemList; QUIT is added by ShowActionsList. Difficulty setting: `/gameplay/difficulty` var `GameDifficulty` (not `/accessibility/difficulty`). VeryHard is found by name and the option list is cut to one entry. Arm from SingleplayerMenuGameController.OnInitialize and gate on IsModEnabled (`--online`), not IsConnected.
- c6277d25: Ink lays out against **1920×1080**. Chat box: BottomLeft anchor, margin(60, 0, 0, 200) (ChatController.reds).
- a57eccda: the client helper that turns a TweakDBID into a name **returns empty strings on 2.31**.
- 564d094f: TweakDB keeps record names as hashes; tweakdb.bin is 42 MB. WolvenKit.CLI 8.20.0 has no TweakDB command. Access-point money = RewardMoney(): deck-quality table (100..2500) × (hackingDifficulty × 0.025 + baseMoney), +5 % for an iconic deck. Respec cost = Price.RespecBase + Price.RespecSinglePerk × (spentPerks + spentTraits).
- 8ea04e1f: money is an inventory item (MarketSystem.Money()). Vendors move it with `TransferItem(..., MarketSystem.Money(), totalPrice, ...)` (vendor.script:1180). Three money APIs, including `TransactionSystem.GiveMoney(target, amount, 'money')`. 17 vanilla money paths go unseen. Restore uses GiveItemByTDBID, so mods/quality/upgrades are lost. Inventory.reds:374 has a removal pass gated on IsCharacterStatusKnown().
- 171b04ba: the starter-kit grant of 20000 never reached the game. The client reported 300 and overwrote it.
- 6e7caac9: audioware and red_data are uncurated dependencies.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 9. HUD / UI / archives / tweaks / saves)_

- 304b4929: WolvenKit CLI targets net10.0. Serialized JSON: entry names at hudEntryName.'$value'; widget library item tree at package.Data.File.RootChunk.rootWidget, often a HandleRefId to a HandleId elsewhere. prototype_hud.inkhud has 70 entries; multiplayer_ui has the most permissive context list, otherwise identical to new_phone. dcc67ebe/ee12df28: WolvenKit 8.20.0 tracked by 72 h server updater.
- 77971ee9: chat invisible because 'hud' sits at authored (0,0) until a vehicle animation moves it to (47,1688); playing from_vehicle at connect failed since AsyncSpawnFromLocal is async (widget not yet existing) → run from spawn callback. (96da4bf earlier.)
- 7b4c9531: PopulateMenuItemList does NOT clear (singleplayerMenu.script:843 = AddMenuItem calls + Refresh) → use ShowActionsList (menuItemListGameController.script:79: Clear, Populate, Refresh). Game ships its script sources at <game>\tools\redmod\scripts\ (authoritative for installed patch).
- 01dcacb7: wrappedMethod() is what adds Continue/New Game/Load Game; New Game flow = dispatch OnNewGame directly. C++ Core::Application::Update returns early each frame without --online; exposed Settings::IsDisabled as IsModEnabled() to redscript. Connect()/RequestJoin()/MarkNewCharacter() refuse in C++ without it.
- 3460ecc8: PauseMenuBackgroundGameController.OnInitialize broadcasts menu-mode event then GetSystemRequestsHandler().PauseGame() (native import, can't wrap) → wrap method and unpause immediately after (1 frame). REVERTED 858d723d's pause-menu Save/Load removal (misread request).
- 858d723d / 77971ee9 / d5d506fb: difficulty = UserSettings group "/gameplay/difficulty", var "GameDifficulty", string_list ["Story","Easy","Hard","VeryHard"] (no Normal); resolve via GetIndexFor("VeryHard"). Control is native (not in redscript). ConfigVarListString.SetValues cuts list to one entry (writes player's OWN settings → restore on disconnect). /accessibility/difficulty is wrong (AdditiveCameraMovements). Arm in SingleplayerMenuGameController.OnInitialize; gate IsModEnabled().
- Money: GetItemQuantity(player, MarketSystem.Money()) (vanilla computerYaibaShowroom.script:452, vehicleShopGameController.script:422) (24429bfe). 2fc7f94c: restore removed (stored − held) every spawn → confiscated earnings; gated to first spawn via roster spawned_before. Every capture read exactly 20000 (unresolved).
- 2b60289a: launcher installs world template save "MultiplayerStart" into saves folder.
- ca3fa28f: character ids 8 chars "AEC-MJ6P", check symbol = weighted sum mod 23, weights 2..7; catches all single substitutions (308,000 tested) and adjacent transpositions (28,697).

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 9. HUD / UI / archives / tweaks / saves / Steam)_

- 96da4bf2: custom HUD 'hud' canvas authored at (0,0) in multiplayer_ui.inkwidget; only to_vehicle/from_vehicle anims moved it (to (47,1688)) → play from_vehicle on connect. Chat looked "invisible" until entering a car. [2.31]
- f61b1b6f: inkWidget native fields (2.31): parentWidget at +0x168, layerProxy at +0x148; take parentLock. inkWidget script on 2.31 has Reparent but no GetParentWidget. prototype_hud.inkhud has 70 entries; multiplayer_ui entry identical to new_phone except ignoreHudScaleOverride. [2.31-only offsets]
- c2adfb9b / 3a388257: FTLog reaches no collected file → use own ScriptLog. inkTextInput is importonly, cannot be constructed from script (09bb0e0a).
- 99341fc1: time dilation: wrap SetTimeDilation, SetTimeDilationOnPlayer AND SetTimeDilationWithProfile (radial menus; profileName is `const ref<String>` = script_ref<String>; timeDilationHelper.script:28). Pause comes from PauseMenuBackgroundGameController.OnInitialize (pauseMenu.script:26). [2.31]
- Save/persistence: 019a4ccf save at top of OnBeforeWorldDetach; fbdff4a6 join from main menu detaches world and reloads local save → body is singleplayer V; m_characterLive gate on all save paths; possessions autosave every 90 s. TweakDBID = CRC32(name) | (length << 32); TDBID.ToStringDEBUG returns empty on 2.31 release; verify ids by hashing and searching r6/cache/tweakdb.bin (31590288). Lifepath enum order {Corporate=0, Nomad=1, StreetKid=2}; PlayerDevelopmentSystem.GetLifePath. RemoveAllPerks (playerDevelopmentSystemRequests.script:150).
- Redscript traps: no `continue` (UNRESOLVED_REF kills ALL scripts) 7ff11767; no hex literals (0xFFFFFFFFul parse error) ccd4cb3c; adding a field to a native class → "constant pool error: definition not found"; @addMethod inside own module → UNRESOLVED_REF (70a34970); RegisterInputListener doesn't dedupe.
- Mods: Audioware (12001) + RedData (14139) boot-block (RED4ext "invalid native definitions") 1d09e72c; Nexus 4198 = ArchiveXL dup (27 files) 456653a6; ArchiveXL pinned 1.26.0; TweakXL 4197, Codeware 7780 ship as pinned prereqs; orphan .reds with unresolved natives abort whole compilation (000a322f).
- Crash dumps in REDEngine\ReportQueue (no WER); PDB won't load (dbghelp SymType None); MINIDUMP_THREAD stack descriptor at +24 not +16; /OPT:ICF folds helpers (08ac5b15). AMD amdkmdag.sys TDRs (50 in 10 days) masqueraded as mod crashes (98ae4734).

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 9. HUD / UI / archives / tweaks / saves / Steam)_

- 1375cb46: "/" chat hotkey broken on 2.31 — mod ships a **2.2-era `prototype_hud.inkhud`** that registers the key listener. f058cc48: chat widget visibility comes from .inkwidget authored default + unplayed animations → set explicitly; re-asserted per message (4b249fd4). `multiplayer_ui.inkwidget` is compiled binary → runtime-built labels.
- f058cc48: Ship only installed Client target → Tweaks/Inputs/Archives never deployed. Input Loader = jackhumbert/cyberpunk2077-input-loader (MIT).
- c3ab88bf: ImGui Win32 backend SetCursor each NewFrame → set NoMouseCursorChange. d014c945/d99494c4: with overlay off create nothing on the D3D12 device (no heaps/allocators/cmdlist/ImGui ctx) — CET ImGui overlay on same present path = GPU hard-lock risk. f32cc32c: bounds-check Present hook device index (hand-derived array).
- 30c9ba3b: main menu is itself a world → OnWorldAttached fires ~0.5 s after startup; join decision must live on a game system (NetworkWorldSystem) that survives loads.
- Saves are native; quicksave/autosave still write files; only Load is dangerous.
- CET compatible with the mod (3b584294, 23a55c00); CET disabled for a while via version.dll renamed.
- 8a50a9c1: native event fields need matching `public native let` in redscript or UNRESOLVED_MEMBER.

## 10. Build gotchas (toolchain, SDK, RED4ext/RedLib, Proton)

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 10. Build gotchas / toolchain / SDK / Proton)_

- 5e749fa6: 2.5 shipped 22 Oct 2026; dependency set: RED4ext 1.30.0→1.31.0 (min game 2.5), redscript 0.5.31→0.5.32 (2.5 bundles), TweakXL 1.11.1→1.12.0, input_loader 0.2.3→0.2.6 (needs RED4ext ≥1.31.0; repo jackhumbert/cyberpunk2077-input-loader), Codeware 1.18.0→1.21.2, ArchiveXL 1.26.0→1.28.6. Patch wiped winmm.dll, RED4ext.dll, plugin folder, scc.exe. RED4ext 1.31.0 sha prefix B9AE4351...E9DF621E.
- 8eca2871: RED4ext 1.30.0 (2026-03-09) removed the max-SDK-version check, added API v1; addresses from `cyberpunk2077_addresses.json`.
- e797867b/0a924940/904b592a: SDK = WopsS RED4ext.SDK `5246e659` (patch 2.5) + tiltedphoques fork's decode commits (35 commits / 90 files of hand-decoded structs; fork base `f52af6a3`), offsets carried and pinned by size/offset asserts, vendored in-tree with NCO-PROVENANCE.md (which decodes verified vs UNVERIFIED for 2.5). 2.5 SDK changes: callback buffer 32→64, changed hashes, game-system virtual ABI, plugin API `RED4ext::v1` (PluginHandle, Sdk, EMainReason, PluginInfo, GameState) + v1 macros. RedLib upstream `a88797f` (2.5 registration fixes) minus TDataBuffer mapping. API: DynArray `.size`→`Size()`, `->entries`→`Data()`; `Red::Detail::GetFunction`→`Red::GetFunction`; `Detail::CallFunctionWithArgs(func,ctx,args)`→`Red::CallFunction(ctx,func,args)`; TagList single-tag Contains/Add need explicit CName; pin `Red::SharedPtr` to engine's. First fork-only blocker: `inkTextInputWidget` caretPosition.
- f42065f8: 2.5 script structs (Codeware 1.21.2 `src/Red/ScriptBundle.hpp`, commit b054150f): ScriptDefinition 0x18→0x40 (fields +0x28); ScriptClass `functions` 0x48→0x90; ScriptBundle `classes` 0xD0→0xD8 (leading pointer). ScriptProperty `type` at 0xB8; defaults must be `RED4ext::Map`. Crash signature: AV reading 0x0 at CyberpunkMP.dll+0x6AA40, 313 ms after scc. [2.5-verified]
- bdeed85f: 2.5 ScriptFunction: `isNative` bit at 0xC0 (unasserted); cross-check via `func->parent`, `param->function`. Game shows max 9 "Missing native function" errors.
- efd88d15: `kDecodedLayoutSize` must be static for MSVC C3493 (CI compiler).
- 30e1fcf8: GCC needs explicit `<utility>` for std::move (MSVC transitive) — server builds with GCC in a container.
- b17a9725: xmake `import` exists only in script scope (on__/after__ callbacks); description-scope `os` lacks `iorunv`; Lua resolves globals in the creation environment. Full build 128 s. xmake sandbox has no `pcall` (use try/catch) (2a59ad9c).
- 2a59ad9c: upstream post-build install deleted `red4ext/plugins/zzzCyberpunkMP` then symlinked — symlink needs Developer Mode/elevation, so failure wiped the install. cf5fe026: `xmake install` can run with a stale file list and still print "install ok!".
- 75c5f080: ImGui not pinned in xmake; moved to 1.92.9 silently.
- 58881c82: RED4ext plugin path: following a dev symlink made `canonical(scriptPath)` throw => RED4ext "did not initialize properly, unloading" => vanilla game. UAF from `canonical(p).c_str()` on a temporary.
- 146cc83b: RPC bindings are generated by RpcGenerator, which launches the game with `--rpc` (not by the build).
- 3596feea: xmake dep name is `libopus`; package cache `%LOCALAPPDATA%\.xmake\packages`; stale PCH not cleared by reconfigure (b9b8124c).
- d3a04459: compare game ProductVersion ("2.31"/"2.5"), not FileVersion (3.0.80.x).
- 0be76dc0: in a vectored handler, ExceptionAddress of an MSVC C++ throw is the runtime throw site (same for all throws); ~20 benign C++ exceptions in the first 250 ms across engine threads.
- 46f61a5b/ee038466: Cyber Engine Tweaks treated as critically incompatible (GPU hard-lock with the mod's overlay).
- b3580d49/60a8ba47: install the crash reporter first thing in plugin Main (startup AV ~300 ms after logger init otherwise unrecorded).

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 10. Build gotchas)_

- d342fdc6: pinned prerequisites: **RED4ext 1.29.1, Codeware 1.18.0, ArchiveXL 1.26.0, TweakXL 1.11.1** (2.31 era).
- 93d3551c/3b05ab9b: `scc.exe` does **not** ship with stock Steam 2.31. It comes with redscript (`engine\tools\scc.exe`). Reading vanilla .script sources needs the REDmod DLC (free on Steam). CET is listed as incompatible with the mod (35e330b0).
- 75d6ece5: the server is built with GCC in a container and dev builds with MSVC. **34 missing includes in 24 files** compiled under MSVC only. A failed container build keeps the old image running and does not roll back (3 h outage on 2026-09-02).
- fc8a950c: spdlog's bundled fmt needs `/utf-8` on MSVC; glm and spdlog are header-only.
- 77e3c814: atomic writes use std::filesystem::rename (MSVC: MoveFileExW REPLACE_EXISTING) and fsync/_commit. Do not include <windows.h> in shared headers: its min/max/GetMessage macros leak. Temp file names must be unique per writer.
- c2ea7a71: electron-builder needs Windows Developer Mode, because winCodeSign extracts macOS symlinks.
- 819740bf: `code/server/admin/xmake.lua` wipes `build/windows/x64/release/assets` on every build. It collided with the mod's assets folder. The RedTypes.cs build output is a 218-byte stub; the staged one is 2366 bytes.
- cecc1ff5: tools resolve the repo with `git -C $PSScriptRoot`, not from the current directory.
- 34568c5c: crash **0x80000003** about 2 min in with a 0-byte log. An exception in launch-argument parsing escaped before the terminate handlers were installed (they are installed later, in NetworkWorldSystem), so std::terminate called abort. Fix: catch it and use the defaults.
- dee53337/8e51c2e1: the feed binds IPv4 only, so use 127.0.0.1, not localhost (::1). The game server binds dual-stack. Game port 11778; coord/key endpoint 11780.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 10. Build gotchas)_

- ae39b9a0/dcab5689: server builds with xmake/MSVC on Windows but runs GCC (libstdc++) in a Linux container: MSVC gives <cstdio> transitively via <string>, GCC doesn't → std::snprintf errors (MessageStore.h, CallStore.h, TradeStore.h, VehicleSeats.h); ChatSystem.cpp missing <map> (PCH = iostream/filesystem/bitset/fstream). Rule: include what you use. A failed build silently kept the old container image serving.
- 88028149: PowerShell 5.1 `Set-Content -Encoding UTF8` writes a BOM → redscript "syntax error at 1:1"; `Get-Content -Raw` reads BOM-less UTF-8 as ANSI (mojibake £→Â£). A redscript native without RTTI_METHOD fails at LOAD (not compile) and kills ALL mod scripts (113 natives). cmd `\"` quoting broke `/Fo:"$out\"` → D8003. Verify.ps1 gate (cdaa55fc).
- 6ca6f835: deploy script fetching 'origin' while branch tracks 'fork' exits silently. Linux build unverifiable locally (no GCC/WSL/Docker).
- 5fe82f0f: manifest signing — signing before installed launchers have the pinned key = BAD SIGNATURE lockout. Ship.ps1 never cleans distrib → leftovers in assets\Archives ship.
- 3460ecc8: PowerShell vars case-insensitive ($out vs $Out). electron-builder refuses Windows icon < 256x256.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 10. Build gotchas)_

- f12def17: xmake under Git Bash detects mingw → pass `--plat=windows` for MSVC.
- 06d732ad / 2eca6ab7: CI failures = GitHub 403 on anonymous clones (xmake build-artifacts, `xmake repo --update`), 504 on cryptopp/flecs/entt; fix insteadOf token rewrite + retries.
- bc42d88e: GridCell::ForEach template<class T, class... Args> vs template<class T> ambiguous with empty pack.
- flecs v4.0.3; FLECS_DEBUG + NDEBUG = #error, and FLECS_DEBUG changes ecs_stack_cursor_t layout (662e054d). mimalloc secure=true, mi_register_error routing (6d39ab69); flecs must be routed to mi_* (ecs_os_set_api_defaults leaves system allocator) b3f4d675.
- RED4EXT_SEMVER from NCO_BUILD_VERSION; dev Server.Loader locks Server.Native.dll preventing local link.

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 10. Build gotchas)_

- 9732c461 (2.31 working snapshot): **protobuf-cpp pinned 29.3** (35.1 removed `FieldDescriptor::is_optional()`; 3.19.4 predates RecordError/absl::string_view API); **SdkGenerator on CppSharp 1.1.84.17100 / net9.0** (1.1.5.3168 bundled Clang too old for MSVC 14.44 STL → STL1000).
- 9732c461: **RedLib type-name bug on 2.31**: failing concept made `HasTypeNameBuilder` reject explicit name → fell to `U::NAME` → NetworkWorldSystem registered as base "gameIGameSystem", accessor "GetGameIGameSystem" → script validation failed, RED4ext refused to start. [2.31-verified]
- Toolchain pins: **MSVC 14.44 / Windows SDK 10.0.22621 / xmake 3.1.0** (02ab05a2, a2761c2a; CI moved from xmake 2.9.6, SDK 10.0.19041; 777d2363 unpinned SDK on CI runners). Windows build `-j 4` or lower; container `BUILD_JOBS=2` (OOM kills silently).
- e30edb40/321d635c: protobuf 29.3 forced transitively on all platforms; **abseil forced to 20250127.0** (gns caps `<=20250127.0` in one repo snapshot, `<=20260107.1` in another); GNS rebuild ~5 min.
- ac5f0677: entt v4.0.0 needs cmake ≥3.28; Debian cmake 3.25 (Recommends of xmake-data) must be removed. 4a5662bb: build stage .NET SDK 9 (SdkGenerator net9.0), runtime 8.0. 6319b88b: CppSharp 1.1.84 linux-x64 assets target net10 → Linux pins previous CppSharp. 6e6c11cb: GCC rejects nested-class default member initializer while outer class incomplete. 865be811: netpack generation serial in before_build (cross-proto includes race). c70383a1: SdkGenerator needs .git (LibGit2Sharp Discover). 8f772d957: static OpenSSL needs SSL_CERT_FILE in container. 48d35de0: pnpm EAGAIN on ZFS → hardlink.
- redscript gotchas: one bad .reds aborts ALL mod script compilation (1dc23d66); class params need ref/wref; no `continue` in for loops (UNRESOLVED_REF); no Int64→Int32 cast; FTLog output goes nowhere readable → added native `ScriptLog` into spdlog (4a41f34c); wrapping a method with a LOCAL array for a `script_ref` param crashed (697b4ec2). scc.exe gate: paths file must have no BOM; with `-compilePathsFile` scc ignores `-compile` dir, must list each RED4ext plugin's Scripts folder (ad3f002e); scc pops a blocking MessageBox on failure → run detached (5cc439a1). TDBID has ToNumber, no inverse (native helper).
- RED4ext loads every plugin subdirectory DLL (renamed folders still load) (3c7904f8, aec0bade).
- Upstream 2024: 98b4130a "support patch 2.2" (load settings during game init state; skip NetworkWorldSystem callbacks when plugin disabled); 393362b8 custom redscript Logger removed (crash with Codeware GetStackTrace) and reverted d5f1c878 (revert of 06be799a); 6c5710db enable RTTI; e7e638f5 flecs v4.0.3; cbeca00b xmake 2.9.6 fixes protobuf cache issue; 2972a70b `Cyberpunk2077` xmake target for debugging.

## 11. Address-library hashes / raw addresses / offsets

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: 11. Address hashes / raw addresses)_

- 2582323196 (u32) — b88e1040: function containing `Cyberpunk2077.exe+0x3E7863` (range 0x3E7848..0x3E789B) on 2.5; site of test.74 observer crash (MMC attach) and test.83 crash. Lookup rule: map address to .pdata function START (follow CHAININFO for cold blocks), never nearest-lower.
- `exe+0x3E7863` / write to `0x7FF7012DBCE8` — 74de8dfd test.74 crash 2.2 s after MMC attach. [2.5]
- `exe+0x887DC4` — 929d924f crash reading 0xFFFFFFFF when dressing via ITransactionSystem vtable slot 0x250+. [2.5]
- 2.31 render offsets: present-fn displacement +0x1F, devices `0xC97F38`, direct queue `0x13BC4D0`; 2.5 SDeviceData swapchains `0xCEBF30`, queue `0x14104D0` (60a8ba47).
- CyberpunkMP.dll+0x6AA40 (f42065f8) — our own DLL, script validator hook.
- Struct offsets: CharacterCustomizationSystem size 0x388 (2.31) / 0x3D8 (2.5), state handle +0x78; IComponent::owner 0x50, IComponent size 0x90; MMC: type at +0x8, controller list component+0xA8, AnimationDriver 0x30, AnimationData 0x3C, IdleController vtable 19 slots; ITransactionSystem vtable 0x250+ (2.31 only); ScriptFunction isNative 0xC0; ScriptBundle classes 0xD8; ScriptClass functions 0x90; ScriptDefinition size 0x40.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: 11. Address-library hashes / raw addresses)_

- No address-library hashes, vtable slots or raw game addresses in this chunk. Hashes seen: protocol ids (section 1), the ccstate hash `78a96dec…` (truncated in the message). The tailnet IPs are infrastructure, not game data.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: 11. Hashes / addresses mentioned)_

- 0xb000b1d029d0cea0 = FNV1a64("seat_front_left") (CName hash) (f34e1462).
- ccSystem + 0x78 = customization state pointer read by GetCustomizationState(), null in gameplay (e795a522) [2.31-only].
- Protocol kIdentifier = FNV1a64(schema string) (68d34b42).
- No address-library u32 hashes or raw addresses in this chunk.

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: 11. Address-library hashes / raw addresses)_

- None in this chunk. Raw offsets: inkWidget parentWidget +0x168, layerProxy +0x148 (2.31); Codeware entity status byte +0x156 (2.31). TweakDBID BaseOverheatHack = 0xC9259006, length 26 (96da79d9).

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: 11. Address hashes / raw offsets mentioned)_

- `ccSystem + 0x78` → CharacterCustomizationState (2.31).
- `gameuiCharacterCustomizationSystem` size 0x388 (unk48 .. 0x388) in 2.31 SDK.
- `moveComponent->speed` hand-mapped offset moved on 2.31 (value ~3e8 garbage) — offset not given.
- Seat CNames = FNV-1a 64 of seat name (e.g. `seat_front_left`).
- No address-library u32 hashes in this chunk.

## 12. Reverts and reversals

### 2026-09-07..11-09 — 2.5 port era (c1)

_(chunk heading: Reverts / reversals)_

- 6e1c52a6 reverted the HUD modal context (166d5a42, 889d4831): blocked chat typing, no cursor, no freeze.
- 0662a27a / e3c5e455 reversed 74de8dfd (MMC retired on 2.5) after static layout check + crash-free test.85/86.
- 832f3eb1 superseded fbade8f7 (TeleportationFacility doesn't visibly move the puppet) and found 162aca8c's archetype-move theory "a misread" (real cause: SpawningComponent field shift).
- ce05d986 corrected 286c0312 (capture only "in world" captured the template; pre-detach save is the right moment).
- 712817b6 corrected the "engine refuses ReFinalizeState" claim.
- cdfc817a corrected a7885f1 (menu text was painted in the backdrop image).
- 3d7e8a23: Songbird fix attempts (8) reverted 2026-08-20; mitigation kept.

### 2026-09-04..09-07 — 2.31 (c2)

_(chunk heading: Reverts)_

- d4b2172e reverts 2a648bfd: the Tailscale invite was put back into server.json. Gating it is impossible because the gate would be on the tailnet itself.
- 848a4000 reverts the pause-menu Save/Load removal from 9f46bbef (it misread what the user asked for).
- 197d02ca fixes 848a4000's inline unpause, which removed the menu.
- 8ff65d0e: the Stage 6 metadata rename was reverted before being applied (netcode freeze for the server swap).
- 101dd2f6: a feature-branch ship republished a stale server.json; it was put back from main.

### 2026-08-24..09-04 — 2.31 (c3)

_(chunk heading: Reverts)_

- 3460ecc8 reverts 858d723d's pause-menu Save/Load removal (request misread; wanted world not to pause).
- 99355161 reverts an earlier launcher hint (100.125.74.56 IS the test server; real issue was stale branch on box).
- 858d723d: an accidental rewrite of Difficulty.reds restored from HEAD (would have used wrong /accessibility/difficulty group).
- 6ac90d13: CreateCharacterRequest removed (declared, never sent/handled).
- aa9808ba mapped "support" to kModerator; superseded by 4f091ade adding kSupport=5 / kEventStaff=15 (kPlayer 0, kModerator 10, kAdmin 20, kOwner 30).

### 2026-08-20..08-28 — 2.31 (c4)

_(chunk heading: Reverts in this chunk)_

- a9fbcaf2: ecs_iter_fini in .run() → server segfault.
- cec1343d: iterator drain → client hang.
- 5b097d9e: reverts c13231af (VehicleComponent RecordId string, crashed: unregistered component).
- 1ad3bdfd: reverts connect-during-creator (3beb47c8) → froze on PL prompt.
- 902bfacf: Songbird holocall attempts (8) reverted to 697b4ec.
- 73380002: character selector removed from main menu (never live-tested); later re-enabled (c5ee252e) with 2.5 s timeout fallback (10 polls × 0.25 s).
- 6652b90b: correction of wrong commit-message reasoning (652551fc).

### 2024-11..12 upstream CyberpunkMP + 2026-08-09..08-20 NCO on 2.31 (c5)

_(chunk heading: Reverts)_

- d5f1c878 (2024-12-01): reverted 06be799a custom redscript Logger — crash with Codeware GetStackTrace; uses FTLog.
- f32cc32c: reverted eight quest facts, appearance-poll instrumentation, RTTI dump that shipped in v0.3.77 and spammed logs.
- bb738cad / d3587258: reverted Player_Puppet_Base remote record (1845cfd3) to mannequins — frozen statues/painted clothes; then player records default again on driver path (3a6f22f7), then mannequins baseline again (d3587258).
- 7d2b1b2f: reversed "template only conditionally" — multiplayer boots template every launch once server overwrites identity.
- d2935a2e: /push removed (puppets can't animate stumble).
- 91016b8b: deleted code pretending to open the creator (APIs don't exist in redscript).
- 27a14c3c: removed death-menu 3 s watchdog that restored FLATLINED.
