# Recordings and the simulated second game

## tandem-peer: recordings

- **NPC record and replay** (first piece of the simulated second game):
  `join --record FILE` saves the host's `NpcSpawn`/`NpcStates`/`NpcGone`
  with their times; `host --replay FILE [--offset X,Y,Z]` sends them again
  at the recorded pace once a player joins. Record with the game as host,
  then let it join as a guest of the replay (`role = join`). Recordings
  carry the NPCs' animation states and shots too; `host --replay FILE
--anims SCRIPT` scripts them instead (the recording's own are then left
  out; a text file: time, NPC or `*`, the three states by name, optional
  weapon, target, shots, workspot; e.g. `tools/scenarios/npcs-anims.anims`).
  `tools/game.sh cmd npc seek [RANGE]` finds the nearest NPC sitting,
  leaning or smoking (in a workspot) within RANGE m of V and moves V within
  sharing range of it (scenario `npcs-anims-host`). A recording with no
  `NpcSpawn` (an empty street: rain or night) is not saved and the previous
  file stays, since its replay gives a guest no proxies (on 2026-11-18 that
  broke every guest scenario); a replay without spawns says so in its log.
  The `npcs-host` scenario spawns its own gangers first
  (`tools/game.sh cmd npc spawn [N]`). Tested on Linux by
  `tests/peer_replay.sh` (part of `task test`).
- **Gameplay recordings** (`core/include/tandem/play.hpp`): so the second
  player in a scenario is real play, not a figure circling and floating
  with no idea of the ground. The game records its own V: `tools/game.sh
cmd record start NAME` … `record stop` (a session is needed: `role =
host` records alone). Everything the game asks its `Endpoint` to send
  about its own V (`Endpoint::SetTap`: states with the movement flags,
  the pose stream, look, held items, shots, seats and cars, radio,
  stealth, time abilities, life, revives, and its actions: strikes and
  effects on NPCs, quickhacks, takedown requests, area hits, noises) is
  written by the net thread, with its time, as real protocol messages, to
  `red4ext/plugins/tandem/recordings/NAME.tplay`; it begins with what the
  others already know (look, held items, seat). Off, it costs one empty
  check per call. `tools/game.sh fetch-play NAME` copies it to
  `recordings/play/NAME.tplay` (not in git: a few MB a minute).
  `tandem-peer --play` hands the same messages to its own endpoint's same
  calls at their recorded pace: the endpoint numbers and stamps them on
  this session's clock (absolute times in them move with the replay),
  our car stays ours (`Seat::owner`), a recorded other player becomes the
  game's V, and a recorded NPC becomes the live one nearest where its hit
  landed (else nearest the replayed V, within 30 m), kept for the rest;
  what has no counterpart is dropped and counted. Hits on NPCs exist only
  in a guest's recording (a host's V hits its own NPCs without a
  message): record fights with the game as a guest of `tandem-peer host
--replay recordings/npcs.rec`. Tested by `tests/play_test.cpp`
  (a stream recorded through one session, replayed through another, seen
  the same by a guest). In scenarios, `play NAME ARGS…` teleports our V
  beside where the recording starts and replays it, or runs the synthetic
  ARGS when the file is missing (`front`: `street`, `drive`: `drive`,
  `movement-course`: `movement`, `npcs-hit`: `fight`). The body pose
  recorder of `tools/re/record_pose.py` (`.tpose`, `--pose`) stays for
  pose codec work; a `.tplay` carries the pose stream itself.

  **A recording session** (5–10 min, by whoever plays; the game as host
  with `tandem.ini` `role = host`, `name = V`; `tools/game.sh cmd …`
  from this machine, or the co-op menu's console later):
  1. `teleport home`, then `record start street`: walk 30 s through the
     streets around, sprint 20 s, walk back; climb and go down a flight of
     stairs; crouch-walk a bit; `record stop`.
  2. `record start movement`: jump, double jump if V has the legs, vault
     a low wall, mantle a ledge (a dumpster, a container), drop from a
     2–3 m ledge, slide, dodge; a ladder if one is near; into water and a
     dive if near (the docks); `record stop`.
  3. `record start drive`: call a car, get in, drive a few blocks on the
     roads (turns, a stop at a light), park, get out, walk 10 s;
     `record stop`.
  4. A fight as a guest: `tools/game.sh role join`, `tandem-peer host
