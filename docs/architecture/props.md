# Physics props: one bin on both screens

A bin a V knocks over, a crate a car sends flying, a chair a blast throws:
before this, each game's physics pushed its own copy, and the bin knocked
over on one screen stood on the other (`parity.md` row 4). **Built and
tested without the game** (core, loopback tests, the mock player, script
compile checks); nothing here has run in Cyberpunk yet. Engine facts:
[`../research/engine/devices.md`](../research/engine/devices.md) §13.169.

## Which ones (decided 2026-11-28)

**A placed prop that something moved, near a V; nothing else.**

- **Placed props only**, named by their entity's static id (sector data:
  the same in every game) and `part`, the index of the moved body among the
  entity's physical meshes. Every game already has its own instance, so
  sharing one spawns nothing and deletes nothing.
- **Only moved ones.** A prop at rest stands where its sector puts it, the
  same in every game: only a moved one differs. Sharing every prop would
  cost a message per bin on every street for nothing, and its state would
  be its sector's.
- **Whatever moved it counts**: a V on foot, a car, a shot, a blast, an
  NPC. The first touch makes it differ, so the first touch starts the
  share (a touch the engine tells: `PhysicalCollisionEvent`, an impulse of
  1 or more).
- **Near a V**: within 100 m of the V of the game that runs it
  (`regions::kShare`, as NPCs).
- **Debris that has no name in every game is not shared**: a wrecked car's
  parts, severed limbs, a window's shards, a vending machine's can. Each
  is spawned by its game when the cause happens, so it has no shared id;
  sharing it would mean spawning a copy and deleting the game's own,
  which the rule forbids. Its cause is shared already (a car's
  destruction, a dismemberment, a break: `traffic.md`, `combat.md`,
  `devices.md`), so it falls at the same moment, in each game's own way.
  By design, not a degradation.
- **Props that are world nodes are not shared (known limit, decided
  2026-11-28).** About 9 in 10 of the bodies a V or a car knocks over in
  a street are no entity: instanced destructible meshes
  (`worldInstancedDestructibleMeshNode`, 1,238 movable bodies around V
  in Kabuki against about 150 entity bodies; `worldDynamicMeshNode`
  is rare), measured live, §13.171. They have no script surface and no
  RTTI function; their bodies are reached only through raw layouts not
  yet measured, so sharing them would cost raw-layout work against little
  that shows: each game moves its own copies from causes that are shared
  already (a car copy pushes them as it drives, `vehicles.md`; shots and
  blasts are shared), so they scatter on both screens and differ only in
  where each comes to rest (which meshes they are was not read: their
  paths are hashes). Whether a
  V's puppet on foot kicks them in the other game is not known
  (`players.md`, the collision capsule). The entity props above are
  shared.

## The rule

| State                                      | Owner                                                                                                                                   | Others                                                                                                      |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| a moving placed prop                       | the game that runs the area round it (`AreaOwner`); while one game speaks for it, no other (first word first, until 2 s after its last) | their own instance held to its word: kinematic, put where its states say, drawn 200 ms in the past          |
| where a moved prop came to rest            | the session's record (the host's), from its owner                                                                                       | their own instance put there, given back to its physics; one far from our V is put there when it streams in |
| a touch of a V on a prop another game runs | that V's game asks (`PropPush`)                                                                                                         | the owner gives its real prop the same speed, unless it moves already (its puppet of that V pushed it)      |

