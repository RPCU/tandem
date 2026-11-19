# Testing without a second player

<!-- prettier-ignore -->
| Level | What | Command |
| --- | --- | --- |
| unit | protocol round trips, strict decoding, sizes | `task test` |
| loopback | real host + guests over UDP in one process: join, relay, clock, leave | `task test` |
| bad link | smoothing and reliable channels through a shaped link (loss, jitter) | `task test` |
| relay | protocol, codes, limits, amplification, real sockets, race detector | `task relay:test` |
| via relay | a real relay process: join by code, fallback from direct, mock players | `task test` (`relay-loopback`) |
| mock player | `tandem-peer` as host or guest against the real game | `task peer -- host --mode mirror` |
| consistency | digests, resyncs, epochs, dedupe; convergence with a third lost; vanishing | `task test` |
| natives | each `*Natives.reds` matches its `*natives.cpp` (plain, story, combat, sync, elsewhere) | `task natives:check` |
| scripts | `scripts/tandem` compiles with the game's redscript, bundle and mods | `task scripts:check` |
| scenarios | the real game against the mock player, checked on the game's log | `task scenario -- all` |
| in game | deploy the DLL, read its log | `task deploy`, `task logs` |

**Scenarios** (`tools/scenario.sh`, `tools/scenarios/*.sh`) are the
in-game regression suite: each one puts the game in a role, drives the
mock player and expects log lines within a time limit. Restarts happen
only when the build or the role changed (one restart per role for a whole
run), so a run takes minutes. A failure keeps its evidence (logs,
a screenshot, every thread's backtrace when the plugin's watchdog saw the
game thread stop). Each feature verified in game gets a scenario.
Every scenario starts from one known state, `tools/game.sh test-reset`
(`cmd test reset`, `scripts/tandem/TestReset.reds`), run after the last
scenario's mock player is stopped and after any fresh start: the save
loads as the player left it and each scenario leaves state behind. It
takes V out of any vehicle (an instant unmount, then the workspot exit) or
workspot; ends the test private scene and quest freeze, test worlds
(Elsewhere), the test dilation and `god on`; resets revive (`revive
fresh`: up, no cooldown); heals V and removes bleeding, burning,
crippling and other lasting effects; asks the police to stand down;
deletes the tests' NPCs (`Tandem.Test`, `npc hostile`, `proxy test`,
`ride npc`) and the car copies nobody drives (parked, or an earlier
player's; once nobody sits in them); then teleports V to the test field
(-915.0 1879.5 36.19) and waits until it has stood still there for 1.5 s
(3 s at least: the world streams in). It logs `test reset: ready (on
foot, at the test field, …)` listing what it could not clear (a scene,
combat, a grapple, a body carried, police heat, a menu or the phone open,
time dilated, health), or `test reset: failed (…)`, which fails the
scenario. A scenario that needs another spot teleports after it.

`tandem-peer` modes:

- **mirror** — copies your V back as a second player, offset and optionally
  delayed: your own movements on a puppet, through the whole network path.
  The main tool for puppet, animation and vehicle sync.
- **ghost** — circles around your V at walking speed.
- **front** — stands 3 m in front of your V, facing it, and echoes your
  look, held weapon, shots, pose (with `--pose FILE`, a recorded pose).
- **drive** — drives a car (`--vehicle RECORD`) in circles around your V:
  tests the copy of a remote player's car, its seated driver and its motion
  without anyone driving. `--radio STATION` (0–13, a station name, or
  `off`) plays that station on its radio: sent at the wheel and again when
  your V gets in (`tools/game.sh cmd vehicle ride`).
- **drive --seat SLOT** — the same car, but the mock player rides in SLOT
  while "an AI" drives it (a `Ride`); `--scene` makes it a story scene's
  (as host: guests near it get a free seat); `--driver-npc RECORD` (host)
  puts an NPC at the wheel (`NpcSeat`). Scenarios `ride`, `scene-ride`;
  with a two-seat car (`--vehicle Vehicle.v_sport2_porsche_911turbo`, the
  NPC driving, the mock player beside it) `scene-ride-full` and
  `scene-ride-follow`. As a guest, `--ride-along` answers a scene ride
  with seats by riding along (`Transit` `Along`, carried with the car,
  out beside it at the end): the host's side of a ride along.
- **metro** — stands beside your V, then rides the NCART (`--metro-at`,
  `--metro-for`): a straight line 8 m up at 20 m/s, skipping (ten times
  faster) for 3 s halfway, then off at the end. Scenario `metro`.
- **course** — runs a scripted course ahead of your V with every way a V
  moves, each with the state machine's own values: jumps, a double jump, a
  1.5 m ledge climb, a drop, a 4 m ladder, swimming and diving, an
  elevator, a sprint, slide and dodge, then back to the start (a teleport).
  Heights are relative to where your V stood, so the puppet floats where
  the ground differs: it tests placement, not looks.
- **path** — `--save-path FILE` (any mode) saves the states it hears, your
  V's moves; `--mode path --path FILE [--offset X,Y,Z]` replays them in a
  loop: your own jumps, ladders and swims on the puppet, over the same
  ground.
- **idle** — connects and reports.
- **watch** — follows the other V 30 m under it (or `--offset`): the
  unseen guest of `--record-session` (`recordings.md`).
- **play** — `--play FILE` replays a real V's play (a gameplay recording,
  `recordings.md`) as the mock player's own V, at its recorded positions: the
  puppet walks the real ground, drives real roads, fights where it
  fought. It overrides `--mode`'s motion (and `--body`: the recording's);
  a missing FILE leaves the mode to run. `--play-loop` starts it again at
  its end, `--play-offset X,Y,Z` moves it. `tandem-peer play-info FILE`
  says what a recording holds and where it starts.
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
- `--drop P`, `--dup P` (any mode): a share P of what the peer sends never
  leaves, reliable messages too (as a bug would lose them), and a share of
  its state goes twice: the game must converge anyway (Consistency;
  scenarios `desync-npcs`, `desync-rejoin`). Resyncs and epochs are logged.