--replay recordings/npcs.rec` here, then in game `record start fight`:
     draw a gun, shoot two or three of the gangers, a quickhack (Short
     Circuit, Blind), a takedown from behind, a melee hit; `record stop`.
  5. `tools/game.sh fetch-play street` (and `movement`, `drive`, `fight`).
     Without a player, the same can be driven with `tools/game.sh cmd`
     (`teleport`, `vehicle …`, `npc punch`, `takedown …`, `hack npc`) and
     `tools/game.sh key`/`hold` (AZERTY), less naturally.

- **Recorded host sessions** (`.tsession`, `play::Kind::Session`): the
  real game as a guest facing a real host's recording instead of a
  scripted mock host, in end-user conditions with one copy of the game.
  `tandem-peer join ADDRESS --mode watch --record-session FILE` is a guest
  that records every message the real host sends it, with its time (the
  endpoint's `SetReceivedTap`; welcome, roster, pings and the consistency
  checks are left out): the host's V and pose, look, held items, shots and
  car; its NPCs' spawns, states, animations, shots, health, deaths, holds
  and seats; hits and hacks on us; the world (time, weather), the story
  (journal, tracked quest, calls, lines, hubs, scene tiers, story effects,
  rewards), time abilities, stealth, awareness, fast travel and regroup
  offers, loot, life and revives, rides, epochs, devices (the host's word
  on each; replayed by whoever of this session did it). `watch` keeps the
  recording guest 30 m under the host's V (or `--offset`): out of sight, out
  of every NPC's reach, in nobody's way (the host shares the NPCs around
  its own V, wherever its guests are). No plugin change: exactly what a
  real host sends. `tandem-peer host --play-session FILE [--play-loop]`
  then is that host for the guest that joins: its V on its body, its NPCs,
  its story, at the recorded pace; the recorded guest's messages (a hit, a
  hack, detection bits, a reward) go to the real guest, other players'
  are dropped; the endpoint numbers and stamps everything on this
  session's clock (absolute times inside move with it), its epochs and
  digests are its own (the recorded epochs are bumped again); the NPC
  world goes through the replay's path (an NPC the guest killed stays
  dead), and what a guest asks (strikes, takedowns, hacks, revives) is
  answered as the mock host always did, with `--npc`, `--detect-at`,
  `--hit-you`, `--hold-npcs` … on top. Positions stay absolute:
  `play-info` gives the teleport beside the host's start. Tests:
  `tests/play_test.cpp` (a session recorded through one guest replays the
  same to another), `tests/peer_play.sh` (record, `play-info`, replay,
  record again). Files go to `recordings/session/` (not in git; about
  25 KB/s, 20 MB for 15 minutes, uncompressed like a `.tplay`). In
  scenarios, `host_world REPLAY ARGS…` hosts
  `recordings/session/${SESSION:-city}.tsession` when it exists (V first
  teleported beside it) and the NPC recording REPLAY otherwise:
  `npcs-guest`, `npcs-hurt-guest`, `npcs-shoot-guest`, `npcs-punch`,
  `hacks-guest`, `takedowns-guest`, `stealth-guest`, `desync-npcs`,
  `desync-rejoin`; `session-guest` replays a whole session and checks
  proxies come, the story is mirrored, at most 3 resyncs, no orphaned
  proxy, no stall beyond loading screens, and the cleanup at its end. The
  `story-*` scenarios keep their scripted stories (a session's story is
  `session-guest`'s).

  **A recorded host session** (about 15 minutes, by whoever plays; the game
  as host; the commands are run here):
  1. `tools/game.sh ensure host`, then `tools/game.sh test-reset` and
     `tools/game.sh cmd ride hide` (no marker or puppet of the recording
     guest on the player's screen).
  2. `build/linux/peer/tandem-peer join "$(getent ahostsv4 anya | awk 'NR==1{print $1}')":27450
