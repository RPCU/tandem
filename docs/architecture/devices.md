# Devices and world objects

A door one player opens is open for the other. A camera one player hacks
no longer watches the partner. A barrel that blew up is gone in every game.
This file is the design; the engine facts behind it are in
[`../research/engine/devices.md`](../research/engine/devices.md), each with
its tag. **Status (2026-11-19): built and tested offline (core, mock
player, scripts compile); nothing has run in game yet** — the scenarios at
the end are the first thing to run.

Code: `core/include/tandem/devices.hpp` (the record, and who breached
what: `devices::Breaches`), `core/src/endpoint.cpp` (who hears what),
`plugin/src/natives.cpp` (`ReportDevice`, `UseDevice`, `AskDevice`,
`TakeDeviceEvents`; `SendBreach`, `TakeBreachEvents`),
`scripts/tandem/Devices.reds` (the game side), `scripts/tandem/Breach.reds`
(Breach Protocol), `tests/devices_test.cpp`, `tests/breach_test.cpp`.

## The decision: the host owns every device, a guest's own copy changes first

Two candidates:

- **Host-owned state, guests ask** (as for NPCs). One owner, one order of
  events, and the host's game is where quests and NPC AI look at devices
  (a quest waits for a door, an NPC walks to a distraction), so its copy
  must be the real one anyway.
- **First actor wins, host arbitrates** (as for dropped loot). Feels
  instant, but a device is not a thing one player ends up holding: it has
  a state every game must agree on, and the host's quests change devices
  with no player involved.

Chosen: **host-owned, with the guest's own copy changed at once**. A
guest's V operates the device in its own game, with the game's own prompt,
animation and sound, and never waits (principle 2). Its game then tells the
host _what the device is now_; the host's game brings its own copy to that
state the game's way and its word goes to everyone. That is first actor
wins in feel and one decider in fact: when two players act within a ping,
the host's order of arrival decides, and the loser's copy is put back by
the host's word (a door that shuts again, as it would for a slammed door).

One principle follows for devices that act on players (cameras, turrets,
mines, lasers, barrels): **a device runs in every game against that game's
own V; what it _is_ is the host's** (and, for a turret, what it turns to
and fires at: "A device's fire"). The game's sensing code is written
around the local player (it does not treat a puppet as one), a turret that
shot a guest through the network would hit late, and each game already has
the device, its cone and its gun. So your V is seen, shot and blown up by
your own game's copy, at once; the host forwards none of a device's hits on
a puppet (`Combat.reds`, `ForwardsToPlayer`); and the copies agree because
their state (on, off, friendly, destroyed, the alarm) is the host's.

## What is synced, and who owns it

