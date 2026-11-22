# Vehicles and radio

## Inside the plugin

- **Vehicles** (`scripts/tandem/Vehicles.reds`, `core/src/vehicle.cpp`):
  where a V sits travels as `Seat` (vehicle record, seat, whose car;
  reliable, cached for late joiners), the car a V drives as `VehicleState`
  (30 Hz). Other games spawn a copy of the driver's car, move it every
  frame to the interpolated state (drawn with the driver's own delay, so the
  driver stays in the seat; 500 ms of extrapolation), and mount the
  driver's and passengers' puppets in its seats with the mounting facility
  (no AI). A player riding in someone else's car sits in that game's copy;
  a puppet riding in ours sits in our real car. The puppet's movement
  controller keeps still while seated and is re-attached when the player
  gets out (how the copy is moved: "How a copy is moved" below). Copies
  are never deleted while the game runs (deleting one that
  had someone seated in it froze the game): a car someone got out of stays
  parked (what crashed, and a guarded deletion to prove in game:
  engine-knowledge §13.101). Puppets leave in steps over several frames (out of the seat, then
  the body). A V downed in a seat stays in it, is revived from beside the
  car, and an out V may respawn into a partner's free seat
  ([`revive.md`](revive.md)).
- **How a copy is moved** (`TandemSystem.MoveBody`, `scripts/tandem/System.reds`;
  `TandemWheels.Move`, `plugin/src/engine/vehiclemove.cpp`; engine facts
  §13.141; written 2026-11-22 from the decompiled function, **nothing of it
  run in game**). Every vehicle another game runs (a player's car copy, a
  lent or rolling car, a proxy of its traffic) goes through one function,
  every frame:
  - **The engine's kinematic vehicle move** is the default: the body is
    given the transform its owner says and the linear and angular velocity
    that take it there in the frame's world time (clamped to 5..200 ms).
    It is the engine's own call for a vehicle driven kinematically (five
    callers in the exe), not a teleport: our V in the car is not teleported
    with it, so the population system keeps spawning (a teleport of our V's
    mount every frame stopped every spawn: no traffic, no pedestrians from
    the seat), and the body has a real velocity (what the game reads for
    the wheels, the engine's sound and a hit on a V; whether each follows
    is to see and hear). All classes (cars, bikes, AVs: one function of
    `vehicleBaseObject`); no class is known to need the teleport, a body
    that does not follow falls back by itself (below).
  - **A teleport** only for a discontinuity: the first placement of a body
    (a spawn, a reused proxy), a body more than 8 m from where its owner
    says (`TandemSystem.SnapDistance`: a resync after a loading screen or
    a pause, its owner's own jump; at 60 m/s a frame is 2 m), and a body
    that does not follow the move. Never per frame, and on the car our own
    V is mounted to only every other frame while a body of ours waits to
    spawn (`RideTeleportAllowed`; `TandemEntities.Create` keeps the list).
  - **Error against the owner** cannot accumulate: the target is the
    owner's interpolated transform itself, fed whole every frame (not a
    velocity integrated here), so a frame's error is at most what that
    frame left. `MoveBody` measures it (the body against where the last
    move sent it) and the report gives the largest of the last 10 to 20 s.
  - **Our world's obstacles** do not stop or deflect a copy: each call
    writes the body's transform, whatever it touched the frame before. It
    ends where its owner says. What it pushes on the way (our own traffic,
    props, a V on foot) is pushed by a body that now has its speed: the
    game's own hit on a V (a knockdown, thrown aside), where a teleported
    body had none and carried a V on its hood. Decided so: the owner's car
    is the truth, our V is hit as by any car and never moved with it. To
    check in game.
  - **At rest**: the same transform twice and the body within 3 mm of it:
    left alone (no call: zero velocity was the last one given), until the
    owner's transform changes or the body is 5 cm off (pushed, sagging).
    Nothing said of where it is (a stream that stopped): it is settled
    where it was last sent (`SettleBody`), never left rolling. A wreck and
    a parked car are at rest.
  - **A pause** (no world time passed): nothing is moved; over 8 m off
    afterwards, a teleport. **Slow motion** of our own: the step is the
    world's time, as the physics' own.
  - **A body that does not follow** (asked 5 cm or more and not a fifth of
    the way there a frame later): its physics are woken at the third frame
    (`PhysicsWakeUp`), and at the fifteenth it is teleported for good, with
    a log line (the first five). Five such bodies and none that followed:
    the whole path goes back to teleports, with a line.
  - **The fallback**: `kinematic_ride = off` (`tandem.ini`), or a
    `vehicleBaseObject` that is not the 0xBC0 bytes measured on 2.50 (the
    plugin logs it once): every body is teleported every frame as before,
    the car our V rides in every other frame while a body spawns
    (measured: bodies then come within 0.1 s; its motion is halved for
    those frames).
  - **In the log**: once a run, `vehicles: the vehicles other games run
    are moved by the engine's kinematic move …` or `… are teleported every
    frame (kinematic_ride = off | the vehicle layout check failed)`;
    `tools/game.sh cmd vehicle moves` (and the end of `street count`):
    `moves: path kinematic, N kinematic, N teleports (N first placements,
    N over 8 m, N of bodies that do not follow, N with the path off), N
    body(ies) that do not follow, N refused, N frame(s) our seat's car was
    not teleported, largest error E m in the last 10 to 20 s`. Scenario
    `ride-traffic`.
