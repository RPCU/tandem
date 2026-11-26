# The observer

Every play session yields debugging data without marking anything by hand
(the user's own sessions: hosting a simulated companion, `log = debug`, the
recorder on). `tandem.ini` `observer = on` (off by default, its own key,
whatever the role or the log level); it changes nothing in game, never
touches saves, and writes only in `red4ext/plugins/tandem/observer/`
(beside `tandem.cmd`: each instance its own).

Code: the platform-independent parts `core/include/tandem/observer.hpp`
(`core/src/observer.cpp`, `tests/observer_test.cpp`): `Classify`, `Ring`,
`Limiter`, `Trail`, `Tally`; the plugin's `plugin/src/observer.{hpp,cpp}`
(files, threads, clock); the tools `tools/observer-report.py`,
`tools/game.sh fetch-observer`, scenario `observer`.

## Files

| File                         | What                                                                                                    |
| ---------------------------- | ------------------------------------------------------------------------------------------------------- |
| `session-STAMP.tsv`          | the story trail of one game run (STAMP: its start, local time, `YYYYMMDD-HHMMSS`)                       |
| `captures/STAMP-NN-KIND.txt` | one capture: what, the context at the trigger and when written, the log around it                       |
| `summary-STAMP.txt`          | a summary block at each session's end, every 30 minutes, on `cmd observer summary`, at the game's close |

Columns of the trail: `time` (local time to the ms, as RED4ext's `tandem-*.log`),
`t` (seconds since the run's start), `kind`, `text`. Rows are written only
on a change, flushed by the writer every 100 ms (a crash leaves them). The
folder is kept under `observer_max_mb` (1024): at load the oldest files go.

## The story trail

| Kind                                             | From                                                                                                                   |
| ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------- |
| `session`                                        | `Runtime::Start` / `Stop` (role and address)                                                                           |
| `phase`                                          | the quest node hook (`engine/quests.cpp`): a phase resource's path hash the first time it runs this run (solo too)     |
| `scene-kind`                                     | `engine/scenes.cpp` `ClassifyRecentScene`: a scene's path hash, class, category and actor names                        |
| `scene`, `cast`                                  | `SceneState` (start: path hash, play, tier; end, interrupted) and `SceneCast` (the NPC ids)                            |
| `journal`, `tracked`, `call`, `reward`, `effect` | `Journal` changes (hash and state, 8 a row), `Tracked`, `PhoneCall`, `QuestReward`/`VehicleUnlock`, `StoryEffect`      |
| `tier`, `fast-travel`, `story-flags`             | `StoryState`: a tier change, `kStoryNoFastTravel` locked or open, the other flags (call, braindance, private, moment…) |
| `restrictions`, `prefabs`, `gates`               | `StoryRestrictions`, `WorldPrefabs` (what came and went), `StoryGates`                                                 |
| `district`                                       | `StoryWorld.reds` once a second in a session (the prevention system's district record, `TDBID.ToStringDEBUG`)          |
| `epoch`                                          | `Runtime::RequestEpoch`: a fast travel, a loading screen, a time skip the scripts saw                                  |
| `watchdog`                                       | the net thread's watchdog lines (pauses and loading screens), and a stalled game thread that came back                 |
| `trigger`, `summary`, `observer`                 | every capture, else one row a kind every 10 s (`(+N since the last row)`); summaries; the game's close                 |

Story messages are read where they cross the plugin: our own in
`Story::Send` (game thread), the others' in `Story::PushInbound` (net
thread). So a host's trail has its own story; a guest's the host's as heard;
solo, only phases and scene kinds (the scripts send nothing solo). Journal
entries, scenes, phases and effects are hashes: the log and the game's
data name them.

## Triggers and captures

| Trigger                 | From                                                                                                                                                                                                                                              | Severity    |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| `stall`                 | the writer: in a session, the game thread's frame stamp (`Runtime::LastGameFrameUs`) 30 s old and no engine frame for 1 s                                                                                                                         | 95          |
| `degraded`              | `degraded: ID is OFF…` (85), `is off:` (55), `player N's game runs without:` (45); not a chosen one                                                                                                                                               | 45 to 85    |
| `fallback`              | `degraded: fallback ID…` (a repeat too)                                                                                                                                                                                                           | 30          |
| `respawn`               | the fallbacks `proxy respawned`, `street body respawned`                                                                                                                                                                                          | 40          |
| `one-sided`             | `resync: asking…` (a digest differed), `[script] consistency:` (orphan, missing proxy), `street: check: … no longer shared`                                                                                                                       | 40          |
| `error`, `script-error` | an error line; a `[script]` line saying ` failed` or `error`                                                                                                                                                                                      | 60, 35      |
| `drift`                 | the puppet controller: a remote V's body `observer_drift_m` (5) from its owner's transform for 1 s                                                                                                                                                | 30 + metres |
| `frame`                 | `TakeEvents`: a frame over `observer_frame_ms` (50), under 2 s (longer: a loading screen); a gap that held 3 engine frames or more is a pause, timed by its longest engine frame; a session's first tick judges none (`observer::FrameToJudgeUs`) | 20 + ms/10  |
| `manual`                | `cmd observer capture [REASON]`                                                                                                                                                                                                                   | 50          |

Log lines come through `log.hpp` (`log::Observe`): every line, debug ones
too whatever `log` says (formatted for the observer when the level would
drop them). A capture is taken at most once a kind a minute, `observer_captures`
(30) a run; every trigger is counted in the summary all the same. It is
written 5 s after its trigger (a stall at once): the ring's lines from 30 s
before to 5 s after (the ring keeps 40 s, 8 MB at most), the trigger's line
marked `>>> `, and its context: the session, our V's last sampled position,
every other player's last state, the trail's "now" (district, scene and its
actors, tracked entry, last phase, restrictions, prefabs, our tier and fast
travel); when written: our id, the roster with each puppet's position, the
registry's summary, the recorder's state.

- **Crash**: the plugin has no crash handler, and none was added (a filter of
  ours would race the game's and RED4ext's). Under Proton a crash freezes
  the game (`testing.md`), so the stall trigger is the crash's capture,
  written by the observer's own thread; a stall that comes back (a hang
  that ended) adds a `watchdog` row saying so. A pause menu or a loading
  screen is no stall: the engine's frames run on (`Runtime::EngineFramesRun`,
  RED4ext's Running state update) while the script tick waits; the
  watchdog's row says `paused` (six pause menus, up to 47 s, were ranked
  the worst moments as crashes, 2026-11-23).