- `--vanish-at N` (any mode): N s after another player is in the session
  the peer ends with no goodbye, as a killed game does (scenarios
  `vanish-down`, `vanish-drive`, `vanish-story`).
- `--pause-at N [--pause-for M]` (any mode) opens "its pause menu" N s after
  another player is in the session, for M s (default 3): every game pauses.
- **Fights**: `join --hit-npcs N` hits one of the host's NPCs every N s
  as a strike, `--strike KIND` its outcome (`plain`, `nonlethal`, `cap`,
  `poise`, `head`, `blade`); as the host of a replay it applies strikes by
  the game's rules (`StrikeLoss`) and answers with health, poise,
  knock-outs, wounds and deaths; `host --hit-you N` has one of its NPCs (a
  replayed one if any) hit every guest's V for 10 physical every N s; a
  guest logs the NPC hits it gets. `host --difficulty D` sets the
  session's difficulty; `--area-hit N` (either) throws "a grenade" (20
  physical, `AreaHit`) at every other V every N s.
- **Down and out** (any mode): `--go-down N` puts its V down N s after
  another player is there (it lies still until someone revives it);
  `--go-out N` puts it out instead, up again 10 s later; `--revive N`
  revives another V that goes down N s later (its V walks beside it and
  holds for 10 s). The lives, revives and losses it hears are logged.
- **Stealth** (any mode): as host, `--npc RECORD` stands an NPC 6 m in
  front of the guest and `--detect-at N [--detect-for M]` makes the NPCs
  it sends notice the guests (meter rising over 2 s), detect them for M s
  (default 10), then lose them; `--camo N` turns "its optical camo" on for
  5 s; as guest, `--shoot N` fires a gunshot (shot and noise) every N s.
  Stealth, noises and the host's awareness of it are logged.
- **Takedowns**: `join --grab-npcs N` grabs one of the host's NPCs every N
  s (standing by it) and takes it down two seconds later, lethal and
  non-lethal in turn; `join --carry-npcs N` picks up a body, carries it
  for three seconds and throws it; as host it answers the game's requests
  (refusing dead or held NPCs) and `host --hold-npcs N` shows its own V
  grabbing and taking down a replayed NPC, with a combat cue for the game.