A game that does not run its area never simulates a competing copy: a
touch on its own instance by its own V (or its car) moves it at once, for
feel, and is a push to the owner, whose word then holds it; any touch the
owner does not answer within 2 s is put back where it stood (fallback
`prop put back`). Two linked games that both run their areas (neither V
deep in the other's region) may both move one prop between them: the
first word wins at the host, the second game hears it and holds its
instance to it from then.

## What travels

Appended, protocol 29 (`protocol.hpp`, `props.hpp`, `endpoint_props.cpp`).

| Message      | Channel                      | From → to                                                        | What                                                                                           |
| ------------ | ---------------------------- | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `PropStates` | State, 10 Hz while any moves | owner → the players linked with it, through the host             | up to 16 props of 33 bytes: entity, part, place, pose (6 bytes), velocity (cm/s)               |
| `PropRest`   | Event, kept                  | owner → everyone (the record)                                    | entity, part, place, pose                                                                      |
| `PropPush`   | Event, one-shot              | a V's game → the prop's speaker, else the owner of that V's area | entity, part, where our instance stood, the speed the push gave it, when (dropped after 0.5 s) |

The host stamps each with its sender, passes on a word only from the game
that speaks for the prop or may start to (it runs its area), and counts
what it refused (`Endpoint::PropsRefused`). The referee checks the speaker
stamps, values, the states' rate, never a game's own word back.

**Budget** (`tests/budget_test.cpp`, "a blast, 16 props fly": a full round
ten times a second, a rest twice a second, a push once a second): a full
round is under 600 bytes; a prop at rest costs nothing. The script follows
at most 32 of ours and holds at most 32 of others' (`TandemPropSync.MaxMoving`);
the budget picks the 16 nearest our V each round, those left out first in
the next (`props::Budget`).

**Frame cost**: a touch is one queued point (the hook reads and queues,
nothing else); each tick reads the body of each prop of ours that moves and
writes the body of each prop held, 64 at most; the record round our V is
checked once a second (32 entries at most).

## Consistency

| How it diverges                                               | What the player would see                   | Guard                                                                                                                                                                                     |
| ------------------------------------------------------------- | ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| two games move one prop within a ping                         | a bin pushed two ways                       | the host passes one speaker's word (props::Speakers); the other hears it, stops speaking (`SendPropStates` drops it) and holds its instance                                               |
| the speaker vanishes mid-motion                               | a bin frozen in the air on the other screen | no word for 2 s: our instance goes back to our physics; its next touch is ours if we run our area                                                                                         |
| a state lost (State channel)                                  | a jump                                      | drawn 200 ms in the past between states, carried on along its speed 250 ms, then held; the rest is reliable                                                                               |
| a rest lost (bug), a late joiner, a player far when it rested | a bin standing up on one screen             | every rest is kept by the host, sent to late joiners and in the world digest and its resync; each game puts a recorded prop there when it streams in (checked once a second within 120 m) |
| a repeat (a resync)                                           | —                                           | a rest equal to the record changes nothing; states are newest wins per prop                                                                                                               |
| our own touch on a prop another game runs, never answered     | our bin moved, theirs not                   | put back after 2 s (`prop put back`)                                                                                                                                                      |
| a push late (a stall)                                         | a kick seconds after it                     | dropped after 0.5 s at the host and at the owner (`consistency::DropStale`)                                                                                                               |
| the record full (1024 props)                                  | a bin moved later stands for a newcomer     | the host keeps no new one (a guest's could not follow); told (`prop not recorded`)                                                                                                        |
| no such body here (another mod, another entity)               | the owner's bin moves, ours not             | told (`prop not held`)                                                                                                                                                                    |

## Inside the plugin

- `core/include/tandem/props.hpp`: `Speakers` (who speaks for a moving
  prop), `Record` (the rests, its digest), `Track` (drawing in the past),
  `Budget`, packing. `core/src/endpoint_props.cpp`: the session.
- `plugin/src/objects_natives.cpp` (`TandemObjects.Prop*`): ours to the
  net thread by rounds, theirs into tracks and the record on the game
  thread, a body's velocity set by its RTTI setter
  (`SetBodyVelocity`; missing: state `prop velocity`, an impulse of a
  nominal 10 kg).
- `scripts/tandem/Props.reds` (`TandemPropSync`, ticked by
  `TandemObjectSync`): touches, ours followed until at rest (under 1 cm in
  0.5 s), theirs held and put at rest, pushes asked and given, the record
  checked; the hook `GameObject.OnPhysicalCollisionEvent` (added: the
  game's scripts handle it only on `ActivatedDeviceTrap`).
- Commands: `tools/game.sh cmd props` (counts), `props look`, `props push
[M/S]` (the prop V looks at, as our V's touch: ours, or a push to the
  owner), `props at HEX`; `test query prop HEX` (the body's place, how it
  stands, the entity's place).
- Mock player: `--prop E:X,Y,Z[@S]` (its game moves the prop: 2 m east in
  1.5 s, then at rest), `--push-prop E:X,Y,Z[@S]`; a push it hears moves
  the prop along it; the props it hears are logged.

## Scenarios to run in game

Each ends in the scenario runner's `PASS` or `FAILED` line; the verdict is
the bodies' places read on the game (`test query prop`) against the mock
player's word.

- **props-host**: the game hosts; V looks at a prop (`PROPS_AT`); the mock
  guest stands beside it; `props push`. Checks: the mock hears the prop
  move and its rest, and the rest it heard is where the game's body lies
  (within 0.25 m); the game is not held by anyone. Look for: the prop
  tumbling as alone.
- **props-guest**: the game joins a mock host at the prop; the mock moves
  it 2 m east (`--prop`). Checks: our instance held to the host's word,
  then at rest within 0.25 m of the host's rest, given back to our physics
  (not kinematic). Then `props push`: the mock hears our push, moves it,
  and our body lies at its new rest. Look for: no stutter while held, no
  hop at the rest.
- **props-latecomer**: the mock host moved the prop before we joined; our
  body is put at the record's rest within 0.25 m.

## Open

- World-node props (§13.171, a known limit above): what reaching them
  would take is a name (the instance's global node id and the body's
  index), the instances (a registry fed by the engine's node attach and
  detach, as RHT's world inspector does, MIT) and the per-instance bodies
  (the array at +0x150 of the node instance, its elements not yet known).
- Whether `PhysicalCollisionEvent` reaches every placed physics entity
  (§13.169, unverified): `props-host` tells (no touch: no share).
- Rotation speed is not sent: a tumbling prop is drawn between poses.