- **Context**: a player who left goes from it (`Observer::Forget`; one
  gone 20 min stayed, "as it said 1287.9 s before", 2026-11-23).
- **Short pauses**: a pause menu under 2 s held the script tick for its
  length and was read as a frame that long (frames of 516 to 682 ms "after
  the close", 2026-11-23: each the pause itself; 104 engine frames in
  682 ms, the first script tick 22 ms after the close). A gap holding 3
  engine frames or more is timed by its longest engine frame; after our
  pause menu closes the plugin logs the next 30 engine frames (`pause:
after our menu closed, the first frame took … ms, the longest …`).
- **The session's start**: its first tick took the engine frames counted
  since the game started, the save's loading screen among them, and
  reported their longest as its own (frames of 1861 and 1370 ms at "session:
  starting as host", 2026-11-26; `BeginSession` took 1 ms of script, the
  first tick came 81 ms after it, and the minute's `perf:` line said 4.37 ms
  at most a tick). That tick judges nothing now; the engine frames before
  it are dropped. The first `perf:` line's rates were over 1 s instead of
  its minute (`5061/s`); they are over the time since the first use now.
- **Screenshot**: none. The plugin cannot take one cheaply (a D3D12
  swap chain under Proton); each capture says so. `tools/game.sh shot` by
  hand.
- **Referee and protocol violations** are the mock player's (`referee:`
  lines in its log); none reaches the game.
- **One-sided vehicles and NPCs**: the digests' resyncs and the scripts'
  consistency checks (above) are the cheap checks; nothing else compares two
  games' lists in play (`tools/street-compare.py` does, offline).
- **Drift** is checked for remote players' puppets only (NPC proxies and
  vehicles are placed by the scripts).

## Summary

`Tally::Summary`: the counts per trigger, the 10 worst moments (severity,
then the earliest) with their capture files, the captures written, the
districts in order, the journal entries that became active, the scenes, the
number of phases first run. `tools/observer-report.py [PATH] [--session
STAMP] [--trail] [--list]` reads a fetched folder (default
`.cache/observer`) and prints the newest session: its span and rows, the
sessions, districts, scenes and story changes, the triggers by kind and the
worst moments (from the trail), each capture's header, and the plugin's
last summary block.

`tools/play-session.sh` turns it on for the player's own sessions and
fetches the folder with the logs. `tools/game.sh fetch-observer` copies the folder to `.cache/observer/`
(`.cache/instances/NAME/observer/` with `TANDEM_INSTANCE=NAME`). It only
reads, so it takes the Steam game (the player's own play) without
`TANDEM_STEAM_GAME=1`.

## Threads and cost

Every entry point takes one lock of the observer's own, does no I/O and no
engine call, and logs nothing (a hook on a job thread may call it); the
writer thread writes the files and takes Runtime's and the registry's locks
only while holding none of its own. The quest node hook asks for a phase's
resource only when a thread moves to another phase (a thread-local).

Off: one relaxed atomic load per log line and per hook. On: per log line a
copy into the ring and a few prefix compares under the lock; per frame a
clock read; in a session one district read a second in the scripts.
`cmd observer` says what it did and costs: rows, captures, the ring's size
and memory, the log lines and the time each took on average and at most,
the game thread's time a frame. Measured on instance b (2026-11-23,
scenario `observer`, host with the mock player, 5.5 min): 1.6 us a log line
on average, 10 us at most; 0.31 us of the game thread a frame; the ring
under 0.1 MB (a quiet test field); 294 trail rows, 266 of them first runs
of quest phases.

## Open problems

- Run in game once (above): the trail (session, phases, districts,
  journal, tracked, tier, story flags, restrictions, prefabs, gates), frame
  captures at the load, a manual capture with its context and log, the
  summary. Not yet seen: a stall, drift, respawn or one-sided capture, a
  scene's rows, a guest's trail.
- Hashes are not names: the report could resolve journal paths and scene
  resources with a dump of the game's data.
- Seen in game 2026-11-23 (scenario `pause-own`): a 5.8 s pause menu says
  `paused`, then `back`; the frames after each close 33 ms (the instance's
  30 fps), the longest of 30 34 ms. Not yet seen: that RED4ext's Running
  state update goes on behind every loading screen (else a loading screen
  over 30 s is still a stall).