- **A partner's car** ([`garage.md`](garage.md), not run in game yet): a
  parked copy remembers whose car it was; a V that takes the wheel of a
  copy is checked against its owner's word (`CarLend`, `tandem.ini`
  `lend`) and, allowed, its game drives the copy and sends its
  `VehicleState`, its `Seat` naming the owner. The other games move their
  copy of that car to the driver's state (`FollowLentCars`), the owner's
  game its real car (the last own car its V drove), and stop following
  the owner's state for it. An owner riding in its own car with nobody at
  the wheel sends its car's state, so a car a partner parked stays where
  it stopped.
- **Radio** (`scripts/tandem/Radio.reds`, engine-knowledge §13.43): the
  driver owns the car's radio. While our V drives its own car, our game
  reads the car's receiver four times a second (on, station as an
  `ERadioStationList` index, song name) and sends `VehicleRadio`
  (reliable, kept by the host for late joiners) when it changes and when
  a remote player gets in; a song that changes on a station we have been
  on for a while is stamped with its start on the host clock. A passenger
  in a remote player's car (our copy of it) sets the copy to the driver's
  radio the game's way (a `VehicleRadioEvent` on the car, as the radio
  popup does), checks a second later and falls back to the receiver
  natives (twice at most), and asks its own copy of the station for the
  driver's song when another plays
  (`AudioSystem.RequestSongOnRadioStation`). The game exposes no playback
  position, so the target is the same station and, best effort, the same
  song: it lines up when the driver's song has just begun, otherwise it
  starts from the top. While riding, the passenger's pocket radio is held
  off with the game's own `VehicleBlockPocketRadio` restriction; getting
  out lifts it and the pocket radio comes back with its station after
  vanilla's short unlock delay. No remote player's radio plays anywhere
  else: a puppet is an NPC (no pocket radio) and a car copy nobody of
  ours rides in is switched off. Volumes stay each player's own
  settings. Not yet verified in game (scenario `radio`). A station a mod
  added (radioExt numbers its own after the game's,
  `RadioStationDataProvider.GetStationsCount`) is sent as off by the driver
  and taken as off by a passenger, with a notice (`TandemRadio.IsVanilla`,
  [`mod-compat.md`](mod-compat.md)).