<!-- prettier-ignore -->
| Device (state class) | What is synced | Owner |
| --- | --- | --- |
| Doors, gates, shutters (`DoorControllerPS`) | open, locked, sealed; status | host; a guest's own copy changes first |
| Elevators (`LiftControllerPS`) | the floor it is at or going to (every game's cabin leaves together); status | host. Who rides: each V is carried by its own game's cabin, puppets are placed by their player's stream (the "in a moving elevator" placement) |
| Cameras, turrets (`SurveillanceCameraControllerPS`, `SecurityTurretControllerPS`) | on, off, unpowered, disabled (destroyed), friendly (turned, or in a player's hands), hacked | host. Seeing and shooting a V: that V's own game's copy; a friendly one sees no V in any game ("A camera or turret turned"). A turret's aim and shots: the host's turret ("A device's fire") |
| The alarm (`SecuritySystemControllerPS`) | safe, alerted, combat | host. Raised by whichever game's device saw its V (on the host: as that V's puppet, where it stands, so the NPCs go there); called off by the host only |
| Access points (`AccessPointControllerPS`), their networks | breached; each device's "quickhacks open" bit; what the breach's daemons did to each device (their own state) | host. Breached once for everyone; the datamine's money and shards go to who breached (rolled by that game, as corpses and containers are) |
| Computers, terminals (`ComputerControllerPS`, `TerminalControllerPS`) | status, hacked; what they did to the devices they command (those devices' own state) | host. Reading mails and files: each player in their own game; a quest fact a read sets follows the story rules |
| Barrels, mines, traps (`ExplosiveDeviceControllerPS` and kin) | exploded (played as an explosion where it is in sight, a record elsewhere); status | host. The blast hurts each game's own V from its own copy, and NPCs in the host's game only; a guest's doing goes off on the host's copy from that guest's puppet (`ExplosiveDevice.TandemDetonate`: its kills and its crime the guest's; not yet run in game) |
| Radios, TVs (`MediaDeviceControllerPS`) | on, off; the station or channel | host |
| Lights, fuse boxes, generators, fans, vending machines, any other device | status (on, off, unpowered, disabled), broken, hacked, distracting (a glitch: on the host a real distraction, so its NPCs come and look) | host. A vending machine's sale: the buyer's own game (money and item are that V's) |
| Glass, covers, breakable scenery | the moment a piece breaks (`Fracture`): physics destruction has no script state; the session's record of what broke, for who was not there | nobody: a break is for good, whoever's game saw it first tells; the host keeps the record ("Glass and breakable scenery") |
| Loot containers | nothing (per player: `loot.md`) | each player's game |
| Spawned devices (dynamic ids), vehicles | nothing here (not the same entity in two games; vehicles have their own sync) | — |

A device is **named** by its entity's static id (64 bits, from the world
file: the same in every game) and the class name of its state; its
`PersistentID` component is always `controller`.

## Messages (appended; no protocol version change)

<!-- prettier-ignore -->
| Message | Direction | What |
| --- | --- | --- |
| `DeviceState` | host → guests | the session's word on a device: status, state bits, one value, the host's revision of it, who changed it last, and `how` (live: it just happened, play it; quiet: a record, set it). Kept for late joiners. About 30 bytes |
| `DeviceState` | guest → host (`kDeviceAsk`) | "this came into my world like this": the host answers with its own when they differ |
| `DeviceUse` | guest → host | "my V made it this" with the revision the guest had seen. One-shot |
| `DeviceDigest` | host → guests | count and hash of the record, with every `Digest` |
| `DeviceSet` | both | guest: send me the record again; host: here it comes, forget yours |
| `Breach` | any game → host → the others | a breach's outcome: the access point (or the NPC breached through the quickhack), the daemons and traps won, who breached; the host passes a granted one on (`kBreachApply`). One-shot ("Breach Protocol", below) |
| `BreachResult` | host → the breacher | granted, or refused: breached first by whom |
| `DeviceFire` | host → the guests linked with it | its turret turned to a target (said again every second), shot at it, or follows nobody. A shot at a V: Event channel (that V's game fires it for real); any other shot: State channel. At most 43 bytes |
| `DeviceFire` | guest → host | "my copy of that turret has locked my V", "…lost it" (kept by the host: a repeat tells nothing, a vanished guest is let go) |
| `TurretHands` | the controller's game → host → the linked | a turret in a player's hands: where it points, whether it fires (5 a second while held), let go. State channel, at most 32 bytes ("A turret in a player's hands") |
| `TurretBusy` | host → the refused player | its claim on a turret another player holds is refused: who holds it. One-shot, at most once a second per claim ("A turret in a player's hands") |
| `Fracture` | any game → host → everyone (a piece new to the record) or the linked (another piece of a known one) | something broke: where, and the placed entity's destruction component when it is one. About 34 bytes; the host holds each guest to 48 then 24 a second. The record's entries again to a late joiner and in a world resync |

Sending the _state the device should be in_, not the action that was
pressed, makes every message idempotent, makes a late joiner and a live
change the same code, and lets one function (`TandemDevices.Apply`) bring
any copy to any state with the actions quests use (`ForceOpen`,
`ForceLock`, `GoToFloor`, `ForceDetonate`, `ForceON`…): the door opens with
its animation and sound because the game opened it.

## How it runs

**Host.** Its game looks at the devices around its V (32 a frame, all of
them every few frames) and, at once, at any device whose state just handled
an action anywhere in the world (a wrap of
`ScriptableDeviceComponentPS.Notify`, which player actions, quickhacks and
quest nodes all end in). What changed is reported (`ReportDevice`), the
record counts a revision, everyone hears it live. A guest's question about a
device the record does not have is answered from the host's own state
(quiet: it was like that).

**Guest.** A local change that is not the host's doing is our V's (or our
game's own: a door closing itself): `UseDevice` says what the device is
here now. What the host says is applied to our copy, loaded or not; what
does not take (the game refused) is tried three times, then logged and left.
A use of a placed device while grouped is also a quest interaction
(`story.md`, "Quest interactions"): our V's change waits for the host's
verdict (`TandemDevices.Hold`, 4 s at most). Performed: the host's V did the
same action on its copy and its report (in our name, `PerformedFor`) is the
device's word; anything else: our change is asked of the host as above
(`Release`).

**Streaming.** The wraps of per-entity callbacks (`Device` and
`VehicleObject` `OnGameAttached`/`OnDetach`, the device state's `Notify`
and `ExplosiveDeviceControllerPS.SetExplodedState`, the loot's
`Entity/Attach`) run as the world streams in, on job threads during a load,
and touch no script state: they queue the entity id (and the state's class,
for a touched device) through `TandemStreaming.QueueStreamed`
(`StreamingNatives.reds`, a mutex-guarded vector per kind, 65536 at most,
overflow dropped and logged). `TandemSystem.OnTick` (session or not) drains
them into `TandemDevices.Drain` and `TandemTraffic.Drain`, `TandemLoot.Tick`
its own, in order; an entity gone by then is skipped (its detach follows).
Mutating the systems' arrays from the wraps themselves is the suspect of heap
corruption while a save loaded (access violation in
`DefaultAllocator::AllocateAligned`, 5 launches in 6, 2026-11-20).

**A partner's quickhack shows** on the device, in their colour, while the
two are linked: the bits a guest's request (host) or the host's live word
(guest) turned on go to the partner-quickhack board
([`comms.md`](comms.md), "A partner's quickhacks": `PartnerDevice`).

**A request at the host** (`Ledger::Judge`, `Endpoint::OnDeviceUse`):

- nobody else changed the device since the revision the guest saw, or the
  last change was the guest's own → the host's game applies it and reports
  what the device became a quarter of a second later (1.5 s for an
  explosion, which has a fuse). Everyone hears the change; when the device
  did not become what was asked (sealed, unpowered), the asker alone gets
  the record back, and its copy follows;
- someone else was first (a newer revision by another player, or another
  player's request already with the host's game) → void; the asker gets the
  device as it is. Counted (`Endpoint::DeviceStale`);
- the host's game has no state for that device (never been there) → the
  record takes the guest's word, and the host's copy is set when it first
  comes into the host's world.

## Consistency

- **Digest and resync.** With each `Digest` the host sends a `DeviceDigest`
  (count, FNV-1a of every record with its revision). A guest whose copy
  differs twice in a row asks (`DeviceSet`, at most once per cooldown); the
  host sends the whole record quietly; what it no longer names goes back to
  what the guest's own save had.
- **Idempotent.** A state that is already the record's is told to nobody
  and applied nowhere (host: `Ledger::Report`; guest: `Ledger::Accept`);
  the script compares with the device's real state before it does anything.
  `DeviceUse` is a one-shot (never duplicated by the fault layer).
- **Fencing.** A device request is fenced by the device's own **revision**,
  not by the world epoch: a door does not go away when the host fast
  travels, and a request made against an old state of the door is exactly
  what the revision catches. A _guest's_ rebuilt world (its fast travel)
  needs nothing either: its devices stream in again and each asks.
- **Late joiner.** Gets every record quietly at its join (with the world
  domain); its own devices follow as they come into its world, and those
  not loaded are set in their state objects at once.
- **An actor that vanishes.** A device is the world's, not a player's:
  what a vanished guest changed stays changed; its request still with the
  host's game is forgotten (the device is whatever the host's game made
  it, and everyone has heard that). A vanished host: each guest's session
  ends and its world is put back (below). `tests/devices_test.cpp` kills a
  guest with a request in flight, and the host.
- **Bounds.** 4096 records; a request for an unknown device when full is
  dropped; queues are bounded on both sides of the plugin.

## Breach Protocol

Three ways in, one outcome: jacking into an access point with the personal
link, the Breach Protocol quickhack on a device or an NPC, and a mod that
skips the minigame (AccessPointSkip wraps `Device.PerformDive` and fakes a
succeeded `AccessPointMiniGameStatus` with the programs). Engine facts:
[`../research/engine/combat.md`](../research/engine/combat.md) §13.60.

- **The minigame is the breacher's**, as in single player: its screen and
  timer (the world does not slow: `time.md`), and its rewards, rolled on
  its game for its V (the datamine's eddies, crafting components, shards,
  the material trap). The others see its puppet busy in a minigame
  (`activities.md`). Nothing is sent before it ends: a breacher that
  vanishes mid-breach leaves the network as it was.
- **The host's grant decides the payout.** The game's own handling of a won
  outcome (the network breached on the breacher's copy, its daemons there,
  the rewards) is held until the host answers, a ping later
  (`TandemBreach.Queue`, `Settle`; core `devices::BreachHolds`, one hold
  per minigame, each network it reached a target). Granted for one of
  them: the game handles the win. Refused for all: it handles a minigame
  that ended with nothing (`HackingMinigameState.Unknown`: the dive ends,
  no daemons, no rewards, no alarm), the breacher reads "NAME breached this
  network first", and its copy of the access point is the host's word. No
  answer in 3 s (`kBreachHoldUs`), or the session over: as won. A lost
  minigame is never held. The host's own breach is held the same way (its
  answer comes at once).
- **Its outcome is caught, not its screen**: every breach ends with an
  `AccessPointMiniGameStatus` to what was breached, the won daemons on the
  `HackingMinigame` blackboard. `Breach.reds` wraps
  `Device.OnAccessPointMiniGameStatus` (access points, devices through the
  quickhack) and `ScriptedPuppet.OnAccessPointMiniGameStatus` (NPCs) and
  sends `Breach` before the game handles it: a mod that fakes the
  outcome needs nothing of its own.
- **The host decides** (`Endpoint::OnBreach`, `devices::Breaches`): the
  first breach of a network stands, any later one (anyone's, the host's V
  included, the same breacher again) is refused with who was first, as is
  one of a network the host's world had breached already; the same for an
  NPC through the quickhack. A granted network's access point becomes
  breached and open in the record, by the breacher (`DeviceState`: every
  game's copy, late joiners and the device digest included); each copy
  gets `BreachAccessPointEvent` and the minigame marked won
  (`TandemSetMinigameWon`), so **the game itself offers no second
  jack-in** and the quickhack's `IsBreached` prereqs lock. A refused
  breacher sees "NAME breached this network first" and is paid nothing
  (above).
- **The owner of what it affects runs the daemons**, the game's own way,
  with the breacher's puppet as the player: the host on the network's
  devices (each child's own minigame action, as
  `ProcessMinigameNetworkActions` picks it: cameras and turrets shut down,
  friendly, malfunctioning; `SetExposeQuickHacks` on each; the awareness
  trap) and on its NPCs of it (the daemons each one's character record
  lists, as `ScriptedPuppetPS.OnSetExposeQuickHacks` runs them; the squad
  trap); every other game on the NPCs of that network it runs; the owner of
  an NPC breached through the quickhack on that NPC, and the host on its
  network when it is on one (the breacher's proxy has no device link to
  find it). A lost minigame (`kBreachFailed`) breaches nothing: the
  owners alert the NPCs. Loot programs never run anywhere but on the
  breacher's game.
- **Results come back the usual ways**: devices through their state (a
  camera off, a turret friendly, every device's exposed bit), NPCs'
  daemon effects (`MinigameAction.ICEBrokenMinigame*`,
  `VulnerabilityMinigame`, the squad buff) through `NpcStatus`
  (`Hacks.reds`), so every proxy looks weakened and a linked partner sees
  them as partner effects (`comms.md`); a proxy never forwards a daemon
  effect of its own (`Combat.reds`).
- **An access point is opened by its breach only**: `SetExposeQuickHacks`
  on one runs `RefreshSlaves` (the local blackboard's last daemons and the
  money), so `TandemDevices.Apply` never sends it there.

## A camera or turret turned

"An allied hacked camera (or turret) is no enemy any more", in every game,
whoever turned it. Built 2026-11-21, **not yet run in game**. Code:
`Devices.reds` (`KeepFriendly`, `Unbefriend`, `SensorDevice.TandemKeepFriendly`,
`TandemUnbefriend`, the `SetAsIntrestingTarget` wrap), `Turrets.reds`.

- **Vanilla's friendly** (2.50 scripts): Friendly Mode on a camera is
  `ForceIgnoreTargets`, on a turret `SetDeviceAttitude`; both end in
  `SensorDevice.OnSetDeviceAttitude` → `SetDeviceFriendly`, on the entity:
  its attitude agent friendly to the **local** player and in the local
  player's group, its senses off the local player
  (`SetDetectionOverwrite`), `KeepHostilityTowardsPlayerHostiles`, the
  friendly sense preset (a turret), green lights. The state only carries
  `m_isAttitudeChanged`. A turned device's sightings are judged by its own
  attitude (`SetAsIntrestingTarget`), so the security system never hears of
  a V it is friendly to; nothing in vanilla calms a security system already
  alerted when a camera is turned (`SendReprimandInstructionToSecuritySystem`
  only ends a reprimand), so neither do we.
- **What was missing**: a copy's `friendly` was the state's flag alone when
  the device was not streamed in (its entity came hostile to that game's V,
  which its camera then called an intruder, and its alarm went to every
  game), friendly to the local V only (the puppets, in groups of their own,
  stayed targets), and "not friendly" was a `SetDeviceAttitude` hostile,
  which vanilla does not undo (the flag stayed set).
- **Now, in every game** (`friendly` = the state's flag, or the device in a
  player's hands: the takeover turns it for the others too): the word's
  arrival runs the game's own `SetDeviceAttitude` friendly on the entity;
  every look at it (32 devices a frame) keeps it so: hostile to our V again
  (streamed in again, a state set while away) → the same action, at most
  every 2 s; each puppet it is hostile to gets its attitude to our V and is
  dropped from its senses; a puppet it followed is lost. A V never becomes
  its target while it is turned (`SetAsIntrestingTarget`, a read-only wrap:
  the moment between a stream-in and the next look). A Friendly Mode that
  ran out (vanilla leaves it neutral) is left so.
- **Cleared** (the session's word, our own save put back): the state's flag
  off, its group back (its security system's, as the game puts it back when
  that system is enabled again, else the one it had before), that group's
  attitude to our V and each puppet, its senses, sense preset, aim assist
  and lights as before.
- **Turrets**: the host's turret takes no puppet while turned or in another
  player's hands (`TandemEngage`); a guest's turned copy says nothing of our
  V (`Locked`).
- Logs: `devices: E C turned for every V here: …`, `… back to its own
attitude for every V here`. Queries `device KIND attitude|puppet|target|
alarm` (testing.md). Scenarios `devices-camera-friendly` (the mock guest
  turns an enemy's camera, then back: friendly to V and the puppet, nothing
  followed, no new alarm) and `turret-friendly` (and its copy's `Seen`:
  the host's turret takes no puppet).

## A partner's hack, marked

"Visual feedback on the minimap and live for a device hacked by an ally."
Built 2026-11-21, **not yet run in game**. Code: `Devices.reds`
(`UpdateMarks`), `Comms.reds` (`MarkerOn`).

- **What**: an enemy's device in our world (on a security system that is
  not our V's friend, or a camera or turret whose own side is hostile to
  our V) that another player's doing holds turned (`friendly`: Friendly
  Mode, a takeover), distracted (`glitching`) or off with `hacked` (a
  shutdown). Our own hacks, and a device of ours or a neutral one, show
  nothing.
- **How**: a mappin drawn as our pings are (world, minimap, map: the `Use`
  icon in the hacker's colour, `TandemMarkers.ColorOf`), on the device,
  labelled "Hacked by NAME" (`Devices-HackedBy`), registered twice a second
  while it holds and the `markers` setting is on; gone when it ends, the
  device leaves our world or the session ends. Who: the player the
  session's word names (guest: the host's `DeviceState`; host: the guest's
  request). The vanilla friendly look (green cone and lights) is the
  device's own (above). Logs `devices: E C marked: hacked by player N`,
  `… unmarked (player N's hack)`; query `device KIND mark`.
- **Vehicles** (2026-11-21, not yet run in game): a quickhack on a shared
  vehicle no V drives (a traffic, parked or placed car, `traffic.md`:
  brakes, accelerate, explode) is `TrafficHack`, to the host and from it to
  every other player; the vehicle's owner applies it to the real one
  (`ActivateNetrunnerQuickhack`, `EnableHighPriorityPanicDriving` for
  acceleration: the game's own effects of the hack; the hacker's game when
  it owns the vehicle, as in solo), and every game but the hacker's marks
  it "Hacked by NAME" (the `car` icon) for the hack's length, 6 s for an
  explosion (`Hacks.reds`: `HackSharedCar`, `TrafficHacked`, `MarkCar`;
  query `car marks`; scenario `traffic-hack`, the mock's `--hack-traffic
S`). A car another V drives stays `VehicleHack`'s, unmarked (an ally's).

## A device's fire

"The others see the host's turret firing at what it actually targets, and
their own copy does not fire at their own V on its own." Built 2026-11-20,
not run in game; engine facts §13.90. Code: `scripts/tandem/Turrets.reds`
(`TandemTurrets`), the `AIWeapon.Fire` wrap in `NpcAnims.reds`,
`Endpoint::SendDeviceFire`, `OnDeviceSeen`.

- **Which devices**: placed security turrets (`SecurityTurret`) on their
  own. Not one a player controls (its shots are that player's), not the
  police's (`IsPartOfPrevention`: `police.md`), not a spawned one. A
  camera fires nothing; the sniper nest fires as its player.
- **Host**: every shot of its turret (`AIWeapon.Fire`, queued) goes to the
  guests linked with the host, with its target named the session's way (a
  player, a shared NPC, a placed entity, else the place); so does each
  target it turns to (`OnCurrentTargetAppears`), again every second while
  it follows one, then "follows nobody".
- **Guest, linked with the host**: its copy never starts firing
  (`SecurityTurret.ShootStart` held; a stray `AIWeapon.Fire` of it
  dropped). It turns where the host's turret turns (a look-at on the
  target's body here, held 1.5 s against its own look-ats) and fires each
  of the host's shots from its own weapon:
  - at another V, an NPC or a place: a drawn bullet
    (`TandemPresenceSync.Bullet`: the weapon's tracer, impact and sounds;
    its hits are nobody's);
  - at our own V: our copy's real shot (`AIWeapon.Fire` at our V, the
    game's own aim and miss rules): **our game decides the hit**, as it
    did when the copy fired on its own, and as for an NPC's shot
    (`NpcShotAt`). A shot older than 1.5 s on the host clock is not fired.
- **Sensing stays per game** (the engine's sensing knows the local player
  only: the principle above). A guest's copy that locks its V (its
  `ShootStart`) says `Seen`; the host's turret then takes that guest's
  puppet as a target the way a quest forces one (`TandemEngage`: hostile to
  it, `RecognizeTarget(puppet, true)`) and locks it at once (`TandemTake`:
  sensor state `TARGETLOCK`, `ShootStart`; not the game's `OnTargetLocked`,
  which sets the host's security system to combat), unless it is off,
  turned friendly or in a player's hands; `Lost` lets it go. The game's own
  lock never comes for a puppet: §13.90. The host's hits on the puppet
  are not forwarded (`ForwardsToPlayer`).
- **Its look** (guest, linked with the host): the copy's laser mesh and
  scan light show the host's turret, not the copy's own senses: laser on
  and red at each aim (and at a shot when the last aim's hold lapsed),
  laser off and yellow at "follows nobody" (green instead to a V it is
  friendly to, the game's own choice). Held 1.5 s against the copy's own
  changes (`DetermineLightScanRefs` wrapped; the laser is set again with
  each aim, said every second).
- **A turret the host's game does not run** (not in its world, or the
  guest is not linked with the host): after 1.5 s without a word of it
  from the host, the guest's copy fires on its own, as before; the host
  speaking of it again takes it back.
- **Consistency**: aim is state, said again every second (a late joiner
  hears it within one); a shot is a one-shot; `Seen` is kept per guest (16
  turrets at most) and dropped with the guest (`ForgetDevicesSeeing`); the
  session's end gives every copy back to its game.
- Tests: `tests/devices_test.cpp` ("a device's fire: …"),
  `tests/referee_test.cpp`, `tests/peer_devices.sh`; the mock player's
  `--turret-seen ENTITY[@S]` (guest) and `--turret-shots N` (host: a burst
  at whoever a copy has); in game `tools/game.sh cmd turret`, `turret aim`,
  `turret shot`, `turret engage`, `turret provoke` (the copy takes our
  own V); scenarios `turret-host`, `turret-guest`. Both need a turret we
  follow (`device near sentry`: a turret that is not the police's); the
  nearest at arasaka-park and drake-ave is the police's (2026-11-21), no
  fixture with a sentry is known yet (`fixture check` lists the nearest).

## A turret in a player's hands

"A turret a player controls (the takeover quickhack, a device's control)
shows its aim and shots on the others' copies; damage as today." Built
2026-11-21, not run in game; engine facts §13.90. Code: `Turrets.reds`
(`PollOurHands`, `OnHands`, `UpdateHands`), `Endpoint::SendTurretHands`.

- **The controller's game** (host or guest): the turret the takeover
  system holds (`TakeOverControlSystem.GetControlledObject`, a placed
  `SecurityTurret`, the police's too: in a player's hands it is that
  player's) is said 5 times a second
  (`TurretHands`): where it points (40 m along the active camera, the
  turret's while it is held) and whether it fires (its anim feature's
  `Shoot`); once more when V lets go. Its shots stay its own: what they
  hit is decided there, as for V's weapon (hits on proxies and puppets go
  the usual ways, `combat.md`).
- **Every other game linked with it**: its copy turns there (`TandemAim`
  on the place, held 1 s), fires nothing of its own (`ShootStart` and a
  stray `AIWeapon.Fire` of it dropped while held), and while the
  controller fires draws its fire every 0.12 s from its own weapon
  (`TandemPresenceSync.Bullet`, the weapon's effects, the barrels spinning):
  nobody is hurt by it. The host's `DeviceFire` words are not said for, and
  not applied to, a copy in a player's hands, nor to the turret in our own.
- **Let go**, or silent for 1 s (the controller vanished, unlinked): the
  copy is its game's again.
- **One player's hands at a time**, first come, the host's to decide
  (`devices::TurretHolds`, `Endpoint::HoldTurret`; built 2026-11-21, not
  yet run in game). The first `TurretHands` claim on a turret nobody holds
  makes its sender the holder until its let-go, 1 s of silence or its
  leaving. Another player's claim meanwhile (the takeover quickhack, a
  control panel, the host's own V) is not passed on: its game hears
  `TurretBusy` (the host's game from its endpoint, at once), at most once a
  second while it claims; its let-go is dropped too (it would free the
  holder's copies). That game lets its V go at once, the game's own way
  out of a device's control (`TakeOverControlSystem.ReleaseControl`, as a
  quest ends one; `Turrets.reds`, `Refused`), shows "NAME is controlling
  this turret" (`Turrets-Busy`) and says nothing more of it; taken again
  within 3 s, it is let go again unsaid, later a claim asks again. The
  referee holds both sides: a refusal of a turret we never claimed
  (`reply.unasked`), a guest refused three times in a row without letting
  go (`authority.turret-taken`).
- **Cameras** (2026-11-28, not run in game; [`remote-views.md`](remote-views.md)):
  a security camera in a player's hands (its takeover, a chain of them)
  turns on the others' copies as a turret does: where it points goes as
  `TurretHands` (never firing), every copy turns there (`TandemAim`, a
  sensor device's look-at), one player's hands at a time; the second is
  let go with "NAME is looking through this camera" (`Turrets-BusyCamera`).
  Its `friendly` bit travels as before (the others' copies see no V while
  it is held: "A camera or turret turned"). One with a dynamic id, or a
  sniper nest, turns only for its holder (`held device unshared`).
- Tests: `tests/devices_test.cpp` ("a turret in a player's hands", "a
  turret's holds", "… is that player's alone"), `tests/protocol_test.cpp`,
  `tests/referee_test.cpp`; the mock player's
  `--turret-hands E[:X,Y,Z][@S]` and `--turret-hands-for D` (held D
  seconds); in game `tools/game.sh cmd turret take` (V takes the nearest
  turret, as its control panel gives it); scenarios `turret-hands`,
  `turret-hands-busy` (the mock guest holds the turret, V's takeover is
  refused and let go, the guest's hold goes on). Run 2026-11-21 at
  arasaka-park: its turret is the police's, `OurHands` left those out, so
  V's takeover was never said, never refused, and the guest's hold lapsed
  while V held it; fixed, not yet run again.

## Glass and breakable scenery

Built 2026-11-20, not run in game; engine facts §13.90 of the research
file. Code: `scripts/tandem/Objects.reds` (`TandemObjectSync`),
`plugin/src/objects_natives.cpp`, core `devices::Fractures`.

- **No owner, no state**: destruction only goes one way, so a break is a
  fact any game may state and every game applies at most once. The host
  relays, stamps and rate-limits; it decides nothing.
- **What is told.** A destruction component of a placed entity that breaks
  (the engine's `PhysicalDestructionEvent` to its entity: the entity's
  static id and the component's name, the same in every game), or a piece
  of the world's own geometry, which has no name: the place the
  destructible spots system gives (`OnNotifyAboutFracture`). Whatever broke
  it: a bullet, a blast, a blade, a car.
- **How the others break it**, hurting nobody: the named component's own
  `TriggerDestruction` when it is not fractured yet; else a physics impulse
  of 1 m at the place (`physicalImpulseSphere`, the one a grenade pushes
  debris with: no attack, no hit). A break more than 200 m from our V is
  dropped (not in our world).
- **Echoes.** Replaying a break makes our game see one: a named part is
  told once per session whoever broke it; a nameless piece is not told
  within 1.5 m and 2 s of a replay, nor again within 0.75 m and 1.5 s of
  one of ours (a pane goes in several pieces). A game says 24 then 12 a
  second at most.
- **Who hears**: a piece new to the session's record, every player, its
  teller too (for its record; its game is not told again); another piece of
  a known one (a pane going in several), the players linked with the
  sender (150 m: the place is in their world).
- **The record, for who was not there** (`devices::FractureRecord`, on the
  host and on every guest as the host sent it): one entry per piece, a named
  part by entity and component, a nameless one by the metre cell it broke
  in; 2048 at most, then a new piece is neither kept nor relayed (counted in
  `FracturesDropped`), so a guest's record never holds one the host's lacks.
  A late joiner gets it whole at its join (world domain); it is in the world
  digest (count and an order-free sum), and a world resync sends it again.
  The session's only: it ends with the session. It comes with the welcome,
  before the scripts see the new session: their reset at a session's start
  forgets what broke here but keeps the queue of what the others said (the
  net thread empties it between sessions, `Runtime::ResetSession`).
- **A break not in our world yet** (more than 120 m from our V, or a named
  part whose entity is not streamed in within 60 m) waits in
  `TandemObjectSync` (512 at most, the oldest dropped) and is tried again
  every second: it breaks when our V comes, through the same replay
  (`TandemObjects.Replaying` notes it first, so its echo is not told). A
  live break between 120 and 200 m now waits too (it was replayed at
  once).
- **Threads**: the hooks (11 `OnPhysicalDestructionEvent` handlers, the
  spots system's notice) only queue (`TandemStreaming.QueuePoint`).
- Tests: `tests/devices_test.cpp` ("fractures: …", "glass: …"),
  `tests/peer_devices.sh`; the mock player's `--fracture
[ENTITY:COMPONENT:]X,Y,Z[@S]`; in game `tools/game.sh cmd glass`,
  `glass break` (the impulse at what V looks at), `glass part` (the
  trigger on the entity V looks at); scenarios `glass-guest`,
  `glass-latecomer` (the game joins a mock host that broke a pane where V
  stands and one 300 m away: the first at the join, the second when V gets
  there).

## Persistence: whose world it is afterwards

Device state lives in each game's own save. In a session the **host's world
is the world**: its save is the record that lasts, and what guests do in it
is saved there, by the host's game, as if the host had done it.

A guest's world is a **mirror for the length of the session** (as its
journal is, `Story.reds`): at its first look at each device the guest
remembers what its own save had; every device the session changed (the
host's word, or the guest's own V) is put back to that when the session
ends and the guest stays in its game (`TandemDevices.End`: quietly, loaded
or not; an exploded barrel is whole again the next time it streams in). A
guest never saves in a session, so a game killed mid-session loads its own
world untouched.

## Testing

- `task test`: `tests/devices_test.cpp` (the record: revisions, verdicts,
  bounds, digest check; over loopback: a door for everyone and a late
  joiner, a guest's request, two guests at once, a refused request, a
  device coming into a guest's world, convergence with a third of the
  messages lost, a guest vanishing with a request in flight, the host
  vanishing) and `tests/protocol_test.cpp` (round trips, sizes, unknown
  bits refused).
- `task test` also: `tests/breach_test.cpp` (the breach record; over
  loopback: a guest's breach applied by the host and heard by the other
  guest for its NPCs, a second breach refused, the host's own, a late
  joiner, the quickhack on a host NPC and its network, a lost minigame, a
  breacher vanishing mid-breach and right after; the holds: paid on a
  grant, never on a refusal, several networks, silence and the session's
  end; two breaches within a ping: one paid) and
  `tests/peer_breach.sh` (`tandem-peer --breach ENTITY[:CLASS][@S] |
npc=N[@S] --programs LIST [--breach-failed]`: two guests and a late
  joiner).
- Mock player: `tandem-peer … --device ENTITY:CLASS:WORDS[@SECONDS]`
  operates a device by id (a guest asks; a host says its own is that);
  `--device-answer WORDS --device-after N` changes back every device another
  player just changed. It logs every device it hears of. As a host it has
  no world: a guest's request is taken as said. `tests/peer_devices.sh`
  (part of `task test`) runs a host and two guests through it. A recorded
  host session (`--play-session`) replays the host's device words too.
- In game: `tools/game.sh cmd device` (counts), `device near|goto|tell
[KIND]`, `device open|close|lock|unlock|on|off|explode|distract|friendly|
hostile [KIND]`, `device floor N|other`, `device station N` — the nearest
  device of a kind (door lift camera turret barrel ap radio any; a barrel
  still whole) around V.
- Breach Protocol in game: `tools/game.sh cmd breach` (counts),
  `breach ap [PROGRAMS]`, `breach fail ap`, `breach npc [PROGRAMS]` (the
  nearest access point or proxy, its outcome faked as AccessPointSkip
  does); scenarios `breach-guest` (the mock guest's breach: the host's
  cameras off, its NPCs' ICE broken; the game's own second breach refused
  and paid nothing) and `breach-host` (the game as a guest breaches the
  host's network: held, granted, paid; the mock host's second breach is
  refused). The mock player logs `our breach pays (granted)` or
  `our breach pays nothing: player N was first`.
- Scenarios (`tools/scenario.sh NAME`, none run yet): `devices-door`,
  `devices-lift` (an elevator ride together), `devices-camera` (hacked by
  the guest), `devices-camera-friendly` (turned by the guest: no V seen,
  marked), `turret-friendly`, `devices-barrel` (shot by the guest), `devices-guest` (the
  game as the guest: its own copy first, the host's word, and its world put
  back on leaving), `devices-media` (a station V picks, then the guest's:
  `--device-answer station=2`). `DEVICES_AT="X Y Z"` names the spot; V's
  flat has a door, lifts and a radio, a camera and a barrel need one.
- **On two real games** (`two-instances.md`, "Scenarios on two games"; not
  yet run): `pair-devices-door`, `pair-devices-lift`, `pair-devices-camera`,
  `pair-devices-barrel`, `pair-devices-media`, `pair-glass`, `pair-turret`:
  each acts on one game and reads the other's log, then compares both
  games' parity dumps of that device (`compare devices ENTITY`).

## Open

- **Not run in game.** Every engine call in `Devices.reds` is read from the
  2.50 scripts and compiles; the list to verify is §13.30, "To verify in game", of the research file.
- **Glass and breakable scenery, not run in game**: the list is §13.90,
  "To verify in game". Unknown until then: whether every pane notifies
  (`systemsToNotifyFlags` is per node), whether the impulse alone
  shatters world glass, and whether it does at 120 m after a pane streamed
  in (a waited break). The session's record is ours: the engine's
  destruction persistency system has no script side, so a guest's own
  save's breaks and the host's from before the session are not shared.
- **A device's fire, not run in game**: the list is §13.90, "To verify in
  game". Known gap: a guest apart from the host with another guest near:
  each of their copies fires at its own V. A turret in a player's hands:
  its camera's aim point is 40 m out (a target nearer or farther is drawn
  along the same line), its drawn rate is not the weapon's own, and the
  copy's own vision cone and lights are not changed while it is held.
- **A camera or turret turned, a partner's hack marked: not run in game.**
  To verify: that `SetDeviceAttitude` on a copy turns its cone green and
  stops "intruder detected" for that game's V; that the per-agent attitude
  and `SetDetectionOverwrite` keep a puppet out of its senses; that the
  undo leaves it hostile again (its group, lights, preset); that the
  `Use` icon reads well in the hacker's colour on the minimap.
- **Detection by NPCs after a camera's alarm** uses the puppet's position
  at the moment of the alarm; the camera feeding the security system
  continuously for a guest is not done.
- **A guest acting on a device it never streamed in** (through a terminal
  across the map): its game cannot tell what the device was before, asks
  the host, and the host's answer wins: the action is undone when the
  host's device differed.
- **A never-visited device on the host** may have no state object there
  (to verify): then the session takes the guest's word until the host's
  copy appears.
- **Automatic doors** open for the local V only: each opening is a request
  like any other, so a busy doorway makes a few messages a second; the
  host's order still decides. No scenario: no fixture with one is known.
- **An exploded barrel set quietly** (a late joiner's record, a resync)
  while it is streamed in on our game: its state says exploded, its entity
  stays whole until it streams in again (no blast is played for a record:
  it would hurt our V).
- **Datamine rewards** go to who breached. Alternative if the players
  prefer it: breach state not shared, each jacks in their own copy.
- **Breach Protocol, not run in game**: the list is §13.60, "To verify in
  game" (5: the held outcome). A breach through a device that reaches two
  networks is paid whole when one of them is granted. The
  quickhack on an NPC another guest runs breaches that NPC only, not its
  network. A guest's NPCs of the network get the daemons once its game
  hears of the breach (a ping later than the breacher's own).
