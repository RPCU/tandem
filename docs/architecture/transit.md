# Scripted transit and quest vehicles

## Inside the plugin

- **Scripted transit and quest vehicles** (`scripts/tandem/Transit.reds`,
  engine-knowledge §13.27). Each game sends its V's ride as `Transit`
  (kind, flags, the metro's stations, the vehicle, the seat; reliable,
  kept by the host for late joiners):
  - _A ride its game runs_ (`Ride`: a Delamain cab, a car an NPC drives, an
    AV; `Scene` when a story scene runs it: the player state machine's
    vehicle state Scene, tier 3+, or the `VehicleScene` tag). The rule "the
    driver's game owns the car" becomes "the game that moves the vehicle
    owns it": a V in a vehicle its own world moves, driving or not (for
    every AV `IsDriver` is false), sends that vehicle's `VehicleState`, so
    the others spawn its copy and seat the puppet as for a car it drives.
    NPCs seated in it (Panam at the wheel, Judy beside the host) go as
    `NpcSeat` (host to guests, kept while seated): a guest seats the proxy
    in the copy, deletes a seated proxy only once it is out of its seat, and
    puts the controller back when it gets out. The host's own vehicle is
    never moved nor reserved by anyone else: a remote player's puppet sits
    in it only in a seat nobody holds (`TandemVehicles.FreeSlot`; a seat the
    scene's passenger holds is never taken), else in another free one, else
    it is hidden until one frees.
  - _The host's quest scene vehicle_ (`Scene` with `kTransitSeats`): the
    quest runs on the host, so the scene and its vehicle are the host's,
    and nothing a guest does moves, reserves or empties a seat of it. A
    guest's V on foot within 20 m of the copy, up and out of combat, is
    asked the game's way (a notice and two input hints, the keys co-op
    offers use): **Ride with NAME** (the phone key) or **Follow** (the
    reject key held); nothing chosen in 6 s while still near is riding;
    walking off (30 m) or getting into a car of its own is following.
    - _Ride, a free seat_ (a passenger or gunner seat nobody holds, never
      the driver's): seated in it, as the scene seats a companion.
    - _No free seat_ (two-seat cars: the host with Panam or Johnny
      beside it, an NPC driving the host; 2026-11-27, the user's rule: a
      seat or nothing): the guest is not asked to ride. Within 20 m of the
      copy it is told once "No seat for you in NAME's ride: follow it, and
      join them when it is over" (`Transit-Full`) and follows; a seat taken
      between the question and the answer follows the same way. Before,
      such a guest was **carried along**: hidden, held by restrictions,
      mounted to the copy's `trunk_body` slot (or placed in the cabin 5
      times a second), its puppet hidden everywhere (`Transit` `Along`, kind
      4, which no game sends any more; its notice `Transit-RidingAlong`
      gone). As in GTA Online and Far Cry, a full
      car cannot be entered; the restrictions a save made while carried
      kept are still cleared when a game loads.
    - _Follow_ (chosen, or no free seat): nothing is done to the guest's V. The host's marker sits
      on its car (Markers.reds); when the host's scene ride ends, the host
      offers everyone far from it to join (the regroup offer, "Join NAME",
      Travel.reds), so a guest left behind is brought to the destination
      if it says yes.
    - _The end, whatever ends it_: the host's ride leaves `Scene`, the copy
      goes, or the guest goes down (a downed V is never asked): out of its
      seat.
      A game that loads clears the restrictions a save could have kept. A
      guest that vanishes is the host's PlayerLeft as always.
    - _Rejected: an extra seat on the guest's copy only._ The host's game
      owns the car and its seats; the copy must look like the host's car
      (an extra body would sit where the host's world has nobody, or in
      Panam's lap), the host's game would still have to place the guest's
      puppet somewhere, and the scene could later seat someone where the
      guest sits.
  - _The NCART metro_ (`Metro`): V mounts an `ncartMetroObject` the
    `ue_metro` quest drives. **Deliberate exception to "one world, one
    owner": every game that rides runs its own train**, with its own
    passengers, and no train is ever copied or shared (no `Seat`, no
    `VehicleState`, the host lists no NPCs while it rides, a guest keeps
    its train's passengers). It is consistent because a metro ride is a
    scripted transit, not world state: the train follows a fixed line on
    a timetable, nothing on it outlives the ride, nobody else can act on
    it, and the only shared facts are where the rider is (its `PlayerState`,
    as always) and when it gets off; a shared train would need one game to
    run the other's quest. Elsewhere the rider's puppet sits in a train of
    that world when one is where the rider is (the entity the rider rides,
    if streamed there too, or the observer's own train, within 60/150 m,
    kept until 300 m), in the rider's seat if free; otherwise it is hidden
    (PuppetVisibility.reds, reason `metro`; `along` for a guest riding
    along, `noseat` for a puppet with no free seat), placed along the
    ride twice a second so the map marker follows, with a notice ("… is
    riding the NCART toward Japantown South"). When the ride ends it is
    shown again where the rider is, the exit station ("… got off the
    NCART at …").
  - _Skipping a ride_: the ride's own fast-forward is a tenfold speed-up of
    the world (`customFFTimeDilation`), not a teleport: kept, on the host
    too (it compresses a ride its owner runs alone; the clock moves on by
    what the ride takes either way), with what the network drives exempted
    as from a slow motion (Time.reds), and flagged (`kTransitSkipping`).
  - _Jumps_: a metro station's fast travel, or any other teleport of the
    rider, is the interpolation's teleport (Time and smoothing): the
    puppet is put there. A fast-forwarded train (≈ 250 m/s, 8 m per
    state) is motion, not a teleport.
  - _Follow-up: a guest gunner's fire._ Shots from a seat already travel as
    `Shot` and hits as `NpcHit` (the guest's own weapon). A vehicle's
    mounted weapons (the Basilisk's guns, a gunner seat's turret) belong to
    the vehicle's owner: the gunner's game would aim its copy's weapon,
    send `VehicleFire{vehicle owner, weapon slot, target point, projectiles}`
    to the owner (through the host), which fires its real weapon
    (`VehicleObject.NPCShoot` / the weapon's own trigger) so the damage is
    the host's world's; every other game plays the effect on its copy.
    Not done; NPCs driving vehicles that carry no player (chases) are the
    host's AI vehicles of the authority table, also not shared yet.
  - Not yet verified in game: scenarios `metro`, `ride`, `scene-ride`,
    `scene-ride-full` (a two-seat car: follow, not asked),
    `scene-ride-follow` (a free back seat, follow chosen);
    `tools/game.sh cmd ride` logs our ride, the vehicle's seats and
    everyone's rides; `ride along` / `ride follow` answer the question;
    `ride npc [RECORD]` seats an NPC in our vehicle (a host test of
    `NpcSeat`).
