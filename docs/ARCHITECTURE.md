# tandem — architecture

tandem is a co-op multiplayer mod for Cyberpunk 2077 (patch 2.50, PC, Windows
or Proton), written from scratch. Two to four players share **one Night
City**: same story, same enemies, same time and weather, each with their own
V. The goal is not "see each other in your own games" but **one world that
several games render**; every design choice follows from that.

This file is the core. Each subsystem has its own file: read only those your
task touches ([Topic files](#topic-files)).

## Principles

1. **One world, one decider per thing.** Every piece of shared state has
   exactly one owner at a time. The others display it; they never simulate a
   competing copy. (The campaign fork let every game run its own copy of each
   enemy and synced only health: enemies stood in different places on each
   screen. That is the failure tandem is designed against.)
2. **Your own V is yours.** Movement, aiming, shooting and menus never wait
   for the network. Responsiveness of your own character beats strict
   consistency; corrections arrive as gentle blends, never snaps.
3. **The host's game runs the shared rules; each game runs the city
   around its V.** Quests, time, weather and the session are the host's.
   NPCs are owned per NPC by the game that runs them: each game keeps the
   crowd around its own V, and players near each other see each other's
   NPCs as proxies (`architecture/regions.md`).
   The traffic and the police around a V are its area owner's; a V's own
   heat stays its own game's.
4. **Facts over guesses.** Engine access goes by name (RTTI, scripts,
   Codeware) or by address-library hash. A hand-measured offset or vtable
   slot is used only behind a layout check
   ([Talking to the engine safely](#talking-to-the-engine-safely)).
5. **Seen and heard.** Everything a puppet shows comes with the game's own
   feedback for it: an animation with its sounds (footsteps, landing,
   reload, holster), a shot with its muzzle flash, shells and report, an
   impact with its decal and hit sound, a vehicle with its engine and tyres.
   A remote action that is only animated, or only heard, is a bug. A
   feature is done when both are there, and the test checks both (a sound
   needs a person listening: note it in the test plan).
6. **Feels native.** Co-op must feel like part of the game, not a mod on
   top of it: smooth motion with no pops or snaps, the game's own
   animations, sounds, UI style and prompts, no debug-looking stand-ins in
   front of the player (a puppet that is invisible, naked, frozen or
   sliding is a bug, not a phase). When a feature works, polish it before
   calling it done: transitions (getting in a car, someone joining,
   someone leaving), edge cases, and how it looks from the other player's
   side.
7. **Testable alone.** Everything that is not engine glue lives in `core/`,
   runs on Linux, and is tested without the game. The mock player
   (`tandem-peer`) stands in for a second game.

## Components

- **Game PC** (Windows or Proton): `Cyberpunk2077.exe` loads, through
  RED4ext, `tandem.dll` (`plugin/`) with a game thread (sample V, apply the
  world) and a net thread (`core/`'s `Endpoint` over ENet); and, through
  redscript, `r6/scripts/tandem` (UI, events).
- **Linux**: `tandem-peer` (`peer/`), the mock player, on the same `core/`.
- **Kubernetes**: `tandem-relay` (`relay/`, Go), which forwards opaque
  datagrams.
- Games and mock players talk UDP, direct or through the relay.

<!-- prettier-ignore -->
| Path | What | Built or checked by |
| --- | --- | --- |
| `core/` | protocol, transport (ENet), session (`Endpoint`); platform-independent C++23 | both CMake presets |
| `plugin/` | the RED4ext plugin (`tandem.dll`, Windows x64): entry points, config, net thread | `windows` preset (clang-cl) |
| `peer/` | `tandem-peer`, the mock player (Linux) | `linux` preset |
| `tests/` | doctest unit and loopback tests of `core/` (Linux) | `linux` preset |
| `relay/` | `tandem-relay`, NAT fallback (Go, stdlib only): `cmd/tandem-relay`, `internal/relay` | Go / `nix/relay.nix` |
| `nix/`, `default.nix` | relay package and OCI image; redscript (the game's scc, built for Linux) | `nix-build -A relayImage` |
| `deploy/kubernetes/base` | kustomize base for the relay | `task k8s:check` |
| `scripts/` | redscript, installed to `r6/scripts/tandem`. Each natives file mirrors one of `plugin/src/`: `Natives.reds` `natives.cpp`, `StoryNatives.reds` `story_natives.cpp`, `CombatNatives.reds` `combat_natives.cpp`, `SyncNatives.reds` `sync_natives.cpp`, `ElsewhereNatives.reds` `elsewhere_natives.cpp`, `RegionNatives.reds` `region_natives.cpp` | `task scripts:check`, `task natives:check` |
| `tweaks/` | TweakXL records, installed to `r6/tweaks/tandem` (the puppet records, their attitude groups, the player marker) | — |
| `dist/` | files installed next to the DLL (`tandem.ini`) | — |
| `cmake/` | `clang-cl-msvc.cmake`: the Windows cross toolchain | — |
| `docs/` | this core; `architecture/`: one file per subsystem; `research/`: engine facts with sources, design research, `data/` (RTTI and skeleton dumps) | `task docs:check` |
| `tools/` | `game.sh` (drive the game), `scenario.sh` + `scenarios/` (in-game tests), `build.sh` and `remote.sh` (build here or on the game machine), `check-natives.py`, `scripts-check.sh`, `docs-check.py`, `docs-tables.py`, `re/` (read-only reverse engineering, see its README) | — |
| `vendor/` | git submodules: RED4ext.SDK, RedLib, ENet, zpp_bits, doctest | — |

The build is Linux-only: the plugin is cross-compiled with clang-cl and
lld-link against the MSVC CRT and Windows SDK that `xwin` downloads. No
Visual Studio, no Windows machine. `direnv allow` loads devenv
(`devenv.nix`): CMake, Ninja, clang-cl/lld-link (unwrapped LLVM 20, through
`TANDEM_*` variables), xwin, Go, go-task, kustomize, kubeconform. nixbook's
shared hooks (treefmt with clang-format, nixfmt, prettier, gofumpt, shfmt;
shellcheck) come from `npins` → `nixbook/devenvModules/devenv.nix`. The
Windows sysroot (MSVC CRT + Windows SDK 10.0.22621) lives in `$TANDEM_XWIN`
(default `~/.cache/tandem/xwin`); `task sysroot` fetches it. SDK 10.0.26100
does not work with xwin.

## Session and transport

More (handshake, liveness, slots, reaching each other): `architecture/session.md`.

**Topology: a star.** One host, up to three guests. Guests talk only to the
host; the host relays what one guest sends to the others. The host is a
player's game, not a server: there is no dedicated game server.

**Transport: ENet over UDP** (`core/src/session.cpp`), three channels:

| Channel | Delivery                | Carries                                                     |
| ------- | ----------------------- | ----------------------------------------------------------- |
| State   | unreliable, newest wins | high-rate snapshots: player and NPC transforms, body poses  |
| Event   | reliable, ordered       | everything that changes state: looks, hits, deaths, facts…  |
| Bulk    | reliable, ordered       | large one-offs (join snapshot) so they never hold up events |

**Protocol** (`core/include/tandem/protocol.hpp`): every message is one
alternative of a `std::variant`, serialized with zpp_bits. The variant index
is the message id, so **messages are only ever appended**, and changing an
existing message bumps `kProtocolVersion`; a mismatched guest is refused with
a reason. Decoding is strict: trailing or missing bytes reject the packet.

**Time**: one clock, the host's; every state carries host-clock
microseconds, and absolute times are integers, never floats
(`architecture/smoothing.md`). Shared state stays the same in every game
through digests, resyncs, idempotent application and epochs
(`architecture/consistency.md`).

## Authority: who decides what

The owner simulates; everyone else displays.

<!-- prettier-ignore -->
| State | Owner | Others |
| --- | --- | --- |
| a V: transform, stance, aim, animation | that player's game | puppet, interpolated |
| a V: appearance, equipment, cyberware | that player's game | applied on the puppet when it changes |
| a V: health, death, downed | that player's game | shown; revives are requests to the owner |
| a V: level, XP, perks, inventory, money | that player's game (save) | not shared: each player keeps their own level |
| a vehicle someone drives | the driver's game | puppet vehicle, passengers mounted |
| a vehicle a V rides that its game moves (cab, quest car, AV) | that player's game | copy, riders and NPCs seated; host's scene: a seat or a ride along |
| an NCART train (scripted transit) | each rider's own game | never shared: puppet in a train of ours, else hidden |
| a world vehicle: traffic, parked, police, a wreck | the owner of its area (`Endpoint::AreaOwner`) | proxy, or their own instance of a placed vehicle; the first player to get in has it (`architecture/traffic.md`) |
| NPCs: AI, position, health, death | the game that runs it (spawned it, or took it over; quest NPCs: host) | **proxy with its AI off** in the linked games, driven by its owner (`architecture/regions.md`) |
| spawns: crowds, encounters | each game around its own V | held back only where a higher-ranked linked player's region covers it |
| quests: facts, journal, phases, scenes | host | followed, never run on their own |
| the story's gameplay restrictions (no weapons, no sprint… during the host's scene) | host (its V's, reported) | worn on its own V by a guest in the host's story moment, taken off when it leaves it (`architecture/story.md`) |
| time of day, weather | host | applied |
| a V's police heat: score, wanted level, stars | that player's game | told; the owner of the area sends police after that V alone (`architecture/police.md`) |
| a V's time abilities (Sandevistan…) | that player's game | shown on the puppet; never slow anyone's world |
| an NPC's speed in a time bubble | the NPC's owner | proxies animated at the same speed |
| NPC shots at a V | the V's game | host's NPC decides to fire; `architecture/time.md` |
| loot: corpses and containers | each player's game | rolled per player: each loots their own copy |
| items a V drops on purpose | host | lie in every game; the first pickup keeps it |
| quest and important items, rewards | every player's own V | whoever gets one, every V gets it; a guest gives the host's quest props (tag `Quest`, keycards) back when it leaves, and keeps iconics and rewards |
| devices: doors, lifts, cameras, turrets, barrels, the alarm | host | own copy first, then the host's word; `architecture/devices.md` |

Enemy level (the mean of the players' levels): `architecture/npcs.md`.

**Combat** follows from the table. A shot is decided by the shooter's game,
on what that player sees, so aiming feels local. A guest hitting an NPC sends
a hit event to the NPC's owner (through the host); the owner applies it to
the real NPC and the result (health, death, stagger) comes back to everyone.
An NPC firing at a player's puppet on its owner's game sends that player a
damage event, applied to its own V. Kills are final in every game because
only the owner's NPC is real.

**Region limits: players anywhere on the map, one world.** Each player
must be free to play on their own anywhere in Night City: one at a gig in
Watson while the other drives the Badlands, or both together. It stays one
coherent world: the same time and weather, the same quest state, the
same dead stay dead, and what one player changes somewhere (a gang wiped
out, a car destroyed, a shop looted, police heat) is what the other finds
there later. Each sees the other on the map and minimap.

A game only simulates what is streamed in around its own V, so the host
cannot run NPCs a kilometre away from it. Hence **authority by
proximity**: the world around each V is run by that V's game (its NPCs,
fights, traffic, police), under the host's rules for what is shared. When
players come together, one game (the host's, or the nearest player's)
owns the region's NPCs and the others show proxies, as now. When they
move apart, ownership hands over cleanly (an NPC in a fight keeps its
health, its target and its status effects). What outlives a region (deaths,
loot taken, destroyed objects, quest facts, heat) goes to the host's record
of the world (the host's save), so it is still true when anyone comes back.

Nobody is tethered: a player who fast travels offers the others to join
(accepted or not, each stays free), and anyone can regroup with another
player at any time (`architecture/travel.md`). **Per-player regions**
(`architecture/regions.md`; research: `research/coop-regions.md`; built and
tested without the game, not yet run in it): each game runs the NPCs
around its own V, players closer than 150 m are linked (190 m to unlink)
and show each other's NPCs as proxies, an NPC is handed over with its state
when its owner walks away from it with another player near, and the NPCs
of a player who vanishes go to the players near them. Apart, a guest's city
is alive; before regions its crowd was off and the host's NPCs were shared
only around the host.

## Identity: naming the same thing in every game

Two games must agree on which NPC, car or object a message is about.

- **Placed entities** (from world sectors, communities, quest prefabs) have
  the same static `EntityID` hash in every game: that hash is their name.
- **Spawned entities** (crowds, traffic, encounter waves, quest spawns) get
  a `NetId` from the game that announces them when they appear (its
  player id minus one in the top 8 bits: `regions::MakeNpcId`). The other
  games do not spawn these themselves: they create a proxy from the
  owner's announcement. Matching by
  "nearest NPC of the same record" is never used: it mismatches waves.
- **Players**: `PlayerId` from the session (host is 1).

## Inside the plugin

Two threads. Each frame tick the game thread samples the local V into an
outbox and applies the snapshots of an inbox; redscript natives (UI, events)
run on its side. The net thread (`NetThread`) runs `Endpoint::Update()`:
the ENet socket, decoding, the interpolation buffers.

- The **net thread** owns the `Endpoint` and never touches the engine.
- The **game thread** never touches the socket. The two exchange plain
  structs through lock-free or mutex-guarded queues, drained once per frame.
- **Engine callbacks may run on job threads** (hit, mount, vehicle events
  reached from redscript): they only enqueue; state changes happen on the
  game thread's frame tick. This race was a main crash source in NCO.
- Redscript is for what scripts do best: UI, player-facing events, reading
  the player state machine. A single broken `.reds` file disables every
  script in the mod: scripts are checked before shipping.

Each subsystem as built: its topic file.

## Talking to the engine safely

In order of preference:

1. **By name**: RTTI classes, properties and script functions
   (`CRTTISystem`, `ExecuteFunction` — with an out slot when the function
   returns something, or it silently does nothing), Codeware's APIs.
2. **By address-library hash** (`RED4ext::UniversalRelocFunc`): hashes
   survived the 2.31 → 2.5 patch; all the ones earlier projects used are in
   the 2.50 list.
3. **Raw offsets and vtable slots**: only behind a **layout fingerprint**
   checked once at startup (for example the class size from RTTI). On a
   mismatch the feature turns itself off and logs why; it never guesses.
   Every hand-measured layout records the game version it was measured on.

Before relying on any engine fact from `docs/research/`, check its tag:
`[2.5-verified]` can be used, `[2.31-only]` and `[unverified]` need checking
in game, `[broken-on-2.5]` must not be used.

## Roadmap

Each milestone ends with something visible in game.

<!-- prettier-ignore -->
| # | Milestone | Done when |
| --- | --- | --- |
| 0 | Toolchain | DLL built on Linux loads in the game and logs; mock player; relay image ✓ |
| 1 | A second V | a puppet follows the other player smoothly (✓ 2026-11-17: engine controller, 0.01 m, walk animation, NPC rig) |
| 2 | Player presence | appearance, equipment, locomotion and aim animations, weapons drawn (pose, look, clothes, held weapon, shot effects ✓ 2026-11-18) |
| 3 | Vehicles | drive together, ride as a passenger (copy, seated driver, get out, leave: ✓ 2026-11-18; passengers to test; rides, quest vehicles, NCART written: to test) |
| 4 | Shared world basics | time, weather follow the host; police heat per player; traffic shared by the owner of its area; fast travel together, no tether (time, weather ✓ 2026-11-18; regroup, traffic, police heat built, untested) |
| 5 | One set of enemies | NPC proxies with AI off on guests; hits, damage, deaths, loot (proxies follow the host's NPCs, hits and deaths ✓ 2026-11-18; loot built) |
| 6 | Story | quest facts and journal follow the host; dialogue choices; scenes for guests (step 1 written: freeze, saves, journal, calls, texts, lines, hubs, rewards; to test in game) |
| 7 | Anywhere | relay fallback in the plugin, invites (join codes), co-op in the main menu: host or pick whom to join (relay and codes ✓ 2026-11-18, loopback; menus written: main and pause menu, runtime start/stop, to test) |
| 8 | Co-op features | revive, pings and shared markers, and what we add as we play (down, revive, respawn, shared game over written; to test in game) |

Alongside every milestone from 2 on: the simulated second game (record,
replay, scripted players, network shaping) covers what that milestone
syncs, so it can be tested without a second PC.

## Topic files

In `docs/architecture/`. Each holds the subsystem as built ("Inside the
plugin") and its open problems.

- `session.md`: handshake, liveness, slots and returning players, reaching each other (direct, relay fallback), solo is vanilla (nothing acts without a session), `tandem.ini` keys. For connection and session code.
- `smoothing.md`: the host clock, interpolation delay, extrapolation, teleports, movement flags and puppet placement, rates. For anything that draws a remote V, NPC or vehicle.
- `consistency.md`: digests, resync, idempotent application, epochs, the audit of every synced state with its guard, unplanned disconnects. For any new or changed state message.
- `players.md`: body poses, weapons, looks, player markers; open: linking up, every way a V moves, cyberware and perks.
- `vehicles.md`: driven vehicles, seats, the radio; open: getting in and out, combat in vehicles.
- `transit.md`: rides a game runs (cabs, AVs, quest vehicles), the NCART metro.
- `traffic.md`: traffic, parked cars and wrecks shared by the owner of their area (untested in game): the budget, collisions, a car a player takes, occupants, explosions; who owns an area (`AreaOwner`).
- `police.md`: police heat per player (untested in game): how a crime finds its V, a wanted V in an area another game owns, what a follower no longer spawns.
- `world.md`: time of day and weather; open: the guest acting on the world, NCPD chases, saves.
- `time.md`: the pause menu, slowed time (time bubbles); open: every time dilation, other timeline and menu, NPC shots simulated by the target's game.
- `npcs.md`: enemy level, NPC proxies, NPC animation, attitudes towards a remote V; open: suppressing a guest's own simulation.
- `regions.md`: per-player regions: links and their hysteresis, who owns an NPC, hand-over without a pop, what travels where, crowd density in the overlap, late joiners and vanishing owners, grouped or apart and holding a region for gigs, the area owner (traffic). For anything about an NPC's owner.
- `combat.md`: hits, outcomes, game rules for every hit, quickhacks.
- `stealth.md`: takedowns, finishers, carried bodies, "the player" reactions, per-player detection.
- `loot.md`: per-player loot, dropped items, quest items.
- `devices.md`: doors, lifts, cameras, turrets, the alarm, access points, computers, barrels, media and other devices: the host's word, a guest's own copy first, revisions, digest, a guest's world put back after; open: glass, a device's shots drawn for the others.
- `revive.md`: down, revive, respawn, shared game over.
- `story.md`: quest freeze, journal, calls, texts, dialogue, scenes, the story's restrictions on a guest in the host's moment, rewards; open: scenes for guests, story decisions, quests together.
- `scene-replay.md`: a guest watches the host's scenes: the stage, the cast, lines and actor poses (`scene_replay = on`, off by default).
- `quest-carryover.md`: (proposal) what a guest brings home: the three tiers, the ledger in the guest's save, edge cases (host ahead or behind, quest props, duplicates), stages and decisions.
- `elsewhere.md`: cyberspace, braindances, replacers: a V outside the city.
- `coop-menu.md`: the co-op menu, invites and join codes in game.
- `travel.md`: fast travel together, regroup; region authority: `regions.md`.
- `relay.md`: the relay's sessions, join codes, abuse limits, wire format, deployment, the game side.
- `testing.md`: test levels, scenarios, every `tandem-peer` mode and flag.
- `recordings.md`: NPC record and replay, gameplay recordings (`.tplay`), recorded host sessions (`.tsession`), the recorder (`recorder = on`: play recorded as it happens, solo or in a session), the planned simulated second game.
- `two-instances.md`: two real games on the game machine (planned, untried there): findings, the setup (`tools/instance.sh`: a game folder and a prefix per instance, outside Steam, sandboxed off the player's files), the first-try checklist; the plugin's `TANDEM_CONFIG`, `TANDEM_CMD`, `TANDEM_PORT`, `TANDEM_LOG` (`core/include/tandem/instance.hpp`).

## Research

In `docs/research/`. Engine facts, each with a source and a tag:
[`engine-knowledge.md`](research/engine-knowledge.md) is the index (sources,
tags, top traps, section number to file) of `engine/<topic>.md`.

Research for the open problems, gathered from the 2.50 game's scripts and
live RTTI (facts with line references, a proposed co-op rule per case, and
the in-game tests still owed; none of it tested in a session yet):

<!-- prettier-ignore -->
| File | Covers |
| --- | --- |
| `pose-streaming.md` | the remote V's body: pose read/write points, retargeting, measured stream sizes |
| `coop-player-vehicles.md` | player state fields, weapons and shots, death/downed, mounting, vehicles, HUD |
| `coop-npcs-combat.md` | NPC proxies, hit/damage/death pipeline, identity, loot, takedowns, quickhacks |
| `coop-scenes.md` | scene tiers, cutscenes, dialogue choices, braindance, Johnny, restrictions |
| `coop-time.md` | every time dilation, pause and time skip, and the game clock |
| `coop-quests-world.md` | facts, journal, persistent state, weather, police, crowds, saves, rewards |
| `coop-quests-design.md` | the story design (milestone 6): what the two files above left open, in steps |
| `coop-perks.md` | every perk tree, relic, cyberware ability and quickhack, by co-op risk |
| `coop-regions.md` | players apart: per-player regions, NPC hand-over, world ledger, staged plan |
| `campaign-fork.md`, `nco-commits.md` | facts from the campaign fork and from NCO's commit bodies (cited by the engine files) |
| `third-person-overhaul.md` | what the Complete Third-person Overhaul mod teaches about drawing a V |