- **Fast travel together** (any mode): `--travel-at N` "fast travels" N s
  after another player is in the session: it stands at `--travel-to X,Y,Z`
  (default the game's `teleport home` spot) until another V comes within
  10 m, and offers everyone to join (`--travel-point RECORD`: the point
  arrived at); `--offer-regroup N` asks the others to come to it; offers it
  gets are answered by `--regroup-answer yes|no|ignore` (yes: it goes
  there).
- **Comms** (any mode, `comms.md`): `--link auto|ask|off` (its consent;
  ask: every request accepted), `--link-at N` (asks everyone N s in),
  `--ping N` (every N s: a spot before the other V, the nearest shared NPC
  as an enemy and at once again as a danger, then taken back), `--tag N`
  (scan tag on and off of the nearest shared NPC), `--reveal N` (up to 4
  shared NPCs for 10 s, as the Ping quickhack), `--waypoint X,Y,Z` and
  `--waypoint-clear N`. It logs every mark, tag, link and waypoint heard.
- **A bad network** (any mode): `--latency MS`, `--jitter MS`,
  `--loss PERCENT`, `--reorder PERCENT`, each way. A UDP relay with a link
  model on each direction (`net::Shaper`, `core/src/shaping.cpp`) sits
  between the mock player's endpoint and the others (host: on the port the
  game joins; guest: a local port its endpoint joins), so ENet itself
  (acknowledgements, resends, pings) crosses the bad link. Jitter keeps
  the order; reordering holds a datagram back 30 ms plus the jitter.
  Tests (`tests/shaping_test.cpp`): at 80 ms each way, 60 ms of jitter,
  10% lost, 5% reordered, a walking body is drawn 174 ms behind, never
  backwards, at most 0.06 m a frame (0.05 m is walking), 8 frames of 510
  extrapolated; a car at 20 m/s stays within 0.02 m of its circle; a
  pose's bone within 0.0001 rad; over real UDP at 40 ms, 15% lost, 200
  reliable changes each way all arrive, in order, once. Scenarios
  `front-badlink`, `drive-badlink`, `npcs-guest-badlink`.
- Loot (any mode): `--loot-quest RECORD` has "its V" loot a quest item
  (every other player gets it); `--drop-item RECORD` drops one RECORD as a shared world
  item 1.5 m in front of the other V; `--take-items N` picks up (claims)
  every stack the others drop N s after it appears; as the host of a
  replay, `--remove-corpses N` takes an NPC a guest killed away N s after
  its death (`NpcGone` reason 0).

## Driving the game and the builds

- `tools/scenario.sh` deploys first. Scripts alone would be hot-reloaded
  with RedHotTools, which is not installed on anya: a script change restarts
  the game too. A failure's evidence is kept under `.cache/scenarios/`.
  After a fresh start the reset runs as `ensure`; it also clears test
  abilities and the revive cooldown.
- `task deploy` installs the DLL, scripts and tweaks on the game machine. A
  test `tandem.ini` with `role = host` and `autoload = true` loads the last
  save on its own; run `task peer -- join anya --mode mirror` here.
- The other way round: `tandem-peer` `host --time --weather` plays the host
  while the game joins as a guest with `role = join`,
  `address = <this machine>`.
- `tools/game.sh cmd` commands (`teleport home`, `where`, `vehicle ride`,
  `pose loop`, `travel`, `regroup accept`…) need no restart. Restart only
  for script or plugin changes.
- Real play instead of synthetic motion: the game records its own V
  (`tools/game.sh cmd record start NAME`, `record stop`; `fetch-play NAME`
  copies it to `recordings/play/NAME.tplay`), `tandem-peer --play FILE`
  replays it, and a scenario's `play NAME ARGS…` uses it when it exists (the
  synthetic ARGS otherwise). What to record: `recordings.md`, "Gameplay
  recordings".
- The game is launched through the Steam already running in anya's Sway
  session (`steam steam://rungameid/1091500` with that session's
  environment). Read `red4ext/logs/tandem-*.log` for the outcome.
- `tools/remote.sh TASK…` runs a task on the game machine instead (more
  cores): it syncs the working tree to `~/.cache/tandem/src/<worktree>`
  there, runs the task in devenv under that machine's own build lock at low
  priority, and copies `build/windows/plugin/tandem.{dll,pdb}` (and, after
  `build`, `build:linux` or `test`, `build/linux/peer/tandem-peer`) back.
  Then `TANDEM_PREBUILT=1 tools/scenario.sh …` deploys that DLL as is
  (`task deploy:files`) instead of rebuilding it here.
  It refuses while the game runs on anya (our own scenario runs set
  `TANDEM_REMOTE_WHILE_PLAYING=1` if they must build);
  `tools/build.sh TASK…` picks: anya when nobody plays there, else here
  through the laptop's lock.
- Through the relay by hand: `task relay:run`, then
  `task peer -- host --relay 127.0.0.1` (it logs `join code K7Q-2M9`) and
  `task peer -- join --code K7Q-2M9 --relay 127.0.0.1`. `task test` does the
  same (`relay-loopback`, `tests/relay_loopback.sh`: it builds the relay
  with Go and runs the `relay` suite of `tandem-tests` against it).
