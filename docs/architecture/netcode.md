# Netcode: bandwidth, smoothness, fairness, frame cost

What a session costs on the wire and in a frame, how smooth remote things
look on real links, and the rules that keep both in budget (2026-11-19).
Code: `core/include/tandem/{traffic,netcode,lagcomp}.hpp`,
`pose.hpp` (`PoseEncoder`), `interpolation.hpp`, `vehicle.hpp`;
`plugin/src/perf.hpp`, `log.hpp`. Tests: `tests/budget_test.cpp`,
`quality_test.cpp`, `netcode_test.cpp`, `reliability_test.cpp`.

## Measuring

- **Traffic counters** (`net::Traffic`, kept by `net::Session`): encoded
  bytes and messages per message type, per channel and per direction, and
  ENet's own datagrams and bytes (headers, acknowledgements, pings,
  resends); kbit/s on the wire add 28 bytes of IP and UDP per datagram.
  `Endpoint::Traffic()`; `net::Summary` (one line) and `net::Table`.
- **The plugin** logs once a minute at `info`: `traffic: up … kbit/s (…
datagrams/s), down …; up: <top types>; down: …; poses K keys of F frames
(T thinned), O over another key; states held H, thinned S; npcs C
compact, W whole`, then the `perf:` line (Frame cost); the whole table at
  `debug`. `tools/game.sh cmd perf` says both at once, whatever the level.
- **tandem-peer** says `traffic: …` in every 5 s report (`--traffic-report`: the
  table), the whole run's table at exit; `--legacy-wire` sends as before
  this work, to compare; `tandem-peer traffic FILE...` measures a recording
  (`.tplay`, `.tsession`, an NPC recording): legacy and compact kbit/s by
  message type, datagrams counted as what falls within 4 ms (ENet's
  service), 32 bytes of headers each and 8 a message.

## Budget

**256 kbit/s each way** on a two-player link, headers included (what co-op
games commonly send; the user, 2026-11-23); NPCs in a fight may take the host's side to 384. `tests/budget_test.cpp` plays each session for 4 s over loopback UDP
with real endpoints (30 Hz states and walk-cycle poses, 24 NPCs at 10 Hz,
strikes, shots and their cues, health and animation twice a second) and
checks it, legacy wire alongside:

<!-- prettier-ignore -->
| Session (2 players unless said) | before up | before down | after up | after down | largest after (kbit/s) |
| --- | --- | --- | --- | --- | --- |
| on foot together, 3 m apart | 132.7 | 132.9 | 54.9 | 52.4 | PoseFrame 31.2, PlayerMotion 8.9 |
| standing together | 131.5 | 131.7 | 31.9 | 31.9 | PoseFrame 18.7, PlayerMotion 1.3 |
| fight, 24 NPCs (half walking) | 137.1 | 187.1 | 59.4 | 83.4 | up: PoseFrame 31.2, PlayerMotion 8.9, Cue 1.6; down: PoseFrame 28.5, NpcMotion 18.4, PlayerMotion 8.9, NpcAwareness 4.2 |
| driving, the host a passenger | 147.0 | 131.6 | 52.1 | 41.4 | up: PoseFrame 18.7, PlayerMotion 8.9, VehicleMotion 8.9 |
| apart (500 m) | 131.5 | 131.7 | 8.6 | 8.8 | PoseFrame 2.9, PlayerMotion 1.5 |
| 4 players on foot, a guest's link | 133.6 | 386.5 | 54.9 | 145.1 | down: three Vs' PoseFrame 86.4, PlayerMotion 26.6 |
| a scene watched: 3 actors' bodies, a cutscene's camera | 131.5 | 472.8 | 32.1 | 52.9 | down: PoseFrame 18.6, ActorFrame 11.4, SceneCamera 4.8, RigTracks 2.7 (the scene's streams at most 22: `scene::Pacer`) |

kbit/s on the wire, each way, 38 datagrams/s for a V's 60 messages/s (the
test sends state and pose together); CI, 2026-11-22, protocol 17. Before,
the pose alone was 104 kbit/s (431 bytes at 30 Hz) and the NPC list 21
bytes an NPC at 10 Hz. The synthetic walk cycle (`tests/motion.hpp`)
averages 130 bytes a pose frame here (150 before protocol 17; 114 in
`netcode_test.cpp`, whose keys fall on the same two moments of the stride);
real play's poses are in the recordings below. The test checks every row
against 64 (the fight's down 96, four players' down 192) and that no row
grew, and prints the fight's row stream by stream.

