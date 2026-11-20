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
own V; what it _is_ is the host's**. The game's sensing code is written
around the local player (it does not treat a puppet as one), a turret that
shot a guest through the network would hit late, and each game already has
the device, its cone and its gun. So your V is seen, shot and blown up by
your own game's copy, at once; the host forwards none of a device's hits on
a puppet (`Combat.reds`, `ForwardsToPlayer`); and the copies agree because
their state (on, off, friendly, destroyed, the alarm) is the host's.

## What is synced, and who owns it

| Device (state class)                                                              | What is synced                                                                                                                           | Owner                                                                                                                                                   |
| --------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Doors, gates, shutters (`DoorControllerPS`)                                       | open, locked, sealed; status                                                                                                             | host; a guest's own copy changes first                                                                                                                  |
| Elevators (`LiftControllerPS`)                                                    | the floor it is at or going to (every game's cabin leaves together); status                                                              | host. Who rides: each V is carried by its own game's cabin, puppets are placed by their player's stream (the "in a moving elevator" placement)          |
| Cameras, turrets (`SurveillanceCameraControllerPS`, `SecurityTurretControllerPS`) | on, off, unpowered, disabled (destroyed), friendly, hacked                                                                               | host. Seeing and shooting a V: that V's own game's copy                                                                                                 |
| The alarm (`SecuritySystemControllerPS`)                                          | safe, alerted, combat                                                                                                                    | host. Raised by whichever game's device saw its V (on the host: as that V's puppet, where it stands, so the NPCs go there); called off by the host only |
| Access points (`AccessPointControllerPS`), their networks                         | breached; each device's "quickhacks open" bit; what the breach's daemons did to each device (their own state)                            | host. Breached once for everyone; the datamine's money and shards go to who breached (rolled by that game, as corpses and containers are)               |
| Computers, terminals (`ComputerControllerPS`, `TerminalControllerPS`)             | status, hacked; what they did to the devices they command (those devices' own state)                                                     | host. Reading mails and files: each player in their own game; a quest fact a read sets follows the story rules                                          |
| Barrels, mines, traps (`ExplosiveDeviceControllerPS` and kin)                     | exploded (played as an explosion where it is in sight, a record elsewhere); status                                                       | host. The blast hurts each game's own V from its own copy, and NPCs in the host's game only                                                             |
| Radios, TVs (`MediaDeviceControllerPS`)                                           | on, off; the station or channel                                                                                                          | host                                                                                                                                                    |
| Lights, fuse boxes, generators, fans, vending machines, any other device          | status (on, off, unpowered, disabled), broken, hacked, distracting (a glitch: on the host a real distraction, so its NPCs come and look) | host. A vending machine's sale: the buyer's own game (money and item are that V's)                                                                      |
| Glass, covers, breakable scenery                                                  | **nothing yet**: physics destruction, with no script state                                                                               | each game (open: see the end)                                                                                                                           |
| Loot containers                                                                   | nothing (per player: `loot.md`)                                                                                                          | each player's game                                                                                                                                      |
| Spawned devices (dynamic ids), vehicles                                           | nothing here (not the same entity in two games; vehicles have their own sync)                                                            | —                                                                                                                                                       |

A device is **named** by its entity's static id (64 bits, from the world
file: the same in every game) and the class name of its state; its
`PersistentID` component is always `controller`.

## Messages (appended; no protocol version change)

| Message        | Direction                    | What                                                                                                                                                                                                                       |
| -------------- | ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DeviceState`  | host → guests                | the session's word on a device: status, state bits, one value, the host's revision of it, who changed it last, and `how` (live: it just happened, play it; quiet: a record, set it). Kept for late joiners. About 30 bytes |
| `DeviceState`  | guest → host (`kDeviceAsk`)  | "this came into my world like this": the host answers with its own when they differ                                                                                                                                        |
| `DeviceUse`    | guest → host                 | "my V made it this" with the revision the guest had seen. One-shot                                                                                                                                                         |
| `DeviceDigest` | host → guests                | count and hash of the record, with every `Digest`                                                                                                                                                                          |
| `DeviceSet`    | both                         | guest: send me the record again; host: here it comes, forget yours                                                                                                                                                         |
| `Breach`       | any game → host → the others | a breach's outcome: the access point (or the NPC breached through the quickhack), the daemons and traps won, who breached; the host passes a granted one on (`kBreachApply`). One-shot ("Breach Protocol", below)          |
| `BreachResult` | host → the breacher          | granted, or refused: breached first by whom                                                                                                                                                                                |

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
  breacher sees "NAME breached this network first"; its rewards were
  already rolled (a race within a ping pays both: open).
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
  breacher vanishing mid-breach and right after) and
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
  device of a kind (door lift camera turret barrel ap radio any) around V.
- Breach Protocol in game: `tools/game.sh cmd breach` (counts),
  `breach ap [PROGRAMS]`, `breach fail ap`, `breach npc [PROGRAMS]` (the
  nearest access point or proxy, its outcome faked as AccessPointSkip
  does); scenarios `breach-guest` (the mock guest's breach: the host's
  cameras off, its NPCs' ICE broken) and `breach-host` (the game as a
  guest breaches the host's network; the mock host's second breach is
  refused).
- Scenarios (`tools/scenario.sh NAME`, none run yet): `devices-door`,
  `devices-lift` (an elevator ride together), `devices-camera` (hacked by
  the guest), `devices-barrel` (shot by the guest), `devices-guest` (the
  game as the guest: its own copy first, the host's word, and its world put
  back on leaving). `DEVICES_AT="X Y Z"` names the spot; V's flat has a
  door and lifts, a camera and a barrel need one.

## Open

- **Not run in game.** Every engine call in `Devices.reds` is read from the
  2.50 scripts and compiles; the list to verify is §13.30, "To verify in game", of the research file.
- **Glass and breakable scenery** have no script state. A remote shot is
  drawn but does not break the other game's glass. Lever to try: replaying
  the hit as a real physics hit at the glass, or the destruction
  persistency system natively.
- **A device's shots are not drawn for the others.** The host's turret aims
  at what it sees there; a guest sees its own copy fire at its own V. Two
  players watching one turret can see it aim at different people.
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
  host's order still decides.
- **Datamine rewards** go to who breached. Alternative if the players
  prefer it: breach state not shared, each jacks in their own copy.
- **Breach Protocol, not run in game**: the list is §13.60, "To verify in
  game". Two breaches of one network within a ping are both paid (each
  game rolls its rewards at once; the host grants the first). The
  quickhack on an NPC another guest runs breaches that NPC only, not its
  network. A guest's NPCs of the network get the daemons once its game
  hears of the breach (a ping later than the breacher's own).