- **Paint** (`scripts/tandem/Paint.reds`, engine-knowledge §13.150; not run
  in game yet): a car's custom paint (CrystalCoat / TwinTone: two colours
  and a lights' hue, or a unique pattern scanned from another vehicle) is
  its owner's game's to say. Four times a second our game reads the paint
  its own car shows (the car our V sits in, or last sat in, that is no
  partner's copy: `VehicleComponentPS`' template while it is active and no
  damage blocks it) and says it when it changes: `CarPaint` (appended,
  protocol 15; the car's record, flags, two RGB colours, the hue, or the
  unique template's record and model; flags 0: stock). It travels as
  `CarLend` does (`Endpoint::SendCombat`): the host stamps who, tells every
  other game and keeps each painted car's for late joiners and resyncs; a
  repeat is applied nowhere; stock paint is kept nowhere; it goes with its
  player. Every other game paints its copy of that car (the copy the
  owner's puppet sits in, by the record its `Seat` names; it stays that
  car's when parked) when the paint changes and when the copy spawns:
  the template into the copy's own state, then
  `VehicleComponent.ApplyVehicleVisualCustomizationTemplate` and the
  lights' own event, never the game's request events (they refuse on a game
  that has not unlocked CrystalCoat, start a cooldown on our V and store
  the template in our garage). A first paint on a copy is instant; a
  change takes the old one off and puts the next on 0.6 s later with the
  game's transition, as the game does. Our own game takes a paint off a
  copy our V rode in and walked away from (5.7 m), or one that lost a part
  here, and writes our garage's template into a copy our V mounts: every
  2 s the copy's state is compared with its owner's word and put back
  (`paint: … put back`, debug). Limits: the paint follows the owner, so a
  partner at the wheel of a lent car cannot repaint it for the others; a
  unique template a game's TweakDB lacks (a mod's) leaves stock paint
  there, said in the log; a stand-in copy of a modded car
  ([`mod-compat.md`](mod-compat.md)) is not painted; the game switches
  CrystalCoat off when its owner walks away from the car, so a car left
  parked turns stock everywhere, as in its owner's game. Mock player:
  `--paint RRGGBB[,RRGGBB][@HUE]`, `--paint-off-at S`; scenario `paint`;
  `test query paint [puppet]`; `cmd paint` (what we say and show),
  `cmd paint car R,G,B [R,G,B] | off` (a test's paint on our own car: it
  is written into the car's saved state).
- **A car our game lacks** ([`mod-compat.md`](mod-compat.md), not run in
  game yet): `Seat::hint` carries the vehicle's type and seats
  (`TandemMods.VehicleHint`); a record our TweakDB does not have is
  resolved once in `RemoteSeatVehicle` to a vanilla one of the same type
  and size (a bike, an AV, a two-seater, a car, a van), so the copy spawns
  and the ride works; a shared world vehicle (`TrafficSpawn`, no hint) is
  the default car. The player is told once per partner.

## Open problems

- **Radio: each V's own on foot, the driver's in a car.** On foot every
  player listens to their own pocket radio, which nobody else hears. In a
  car everyone aboard hears the car's radio, owned by the driver: their
  station, volume and track position, in sync, changing when the driver
  changes station; a passenger's own radio goes quiet while they ride and
  comes back as it was on getting out. To sync: the driver's station and
  the track's start time (host clock). Done (2026-11-18, to verify in game
  with scenario `radio`; see Inside the plugin): the driver's on/off,
  station and song reach the passengers. Not possible with the game's scripts: the same point of
  a song (no playback position to read or seek; the song is asked for by
  name) and the driver's volume (a per-player setting, left alone).
- **Getting in and out of a car, every way.** (The hard exits and the car
  rolling on with nobody at the wheel: `vehicle-combat.md`.) Today a puppet is mounted
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

- **Combat in and around vehicles.** Built (2026-11-19, not yet run in
  game): [`vehicle-combat.md`](vehicle-combat.md), with the hard ways out
  (bail, thrown, knocked off a bike, dead, blown out) and the car rolling
  on. The problem as it was stated: everything that happens in a car must
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
