# Networking stack and protocol

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

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
