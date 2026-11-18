# tandem — architecture

tandem is a co-op multiplayer mod for Cyberpunk 2077 (patch 2.50, PC, Windows
or Proton). Two to four players share **one Night City**: same story, same
enemies, same time and weather, each with their own V. It is written from
scratch; what earlier projects learned about the engine is collected, with
sources, in [`research/engine-knowledge.md`](research/engine-knowledge.md).

The goal is not "see each other in your own games" but **one world that
several games render**. Every design choice below follows from that.

## Contents

1. [Principles](#principles)
2. [Components](#components)
3. [Session and transport](#session-and-transport)
4. [Authority: who decides what](#authority-who-decides-what)
5. [Identity: naming the same thing in every game](#identity-naming-the-same-thing-in-every-game)
6. [Time and smoothing](#time-and-smoothing)
7. [Inside the plugin](#inside-the-plugin)
8. [Talking to the engine safely](#talking-to-the-engine-safely)
9. [The relay](#the-relay)
10. [Testing without a second player](#testing-without-a-second-player)
11. [Roadmap](#roadmap)
12. [Open problems](#open-problems)

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

**Handshake** (`core/src/endpoint.cpp`): `Hello` (magic, version, name) →
`Welcome` (your player id, host id, host clock) or `Refused`. The host then
broadcasts the `Roster`. A guest's messages count only after its `Welcome`,
and the host stamps every relayed `PlayerState` with the sender's own id: a
guest cannot speak for someone else.

**Liveness** (`core/src/session.cpp`, `Timing` in `endpoint.hpp`): a
connection that acknowledges nothing for **5 s** is dead, on either side:
ENet pings an idle peer every 0.5 s, so a live one is heard from several
times a second and a hiccup of 1–2 s passes. `Session::Poll` checks it
itself (time since the peer's last acknowledgement) because ENet's own
timeout only fires when a backed-off resend is due (5 to 30 s); ENet's
stays as a backstop at 10 s. A host that never answers a connect is given
up after the same 5 s ("could not reach the host"). After a stall of our
own (`Poll` not called for a while) peers get a moment to be heard before
they are judged. A goodbye (`Refused`, `Bye`) is delivered before the
disconnect that follows it, and a guest's `Bye` removes it at once.

**Slots and returning players**: the host takes up to `kMaxGuests` (3)
welcomed guests and refuses the next ("the session is full"), but keeps
`kSpareConnections` (2) more ENet connections open, so a player coming back
can always reach it. A `Hello` under the name of a player whose connection
has been silent for **2 s** replaces that connection at once: the player
keeps its id, so the others keep its puppet and seat (no `PlayerLeft`,
no `PlayerJoined`; the host sees `PlayerReturned`), and the new game sends
its look, held items and seat again. A live player (heard within 2 s)
cannot be replaced: a namesake joins as another player. A guest numbers
its states from the host clock's millisecond at its `Welcome`, so what it
sends after coming back is newer than what it sent before.

**Reaching each other**: direct first (LAN, Tailscale/NetBird — already on
every nixbook machine), then the relay when NAT blocks UDP. A host with a
relay configured is reachable both ways at once: directly on its port, and
through the relay by its **join code** (see [The relay](#the-relay)). A guest
joining by code goes through the relay; given the host's address too, it
tries that first and switches to the relay when the host has not answered
within half the timeout (2.5 s), without reporting an end in between
(`JoinTarget` in `endpoint.hpp`).

## Authority: who decides what

The owner simulates; everyone else displays.

| State                                   | Owner                     | Others                                         |
| --------------------------------------- | ------------------------- | ---------------------------------------------- |
| a V: transform, stance, aim, animation  | that player's game        | puppet, interpolated                           |
| a V: appearance, equipment, cyberware   | that player's game        | applied on the puppet when it changes          |
| a V: health, death, downed              | that player's game        | shown; revives are requests to the owner       |
| a V: level, XP, perks, inventory, money | that player's game (save) | not shared: each player keeps their own level  |
| a vehicle someone drives                | the driver's game         | puppet vehicle, passengers mounted             |
| a parked or AI vehicle                  | host                      | proxy                                          |
| NPCs: AI, position, health, death       | host                      | **proxy with its AI off**, driven by the host  |
| spawns: crowds, traffic, encounters     | host                      | guests' own spawning is suppressed             |
| quests: facts, journal, phases, scenes  | host                      | followed, never run on their own               |
| time of day, weather, police heat       | host                      | applied (wanted level: open, see below)        |
| a V's time abilities (Sandevistan…)     | that player's game        | shown on the puppet; never slow anyone's world |
| an NPC's speed in a time bubble         | host                      | proxies animated at the same speed             |
| loot drops                              | host                      | each player loots their own copy               |

**Enemy level** (decided 2026-11-18): enemies the host spawns and runs are
scaled to the **mean of the character levels of the players in the
session**, not to the host's level. Each game reports its V's level to the
host when it changes (a field of a reliable message, alongside the look);
the host applies the mean where the game scales an NPC to the player's
level, and recomputes it when someone joins, leaves or levels up. In the
game an NPC's level is its `PowerLevel` stat (`gamedataStatType.PowerLevel`,
set from its record's `PowerLevelMod`; scripts compare it with the
target's, e.g. `GetPowerLevelDiff`): the likely lever is a stat modifier on
each NPC the host runs, moving its `PowerLevel` to the mean. To confirm in
game (whether health, damage and loot follow the stat).

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
first version keeps players together (proxies of the host's NPCs only
near the host, a soft tether with regroup on fast travel); per-player
regions are the step after it.

## Identity: naming the same thing in every game

Two games must agree on which NPC, car or object a message is about.

- **Placed entities** (from world sectors, communities, quest prefabs) have
  the same static `EntityID` hash in every game: that hash is their name.
- **Spawned entities** (crowds, traffic, encounter waves, quest spawns) get
  a `NetId` from the host when they appear. Guests do not spawn these
  themselves: they create a proxy from the host's announcement. Matching by
  "nearest NPC of the same record" is never used: it mismatches waves.
- **Players**: `PlayerId` from the session (host is 1).

## Time and smoothing

- **One clock: the host's.** Guests estimate the offset from pings, keeping
  the lowest round trip (`Endpoint::HostNowUs`). Every state carries
  host-clock microseconds.
- **Absolute times are integers.** A float can't hold an epoch timestamp
  (one earlier project froze every remote player that way); only
  differences become float.
- **Remote things are drawn slightly in the past** and interpolated between
  the two samples around that time: 65 ms behind at 30 Hz, widened per
  player by measured jitter and transit, capped at 400 ms (numbers measured
  on 2.5 by NCO, see the research file). Short gaps are extrapolated (250 ms
  for players, 500 ms for vehicles), then held; recovery blends back.
- Rates: players and vehicles 30 Hz; NPC proxies 10–30 Hz by distance and
  relevance (budgeted per frame).

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
- **Body poses** (`engine/posestream.cpp`, `core/src/pose.cpp`): the local
  V's pose is read after the engine's per-entity animation job, turned into
  local rotations on the third-person body rig and sent as `PoseState` at
  30 Hz; on the others it is written into the puppet's root graph output
  where the engine commits it, so the puppet's own deformation and face
  graphs follow (`docs/research/pose-streaming.md`). Both hooks run on job
  threads and only touch the pose buffers.
- **Weapons** (`scripts/tandem/Weapons.reds`): what each V holds travels as
  `Held` (the right and left hand items' TweakDB ids, reliable, cached for
  late joiners); the puppet gets the same items in the same slots and the
  pose stream moves them. Each shot, reload and melee swing (wraps of the
  player state machine's `ShootEvents.OnEnter`, `ReloadEvents.OnEnter/OnExit`,
  `MeleeAttackGenericEvents.OnEnter`, `QuickMeleeEvents.OnEnter`) travels as `Shot` (unreliable) and
  plays the weapon's own effects on the puppet
  (`WeaponObject.TriggerWeaponEffects`: flash, shells, report, reload; the
  NPC melee sound `AI_PlayMeleeAttackSound`), no projectile. Hits are
  separate (milestone 5). Verified in game 2026-11-18: held weapon, shots
  (28 in one session), reloads (start and end), quick melee; melee-weapon
  swings and every sound still to check.
- **Vehicles** (`scripts/tandem/Vehicles.reds`, `core/src/vehicle.cpp`):
  where a V sits travels as `Seat` (vehicle record, seat, whose car;
  reliable, cached for late joiners), the car a V drives as `VehicleState`
  (30 Hz). Other games spawn a copy of the driver's car, teleport it every
  frame to the interpolated state (drawn with the driver's own delay, so the
  driver stays in the seat; 500 ms of extrapolation), and mount the
  driver's and passengers' puppets in its seats with the mounting facility
  (no AI). A player riding in someone else's car sits in that game's copy;
  a puppet riding in ours sits in our real car. The puppet's movement
  controller keeps still while it is seated and is re-attached when the
  player gets out. Copies are never deleted while the game runs (deleting
  one that had someone seated in it froze the game): a car someone got out
  of stays parked. Puppets leave in steps over several frames (out of the
  seat, then the body).
- **Radio** (`scripts/tandem/Radio.reds`, engine-knowledge §13.12): the
  driver owns the car's radio. While our V drives its own car, our game
  reads the car's receiver four times a second (on, station as an
  `ERadioStationList` index, song name) and sends `VehicleRadio`
  (reliable, kept by the host for late joiners) when it changes and when
  a remote player gets in; a song that changes on a station we have been
  on for a while is stamped with its start on the host clock. A passenger
  riding in a remote player's car (our copy of it) sets the copy to the
  driver's radio the game's way (a `VehicleRadioEvent` on the car, as the
  radio popup does), checks a second later and falls back to the
  receiver natives (twice at most), and asks its own copy of the station
  for the driver's song when another plays
  (`AudioSystem.RequestSongOnRadioStation`). The game exposes no playback
  position, so the target is the same station and, best effort, the same
  song: it lines up when the driver's song has just begun, otherwise it
  starts from the top. While riding, the passenger's pocket radio is held
  off with the game's own `VehicleBlockPocketRadio` restriction; getting
  out lifts it and the pocket radio comes back with its station after
  vanilla's short unlock delay. No remote player's radio plays anywhere
  else: a puppet is an NPC (no pocket radio) and a car copy nobody of
  ours rides in is switched off. Volumes stay each player's own
  settings. Not yet verified in game (scenario `radio`).
- **World** (`scripts/tandem/World.reds`): the host sends `World` (game
  time in seconds, the weather state's name, paused) on a weather change or
  a clock jump and every 10 s; guests set their clock to the host's when it
  is more than a minute off and blend to the host's weather over 10 s
  (Codeware's `WeatherSystem.SetWeather`). Verified 2026-11-18 with the game
  as a guest of `tandem-peer host --time 06:30 --weather 24h_weather_rain`.
  Open: a guest whose own quest pins its clock (the guest's quest phases
  are to be suppressed: milestone 6) is corrected again every 10 s.
- **Pause** (`scripts/tandem/Pause.reds`): the pause menu pauses everyone.
  Our pause menu opening and closing is seen where vanilla pauses for it
  (wraps of `PauseMenuBackgroundGameController.OnInitialize/OnUninitialize`;
  the death menu shares that background and is skipped) and sent as `Pause`
  (reliable). The host stamps the sender, keeps who is paused (late joiners
  get it; a player who leaves is no longer paused) and relays it. While any
  other player is in their pause menu, a game pauses its world the way the
  pause menu does (the system requests handler's `PauseGame`, Codeware's
  `GameInstance.GetSystemRequestsHandler`), without opening its own menu,
  shows "NAME paused the game" (the HUD's `UI_Notifications.WarningMessage`)
  and resumes when nobody is paused. Scripts' delayed tick stands still
  while paused, so the plugin calls `TandemPause.Frame` every frame from
  RED4ext's running game state (`plugin/src/main.cpp`). Not yet verified in
  game (scenario `pause`).
- **NPCs** (`scripts/tandem/Npcs.reds`, milestone 5): once the world
  plays, the host lists the NPCs within 60 m of its V ten times a second
  (`GetNPCsAroundObject`, skipping our own puppets, copies and the dead),
  announces each new one (`NpcSpawn`: a session id, record, appearance,
  transform), sends where all of them are (`NpcStates`) and which left
  (`NpcGone`). A guest spawns a proxy of each one with the dynamic entity
  system, switches its brain off at once (`TandemProxies.Pacify`), and two
  seconds later, once its rig has loaded, hands it to tandem's movement
  controller as puppet `0x10000 + id`, drawn like a remote player.
  Verified 2026-11-18 with the game as a guest of a replayed recording: 10
  proxies, 0.00–0.03 m from their tracks, walking when the host's NPC
  walks. A guest's own NPCs are never the world's: in a session its
  crowd density is 0 (`CommunitySystem.ChangeDensityModifier`) and what
  still spawns within 60 m (encounters, communities) has its brain off and
  is hidden for good (`HideIrreversibly`). When the session ends the
  proxies are deleted and the density reset. Verified 2026-11-18: 9–12 own
  NPCs hidden, 9–13 proxies shown, all proxies gone when the host left.
  Open: police (`PreventionSpawnSystem`), quest NPCs the guest needs to
  talk to (milestone 6), proxy animation beyond gait.
- **Fights** (`scripts/tandem/Combat.reds`, milestone 5): a guest's hit on
  a proxy runs the game's damage pipeline as usual (aim, weapon, perks,
  the proxy's armour) up to `DamageSystem.DealDamages`. There the damage
  is computed without being applied (`StatPoolsManager.ApplyDamage` with
  `forReal` false) and sent as `NpcHit`. Instant kills
  (`ProcessInstantKill`, the `Kill` flag) are sent as lethal hits. The
  hit marker and damage numbers still show at once. The host lowers the
  real NPC's health (`RequestChangingStatPoolValue`, the guest's puppet as
  instigator) or kills it, and sends its health (`NpcHealth`, on every 1 %
  change, kept for late joiners) and its death (`NpcGone` reason 1). The
  guests set the proxy's health and `Kill` it, so it dies the game's way,
  and the corpse stays until the host's goes. Verified 2026-11-18
  (scenarios `npcs-hit`, `npcs-punch`): 40 physical damage became 9.7
  after the proxy's armour; three hits of 35 killed an NPC on the host.
  **Game rules hold for every hit**: the attacker's game applies the
  attacker's side (weapon, perks, crits, weak spots, its own status
  effect chances), the target owner's game applies the target's side, and
  the killer's game gets the kill (on-kill perks, XP). Status effects a
  guest's hit applies to a proxy go to the host (`NpcEffect`), which puts
  them on the real NPC for their behaviour (stun, panic, blind, burning);
  their damage ticks stay the guest's (they reach the host as hits), and
  the host's own ticks for them deal nothing. Stealth bonuses depend on
  the attacker's combat state, so they follow per-player detection
  (above). The guest also queues the skill XP of each hit on a proxy, as
  the game does when it drains health (`RPGManager.AwardExperienceFromDamage`
  with the share of the proxy's health pool the hit takes, which mirrors the
  host's); the queue is paid when the proxy dies, by the game's own
  neutralize event when the kill is ours (`Kill` with our V), and by a
  `ProcessQueuedCombatExperience` request when another player landed it
  (assists pay too).
  **NPCs hitting guests**: on the host, a hit of one of its NPCs (or a
  device, a car) on a guest's puppet runs the attacker's side only: the
  target's stages are skipped for puppets (`ProcessDamageReduction`, dodge,
  evasion, mitigation, the player incoming multiplier,
  `CalculateTargetModifiers` with armour, `ProcessBlockAndDeflect`). At
  `DealDamages` what is left goes as `PlayerHit` (the NPC's session id, the
  damage per type, attack type, flags, armour penetration, where and which
  way, the attack and weapon records) to that guest alone; instant kills go
  the same way. Damage ticks of effects on the puppet are not sent (the
  guest runs its own copy of the effect). The guest builds a hit on its own
  V from the NPC's proxy (instigator and source; none when it has no proxy)
  with the host's numbers and queues it in its damage system, which skips
  the attacker's stages (`ConvertDPSToHitDamage`, `CalculateSourceModifiers`,
  `ProcessEffectiveRange`; armour penetration is the host's) and runs the
  rest by its own rules: armour, resistances, dodge, block and deflect,
  perks, difficulty, god mode, death, the attack record's status effects,
  hit reaction, direction indicator and sounds. Not yet verified in game
  (scenario `npcs-hurt-guest`). Open: aggro (the NPC turning on whoever hit
  it), loot, the proxy's own weapon (armour
  needs an attacking weapon object: the proxy's if it holds one, else the
  guest's own stands in), status effects on the host's puppet (they stay
  there, unsynced).
- **Quickhacks** (`scripts/tandem/Hacks.reds`, milestone 5): a guest's
  quickhack on a proxy runs in its own game as in solo (panel, RAM, its
  cooldown and queue, upload bar, the effect on the proxy, whose effects
  reach the host as `NpcEffect` like a hit's). When its upload starts
  (`ScriptedPuppet.OnUploadProgressStateChanged`, `STARTED`) it sends
  `NpcHack` (action record, NPC, the upload time its perks give, whether
  it is traceable); the host keeps the real NPC being hacked for that long
  (`AIQuickHackStatusEffect.BeingHacked`, `…WithTraceableHack`), uploads
  from several players at once, ended in arrival order. A guest's
  non-stealth quickhack effect makes the host's NPC do what the game does
  for the host's V (`NonStealthQuickHackVictimEvent`): alert
  (`NPCStatesComponent.AlertPuppet`), turn on the guest's puppet
  (`AIActionHelper.TryChangingAttitudeToHostile`) and trace that V
  (`PlayerHack` with `AIQuickHack.HackRevealPosition`). The host sends
  everyone every upload on its NPCs (`NpcHackState`, the host's V's too)
  and every `Quickhack`-tagged effect that comes and goes (`NpcStatus`);
  guests put them on their proxies, so the proxy looks hacked and the
  menu's "already affected" lockouts (the action's target-active prereqs,
  checked on the proxy) are the host's. **NPC netrunners**: a host NPC's
  hack on a puppet (`ScriptedPuppet.OnHackTargetEvent`) is not run there
  but sent to that guest (`PlayerHack`), whose game runs it on its own V
  through the same event, with its proxy as the netrunner: its ICE, the
  upload bar, the effect; an ICE counterhack lands on the proxy and goes
  back as an effect. **Spreading** runs on the caster's game only, over
  its proxies (the vanilla spread takes the local player's stats, spread
  map and executor: on the host it would be the host's V's); each copy it
  lands is forwarded like any effect, and the host never runs a guest's
  completion effects, so it does not spread them again. **Cars**: the
  brake, accelerate and explode hacks on the copy of a car another V
  drives (`VehicleComponentPS.OnVehicleOverride…`) are not applied to the
  copy but sent (`VehicleHack`, through the host) to the driver's game,
  which applies them to its car (`ForceBrakesUntilStoppedOrFor`,
  `ActivateNetrunnerQuickhack`); taking control of such a car is refused
  for now (it needs the car handed over to the hacker). Not yet verified
  in game (scenarios `hacks-upload`, `hacks-guest`, `hacks-car`). Open:
  Breach Protocol (`Breach{access point, programs}` to the host, which
  runs the daemons with the puppet as executor and sets the breached flag;
  the loot stays the guest's), an upload of the same hack by another
  player shown as locked in the menu while it runs, the host NPC
  netrunner's own AI not told how its forwarded hack ended, take control.
- **Looks** (`engine/look.cpp`, `scripts/tandem/Outfit.reds`): a V's face
  and body travel as the appearance keys the game's appearance changer takes
  (a resource path hash and a name hash each: the same in every copy of the
  game), its clothes as the TweakDB ids of what the wardrobe shows. Sent as
  `Appearance` (reliable) when the clothes change; the host keeps everyone's
  latest for late joiners. A puppet wears the clothes as preview items, the
  way the inventory paper doll dresses V.
- **Player markers** (`scripts/tandem/Markers.reds`,
  `tweaks/tandem/markers.tweak`): every remote player has a marker in the
  world, on the minimap and on the world map, the way the game marks your
  car. It is a mappin registered with `MappinSystem.RegisterMappinWithObject`
  on the puppet as soon as it exists (the `roleMappin` slot if the body has
  one, else 2 m above its feet), moved to the car the puppet sits in (the
  vehicle's `vehMappin` slot, as for your car's marker) and back, and
  unregistered when the player leaves (`TandemSystem.Leave`). The mappin
  system moves it with the entity. Its variant is the game's own for a
  remote player, `CPO_RemotePlayerVariant` (from the cut multiplayer; never
  registered in single player), so the UI changes touch no other marker:
  the world container draws it with the quest marker widget and the car's
  profile (`WorldMappinUIProfile.Vehicle`, spawned at any distance), the
  minimap with its point-of-interest widget, both with the `fixer` icon of
  the mappin atlas; the world map shows it whatever the zoom and filters.
  The player's name is the world label and the world map tooltip's title.
  Its definition, `Mappins.TandemPlayerMappin`, is the game's remote player
  definition with `showInWorld`, `showOnMinimap`, `showOnMap` forced on.
  `tools/game.sh cmd markers` logs the definition's flags and where each
  marker is; scenario `markers`. **Not yet verified in game**: that it
  shows at all in each of the three places (the definition's other fields,
  e.g. `visibilityRange`, are the base's), the icon and its colour (no
  mappin group knows the variant: the widgets fall back to their quest
  state), the name label in the world (the quest widget's display name or
  distance text, rewritten after each native update: watch for flicker)
  and on the map tooltip, and that the marker follows a seated player's car
  copy.
- **Story** (`scripts/tandem/Story.reds`, `Phone.reds`, `Dialogue.reds`,
  `plugin/src/engine/quests.cpp`, `core/src/story.cpp`; milestone 6 step 1
  of `research/coop-quests-design.md`). The host's game runs the story;
  everything travels as the story messages (`Journal` … `QuestReward`,
  one path through `Endpoint::SendStory` / `events::Story`; the host keeps
  the journal, tracked entry, open call and hub for late joiners).
  - _A guest's own story waits_: a hook on `QuestPhaseInstance::ExecuteNode`
    (hash 3227858325, behind an RTTI layout check) refuses the nodes of the
    guest's story phases while it is in a session, the way the engine
    skips a disabled node (result 1, read offline: engine-knowledge §13.15),
    holds each refused signal and gives it back through the root phase's
    `ExecutePath` when the freeze ends. **Per phase, not global**:
    open-world phases (communities, vendors, NCPD hustles, city scenes,
    fixers: Codeware's list, plus `quest_freeze_allow`) and phases under
    them keep running, so a guest away from the host runs its own
    surroundings (`research/coop-regions.md`). **Off by default**
    (`quest_freeze = on` to use it) until checked in game; without it a
    guest gets calls, lines, hubs and rewards but not the journal or texts.
  - _Saves_: a guest in a session never saves (`TandemStorySystem` overrides
    `IsSavingLocked`, plus a `SaveLocksManager` lock and dropped autosaves).
    Every journal entry the mirror changes keeps its state from before; on
    leaving (the guest quits or the host is gone) the journal and tracked
    entry go back, are checked, the freeze ends and saving unlocks. A
    mismatch keeps saving locked and says "load your last save". The
    design's final step (write one "back from co-op" save and reload it) is
    a logged TODO: the guest goes on in place.
  - _Journal, tracker, markers_: the host's journal (snapshot at join,
    changes with their notify flag, so the guest gets the same toasts and
    sounds) and tracked quest; quest pins follow the journal. Shown only on
    a frozen guest. Entity-bound pins and objective counters: step 2.
  - _Calls and texts_ (`Phone.reds`): the host's `PhoneSystem` phases go to
    every phone; a guest presents them through its own `PhoneSystem`
    (ringtone, incoming widget, audio call with the caller's portrait) with
    its phone facts held back. Anyone answers: the first answer wins
    (`story::CallArbiter` on the host's endpoint) and picks up the host's
    phone. Texts come through the journal; a guest's reply goes to the host,
    which applies it if the option is still open (first reply wins).
  - _Dialogue_ (`Dialogue.reds`): the host's subtitles and its active choice
    hub reach guests within 25 m (holocall lines and phone hubs: everyone),
    drawn by the guest's own subtitle and hub widgets (a stand-in hub). A
    guest moves its own cursor and can suggest; the host sees the
    suggestion; the host's pick closes every hub. No story voice yet.
  - _Scenes_: each game sends its V's scene tier (`StoryState`). A guest
    within 30 m of the host in a tier 3+ scene has its weapons lowered, tier
    4+ also invulnerable; a guest further than 100 m from a tier 4+ scene
    gets an offer to join (the regroup itself: TODO).
  - _Johnny, Songbird, takeovers_ (`research/coop-quests-design.md` §2.11):
    Johnny and Songbird are NPCs of the host's world, so a guest near the
    host sees them as proxies where the host does. Johnny's relic glitches
    and Songbird's glitch effects on the host's V (`StoryEffect`) go onto a
    guest's V within 30 m while they last. Songbird's calls keep their own
    widget. The host playing as Johnny (`isReplacer`) or in a braindance
    counts as a cinematic for the holds and the offer, with a notice. The
    host's puppet does not yet take Johnny's look.
  - _Rewards_: the host's `OnRewardEvent` goes to the guests that took part
    (within 150 m of the host as the quest moved or completed;
    `story::Participation`); each guest gives its own V the reward
    (`RPGManager.GiveReward`) once, and keeps it in a persistent ledger so
    its own story pays nothing again for it.
  - Not yet verified in game: scenarios `story-guest`, `story-host`, `story-johnny`.
- **Other worlds** (`scripts/tandem/Elsewhere.reds`, `core/src/elsewhere.cpp`,
  `plugin/src/elsewhere_natives.cpp`; engine-knowledge §13.18): a V in
  cyberspace, a braindance, playing as Johnny (a takeover in the city, a
  flashback) or as someone else (another replacer), any player, host or
  guest. Its own game reads which world it is in four times a second (the
  `Braindance` blackboard; a status effect tagged `Cyberspace` or
  `CyberspacePresence`; a `*Cyberspace` district on `UI_Map`; Johnny's or
  another replacer record) and sends it as `Elsewhere` (reliable, kept by
  the host for late joiners, back to the city when a player leaves or comes
  back from a crash). Every state it sends goes through a tracker
  (`elsewhere::Tracker`): a jump of more than 50 m outside a vehicle is a
  teleport, and when a world comes with it (or within 4 s) the scene has
  taken V away from its body; cyberspace and braindances always do, jump or
  not (V's entity is their camera: a braindance flies it). The body stays
  where V was just before: that game then sends the body's place as its
  `PlayerState` (no velocity), holds the body's pose as it was then (kept 5 s back, sent again
  10 times a second), says whether it sat or lay (a workspot: a netrunner
  chair, a braindance rig, a bed) and shares no NPCs (they are the other
  world's). A teleport with no world for 3 s was one in the city: the body
  goes with V. Back from the world, the body follows V again once V is near
  it or jumps back (3 s at most). What the others do:
  - _A body left sitting or lying_ stays there as it was, jacked in;
    _one moved by the scene_ is hidden (`IVisualComponent.TemporaryHide` on
    the puppet and what it wears and holds) with its marker off. A takeover
    in the city keeps the puppet. A notice says it the game's way
    ("… is in cyberspace", "… is living one of Johnny's memories", "… is
    back"); a hidden puppet shows again 0.75 s after its player is back,
    once it has moved there.
  - _Our own V elsewhere_ (cyberspace, a braindance, or away from its body)
    sees no puppet and no marker of the others; they come back with it.
  - _The story_: a host away from its body, or our V in another world,
    holds nobody and offers no join (there is no scene here to join); the
    guest keeps playing. NPCs never hit a player in another world (the host
    does not send the hit).
  - Nothing stays hidden: a player leaving takes its state away, the end of
    the session clears everyone's, and a new game starts in its body.
  - Not yet verified in game: scenarios `story-worlds` (a scripted host's
    dives, as a guest), `elsewhere-dive` (our V's, as host, with
    `tools/game.sh cmd elsewhere test PLACE|off` and a teleport). Open: the
    signals of the Phantom Liberty Blackwall and Songbird sequences, and of
    Mikoshi (§13.18), the guest following a host's dive (spectate: the
    host's camera or its scene), Johnny's look on the puppet, a fade on the
    puppet instead of a cut.
- **How NPCs see a remote V** (`scripts/tandem/Attitudes.reds`,
  `tweaks/tandem/attitudes.tweak`, `tweaks/tandem/puppet.tweak`): every
  puppet is in an attitude group of its own, `tandem_player_1` to `_3`
  (one per puppet at a time; `tandem_player`, the records' own, beyond
  that), declared as copies of the `Attitudes.Group_Player` record. A
  group's relations to the others are not in its record: the attitude
  system holds one per pair of groups, so the script copies V's group's
  relations into ours for every group seen on an NPC within 60 m of V or a
  puppet (checked every 2 s), and when V's relation to a group changes (a
  quest), the puppets' follow. A puppet's relation that changed while V's
  did not (a security system turned on that puppet's group) stays its own.
  Each copy is read back and logged. On top of the groups, each NPC around
  gets, on the puppet's agent, the attitudes it and V have to each other
  where the groups do not already give them (first run in game: the group
  copy did not show, civilians saw the puppet as friendly; and a quest can
  set one NPC's attitude to V alone): once per NPC and puppet, again when
  the NPC's attitude to V changes, forgotten 30 s after it leaves range.
  Vs are friends to each other. The puppet record drops what made it Panam:
  her group and faction (Aldecaldos: anim wrappers, the scan's faction),
  her voice, her burning immunity and damage presets, and shows no NPC UI
  (`hideUIElements`, `hideUIDetection`): no detection indicator from the
  senses Body.reds grafts. `tools/game.sh cmd puppet attitude` logs each
  puppet's group and how the NPCs around V see it next to V; scenario
  `puppet-attitude`. Verified in game: the groups are accepted. Failed on
  the first run: civilians friendly to the puppet, neutral to V (fixed
  since, to run again; engine-knowledge §13.17).
- **Slowed time** (`scripts/tandem/Time.reds`, `core/src/timebubble.cpp`):
  each game sends its V's time abilities when they change (`TimeEffect`:
  Sandevistan, Kerenzikov, slow-motion perks, scanning, and the world speed
  the implant asks for; reliable, kept by the host for late joiners, ended
  when a player leaves). Every V with a Sandevistan or Kerenzikov on has a
  **time bubble**: the NPCs within 50 m of it are slowed by its factor, for
  everyone. The host does it to its real NPCs with the per-entity dilation
  (`SetIndividualTimeDilation`, the local global factor ignored); each
  guest computes the same bubbles (same core code, same messages) for its
  proxies, so they animate at the speed they move. Membership is sticky
  until the bubble ends; an NPC in several takes the smallest factor; when
  a bubble ends its NPCs go back together. The owner keeps its own
  single-player slow motion, except the host, whose global factor is taken
  back within the frame (re-set at normal speed under the same reason, so
  the state machine carries on) unless `time_host = full`. A game whose
  world runs dilated exempts what the network drives (proxies, puppets) to
  their true speed. The plugin calls `TandemTime.Frame` every frame
  (`plugin/src/main.cpp`). Scenario `time`; not yet run in game.
- Configuration: `tandem.ini` next to the DLL (role, address, port, name,
  pose, story, quest_freeze, quest_freeze_allow, time_host; `relay`,
  `relay_token`, `join` for the relay and join codes).

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

## The relay

`relay/` — Go, standard library only. It forwards opaque ENet datagrams
between a session's host and guests; it never parses game traffic.

- **Sessions**: a 16-byte secret the host's game draws at random for each
  session. Guests register with the secret; the relay gives each a channel
  number and stamps it on what it forwards to the host.
- **Who may host**: only hosts presenting a token from the relay's token
  file (a Kubernetes Secret), so the relay is not an open bandwidth donor.
- **Join codes**: a registered host asks for a code naming its session
  (`RequestCode`, accepted only from the host's own address with its
  secret): 6 characters of Crockford's base32 (no I, L, O, U; read in any
  case, with or without the dash, O as 0, I and L as 1), drawn with
  `crypto/rand`, shown as `K7Q-2M9`. A code lives 10 minutes (`--code-ttl`)
  and dies with its session; the host's game asks again every 20 s (its
  time left, or a new code once it expired or the relay restarted). A guest
  trades the code for the session's secret (`ResolveCode` → `Secret`),
  registers with the secret, and keeps it to join again after the code
  expired (a host that crashed and came back has a new secret: the code it
  shows then is needed). The plugin tries a code that led to no session 3
  times, then stops; once in the session it joins again by the secret
  alone, so a stale code never spends lookups.
- **Codes cannot be guessed cheaply**: 2^30 codes, at most one live per
  session (≤ `max-sessions`, 256). Lookups are limited per source (an IPv4
  address or an IPv6 /64: 5 at once, then one per 6 s; told once that it is
  over, then ignored) and in total (120 failed lookups a minute from
  everyone; past that every lookup waits for the next minute, so many
  sources together are no faster). Even with 256 live sessions and the
  total cap saturated, one hit takes ~24 days on average, against codes that
  change every 10 minutes. The cost is that a flood of wrong codes delays
  honest joins by up to a minute; sources tracked are capped (4096).
- **Abuse limits**: unknown addresses never get an answer to data (no
  reflection); only registration-type requests (register, resolve a code),
  padded to 64 bytes, are answered from an unregistered address, and every
  answer is smaller than its request (at most 21 bytes: no amplification);
  per-client packet and byte rate limits; at most `max-sessions`; idle
  clients forgotten after two minutes.

Wire format (`relay/internal/relay/wire.go`, big-endian, `TNR1` + type):

| Type              | Body                                  | Direction                    |
| ----------------- | ------------------------------------- | ---------------------------- |
| 1 `RegisterHost`  | secret 16, host token 32, pad to 64   | host → relay                 |
| 2 `RegisterGuest` | secret 16, pad to 64                  | guest → relay                |
| 3 `Registered`    | channel 2                             | relay → client               |
| 4 `Data`          | channel 2, ENet datagram              | both ways                    |
| 5 `Keepalive`     | —                                     | client → relay               |
| 6 `Error`         | code 1 (bad token, no session, full…) | relay → client               |
| 7 `RequestCode`   | secret 16, renew 1, pad to 64         | host → relay                 |
| 8 `Code`          | code 6, seconds left 2                | relay → host                 |
| 9 `ResolveCode`   | code 6, pad to 64                     | guest → relay (unregistered) |
| 10 `Secret`       | secret 16                             | relay → guest                |

- **Observability**: JSON logs, `/healthz`, Prometheus `/metrics`.
- **Shipping**: `nix-build -A relayImage` builds an OCI image with only the
  static binary and CA certificates, running as uid 65532.
- **Kubernetes** (`deploy/kubernetes/base`): one replica (sessions live in
  memory; `Recreate`), non-root, read-only root, no capabilities, seccomp
  `RuntimeDefault`, no service-account token, a NetworkPolicy with UDP in
  from anywhere, metrics only from `monitoring`, and **no egress**
  (everything the relay sends is a reply on a flow a player opened). UDP
  LoadBalancer with `externalTrafficPolicy: Local` to keep players'
  addresses. The host-token Secret comes from the overlay's secret store.

**On the game side** (`core/src/relay.cpp`, `tandem/relay.hpp`) the session
talks to the relay through a small UDP proxy on 127.0.0.1, so ENet itself
never knows the relay exists. A guest's ENet connects to the proxy's local
socket; the proxy wraps what it sends in `Data` and unwraps what comes back
(datagrams ENet sends before the relay registered it wait, a few at most).
A host's ENet keeps listening on its port for direct guests; the proxy opens
one local socket per relayed guest (relay channel) and sends from it to
ENet's port, so ENet sees each relayed guest as its own address
(127.0.0.1:N); a socket silent for a minute closes. The proxy is pumped
around each ENet service in `Session::Poll`, which waits on ENet's socket
and the proxy's together, so relayed traffic takes no extra frame. Only the
relay's address is heard on the outer socket, and only ENet's own socket on
the local ones. A host whose relay fails (no answer, refused token) keeps
hosting directly and tries the relay again every 20 s; a guest's failure
ends its join with the relay's reason ("no session has this join code
(wrong, or expired)").

### Invites: the in-game flow (for the main menu)

The co-op main menu (another piece of milestone 7) builds on three natives
(`scripts/tandem/Natives.reds`), all safe to call every frame from the
game thread:

- `TandemNative.RelayCode() -> String`: the host's join code to show,
  `"K7Q-2M9"`; `""` while there is none (no relay set, not registered yet,
  or the code just expired and a new one is on its way).
- `TandemNative.RelayStatus() -> String`: `""` no relay, `"connecting"`,
  `"ready"`, or what went wrong in words (`"the relay did not answer"`,
  `"the relay refused our host token (relay_token)"`, `"no session has this
join code (wrong, or expired)"`, `"too many join attempts: try again in a
minute"`…), to show as is.
- `TandemNative.JoinByCode(code: String) -> Bool`: become the guest of the
  session the typed code names, through `tandem.ini`'s relay (false: not a
  code, or no relay). A running session is left for it at once; otherwise it
  starts when the next game is loaded (`TandemNative.Start`). The outcome
  arrives as the usual `TandemEvent`s (4 in the session, 3 ended with a
  reason).

The flow: **host** — the menu's "Host co-op" loads one of the player's saves
with `role = host` and shows `RelayCode()` (with a copy button and the
code's state from `RelayStatus()`), refreshing it when it changes; the code
is what the player sends a friend. **Join** — "Join co-op" asks for the code
(6 characters, a dash optional), calls `JoinByCode`, loads the guest's own
save and waits for event 4, showing `RelayStatus()` and the end reason on
failure. Still missing for that menu: a native to become host at runtime
(today the role comes from `tandem.ini`), hooks installed when the ini says
`role = off` (they are installed at load only for a configured role), a
"new code" request (the wire has `renew`; `RelayLink` does not expose it
yet), and a relay address shipped by default (today `relay =` is set by
hand).

## Testing without a second player

| Level       | What                                                                      | Command                           |
| ----------- | ------------------------------------------------------------------------- | --------------------------------- |
| unit        | protocol round trips, strict decoding, sizes                              | `task test`                       |
| loopback    | real host + guests over UDP in one process: join, relay, clock, leave     | `task test`                       |
| relay       | protocol, codes, limits, amplification, real sockets, race detector       | `task relay:test`                 |
| via relay   | a real relay process: join by code, fallback from direct, mock players    | `task test` (`relay-loopback`)    |
| mock player | `tandem-peer` as host or guest against the real game                      | `task peer -- host --mode mirror` |
| natives     | each `*Natives.reds` matches its `*natives.cpp` (plain, story, elsewhere) | `task natives:check`              |
| scenarios   | the real game against the mock player, checked on the game's log          | `task scenario -- all`            |
| in game     | deploy the DLL, read its log                                              | `task deploy`, `task logs`        |

**Scenarios** (`tools/scenario.sh`, `tools/scenarios/*.sh`) are the
in-game regression suite: each one puts the game in a role, drives the
mock player and expects log lines within a time limit. Restarts happen
only when the build or the role changed (one restart per role for a whole
run), so a run takes minutes. A failure keeps its evidence (logs,
a screenshot, every thread's backtrace when the plugin's watchdog saw the
game thread stop). Each feature verified in game gets a scenario.

`tandem-peer` modes:

- **mirror** — copies your V back as a second player, offset and optionally
  delayed: you watch your own movements on a puppet, through the whole
  network path. The main tool for puppet, animation and vehicle sync.
- **ghost** — circles around your V at walking speed.
- **front** — stands 3 m in front of your V, facing it, and echoes your
  look, held weapon, shots, pose (with `--pose FILE`, a recorded pose).
- **drive** — drives a car (`--vehicle RECORD`) in circles around your V:
  tests the copy of a remote player's car, its seated driver and its motion
  without anyone driving. `--radio STATION` (0–13, a station name, or
  `off`) plays that station on its radio: sent at the wheel and again when
  your V gets in (`tools/game.sh cmd vehicle ride`).
- **idle** — connects and reports.
- `--relay HOST[:PORT]` (any mode): as host, also reachable through that
  relay (`--relay-token` if it needs one), logging `join code K7Q-2M9`; as
  guest, `join --code K7Q-2M9 --relay …` joins by code, and `join ADDRESS
--code … --relay …` tries ADDRESS first (`tests/relay_loopback.sh`).
- **story** (milestone 6): `host --story FILE` plays a scripted host story
  (journal snapshot and changes, tracked entry, call phases, lines, hubs, a
  pick, scene tiers, a reward, the host's V in another world with `world
PLACE [moved] [settled]`, its body held where the mock player was;
  `tools/scenarios/story-host.story`, `story-worlds.story`); as a
  guest, `--answer N`, `--reply HASH`, `--suggest N` act on the host's
  calls, texts and hubs, and every story message heard is logged.
- **quickhacks**: `join --hack-npcs N` uploads `--hack-action` on one of
  the host's NPCs every N s (1.5 s) and then lands `--hack-effect`;
  `host --replay FILE --hack-player N` has a replayed NPC hack the guest's
  V with `--npc-hack` (default the trace); `--hack-car N` brakes the car
  another player drives. The game side's test commands: `tools/game.sh cmd
hack npc [RECORD]` (our V quickhacks the nearest proxy) and `hack car`.
- `--pause-at N [--pause-for M]` (any mode) opens "its pause menu" N s after
  another player is in the session, for M s (default 3): every game pauses.
- **Fights**: `join --hit-npcs N` hits one of the host's NPCs every N s;
  `host --hit-you N` has one of its NPCs (a replayed one if any) hit every
  guest's V for 10 physical every N s; a guest logs the NPC hits it gets.
- **Stealth** (any mode): as host, `--npc RECORD` stands an NPC 6 m in
  front of the guest and `--detect-at N [--detect-for M]` makes the NPCs
  it sends notice the guests (meter rising over 2 s), detect them for M s
  (default 10), then lose them; `--camo N` turns "its optical camo" on for
  5 s; as guest, `--shoot N` fires a gunshot (shot and noise) every N s.
  Stealth, noises and the host's awareness of it are logged.
- **NPC record and replay** (first piece of the simulated second game):
  `join --record FILE` saves the host's `NpcSpawn`/`NpcStates`/`NpcGone`
  with their times; `host --replay FILE [--offset X,Y,Z]` sends them again
  at the recorded pace once a player joins. Record with the game as host,
  then let it join as a guest of the replay (`role = join`).
- Planned — **the whole simulated second game**, so everything can be
  tested with one PC and one copy of the game. Built up as the protocol
  matures (each milestone adds what its messages need):
  - **record**: save everything a real game sends in a session (its V, and
    as host its world: NPCs, events, quest facts) to a file (NPCs: done);
  - **replay**: play a recording back as that game — as host, so the one
    real game is tested as a guest against a real host's world; or as a
    guest, so it is tested as host against a real player's V (NPCs: done);
  - **scripted players**: recordings edited or generated (walk a path, get
    in a car, shoot an NPC, join late, disconnect mid-fight) as repeatable
    regression scenarios, with checks on what the real game reports back;
  - **network shaping** in the peer (latency, jitter, loss, reordering) to
    test the smoothing and the reliable channels under bad links.

## Roadmap

Each milestone ends with something visible in game.

| #   | Milestone           | Done when                                                                                                                                                                  |
| --- | ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | Toolchain           | DLL built on Linux loads in the game and logs; mock player; relay image ✓                                                                                                  |
| 1   | A second V          | a puppet follows the other player smoothly (✓ 2026-11-17: engine controller, 0.01 m, walk animation, NPC rig)                                                              |
| 2   | Player presence     | appearance, equipment, locomotion and aim animations, weapons drawn (pose, look, clothes, held weapon, shot effects ✓ 2026-11-18)                                          |
| 3   | Vehicles            | drive together, ride as a passenger (copy, seated driver, get out, leave: ✓ 2026-11-18; passengers to test)                                                                |
| 4   | Shared world basics | time, weather, police heat follow the host; tether and regroup on fast travel (time, weather ✓ 2026-11-18)                                                                 |
| 5   | One set of enemies  | NPC proxies with AI off on guests; hits, damage, deaths, loot through the host (proxies follow the host's NPCs, hits and deaths ✓ 2026-11-18)                              |
| 6   | Story               | quest facts and journal follow the host; dialogue choices; scenes for guests (step 1 written: freeze, saves, journal, calls, texts, lines, hubs, rewards; to test in game) |
| 7   | Anywhere            | relay fallback in the plugin, invites (join codes), co-op in the main menu: host or pick whom to join (relay and codes ✓ 2026-11-18, loopback)                             |
| 8   | Co-op features      | revive, pings and shared markers, and what we add as we play                                                                                                               |

Alongside every milestone from 2 on: the simulated second game (record,
replay, scripted players, network shaping) covers what that milestone
syncs, so it can be tested without a second PC.

## Open problems

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

- **Scenes for guests.** Replaying the host's quest-scene nodes in a guest's
  game (the campaign fork's approach) is unproven: nodes run outside their
  phase. Alternatives to study: guests follow the scene as spectators of the
  host's camera; or run the same phase on guests with quest progression
  locked to the host.
- **Suppressing guests' own world simulation** (crowds, traffic, encounters,
  quest phases) without breaking systems that expect them.
- **Region authority** once players spread out (feasibility and a staged
  plan: `research/coop-regions.md`).
- **Co-op from the main menu.** Today a session is set up in `tandem.ini`
  (role, address). The goal: a co-op entry in the game's main menu, where a
  player hosts (with one of their saves) or picks whom to join — friends'
  sessions, a join code from an invite, the relay for players behind NAT —
  and then loads straight into the host's world with their own V. Options
  (name, privacy, max players) live there too; `tandem.ini` stays for tests.
  Part of milestone 7 (invites, relay); needs an ink widget in the main
  menu's controller and a lobby list the relay or a Steam-free directory can
  answer. Done: the relay and join codes, with the natives the menu needs
  (see [Invites](#invites-the-in-game-flow-for-the-main-menu)).
- **Unplanned disconnects, on either side.** A guest's game can crash, lose
  its network or be killed; so can the host's. Every case must end cleanly
  for the ones left: a guest gone (timeout or ENet disconnect) — its puppet,
  car copy, seats it held in others' cars, held items, pending requests and
  any NPC it was fighting (aggro, quickhack in progress) are released, and
  the world it was asking the host about stays consistent; the host gone —
  each guest leaves the session without losing its own V (its save stays
  its own), removes every proxy and puppet, gives its game its own world
  back (spawning, prevention, quest progression that was suppressed), and
  can rejoin a new session; a short drop — a guest that comes back within a
  grace time takes its puppet and seat back instead of respawning; the host
  migrating to another player is out of scope at first. Done (2026-11-18,
  scenario `npcs-guest`): when the host goes, a guest removes the proxies
  and puppets, gets its crowd back, and tries to join again every 5 s; on
  its return the guest's look, held items and seat go to the new session.
  Done (2026-11-18, loopback tests in `tests/endpoint_test.cpp`, see
  [Session and transport](#session-and-transport)): a guest or host that
  dies without a goodbye is noticed in 5 s (it was ~30 s, and dead mock
  players filled the guest slots: "could not reach the host"); a player
  back on a new connection while its old one is silent takes its place at
  once, with its id, puppet and seat, even in a full session; a live
  player cannot be replaced. Measured on loopback with the timings scaled
  to 1 s / 0.4 s: crash and hang reported in 1.00 s, a vanished host in
  1.00 s, back in the session 0.52 s after a crash; with the mock player
  and the real timings, killed (`kill -9`) guest or host gone after 4.5 s
  of silence. Still open: a grace time **after** the timeout (a link down
  for more than 5 s comes back as a new player, its puppet respawned; a
  crashed game takes longer than that to restart anyway). When a session
  ends, the plugin now forgets every remote player's tracks, so a
  restarted host's states (counted from the start again) are not dropped
  as old. Not yet checked in game. Each case gets a
  scripted test with the mock player (kill it mid-drive, mid-fight, while
  seated in our car; host vanishes while a guest is in a cutscene).
- **The second player acts on the world too.** A guest is not a spectator:
  it shoots, hits and kills NPCs, pushes physics objects, opens doors, uses
  devices and terminals, loots, hacks, drives — and every other game sees
  the result. By the authority table these become requests to the state's
  owner (NPCs, devices, physics props, quest objects: the host), which
  applies them and replicates the outcome; the guest's own game shows its
  action at once (its V's animation, sound and effects) so it feels local.
  Physics objects (crates, bodies, cars hit) need an owner and a sync of
  their transform like vehicles; which ones (all props, or only those a
  player touched) is to decide.
- **Radio: each V's own on foot, the driver's in a car.** On foot every
  player listens to their own pocket radio, which nobody else hears. In a
  car everyone aboard hears the car's radio, and the driver owns it: their
  station, their volume and their track position are what the passengers
  hear, in sync. A passenger's own radio goes quiet while they ride. To
  sync: the driver's station and the track's start time (host clock), so
  each passenger's game plays the same song at the same point, and changes
  when the driver changes station. Getting out gives each player their own
  radio back as it was. Done (2026-11-18, to verify in game with scenario
  `radio`; see Inside the plugin): the driver's on/off, station and song
  reach the passengers, who hear them on their copy of the car with their
  own pocket radio held off, and get it back on the way out; copies and
  puppets play nothing. Not possible with the game's scripts: the same
  point of a song (no playback position to read or seek; the song is
  asked for by name and lines up only when it has just begun on the
  driver's side) and the driver's volume (a per-player setting, left
  alone).
- **The pause menu pauses everyone; nothing else does.** When any player
  opens the pause menu, the game pauses for every player: the world, the
  NPCs and every V stop together, and each player sees who paused it. It
  resumes when nobody is in the pause menu any more. Every other menu
  that stops time in solo play (inventory, perks and attributes,
  cyberware, crafting, map, journal, messages, shards, wardrobe, vendors)
  does **not** pause in co-op: the world goes on, so the others keep
  playing while one of them levels perks or changes weapons, as in other
  co-op games. The player in such a menu stays in the world, vulnerable,
  and the others see them standing there (later: a small "in menu" sign
  over their V). Only the pause menu, with its settings, save/load and
  quit, stops everyone. Scanning slow-motion, braindances and the like
  follow their own notes. Done (2026-11-18, to verify in game with scenario
  `pause`): every game sends its own pause-menu state (`Pause`), the host
  keeps and relays it, and each game pauses its world while anyone else is
  in the pause menu, with a notice naming them (see Inside the plugin).
  (`World.timePaused` still carries only the host's own time-system state.)
  Still to do: the other menus keep pausing their own game (vanilla:
  `MenuHubGameController`/`RadialMenuHubGameController` set the `hubMenu`
  time dilation profile and the `HubMenu` menu mode; `TimeDilationHelper`
  gives up when `IsMultiplayer()`), and the "in menu" sign.
- **A second V is welcome wherever the first is.** In co-op every V
  starts with the attitudes the player has: friendly to friendly NPCs
  (fixers, allies, quest characters, vendors), neutral to the neutral,
  hostile to the hostile, so a quest never breaks because an ally sees a
  stranger. Puppets get an attitude group of their own that copies the
  `player` group's relations (a TweakXL record), not `player` itself: in
  `player`, hostility to one V spreads to every V (§13.11). What a single
  V does (provoking a gang, crimes) changes that NPC's attitude to that V
  only, per NPC and per player, by the game's own rules. Done (2026-11-18,
  to verify in game with scenario `puppet-attitude`): one group per puppet,
  relations copied from V's at runtime (see Inside the plugin). Known
  limits: a group seen only on devices (a turret's security system with no
  NPC of its faction near) keeps the record's default attitude; a group
  relation V's own deeds change (if a provocation or a security breach by
  V changes a group relation, not a per-NPC attitude) reaches the puppets
  too; per-NPC attitudes a quest sets towards V are not copied.
- **Players linking up (later).** One V can send the other a request to
  connect, in the game's own style (a neural link, like jacking in with a
  partner); once the other accepts, the two share what a linked crew
  would: the Ping quickhack's highlights and what each one scans or tags
  shows on both HUDs, and later more (shared marks, a netrunner seeing
  through the other's optics, quickhacks relayed through a partner's
  link). Either side can break the link at any time. The link is a
  per-pair state the host relays; what it shares is decided feature by
  feature.
- **Quickhacks from two players on one target.** Both Vs can hack the same
  NPC, device, camera, turret or car at once, as two netrunners would:
  uploads in progress at the same time, effects stacking or not by the
  game's rules (two Short Circuits damage twice, two Overheats refresh one
  burn, Reboot Optics while blinded), the target's queue of uploads and its
  per-target cooldowns (a quickhack already running on it, the "already
  affected" lockouts), RAM costs paid by each V on their own, and the
  target's trace (an NPC tracing back to one netrunner, not to both). The
  owner of the target (host for NPCs and devices, the driver for a car)
  applies the uploads in the order they land and tells both players what
  is running on it, so each one's scanner shows the true state.
  Done (2026-11-18, not yet verified in game; see Inside the plugin):
  uploads to the host and their state to all, quickhack effects
  mirrored on proxies, guest hacks alerting host NPCs, NPC netrunners
  hacking a guest's own V, car hacks to the driver.
  A V can also quickhack the car another V is driving (vehicle quickhacks:
  forced acceleration, sudden brake, loss of control, overheat). The
  driver's game owns that car, so the hack is a request to the driver's
  game, which applies it to the car it simulates: the driver feels it
  exactly as from an NPC netrunner (the car fights their input), and every
  other game sees the result through the car's usual sync.
- **Each player is detected on their own.** Stealth, detection and
  combat are per player, never shared. A host NPC can be hunting one V
  while another sneaks past it unseen: each player has their own stealth
  meter, detection, "in combat" state and combat music. The host's game
  measures each NPC's detection of every V (its own, and each guest's
  puppet: `SenseComponent.GetDetection`, the NPC's threats) and sends it
  per player: `NpcEntry::detected` has a bit per player for "has detected
  them" (full detection meter, or in combat with them as a hostile threat:
  `NPCPuppet.IsInCombatWithTarget`), and `NpcAwareness` (5 Hz) the level
  of each NPC's meter for each guest it is noticing. A guest's game drives
  its own V from that (`scripts/tandem/Stealth.reds`): while any host NPC
  has detected it, its V is "tracked as hostile" through the game's own
  switch (the player combat controller's `OnStartedBeingTrackedAsHostile`,
  the call a local NPC's target tracker makes), so the PSM combat state
  and everything on it follow: combat music and HUD, the fast travel lock,
  stealth bonuses (`AttackData.IsPlayerInCombat`), combat-only perks; it
  leaves combat the same way when no host NPC has it any more (a local
  tracker, if any, keeps it in). The awareness levels drive the stealth
  meter: `UI_Stealth.highestDetectionOnPlayer` (what the detection perks
  listen to), each proxy's detection percentage, and the proxy's sense
  mappin (the indicator over an NPC) while it notices us. What host NPCs
  know of another V never counts. Verified 2026-11-18 (scenario
  `stealth-guest`: in and out of combat, the meter's log); whether the
  native meter draws a proxy's level is open. Open: player ids grow
  within a session (players coming back), but `detected` has bits for
  players 1–8 only, so a guest numbered 9 or more is never detected.
  The other way, **a guest's stealth reaches the host's NPCs**: every game
  sends its V's optical camo (the camo status effect, the relic's fully
  blocking state), its `Visibility` stat and whether it crouches as
  `Stealth` (reliable, kept for late joiners); the host puts it on the
  puppet where its NPC senses look (camo: the grafted visible object off,
  and the camo effect itself, which every game also shows; the visibility
  as the detection multiplier; crouch height for the visible point; fully
  blocking: its NPCs drop the puppet as a threat). A guest's noises (every
  stimulus its V broadcasts: gunshots, silenced shots…) go to the host as
  `Noise` and are broadcast again from its puppet. Grenade and other
  projectile explosions are broadcast by the projectile, not the V: still
  open. Not yet verified in game (scenario `stealth-host`). Puppets keep
  an attitude group of their own (in `player`, hostility to one V becomes
  hostility to all): each puppet has one (`tandem_player_N`,
  Attitudes.reds), so hostility to one guest's V reaches neither the
  host's V nor another guest's.
- **Every way a V moves, on the puppet.** Walking on flat ground works; the
  rest must too: stairs and slopes, ladders, climbing and vaulting
  (mantling ledges), jumps and double jumps, falls and landings, sliding,
  dodges and air dashes, swimming on the surface and diving, elevators and
  moving platforms, being carried or knocked down. The pose stream gives
  the body its animation; placement is the hard part: the movement
  controller moves an NPC body along the ground and may refuse or snap
  positions an NPC could not reach (off the navmesh, mid-air, in water, on a
  ladder). Options per case: let the controller follow the streamed
  position freely (no ground snap) while the player is airborne, climbing
  or swimming (`LocomotionDetailed` says which); teleport for jumps up a
  ledge; check that an NPC body can enter water at all (else a dedicated
  "swim" placement). Each case gets a recorded-pose test and a scripted
  mock-player path (stairs, ladder, water). Facts:
  `research/coop-player-vehicles.md` §5 (locomotion specials).
- **Getting in and out of a car, every way.** Today a puppet is mounted
  and unmounted instantly, so it pops in and out of the seat. It must look
  like the game: the enter and exit animations, and every way out:
  - jumping out of a moving car (the bail-out roll), and the car rolling on
    with nobody at the wheel until it stops;
  - being thrown out or ejected by a crash, and dying at the wheel;
  - the car exploding while someone sits in it, or being stolen by another
    player;
  - a motorbike's mount and dismount, and falling off one;
  - leaving the session mid-drive (unplanned disconnects).

  The driver's game owns the car while it is driven. Once nobody drives
  it, the car (still moving) belongs to the host, which simulates it to a
  stop and syncs it like a parked car.

- **Cyberware, skills and perks in a shared world.** Every ability a V has
  must work in co-op, on real enemies and on the other players' screens,
  with the game's own sound and visual feedback ("seen and heard"): arm
  cyberware (mantis blades, gorilla arms, monowire, projectile launcher),
  legs (double jump, charge jump), Sandevistan, Kerenzikov, Berserk, optical
  camo, quickhacks and the RAM they use, skill and perk effects (slow
  motion, damage bonuses, finishers, health and stamina effects). Rule to
  start from: the player's own game runs the ability for that V (input,
  animation, cost, cooldown, its local effects); what it does to shared
  state goes through the owner of that state (a quickhack or perk effect on
  an NPC is a request to the host, like a hit); the others see and hear the
  ability on the puppet (status effect VFX, weapon and cyberware visuals,
  sounds). Abilities that bend time follow the time rules above. Facts per
  ability: `research/coop-player-vehicles.md` §4, `research/coop-time.md`,
  `research/coop-npcs-combat.md` (quickhacks, finishers).

  Every perk, relic perk, cyberware ability and quickhack has been read for
  co-op risk (`research/coop-perks.md`, 2026-11-18, scripts only). Most
  perks are attacker-side modifiers and already work through the guest's
  hit pipeline. The blockers have a few root causes: on the host a guest's
  puppet has no perks (`PlayerDevelopmentSystem.GetData` is null for it)
  and NPC-side code reacting to "the player" acts for the host's V (second
  chance, gag, finisher offers, Bait, quickhack alerts and reveals, spread
  stats, NPC netrunners hacking only the host); a V's defensive side
  (blade deflect, armour cyberware, dodge, health floors) must run on the
  guest for NPC hits (done: the host sends the attack, the guest runs the
  hit on its own V; see Fights); camo and visibility must reach the host's senses;
  carrying, throwing, finishers and Breach Protocol daemons change host
  NPCs and devices. Rules to adopt with them: a hit carries its whole
  outcome (poise, non-lethal, cap at 1 HP, reaction, dismemberment); the
  guest predicts kills so on-kill perks fire at the hit, and queues its
  skill XP itself (done: see Fights); area and spreading effects run on one game;
  the session plays at the host's difficulty.

- **Combat in and around vehicles.** Everything that happens in a car must
  work with several players, seen and heard by all: shooting from a seat
  (driver combat with a one-handed weapon, passengers leaning out, aiming
  and the seat's own animations), weaponized vehicles (mounted guns and
  missiles: `vehicleBaseObject.NPCShoot`, `vehicleMountedWeaponShootEvent`),
  quickhacks on vehicles, ramming and running people over, vehicle damage,
  destruction and explosions, tyres shot out, being thrown out on a crash,
  car chases with enemies (and NCPD). Direction: the driver's game owns the
  car's movement and its damage; a passenger's shots are that passenger's
  like any other shot (a `Shot` from the seat, hits to the host); damage
  done _to_ a player's car by NPCs goes through the host to the driver's
  game, like damage to a V; destruction and explosions are events from the
  car's owner so every copy breaks the same way. Facts:
  `research/coop-player-vehicles.md` §8.6–8.8, `research/coop-npcs-combat.md`.
- **NCPD chases.** The authority table gives police heat to the host, but a
  chase is about one player: who committed the crime, whom the units hunt,
  who escapes. The game's prevention system only knows its own V
  (`research/coop-quests-world.md` §9). Whatever the rule, police units are
  NPCs, so the host runs them all; a guest's crimes reach it as crime
  events; guests' own prevention is locked off (that lock is saved: remove
  it on leave) and their wanted bar shows what the host says
  (`UI_WantedBar`). **Undecided**: one wanted level shared by the group or
  one per player; chases when players are far apart; vehicle pursuits; what
  losing the police means for a group. Sirens, radio chatter and the wanted
  bar follow the "seen and heard" rule.
- **Saves** (decided 2026-11-18): **quest progress, NPC lives and the state
  of the city belong to the host's save; guests join it.** Whatever happens
  in a session (quests advanced, NPCs killed, doors opened, cars parked,
  time of day) is recorded by the host's game in the host's save. A guest's
  game neither saves that world nor brings its own: its world-state saving
  is locked while in a session, and leaving puts it back in its own world
  as its save had it. What stays the guest's own, in its own save: its V
  (level, XP, perks, inventory, money, appearance, as the authority table
  says). Open: what a guest keeps of a session it played (loot it picked
  up, XP it earned: yes; quest progress: no, unless it plays that quest in
  its own game), and rejoining a host's save later.

  Option for later, when players play apart (region limits): **a world
  ledger on the server.** The simulation cannot leave the players' games
  (no headless game: NPC AI, physics and fights run only in a copy of
  Cyberpunk), but the server (the relay, grown) can hold the world's
  lasting facts: quest facts, the dead, what was looted or destroyed,
  heat, the clock and weather. It can also decide which game runs which
  region. Each game writes what happens around its V, and applies on load
  or on entering a region what it has not seen yet. Each player's save
  then keeps only their V. Cost: persistent storage, accounts, conflict
  rules between facts, and replaying facts into a game (quest facts via
  the quests system; deaths, loot and destroyed objects via their
  persistent states). Until then the host's save is the source.

- **Story decisions** (2026-11-18; design in `research/coop-quests-design.md`):
  the host's game runs the story; a guest's own story is frozen for the
  session (not overwritten), its saves locked, and restored as it was when
  it leaves (one save and reload). Decided by the user: in dialogues with
  choices **the host picks and guests suggest** (the same native choice hub
  on every screen, each with its own cursor, the host sees suggestions);
  **holocalls and texts reach everyone in sync and anyone can answer or
  reply for all, first wins**; a guest far away when the host starts a
  story scene **is offered to join** (a native-looking prompt to regroup;
  declining leaves it the subtitles and journal); a guest earns a quest's
  rewards when it was near the host (150 m) as the quest completed or
  advanced, **only once**: a quest it was rewarded for in co-op pays
  nothing again when it later plays it in its own save. Provisional (the
  design's proposals, to confirm): romance and intimate scenes are never
  replayed for guests; leaving reloads the guest's game; subtitles in the
  host's language for now; shards and the codex stay per player; Johnny's
  appearances beside the host are shown to guests later; after the first
  version: scene voice, then quest-only interiors, then solo gigs.
- **Quests together, gigs apart.** A quest or gig the players finish
  together rewards both: each game gives its own V the rewards (XP,
  street cred, money, items, the fixer's message) by the game's own
  rules, the guest's game told by the host when it completes with them
  taking part. Players may also split up and do gigs on their own (fixer
  jobs, side gigs, NCPD hustles) while the other does something else: the
  game of the player doing it runs that gig's quest and NPCs in its region
  (Region limits) and reports the outcome (gig done, NPCs dead, items
  taken) to the host's record of the world, so it stays one world. Main
  story quests stay the host's, played together. Solo gigs come after the
  first story version if they prove too costly; design in
  `research/coop-quests-design.md`.
- **Slowed and stopped time.** The single-player game bends time for one V,
  and in a shared world that V is not alone. Everything that does it must be
  handled, each with a deliberate co-op rule:
  - _time dilation_: Sandevistan, Kerenzikov, scanning with the Kiroshi
    optics (and perks that slow time while scanning or aiming), slow-motion
    finishers, quickhack breach timers, dodge/slide slow-mo;
  - _another timeline_: braindances (the player is in a recording, not in the
    world), Johnny flashbacks, the Relic sequences;
  - _skipped time_: sleeping, waiting, fast travel, phone-call time skips;
  - _stopped time_: the pause menu, inventory, map, character and perk
    screens, crafting, the wardrobe, photo mode, tutorials and popups —
    single-player pauses the whole world for all of them.

  Done (2026-11-18, not yet run in game; scenario `time`, unit tests in
  `tests/timebubble_test.cpp`): the rule per mechanism, in
  `scripts/tandem/Time.reds`.
  - _Sandevistan, Kerenzikov_: a **time bubble** around the V using it
    (Inside the plugin). On its own screen the user keeps the
    single-player slow motion; the shared world slows only where it
    matters, the enemies around that V, really and for everyone, so the
    ability keeps both its purposes: more actions per enemy action, and
    more real time to aim. Its hits go to the host at real time, as
    always. The host cannot slow only its view of its own NPCs (the time
    system has global, local-player and per-entity scopes, no render-only
    one: `research/engine-knowledge.md` §13.15), so as the user it keeps
    its world at normal speed and its bubble does the slowing (default),
    or with `time_host = full` keeps the whole slow motion and exempts
    what it shares (experimental: its projectiles, physics and clock
    still slow). Kerenzikov is the same, shorter; its V keeps its own
    player factor.
  - _What the others see_: the NPCs around that V slow down, the V itself
    moves at its normal speed among them (it is never slowed, nor sped
    up), with the implant's effect played on its puppet (effect name to
    confirm in game). Two Vs in a Sandevistan at once: one bubble each, an
    NPC in both takes the stronger factor; each sees the NPCs slowed and
    the other V at normal speed; when one ends, its NPCs go back unless
    the other's bubble holds them.
  - _Scanner and quickhack panel, Focus, Deadeye's reload, Berserk, melee
    hit stop, deflect_: local only, no bubble. A guest keeps them on its
    screen (proxies and puppets exempted, so the shared world shows at its
    true speed); the host drops their world part. Others see the V as it
    is (standing, scanning).
  - _Second chance_ (its dilation is in TweakDB, reason unknown): any
    other dilation left on the host is logged to find it.
  - Breach timers and network dive: not handled yet (owner-local, real
    time, `research/coop-time.md` §2.7).

  To verify in game: the bubble NPCs slow on host and guest alike, that a
  factor of 1.0 keeps Sandevistan going on the host (else 0.999 is used),
  the effect on the puppet, the projectile bullets of NPCs while a held
  dilation is active, and how a guest's proxies look under its own slow
  motion.

  Direction to start from: the world (host) never slows or stops for one
  player. A V's own time effects are local to that player's game and seen by
  the others as what they are (that V moving very fast, or scanning); the
  rest of the world keeps running on the host's clock, with a co-op stand-in
  for what the effect gives the player (for example, Sandevistan speeds that
  V up for everyone instead of slowing everyone else). Menus don't pause a
  co-op world. Story moments that move everyone (braindances, flashbacks,
  time skips) follow the host, with the guests brought along. Every effect
  must be listed, decided and tested; the authority table gets a row for
  each.