The fight's up is the fullest row a guest sends: its V's pose and state,
6 shots a second with their 6 tracer ends, 2 ricochets and 4 charge cues
(12 `Cue`s a second, 1.6 kbit/s, 2.4 with ENet's 8 bytes a message), 2
strikes. It was 64.3 or more from run to run, over the budget, since the
cues joined it (protocol 13: 2.2 kbit/s of cues, 3.0 with their headers,
on 61.3); protocol 17's shorter forms (What goes on the wire) took 4.8
from the pose, 0.7 from the state, 0.6 from the cues. A run's timing moves
a row by 1 to 2 kbit/s (42 datagrams/s here instead of 38, 32 bytes each:
events that miss their frame's datagram).

Recordings of real play (`tandem-peer traffic`, `recordings/play/` of the
integration worktree, a V's own upstream as its game sent it):

<!-- prettier-ignore -->
| Recording | length | legacy kbit/s | compact kbit/s | pose legacy → compact | state legacy → compact |
| --- | --- | --- | --- | --- | --- |
| `drive.tplay` | 165 s | 118.3 | 54.2 | 87.5 → 33.9 | 11.3 → 4.9 (car 5.0 → 3.0) |
| `fight.tplay` | 73 s | 112.2 | 46.8 | 87.1 → 30.9 | 11.4 → 4.3 |
| `movement.tplay` | 117 s | 114.7 | 58.3 | 89.4 → 42.3 | 11.4 → 4.5 |
| `street.tplay` | 177 s | 114.9 | 59.3 | 89.5 → 42.1 | 11.4 → 5.2 |
| `npcs.rec` (a host's NPCs) | 23 s | 25.0 | 11.8 | NPCs 18.6 → 5.4 | — |

The game sends poses at about 25 Hz (`drive`: 4193 in 165 s), a key every
13 frames; every recording is within the budget with no relevance
applied (the other player's place is not in a recording).

## What goes on the wire

Messages are appended (protocol 7). Endpoint encodes and decodes them:
events, taps and recordings above it see `PlayerState`, `PoseState`,
`VehicleState` and `NpcStates` as before, so the game, the mock player and
`.tplay`/`.tsession` files are unchanged.

- **`PoseFrame`** (the largest stream, 431 bytes at 30 Hz before): key
  frames (every 15 frames or 0.5 s) carry every bone in 5 bytes
  (smallest three, 13+13+12 bits, ≤ 0.0015 rad); between keys, per bone 2
  bits say kept (within 0.002 rad of its key's), turned (a rotation vector
  from the key's, 3 signed bytes of 1/256 rad, up to ±0.5 rad, ≤ 0.004
  rad; in 2 bytes, 5 signed bits a step, when the turn is within 1/16
  rad: the same turn, bit for bit, protocol 17) or whole.
  No acknowledgements: a frame names its key; a receiver that lost the key
  takes the bones left out from its newest key (they moved little), and
  drops frames only before its first key. The host decodes a guest's
  stream and re-encodes it for the others (one encoder per player), so
  each link's keys are its own.
  Around the rotations (protocol 17, written out in `protocol.cpp`: 34
  bytes on a frame between keys, 21 on a key, 48 before): the player in a
  byte, the key as how many frames back it is (a varint), a key's time
  whole and another frame's low 32 bits (`PoseDecoder` completes them from
  its key's time: the frames it can decode have one), no codes on a key
  (all whole), no length (the codes say it).
- **`NpcMotion`**: positions in 1/128 m from an origin (±256 m; wider
  spreads go whole as `NpcStates`), yaw in 1/65536 turn, only the NPCs that
  moved more than 2 cm, turned more than 0.01 rad or changed what they
  detect, each at least every 5 states (a lost one heals within 0.5 s;
  newcomers' refreshes staggered), and an NPC dropped from the list flagged
  once. The guest's `NpcBook` fills in the rest, so `NpcStates` comes out
  whole (detection counts stay right).
- **`PlayerMotion`**, **`VehicleMotion`**: a V's orientation is its yaw
  (16 bits), velocities in cm/s, a car's orientation in 5 bytes, the
  player in a byte (protocol 17; `PlayerId` is 4 everywhere else): 37 bytes
  for a V instead of 60, 37 for a car instead of 57.
- **`Cue`** (a shot's end, a bounce, a charge: up to a dozen a second
  from a V that fires): the player in a byte; a charge's level (255
  steps, `proto::ChargeLevel`) and hand in a byte each, not three floats:
  20 bytes for a place, 10 for a charge, 23 before (protocol 17).
- **`HitTaken`** (a hit a V took, for its puppet: `presence.md`; at most
  10 a second, 2 in the fight's row): the player in a byte, where it
  landed and the way it went a signed byte an axis (`proto::Snapped`):
  26 bytes, 47 as plain fields (protocol 18); 0.4 kbit/s each way in the
  fight's row, which it takes to 59.9 up and 84.0 down (CI, 2026-11-22).
- **A scene's streams** (scene replay: each actor's body as `ActorFrame`,
  the camera as `SceneCamera`): one budget for all of them on the host,
  22 kbit/s by default, split by `scene::Pacer`
  (`architecture/scene-replay.md` §2).
- **Relevance** (`netcode::PoseIntervalUs`, `StateIntervalUs`, by the
  distance from our V to the nearest other): poses every frame within
  30 m, 15 Hz to 80 m, 5 Hz to 200 m, then 1 Hz; states and cars every
  frame within 80 m, 15 Hz to 200 m, then 5 Hz. The receiver's pose is
  drawn `PoseTrack::IntervalUs` later when they come less often. A state
  5 m or more from where the last one sent puts it by now (a teleport, a
  fast travel) goes at once, near or far: per-player regions decide links
  from these places.
- **Still**: a V or a car that does not move goes 5 times a second; when it
  moves again the last held state goes first with its own time, so it
  leaves from where and when it stood; not one held over a pause in the
  states (our V elsewhere: a state 4.4 s old, refused by the referee's
  clock rule, 2026-11-21). The receiver's interval estimate skips still
  gaps, so the delay does not grow.
- **Coalescing**: the net thread holds our V's state or pose up to 8 ms for
  the other (they come from the script tick and the animation job a few ms
  apart): one datagram per frame instead of two. A broadcast is encoded
  once for every guest (`Session::Broadcast`).

Decided against: acknowledged baselines (they do not survive the host's
relay to several guests, and need a reverse stream); a lower pose rate
near (30 Hz within 30 m keeps melee and reloads crisp); compression of the
reliable channels (events are a few percent of the total).

## Smoothness

Audit (players, NPC proxies, cars; the jitter buffer; the clock), as found:

- The delay followed its target at once: a jitter spike or a better clock
  estimate moved the drawn timeline back (bodies stepping backwards).
- The clock offset jumped to each better ping (a few ms each, every few
  seconds on a jittery link).
- Extrapolation ended in a snap when states came back after a gap, up to
  0.5 m for a walking V at 150 ms / 5 % and for NPCs (no velocity is sent
  for them, so a late state was held, not carried on).
- Cars were drawn on the chord between states (a corner cut), snapping back
  after extrapolating; braking popped up to 0.5 m.
- A still V's slow rate would have widened the delay.

Fixes: the delay moves to its target at most 10 % slower or 3 % faster
than real time (`Track::kDelayGrow`, `kDelayCatchUp`); the clock offset is
slewed at 50 µs a ms (`Endpoint::kClockSlew`; over 250 ms off it jumps);
a correction after a guess fades out with a 0.1 s time constant
(`Track::kBlendSeconds`; above 3 m, 5 m for a car, it is a snap: a
teleport); an NPC's velocity is the one between its last two states; cars
follow a cubic through both states and their velocities (Hermite), the
chord when the velocities disagree with the motion; still gaps are left out
of the interval estimate. A view asked for twice in a frame is computed
once (`Track::kSameFrameUs`).

**Metrics** (`tests/quality_test.cpp`, 60 s each, drawn at 60 fps; each
link with jitter, loss in bursts of 2 to 4 datagrams and 1–3 % overtaken):
error from the truth at the drawn moment (p95, max), pop (a frame's step
off the true step), backwards (drawn against the motion), extrapolated
frames, delay.

<!-- prettier-ignore -->
| Link | What | before: error max, pop, back (m) | after: error max, pop, back (m) | extrapolated | behind |
| --- | --- | --- | --- | --- | --- |
| 30 ms 1 % | V walking | 0.001, 0.001, 0 | 0.001, 0.001, 0 | 1.6 % | 75 ms |
| 30 ms 1 % | car 20 m/s, braking | 0.147, 0.186, 0 | 0.169, 0.024, 0 | 1.9 % | 75 ms |
| 30 ms 1 % | NPC 10 Hz | 0.478, 0.478, 0 | 0.505, 0.027, 0 | 1.6 % | 172 ms |
| 80 ms 3 % | V walking | 0.003, 0.005, 0 | 0.004, 0.001, 0 | 1.7 % | 148 ms |
| 80 ms 3 % | car | 0.269, 0.351, 0 | 0.299, 0.039, 0 | 2.9 % | 150 ms |
| 80 ms 3 % | NPC | 0.085, 0.085, 0 | 0.121, 0.040, 0 | 1.8 % | 220 ms |
| 150 ms 5 % | V walking | 0.569, 0.569, 0.022 | 0.596, 0.027, 0 | 3.6 % | 242 ms |
| 150 ms 5 % | car | 0.448, 0.540, 0.376 | 0.488, 0.058, 0 | 4.2 % | 243 ms |
| 150 ms 5 % | NPC | 0.454, 0.454, 0 | 0.482, 0.027, 0 | 2.9 % | 311 ms |

p95 errors stay under 2 cm everywhere. The worst errors come from a V or a
car stopping during a burst of losses (the guess runs on); they are now
corrected over a few frames (at most 1.5 m/s for a V, 6 m/s for a car)
instead of in one, so the largest error lasts a little longer and the pops
are gone. Bounds checked: p95 error, pop, backwards, extrapolated share
and delay per link.

## Fairness under latency

A guest's request is checked by the NPC's owner where the guest saw it
(`lagcomp::Check`): the guest's V where its own states put it half a round
trip ago, the NPC where the host's was a round trip and the guest's view
delay (100 ms of interval and 15 ms) ago, rewound at most 400 ms
(`kMaxRewindUs`), with 0.5 m of tolerance; the nearer of that and now
counts. The host records its NPCs (`Runtime::SubmitNpcStates`) and the
guests' Vs (`PushRemote`) for 1.5 s. Used for the grab and pick-up reach
(6 m, `Takedowns.reds`, `TandemNative.InReach`), where "too far"
refusals were seen; melee hits are decided by the striker's game and need
none; revives are checked on the reviver's side against a still body.

## Frame cost

- **Profiling** (`plugin/src/perf.hpp`): `TandemSystem.OnTick` marks its
  22 sections (`TandemNative.PerfMark`, the order of `kTickSections`); the
  movement controllers, the pose read and write hooks and the net thread's
  passes are timed where they run; the deepest each queue was when taken
  (`Runtime::QueueReport`). Once a minute and on `cmd perf`: `perf: script
tick M ms mean, X ms max, N/s; sections (ms a tick): …; native:
controller U us x N/s (max …), pose read …, pose write …, net pass …;
queues at most: …`.
- **Hot paths fixed**: a puppet's controller asked the runtime three times
  a frame under two locks (seat, ride, hold), now once
  (`Runtime::PlacementOf`); the scripts and the controller each computed a
  puppet's view (`Track::At`) every frame, now once; NPC states took the
  track lock once per NPC, now once per message; a broadcast encoded its
  message once per guest, now once.
- **Log levels** (`tandem.ini` `log = warn | info | debug`, default info):
  per-puppet, per-proxy, per-hit and per-shot lines (thousands a minute in
  a fight with streaming crowds: six lines per proxy spawned, a health line
  per proxy every 5 s) are `debug` (`log::Debug`, `TandemNative.LogDebug`);
  `info` keeps the session's life, warnings and the minute's summaries.
  `tools/game.sh role host|join|off` writes `log = debug`: the scenarios
  read those lines.

## Reliability

- **Large reliable messages**: ENet cuts what is larger than a datagram
  into fragments on the reliable channels and puts them back whole; a
  journal snapshot of 1000 entries (6 KiB) and of the most there may be
  (32768, 192 KiB) cross 150 ms / 5 % in bursts whole
  (`reliability_test.cpp`; 1000 entries in 0.19 s, 32768 in 1.7 s, 193 fragments).
- **Keepalives and timeouts** (`session.md`): ENet pings an idle peer
  every tenth of the timeout; a quiet session over a lossy link stays up on
  pings alone; an outage shorter than the timeout is ridden out (states flow
  again); a longer one ends the session on both sides within the timeout,
  and a new join works as soon as the link is back (the plugin joins again
  every 5 s, `net_thread.cpp`).
- **Found by the outage test**: ENet doubles a reliable resend's wait at
  every try, without bound, so after an outage of 60 % of the timeout its
  next try came after the timeout and a live link was judged dead (1.2 s
  off with a 2 s timeout ended the session; with the default 5 s, about
  3 s off would). `Session::Poll` caps the wait at a tenth of the timeout
  (100 to 500 ms, `CapResends`): the link is heard again within half a
  second of coming back. A join's connect is capped the same way: its tries
  came at 0, 0.5, 1.5 and 3.5 s, none after that before the join gives up
  at the timeout (a host that starts listening late, a lossy link that
  loses the early tries).

## In game (not run here)

To collect on anya with the mock player (`tools/game.sh role host`,
scenarios with their defaults):

- `task scenario -- front front-badlink drive drive-badlink npcs-guest
npcs-guest-badlink takedowns-guest takedowns-host` and the rest of `all`: the `debug` lines
  they read, now behind `log = debug`.
- In each, `tools/game.sh cmd perf`: the `traffic:` line (kbit/s each way,
  datagrams/s: ~30/s expected with coalescing, was ~60/s) and the `perf:`
  line (script tick mean and max, the costliest sections, controller and
  pose hook microseconds, queues).
- `tandem-peer join anya --mode front --latency 150 --jitter 50 --loss 5
--burst 4 --reorder 3`: the puppet's walk without pops; the pose with no
  stutter (`poses … over another key` stays a few percent).
- A guest grabbing an NPC walking away at 150 ms: `reach: … as it saw
them … in reach` at `debug`.
- The player's own `tandem.ini` left at the default (`role player`): the
  log at `info` should grow by a few lines a minute.
