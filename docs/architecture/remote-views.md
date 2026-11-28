# Remote views

The user's words: "about slow time effect etc, you also need to take into
account when user 'camera' move to a hacked vehicle or camera". A **remote
view** is any time a player's camera leaves its V's body for something it
controls; its body stays where it stood. Engine facts:
[`../research/engine/devices.md`](../research/engine/devices.md) §13.182.
Code: `scripts/tandem/RemoteView.reds`, its uses below, core
`activity::ViewOf`, `regions::Apart`, `Endpoint::ArbitrateTraffic`.

**State: built and tested without the game** (core, loopback tests, the
referee, `task scripts:check`). Nothing here has run in game: the
scenarios at the end are the checks.

## The remote views of 2.50

<!-- prettier-ignore -->
| View | How the game holds it | Read by tandem from |
| --- | --- | --- |
| a security camera, and jumping along its chain | `TakeOverControlSystem` (the quickhack, a computer's camera list, a quest) | `GetControlledObject()` is a `SurveillanceCamera` (sort 1) |
| a turret | the same (the quickhack, its control panel) | a `SecurityTurret` (sort 2) |
| a sniper nest (Phantom Liberty), another device | the same | any other device (sort 4) |
| a car or a bike driven remotely | the take control quickhack (`TakeControlVehicleClassHack`), a quest node | the state machine's `EntityIDVehicleRemoteControlled`, the car `IsVehicleRemoteControlled` (sort 3) |

Not remote views: Flathead has no player view in 2.50; photo mode is an
activity, braindances and Johnny are other worlds
([`elsewhere.md`](elsewhere.md)); a quest's camera animation is the
story's ([`story.md`](story.md)).

## What each game does

Our V's remote view is read four times a second on the game thread
(`TandemRemoteView.Sample`, from `Activities.reds`) and said as our
**Activity** (`ActivityKind::Remote`, protocol 27): its sort in `entry`,
the object's `EntityID` hash in `device`, where the view is in `anchor`
(said again each 8 m a car drives), the shared vehicle it drives in
`traffic`. The host stamps, keeps it for late joiners and relays it, as
any activity (players domain of the digests). It holds no body: the V
stands where its `PlayerState` says, in the game's own pose (the takeover
shows its third-person body).

- **Time** ([`time.md`](time.md)): a V's time bubble (the scanner's
  through a camera, a Sandevistan's) is centred on its remote view while
  it has one: the NPCs around what it sees and acts on slow, for everyone;
  those around its standing body do not. A V in a remote view is never
  slowed by another's bubble (the camera, turret or car it acts through
  has no time of its own). Logged: `time: bubble of player P: N NPC(s),
centred on its remote view at X Y Z`.
- **Links** ([`regions.md`](regions.md) §2): the host links a player where
  it looks from too (`regions::Apart`, the nearest of its V's and its
  view's places, the same 150/190 m): a V looking through a camera beside
  a partner far from its body sees the partner's NPCs there. A view more
  than 200 m (`regions::kViewReach`) from its V counts for nothing.
  Ownership is unchanged: an NPC is its owner's; a quickhack cast from a
  camera on another game's NPC goes to that owner as any
  ([`combat.md`](combat.md)).
- **A shared car driven remotely** ([`traffic.md`](traffic.md)): its
  driver's game runs it. The host hands it to that game at once
  (`TrafficHandover`, no distance rule) and to nobody else while its view
  names it; the driver's game stops moving its proxy to the owner's states
  the frame its V takes the car, keeps it where it drives it when it
  becomes its own (no teleport, no traffic drive on), and says it driven
  (a remote-controlled car with no driver is not parked: its states
  stream). Every other game's copy follows it. Out of the car, it is a
  car like any other.
- **Refused** (the takeover's hook, `Hacks.reds`): a car another V drives
  at its wheel, one another V drives remotely, a partner's own car
  (`TandemRemoteView.TakenBy`). The takeover does nothing and our V reads
  "NAME is driving this car" (`Remote-CarBusy`) or "NAME's car: only they
  can take control of it" (`Remote-CarTheirs`).
- **A held camera or turret** ([`devices.md`](devices.md), "A turret in a
  player's hands"): where it points travels (`TurretHands`), every copy
  turns there; one player's hands at a time (first come, the host's), the
  second let go with "NAME is looking through this camera"
  (`Turrets-BusyCamera`) or the turret's words.
- **The partner's HUD** ([`presence.md`](presence.md)): its party entry
  says "In a camera", "At a turret", "Driving remotely" or "Remote control"
  (`presence::Status::Remote`, after down, out, elsewhere, a scene and a
  menu); its nameplate stays over its standing body.
- **The watch camera** ([`story.md`](story.md), "Spectating"): the
  takeover's own `NoCameraControl` on the host's V is its remote view, not
  the story's body mode, so a host in a camera is no scripted moment and
  its camera is not streamed; a quest that holds the takeover
  (`IsInputLockedFromQuest`) is, as before.

## Registered when lesser ([`degradations.md`](degradations.md))

- `remote car unseen`: our V drives a player's car remotely (its garage's,
  one it took): never a shared world vehicle, the others see it where it
  was left.
- `held device unshared`: a camera or a turret with a dynamic id, or
  another device (a sniper nest): its copies do not turn with our V's
  hands.

## Tests

- Core: `tests/activity_test.cpp` (what a remote view may say),
  `tests/regions_test.cpp` (links through a view),
  `tests/traffic_regions_test.cpp` (a car driven remotely is its driver's
  game's and stays so), `tests/presence_test.cpp` (the status),
  `tests/protocol_test.cpp`, `tests/referee_test.cpp` (`value.range`:
  a remote view as its game leaves it).
- The mock player: `--activity remote` (a camera's view 20 m off its V).
- In game: `tools/game.sh cmd remote` (ours and the others'), `remote
camera`, `remote turret`, `remote car`, `remote push SPEED`, `remote
off`; queries `remote [player N]`, `traffic ID position`.
- Scenarios on two games (`TANDEM_PARTNER`), in this order:
  `pair-remote-camera` (a camera's takeover while scanning),
  `pair-remote-car` (a car driven remotely), `pair-remote-turret` (a
  turret's takeover).

## Open

- Measured in no game yet: the remote range (`player.vehicleQuickHacks.
maxRange`), whether `focusMode` slows time through a camera, the
  partner's copy of a held camera turning (`CreateLookAt` on a
  `SurveillanceCamera`), V's pose stream while its third-person body
  shows.
- A player's car driven remotely is not seen moving (registered above):
  its copies follow its driver's `Seat`, and no V sits in it.