--mode watch --name watcher --record-session recordings/session/city.tsession`
     here (it logs `recording the session to …`).
  3. Then play, in this order: walk 2 minutes through a crowded street
     (Kabuki, Jig-Jig Street); take a gig or side job with a fight: shoot,
     quickhack (Short Circuit, Blind), a melee hit, a takedown; the
     Sandevistan if V has one; call a car, drive a few blocks, get out;
     fast travel once (a loading screen is fine); answer a phone call;
     talk to an NPC with dialogue choices (pick one); walk a minute more.
  4. Ctrl-C the recording guest (`session saved: N messages, S s`), then
     `tools/game.sh cmd ride show`.
  5. `build/linux/peer/tandem-peer play-info recordings/session/city.tsession`
     says what it holds; `tools/scenario.sh session-guest` replays it.

- **The whole simulated second game** (built: `sim.md`, `tandem-peer sim`:
  a host world, a story, a simulated V whose moves are stitched from the
  `.tplay` recordings, personas, a referee) — the plan it grew from, so everything can be
  tested with one PC and one copy of the game. Built up as the protocol
  matures (each milestone adds what its messages need):
  - **record**: save everything a real game sends in a session (its V, and
    as host its world: NPCs, events, quest facts) to a file (NPCs: done;
    its V: done, gameplay recordings above);
  - **replay**: play a recording back as that game — as host, so the one
    real game is tested as a guest against a real host's world; or as a
    guest, so it is tested as host against a real player's V (NPCs: done;
    a V: done, `--play`);
  - **scripted players**: recordings edited or generated (walk a path, get
    in a car, shoot an NPC, join late, disconnect mid-fight) as repeatable
    regression scenarios, with checks on what the real game reports back;
  - **network shaping** in the peer (latency, jitter, loss, reordering):
    done (`testing.md`, "A bad network").

## The recorder: play recorded as it happens

The recorder (`tandem.ini` `recorder = on`; `plugin/src/recording.*`,
`core/src/recorder.cpp`): what this game plays, recorded for later
replays and tests, solo or in a session, without changing anything.
Solo, `TandemSystem.RecordTick` runs a host's senders as if a guest were
there (our V sampled as in a session, its look, held items, seat, car,
shots, pose stream; the world's time and weather; the NPCs around V
with `TandemNpcs.Share`; V's death) and a writer thread of the
recorder's own takes Runtime's outboxes, as the net thread would; in a
session the net thread's Endpoint hands it everything it says
(`Endpoint::SetRecordTap`). Segments of 10 minutes
(`recorder_segment_minutes`) in `recordings/auto/`, named by local time,
in the recordings' format (`tandem/play.hpp` version 2): solo or as
host a `.tsession` (`Kind::Session`, host 1, no guest), as a guest a
`.tplay` of its own V. Flushed every second, so a crash leaves at most
one segment, readable up to its last whole entry and indexed at the next
start. `recordings/auto/index.jsonl`: one line per segment, with where
(first and last position, metres moved), what happened (tags combat,
driving, scene, call, fast travel, death, loot, quest, and counts) and
the cost (the scripts' recording work per frame, average and maximum;
the writing time). Over `recorder_max_mb` (2048) the oldest go. Any
file error turns it off for the run (logged once), nothing else; it
writes only in its folder and never touches saves. The co-op menu shows
its state and switches it until the game closes. `tools/game.sh
fetch-recordings [--since YYYYMMDD]` brings segments and the index
here; `tools/game.sh cmd recorder status|cut|on|off`; scenario
`recorder`. Not yet run in game. Not recorded solo yet (they only run in
a session): the story, time effects, fast travels as regroups, loot,
devices.
