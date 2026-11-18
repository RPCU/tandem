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
every nixbook machine), then the relay when NAT blocks UDP. An invite carries
the host's addresses and a relay session secret; the joining game tries them
in order.

## Authority: who decides what

The owner simulates; everyone else displays.

| State                                   | Owner                     | Others                                        |
| --------------------------------------- | ------------------------- | --------------------------------------------- |
| a V: transform, stance, aim, animation  | that player's game        | puppet, interpolated                          |
| a V: appearance, equipment, cyberware   | that player's game        | applied on the puppet when it changes         |
| a V: health, death, downed              | that player's game        | shown; revives are requests to the owner      |
| a V: level, XP, perks, inventory, money | that player's game (save) | not shared: each player keeps their own level |
| a vehicle someone drives                | the driver's game         | puppet vehicle, passengers mounted            |
| a parked or AI vehicle                  | host                      | proxy                                         |
| NPCs: AI, position, health, death       | host                      | **proxy with its AI off**, driven by the host |
| spawns: crowds, traffic, encounters     | host                      | guests' own spawning is suppressed            |
| quests: facts, journal, phases, scenes  | host                      | followed, never run on their own              |
| time of day, weather, police heat       | host                      | applied (wanted level: open, see below)       |
| loot drops                              | host                      | each player loots their own copy              |

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
  (above). Open: aggro (the NPC turning on whoever hit it), quickhacks
  (uploads, not hits), NPCs hitting guests (milestone 5, next), loot.
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
- Configuration: `tandem.ini` next to the DLL (role, address, port, name, pose).

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

- **Sessions**: a 16-byte secret the host picks and shares in the invite.
  Guests register with the secret; the relay gives each a channel number and
  stamps it on what it forwards to the host.
- **Who may host**: only hosts presenting a token from the relay's token
  file (a Kubernetes Secret), so the relay is not an open bandwidth donor.
- **Abuse limits**: unknown addresses never get an answer (no reflection);
  registration requests are padded so no answer is larger than its request
  (no amplification); per-client packet and byte rate limits; at most
  `max-sessions`; idle clients forgotten after two minutes.
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

On the game side, the plugin talks to the relay through a small UDP proxy on
localhost, so ENet itself never knows the relay exists (planned, milestone 7).

## Testing without a second player

| Level       | What                                                                  | Command                           |
| ----------- | --------------------------------------------------------------------- | --------------------------------- |
| unit        | protocol round trips, strict decoding, sizes                          | `task test`                       |
| loopback    | real host + guests over UDP in one process: join, relay, clock, leave | `task test`                       |
| relay       | protocol, limits, amplification, real sockets, race detector          | `task relay:test`                 |
| mock player | `tandem-peer` as host or guest against the real game                  | `task peer -- host --mode mirror` |
| natives     | `Natives.reds` matches `natives.cpp`: types, struct fields, alignment | `task natives:check`              |
| scenarios   | the real game against the mock player, checked on the game's log      | `task scenario -- all`            |
| in game     | deploy the DLL, read its log                                          | `task deploy`, `task logs`        |

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
  without anyone driving.
- **idle** — connects and reports.
- **quickhacks**: `join --hack-npcs N` uploads `--hack-action` on one of
  the host's NPCs every N s (1.5 s) and then lands `--hack-effect`;
  `host --replay FILE --hack-player N` has a replayed NPC hack the guest's
  V with `--npc-hack` (default the trace); `--hack-car N` brakes the car
  another player drives. The game side's test commands: `tools/game.sh cmd
hack npc [RECORD]` (our V quickhacks the nearest proxy) and `hack car`.
- `--pause-at N [--pause-for M]` (any mode) opens "its pause menu" N s after
  another player is in the session, for M s (default 3): every game pauses.
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

| #   | Milestone           | Done when                                                                                                                                     |
| --- | ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| 0   | Toolchain           | DLL built on Linux loads in the game and logs; mock player; relay image ✓                                                                     |
| 1   | A second V          | a puppet follows the other player smoothly (✓ 2026-11-17: engine controller, 0.01 m, walk animation, NPC rig)                                 |
| 2   | Player presence     | appearance, equipment, locomotion and aim animations, weapons drawn (pose, look, clothes, held weapon, shot effects ✓ 2026-11-18)             |
| 3   | Vehicles            | drive together, ride as a passenger (copy, seated driver, get out, leave: ✓ 2026-11-18; passengers to test)                                   |
| 4   | Shared world basics | time, weather, police heat follow the host; tether and regroup on fast travel (time, weather ✓ 2026-11-18)                                    |
| 5   | One set of enemies  | NPC proxies with AI off on guests; hits, damage, deaths, loot through the host (proxies follow the host's NPCs, hits and deaths ✓ 2026-11-18) |
| 6   | Story               | quest facts and journal follow the host; dialogue choices; scenes for guests                                                                  |
| 7   | Anywhere            | relay fallback in the plugin, invites (join codes), co-op in the main menu: host or pick whom to join                                         |
| 8   | Co-op features      | revive, pings and shared markers, and what we add as we play                                                                                  |

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

- **Scenes for guests.** Replaying the host's quest-scene nodes in a guest's
  game (the campaign fork's approach) is unproven: nodes run outside their
  phase. Alternatives to study: guests follow the scene as spectators of the
  host's camera; or run the same phase on guests with quest progression
  locked to the host.
- **Suppressing guests' own world simulation** (crowds, traffic, encounters,
  quest phases) without breaking systems that expect them.
- **Region authority** once players spread out.
- **Co-op from the main menu.** Today a session is set up in `tandem.ini`
  (role, address). The goal: a co-op entry in the game's main menu, where a
  player hosts (with one of their saves) or picks whom to join — friends'
  sessions, a join code from an invite, the relay for players behind NAT —
  and then loads straight into the host's world with their own V. Options
  (name, privacy, max players) live there too; `tandem.ini` stays for tests.
  Part of milestone 7 (invites, relay); needs an ink widget in the main
  menu's controller and a lobby list the relay or a Steam-free directory can
  answer.
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
  radio back as it was.
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
  only, per NPC and per player, by the game's own rules.
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
  them", and a per-player detection level for the meter is to come. A
  guest's game drives its own V from that: the stealth meter and the
  detection indicators over the proxies, its combat state (which decides
  stealth bonuses, `AttackData.IsPlayerInCombat`, and combat-only perks),
  and its music. Blocker found (engine-knowledge §13.11): the host's NPCs
  do not see puppets at all (detection 0, injected threats ignored), so
  they cannot detect or fight a guest yet. Puppets need the components NPC
  senses look for; they must keep an attitude group of their own (in
  `player`, hostility to one V becomes hostility to all).
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
  (blade deflect, armour cyberware, dodge, health floors) runs on the host
  for NPC hits, so the host must send the attack and the guest run the
  hit on its own V; camo and visibility must reach the host's senses;
  carrying, throwing, finishers and Breach Protocol daemons change host
  NPCs and devices. Rules to adopt with them: a hit carries its whole
  outcome (poise, non-lethal, cap at 1 HP, reaction, dismemberment); the
  guest predicts kills so on-kill perks fire at the hit, and queues its
  skill XP itself (today a guest earns none: XP is queued only when
  damage is applied for real); area and spreading effects run on one game;
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
