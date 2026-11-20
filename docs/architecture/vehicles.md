# Vehicles and radio

## Inside the plugin

- **Vehicles** (`scripts/tandem/Vehicles.reds`, `core/src/vehicle.cpp`):
  where a V sits travels as `Seat` (vehicle record, seat, whose car;
  reliable, cached for late joiners), the car a V drives as `VehicleState`
  (30 Hz). Other games spawn a copy of the driver's car, teleport it every
  frame to the interpolated state (drawn with the driver's own delay, so the
  driver stays in the seat; 500 ms of extrapolation), and mount the
  driver's and passengers' puppets in its seats with the mounting facility
  (no AI). A player riding in someone else's car sits in that game's copy;
  a puppet riding in ours sits in our real car. The puppet's movement
  controller keeps still while seated and is re-attached when the player
  gets out. Copies are never deleted while the game runs (deleting one that
  had someone seated in it froze the game): a car someone got out of stays
  parked. Puppets leave in steps over several frames (out of the seat, then
  the body).
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
- **Radio** (`scripts/tandem/Radio.reds`, engine-knowledge §13.12): the
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
