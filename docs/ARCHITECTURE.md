# tandem — architecture

tandem is a co-op multiplayer mod for Cyberpunk 2077 (patch 2.50, PC, Windows
or Proton). Two to four players share **one Night City**: same story, same
enemies, same time and weather, each with their own V. It is written from
scratch; what earlier projects learned about the engine is collected, with
sources, in [`research/engine-knowledge.md`](research/engine-knowledge.md).

The goal is not "see each other in your own games" but **one world that
several games render**. Every design choice below follows from that.

## Principles

1. **One world, one decider per thing.** Every piece of shared state has
   exactly one owner at a time. The others display it; they never simulate a
   competing copy. (The campaign fork let every game run its own copy of each
   enemy and synced only health: enemies stood in different places on each
   screen. That is the failure tandem is designed against.)
2. **Your own V is yours.** Movement, aiming, shooting and menus never wait
   for the network. Responsiveness of your own character beats strict
   consistency; corrections arrive as gentle blends, never snaps.
3. **The host's game runs the world.** Quests, NPC AI, spawns, time,
   weather, police. Guests' games stop doing those things themselves and
   follow.
4. **Facts over guesses.** Engine access goes by name (RTTI, scripts,
   Codeware) or by address-library hash. A hand-measured offset or vtable
   slot is used only behind a layout check (see
   [Talking to the engine safely](#talking-to-the-engine-safely)).
5. **Seen and heard.** Everything a puppet shows comes with the game's own
   feedback for it: an animation with its sounds (footsteps, landing,
   reload, holster), a shot with its muzzle flash, shells and report, an
   impact with its decal and hit sound, a vehicle with its engine and tyres.
   A remote action that is only animated, or only heard, is a bug. Each
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

```
                      ┌──────────────── game PC (Windows or Proton) ───────────────┐
                      │ Cyberpunk2077.exe                                           │
                      │  ├─ RED4ext ── tandem.dll (plugin/)                         │
                      │  │              ├─ game thread: sample V, apply world       │
                      │  │              └─ net thread: core/ Endpoint over ENet ────┼──┐
                      │  └─ redscript ── r6/scripts/tandem (UI, events)             │  │
                      └─────────────────────────────────────────────────────────────┘  │ UDP
                                                                                       │ direct, or
   ┌──── Linux ────────────────┐            ┌──── Kubernetes ─────────────┐           │ through the
   │ tandem-peer (peer/)       │◄──── UDP ──┤ tandem-relay (relay/, Go)   │◄──────────┘ relay
   │ mock player, same core/   │            │ forwards opaque datagrams   │
   └───────────────────────────┘            └─────────────────────────────┘
```

| Path                  | What                                                         | Built by                    |
| --------------------- | ------------------------------------------------------------ | --------------------------- |
| `core/`               | protocol, transport (ENet), session (`Endpoint`)             | both CMake presets          |
| `plugin/`             | the RED4ext plugin, Windows x64 DLL                          | `windows` preset (clang-cl) |
| `peer/`               | `tandem-peer`, the mock player                               | `linux` preset              |
| `tests/`              | doctest unit and loopback tests of `core/`                   | `linux` preset              |
| `relay/`              | `tandem-relay`, NAT fallback (Go, stdlib only)               | Go / `nix/relay.nix`        |
| `nix/`, `default.nix` | relay package and OCI image                                  | `nix-build -A relayImage`   |
| `deploy/kubernetes/`  | kustomize base for the relay                                 | `task k8s:check`            |
| `docs/research/`      | engine facts from earlier projects, with sources             | —                           |
| `vendor/`             | git submodules: RED4ext.SDK, RedLib, ENet, zpp_bits, doctest | —                           |

The build is Linux-only: the plugin is cross-compiled with clang-cl and
lld-link against the MSVC CRT and Windows SDK that `xwin` downloads. No
Visual Studio, no Windows machine.

## Session and transport

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

## Authority: who decides what

The owner simulates; everyone else displays.

| State                                                        | Owner                     | Others                                                             |
| ------------------------------------------------------------ | ------------------------- | ------------------------------------------------------------------ |
| a V: transform, stance, aim, animation                       | that player's game        | puppet, interpolated                                               |
| a V: appearance, equipment, cyberware                        | that player's game        | applied on the puppet when it changes                              |
| a V: health, death, downed                                   | that player's game        | shown; revives are requests to the owner                           |
| a V: level, XP, perks, inventory, money                      | that player's game (save) | not shared: each player keeps their own level                      |
| a vehicle someone drives                                     | the driver's game         | puppet vehicle, passengers mounted                                 |
| a vehicle a V rides that its game moves (cab, quest car, AV) | that player's game        | copy, riders and NPCs seated; host's scene: a seat or a ride along |
| an NCART train (scripted transit)                            | each rider's own game     | never shared: puppet in a train of ours, else hidden               |
| a parked or AI vehicle                                       | host                      | proxy                                                              |
| NPCs: AI, position, health, death                            | host                      | **proxy with its AI off**, driven by the host                      |
| spawns: crowds, traffic, encounters                          | host                      | guests' own spawning is suppressed                                 |
| quests: facts, journal, phases, scenes                       | host                      | followed, never run on their own                                   |
| time of day, weather, police heat                            | host                      | applied (wanted level: open, `architecture/world.md`)              |
| a V's time abilities (Sandevistan…)                          | that player's game        | shown on the puppet; never slow anyone's world                     |
| an NPC's speed in a time bubble                              | host                      | proxies animated at the same speed                                 |
| NPC shots at a V                                             | the V's game              | host's NPC decides to fire; `architecture/time.md`                 |
| loot: corpses and containers                                 | each player's game        | rolled per player: each loots their own copy                       |
| items a V drops on purpose                                   | host                      | lie in every game; the first pickup keeps it                       |
| quest and important items, rewards                           | every player's own V      | whoever gets one, every V gets it                                  |

**Combat** follows from the table. A shot is decided by the shooter's game,
on what that player sees, so aiming feels local. A guest hitting an NPC sends
a hit event to the host; the host applies it to the real NPC and the result
(health, death, stagger) comes back to everyone. An NPC firing at a guest's
puppet on the host's game sends that guest a damage event, applied to the
guest's own V. Kills are final in every game because only the host's NPC is
real.

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
health, its target and its status effects), the way a region's owner
changes as players move. What outlives a region (deaths, loot taken,
destroyed objects, quest facts, heat) goes to the host's record of the
world (the host's save), so it is still true when anyone comes back. The
first version shares the host's NPCs only near the host and never tethers
anyone: a player who fast travels offers the others to join (accepted or
not, each stays free), and anyone can regroup with another player at any
time (see `architecture/travel.md`); per-player regions are
the step after it. Until then a guest far from the host plays in a quiet
world: its own crowd is off and the host's NPCs are shared only around the
host.

## Identity: naming the same thing in every game

Two games must agree on which NPC, car or object a message is about.

- **Placed entities** (from world sectors, communities, quest prefabs) have
  the same static `EntityID` hash in every game: that hash is their name.
- **Spawned entities** (crowds, traffic, encounter waves, quest spawns) get
  a `NetId` from the host when they appear. Guests do not spawn these
  themselves: they create a proxy from the host's announcement. Matching by
  "nearest NPC of the same record" is never used: it mismatches waves.
- **Players**: `PlayerId` from the session (host is 1).

## Inside the plugin

```
game thread                                 net thread (NetThread)
───────────                                 ──────────────────────
frame tick: sample local V ──► outbox ──►   Endpoint::Update()
            apply snapshots ◄── inbox ◄──   decode, interpolation buffers
redscript natives (UI, events)              ENet socket
```

- The **net thread** owns the `Endpoint` and never touches the engine.
- The **game thread** never touches the socket. The two exchange plain
  structs through lock-free or mutex-guarded queues, drained once per frame.
- **Engine callbacks may run on job threads** (hit, mount, vehicle events
  reached from redscript): they only enqueue; state changes happen on the
  game thread's frame tick. This race was a main crash source in NCO.
- Redscript is for what scripts do best: UI, player-facing events, reading
  the player state machine. A single broken `.reds` file disables every
  script in the mod: scripts are checked before shipping.

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

| #   | Milestone           | Done when                                                                                                                                                                                                       |
| --- | ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | Toolchain           | DLL built on Linux loads in the game and logs; mock player; relay image ✓                                                                                                                                       |
| 1   | A second V          | a puppet follows the other player smoothly (✓ 2026-11-17: engine controller, 0.01 m, walk animation, NPC rig)                                                                                                   |
| 2   | Player presence     | appearance, equipment, locomotion and aim animations, weapons drawn (pose, look, clothes, held weapon, shot effects ✓ 2026-11-18)                                                                               |
| 3   | Vehicles            | drive together, ride as a passenger (copy, seated driver, get out, leave: ✓ 2026-11-18; passengers to test; rides, quest vehicles, NCART written: to test)                                                      |
| 4   | Shared world basics | time, weather, police heat follow the host; fast travel together, no tether (time, weather ✓ 2026-11-18; regroup built, untested)                                                                               |
| 5   | One set of enemies  | NPC proxies with AI off on guests; hits, damage, deaths, loot (proxies follow the host's NPCs, hits and deaths ✓ 2026-11-18; loot built)                                                                        |
| 6   | Story               | quest facts and journal follow the host; dialogue choices; scenes for guests (step 1 written: freeze, saves, journal, calls, texts, lines, hubs, rewards; to test in game)                                      |
| 7   | Anywhere            | relay fallback in the plugin, invites (join codes), co-op in the main menu: host or pick whom to join (relay and codes ✓ 2026-11-18, loopback; menus written: main and pause menu, runtime start/stop, to test) |
| 8   | Co-op features      | revive, pings and shared markers, and what we add as we play (down, revive, respawn, shared game over written; to test in game)                                                                                 |

Alongside every milestone from 2 on: the simulated second game (record,
replay, scripted players, network shaping) covers what that milestone
syncs, so it can be tested without a second PC.

## Topic files

Read this core, then only the files your task touches. Each holds the
subsystem's design as built ("Inside the plugin") and its open problems.

- [`architecture/session.md`](architecture/session.md): handshake, liveness, slots and returning players, reaching each other (direct, relay fallback), `tandem.ini` keys. For connection and session code.
- [`architecture/smoothing.md`](architecture/smoothing.md): the host clock, interpolation delay, extrapolation, teleports, movement flags and puppet placement, rates. For anything that draws a remote V, NPC or vehicle.
- [`architecture/consistency.md`](architecture/consistency.md): digests, resync, idempotent application, epochs, the audit of every synced state with its guard, unplanned disconnects. For any new or changed state message.
- [`architecture/players.md`](architecture/players.md): body poses, weapons, looks, player markers; open: linking up, every way a V moves, cyberware and perks.
- [`architecture/vehicles.md`](architecture/vehicles.md): driven vehicles, seats, the radio; open: getting in and out, combat in vehicles.
- [`architecture/transit.md`](architecture/transit.md): rides a game runs (cabs, AVs, quest vehicles), the NCART metro.
- [`architecture/world.md`](architecture/world.md): time of day and weather; open: the guest acting on the world, NCPD chases, saves.
- [`architecture/time.md`](architecture/time.md): the pause menu, slowed time (time bubbles), NPC shots simulated by the target's game; open: every time dilation, timeline and menu.
- [`architecture/npcs.md`](architecture/npcs.md): enemy level, NPC proxies, NPC animation, attitudes towards a remote V; open: suppressing a guest's own simulation.
- [`architecture/combat.md`](architecture/combat.md): hits, outcomes, game rules for every hit, quickhacks.
- [`architecture/stealth.md`](architecture/stealth.md): takedowns, finishers, carried bodies, "the player" reactions, per-player detection.
- [`architecture/loot.md`](architecture/loot.md): per-player loot, dropped items, quest items.
- [`architecture/revive.md`](architecture/revive.md): down, revive, respawn, shared game over.
- [`architecture/story.md`](architecture/story.md): quest freeze, journal, calls, texts, dialogue, scenes, rewards; open: scenes for guests, story decisions, quests together.
- [`architecture/elsewhere.md`](architecture/elsewhere.md): cyberspace, braindances, replacers: a V outside the city.
- [`architecture/coop-menu.md`](architecture/coop-menu.md): the co-op menu, invites and join codes in game.
- [`architecture/travel.md`](architecture/travel.md): fast travel together, regroup; open: region authority.
- [`architecture/relay.md`](architecture/relay.md): the relay's sessions, join codes, abuse limits, wire format, deployment, the game side.
- [`architecture/testing.md`](architecture/testing.md): test levels, scenarios, every `tandem-peer` mode and flag.
- [`architecture/recordings.md`](architecture/recordings.md): NPC record and replay, gameplay recordings (`.tplay`), recorded host sessions (`.tsession`), the planned simulated second game.

Engine facts: [`research/engine-knowledge.md`](research/engine-knowledge.md) is the index (sources, tags, top traps, section number to file); the facts are in `research/engine/<topic>.md`.

## Open problems

Each topic file ends with its own open problems.

Research for them, gathered from the 2.50 game's scripts and live RTTI
(facts with line references, a proposed co-op rule per case, and the
in-game tests still owed; none of it tested in a session yet):

| File                               | Covers                                                                          |
| ---------------------------------- | ------------------------------------------------------------------------------- |
| `research/pose-streaming.md`       | the remote V's body: pose read/write points, retargeting, measured stream sizes |
| `research/coop-player-vehicles.md` | player state fields, weapons and shots, death/downed, mounting, vehicles, HUD   |
| `research/coop-npcs-combat.md`     | NPC proxies, hit/damage/death pipeline, identity, loot, takedowns, quickhacks   |
| `research/coop-scenes.md`          | scene tiers, cutscenes, dialogue choices, braindance, Johnny, restrictions      |
| `research/coop-time.md`            | every time dilation, pause and time skip, and the game clock                    |
| `research/coop-quests-world.md`    | facts, journal, persistent state, weather, police, crowds, saves, rewards       |
| `research/coop-perks.md`           | every perk tree, relic, cyberware ability and quickhack, by co-op risk          |
| `research/coop-regions.md`         | players apart: per-player regions, NPC hand-over, world ledger, staged plan     |
