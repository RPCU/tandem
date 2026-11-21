# Testing without a second player

<!-- prettier-ignore -->
| Level | What | Command |
| --- | --- | --- |
| unit | protocol round trips, strict decoding, sizes | `task test` |
| loopback | real host + guests over UDP in one process: join, relay, clock, leave | `task test` |
| bad link | smoothing and reliable channels through a shaped link (loss, jitter) | `task test` |
| netcode | bandwidth budget of typical sessions, smoothness metrics at 30/80/150 ms, fragments, outages (`netcode.md`) | `task test` |
| relay | protocol, codes, limits, amplification, real sockets, race detector | `task relay:test` |
| via relay | a real relay process: join by code, fallback from direct, mock players | `task test` (`relay-loopback`) |
| mock player | `tandem-peer` as host or guest against the real game | `task peer -- host --mode mirror` |
| consistency | digests, resyncs, epochs, dedupe; convergence with a third lost; vanishing | `task test` |
| referee | every message each test's endpoints hear, checked against the protocol and authority rules; a violation fails the test unless it calls `test::ExpectReferee` (`referee-suite:`) | `task test` |
| sim | simulated games against each other, refereed (`sim.md`) | `task test`, `task sim:fuzz` |
| regions | links, owners, hand-over (also with a third lost), a vanishing owner, digests both ways; two mock players meeting | `task test` |
| natives | each `*Natives.reds` matches its `*natives.cpp` (plain, story, combat, sync, elsewhere, region, street, presence, activity) | `task natives:check` |
| scripts | `scripts/tandem` compiles with the game's redscript, bundle and mods; no method defined twice, no bare value local, no array intrinsic or struct field on a call's result (`tools/scripts-temporaries.py`, also in CI) | `task scripts:check` |
| co-op menu | the settings page's arrays aligned, each row's label and description in English and French, keys the plugin takes, no duplicate or vanilla-looking entry (`tools/menu-check.py`) | `task menu:check` |
| threads | every script hook that may run on a job thread (per-entity, component and persistent-state wraps, `cb func`s, lifecycle methods, `Entity/*` callbacks) and all it calls: reads, the wrapped method, natives; no write to a system's state (`tools/scripts-threads.py`; `.known`: 82 hooks older than the check, unproven, each with the thread probe's call) | `task threads:check` |
| references | every `TandemX.Name(…)` call names a declared function, every record of ours the scripts name is in `tweaks/tandem` (`tools/scripts-refs.py`: what scc and the game would say, in CI); every scenario's `--flags` exist in the mock player, its `cmd` words in the scripts, its helpers in `tools/scenario.sh`, its queries in `TestQuery.reds` (`tools/scenarios-check.py`); every log pattern a scenario waits for, can match a line the game or the mock player writes (`tools/scenarios-patterns.py`, below) | `task refs:check`, `task scenarios:check` |
| other mods | every method another mod's redscript or CET Lua wraps, replaces or observes, against the ones tandem wraps: conflicts, order-sensitive, harmless chains (`tools/mods-check.py`, on any game folder or copy) | `task mods:check DIR=…` |
| scenarios | the real game against the mock player, checked on the game's log | `task scenario -- all`, `task sweep` |
| in game | deploy the DLL, read its log | `task deploy`, `task logs` |

The autopilot (our real V played without a human, for recordings and scenarios): [autopilot.md](autopilot.md).

**Scenarios** (`tools/scenario.sh`, `tools/scenarios/*.sh`) are the
in-game regression suite: each one puts the game in a role, drives the
mock player and expects log lines within a time limit. Restarts happen
only when the build or the role changed (one restart per role for a whole
run), so a run takes minutes. A failure keeps its evidence (logs,
a screenshot, every thread's backtrace when the plugin's watchdog saw the
game thread stop or the stall report has an open stall). Each feature
verified in game gets a scenario.

**The stall report** (`plugin/src/stall.hpp`): the watchdog's line comes
from the net thread, so a hang that stops it too (the whole plugin stuck:
a lock, the logger, the process; 2026-11-21, a guest's join with the quest
freeze: game thread, net thread and log silent at once, no watchdog line)
was not said anywhere. A thread of its own, sharing no lock with the game,
the net thread or the logger, watches the net thread (which never rests
5 s) and writes `stall.log` beside `tandem.ini` (emptied at each start) with
Win32 calls only: `stall: the net thread is silent for 5.0 s (STEP); …;
threads: net N, frame update F, script tick T`, each of those threads'
registers (`rip`, `rsp`), then their stacks as `#i MODULE+0xOFFSET`
(tandem's frames: its `.pdb`; the game's: `tools/re/addr.py`), and `stall:
the net thread is back after X s` if it was not a hang. A failed scenario
copies it into its evidence.
More than two players: `peer2 ARGS…` starts a second mock player beside
the first (`expect_peer2` reads its log, `.cache/peer2.log`); scenario
`three-players`. Mock players under the same `--name` are shown as "V (2)",
"V (3)" (each logs `the session shows us as …`); scenario `names-duplicate`.
Every scenario starts from one known state, `tools/game.sh test-reset`
(`cmd test reset`, `scripts/tandem/TestReset.reds`), run after the last
scenario's mock player is stopped and after any fresh start: the save
loads as the player left it and each scenario leaves state behind. It
has the systems say again what a session says once (its rules, comms'
settings, our level, the party HUD's layout, the throws' once-only lines:
the host's session outlives scenarios, and a scenario reads its log from
before the reset unless the game was launched for it); takes V out of any vehicle (an instant unmount, then the workspot exit) or
workspot; ends the test private scene and quest freeze, test worlds
(Elsewhere), the test dilation and `god on`; resets revive (`revive
fresh`: up, no cooldown); heals V and removes bleeding, burning,
crippling and other lasting effects; asks the police to stand down;
deletes the tests' NPCs (`Tandem.Test`, `npc hostile`, `proxy test`, the NPCs taken over from a mock player,
`ride npc`) and the car copies nobody drives (parked, or an earlier
player's; once nobody sits in them); then teleports V to the test field
(-915.0 1879.5 36.19) and waits until it has stood still there for 1.5 s
(3 s at least: the world streams in). It logs `test reset: ready (on
foot, at the test field, …)` listing what it could not clear (a scene
of tier 2 and up, a background scene the scene system counts V in at
tier 1, combat, a grapple, a body carried, police heat, a menu or the phone open,
time dilated, health), or `test reset: failed (…)`, which fails the
scenario. Ready, `test-reset` asks `test choice` and, while a dialogue's
choices are up (the save may load on a call waiting for one, its quest
holding the save), presses ChoiceApply (F) and waits for the call to end,
60 s at most: the first option, in memory only (the tests write no save). A scenario that needs
another spot goes there after it with `at` (Fixtures, below).

Before that reset, `tools/scenario.sh`'s `run`:

- **waits for the last session's end** (`teardown`), when a mock player
  ran: its stop ends the session in the game ("player N left", "marker:
  player N removed" as host; "session ended", "save: leave cleaning ->
  solo" as guest), lines that matched a later scenario's expects
  (`saves-leave`, `spectate-host`, `three-players`). Done on a host's
  "left" or a guest's "cleaning -> solo" and 1 s without such a line, or
  2 s without one, or none in the first 3 s; 8 s at most, never a failure.
  Then the mark, so the reset's own lines (above) are still the scenario's.
- **repairs** what the reset cannot undo (`repair`, each a `harness:`
  line): our V dead on the death screen
  (`revive: our V dies: death screen` since the last `playing`), saving
  locked after a story not put back (`save: stays locked`, no
  `save: unlocked` since) or the game in the main menu (`main menu world:
not a game` since, `exit-host`'s end): the game is stopped and the
  scenario's `ensure` starts it again; co-op left off (`session: chosen off` last, after a
  `coop leave`) in the role and keys the game runs with, which `ensure`
  would not restart: `coop join ADDRESS:27450` (tandem.ini's address) as
  guest, `coop host` as host.

**Fixtures** (`tools/scenarios/fixtures.txt`: `NAME X Y Z YAW`): the named
spots scenarios need that the test field lacks. `at NAME` (or `at "X Y
Z [YAW]"`; `tools/scenario.sh`) teleports our V and waits until it stands
within 4 m of the spot (its position asked every 2 s, 45 s at most: a far
teleport has a loading screen), then 3 s. Our V's position is set before
a loading screen ends: when one came (the watchdog's `silent` line), `at`
also waits for its `back` line (90 s at most) and 5 s of streaming, then
marks the log past it (three spots surveyed empty, each shot the loading
screen, 2026-11-21). Each scenario's `*_AT` variable takes a name
or coordinates (default below). `cmd fixture check [M]`
(`scripts/tandem/Fixtures.reds`, solo too) logs `fixture: …` lines: our V's
spot, the devices within M m (60) by class, the nearest door, lift,
camera, turret, barrel, access point and radio within 200 m with position
and state, the NPCs within M m (vendors of our own world, in a workspot)
and the nearest vendor and workspot NPC within 200 m, then `fixture:
done`. World glass has no entity: not listed. Scenario `fixtures-survey`
visits each (`FIXTURES="a b"`, `RADIUS`), notes those lines and a
screenshot, `glass break` at kabuki-market. Every spot is **unverified**
until the survey has run in game.

<!-- prettier-ignore -->
| Fixture | X Y Z yaw | Source | Expected there | Scenarios |
| --- | --- | --- | --- | --- |
| `test-field` | -915.0 1879.5 36.19 0 | `TestReset.reds` (verified: every reset) | Kabuki street corner: doors, traffic, 2 NPCs in a workspot 12 m off; no ap, camera, lift, turret, vendor within 200 m; something 7 m north (pilot, 2026-11-21) | the rest |
| `h10` | -1397.31 1278.07 123.08 0 | fast-travel point `wat_lch_dataterm_10` (dataterm tracker, 2.50 sectors); the save's own start (body held there, `police-host-cars`, 2026-11-21) | H10's floor; its lift terminal 70 m (journal `#q101_mp_elevator_terminal_mb`, -1434.85 1314.51 120.81) | `devices-lift` |
| `v-door` | -1389.66 1271.12 123.08 0 | journal `#q101_mp_leave_v_room` ("Leave the apartment"; the pin's z 124.59, the floor h10's) | V's door; V's computer 3.6 m (`#q101_mp_v_room_computer`); the scenarios `device goto door` | `interact-guest-pickup`, `interact-guest-hostonly`, `interact-apart-own-gig` |
| `kabuki-market` | -1158.78 2014.22 12.06 0 | `wat_kab_dataterm_01` (tracker) | market stalls: vendors, NPCs at stalls (a workspot); shop panes a guess (no data) | `vendor-guest`, `ledger-guest`, `npcs-anims-host`, `glass-guest`, `glass-latecomer` |
| `h11-basement` | -1390.79 1963.74 -25.02 0 | journal `#lch_01_tr_main_computer_room_001` (gig "Catch a Tyger's Toe"); walkthroughs (Fandom, Fextralife, GameClubz) | Tyger Claws server room, floor -05: 2–4 cameras, a laptop with network access, the lift; hostile unless the gig is done | `breach-guest`, `breach-host`, `comms-network`, `devices-camera` |
| `arasaka-park` | -209.26 -1454.22 7.60 0 | journal `#q112_mp_truck_drive_inside` ("Drive into Arasaka Industrial Park"); players' reports (perimeter turrets, cameras; hostile, empty after "Gimme Danger") | hostile turrets that are on | `turret-guest`, `turret-hands`, `turret-hands-busy`, `turret-host` |
| `drake-ave` | -1608.11 1772.70 18.38 0 | `wat_lch_dataterm_02` (tracker) | an avenue with traffic for police pursuit points (the test field found 0) | `police-host-cars` |
| `niwaki-plaza` | -230.49 306.01 29.55 0 | `wbr_hil_dataterm_01`, Gold Niwaki Plaza (tracker) | open plaza ground for `street.pilot` (14 m ahead, 8 m aside) | `pilot-street` |

Sources: the journal's quest map pins (`journal-quest-data.json`, the RED
modding wiki's export, as `engine/revive.md` §13.110); the fast-travel
points' arrival spots from cadugi/cyberpunk2077-dataterm-tracker
`data/reference.json` (2.50 records and 3263 world sectors, 2026-11-08). No
device placement was read from the archives (no WolvenKit here).

**Sweeps** (`tools/sweep.sh`, `task sweep -- …`): every scenario, or the
NAMEs given, through `tools/scenario.sh` one at a time, into
`.cache/sweeps/YYYYMMDD-HHMMSS/` (`--out DIR`): `results.txt` and each one's
output, `NAME.log`. Build first (`tools/build.sh`, or `tools/remote.sh` and
`TANDEM_PREBUILT=1`, which the sweep passes on).

- **Left out:** the by-hand ones (`# by hand` on line 2, or an expect of
  120 s and more in a file that says "by hand" or "waits for": a person
  does what its notes say; `--by-hand` keeps them) and, without `PILOT=1`,
  the pilot's.
- **Order** (`--list` prints it): grouped by `ensure` line, role and
  tandem.ini keys, one game start per group (10 groups for 155 scenarios,
  2026-11-21; alphabetical order restarted the game ~75 times, ~90 s
  each); in a group, those that stop the game at their end (`$game stop`:
  the death screen of `vanish-down`) last; `# runs last` ones after every group.
  `tools/scenario.sh all` runs the same order, by-hand ones included.
- **A limit** per scenario (`--timeout`, default 900 s: `soak-host` takes
  ~600), signalled to its whole process group (subshells, mock players).
- **After a failure**, a game that crashed (`[crash] thread` in the newest
  `red4ext/logs/redgraph-*.log`, or `CrashReporter.exe`) or hangs (the
  newest tandem log does not grow within 20 s of a `coop status` command
  while the game process lives; a game that runs answers it at once): every
  thread's backtrace first (`tools/game.sh bt` into the evidence folder,
  `bt.txt`; REDGraph's lines in `crash.txt`, the output in
  `scenario.log`), then `tools/game.sh stop`; the next `ensure` starts it.
- **A failed scenario that made a co-op menu choice** (`cmd coop join|pick|host|new`):
  the game is stopped (a `harness:` repair). The choice lasts for the
  game's run and no reset takes it back; left, the game kept joining an
  address for every later scenario (rerun 10, 2026-11-21).
- **A failed test reset** (class `reset`) in a game that runs: stopped too
  (a `harness:` repair). What the last one left (an open phone stops the
  tick) would fail every later one; `test reset` closes the phone.
- **On several games at once** (`--instances N`, not yet run in game: the
  coordinator will try it): the Steam game and the first N-1 instances of
  `SWEEP_INSTANCES` (default `a b c`, `tools/instance.sh`), each a slice of
  the order in parallel, on its own port and mock players; those that need
  the Steam game (keys, the pilot's, by hand, the port named) stay on it.
  `TANDEM_INSTANCE=NAME` points `tools/game.sh`, `tools/scenario.sh` and a
  one-game sweep at an instance. How, and what is routed:
  `two-instances.md`, "Sweeps on two games".
- **`results.txt`**, a line per scenario, tab-separated: name, `PASS` or
  `FAIL`, seconds, class, the first `FAIL:` line, the evidence folder (and
  the game, with `--instances`). The
  end prints the passes, game starts, `harness:` repairs, and the failures
  grouped by class. `--rerun-failed FILE` runs a results file's failures
  again (in a new folder).
- **Classes**, the first that fits: `abort` (the scripts do not compile:
  the sweep stops), `crash`, `hang` (also a `game thread silent` failure),
  `timeout` (the sweep's limit), `by-hand`, `dns` (a mock player or
  `coop join` given `:27450`, no address: `getent` answered nothing),
  `peer-args` (the mock player's log: `needs a value`, `usage:`),
  `referee`, `threads` (a hook beside the script tick), `reset` (not in
  the test state), `launch` (`ensure` failed), `pilot`, `location` (the
  game's log says the spot lacks what the scenario needs:
  `no turret within 60 m`, `interact look: nothing there`), `wording` (an expect timed out, and a line beginning as
  its pattern does, its text up to the first regex sign, 8 characters at
  least, is there: reworded, or other values), `missing` (no such line at
  all), `unexpected` (a `reject`), `command` (a `cmd` not taken), `other`.

**Log patterns** (`tools/scenarios-patterns.py`, in `task scenarios:check`
and CI, about 20 s): a reworded log line leaves its scenarios waiting for a
line that never comes. Each `expect`, `reject`, `at_most` and `since | grep`
pattern (and each `query`'s answer) must be able to match a line of the game's log, each `expect_peer`,
`expect_peer2` and `reject_peer` one of the mock player's. The lines are
formats collected from the sources: the scripts' string expressions (behind
`[script] `) and `Log*`/`Once` arguments, every `std::format`-style call
and literal of `plugin/src`, `core/` and `peer/`, the co-op notices
(`coop notice: <English text> [<key>]`). Values are followed where they are
plain (ternaries, locals and fields with their `+=`, parameters from their
callers, our functions' returns, Text.reds texts, `{}` arguments) and typed
as printed: a script Float is `60.000000`, `{:.0f}` digits. Any other value
stands for at most three words, none ending with a colon (a line's last
one, any number): a value never holds the `subsystem: ` a pattern begins
with. Pattern and format become automata and their product is searched.
A finding names the formats nearest to the pattern; `-v` shows which format
answers each pattern. It catches the rewordings and the Float formatting
that failed scenarios in game, not a changed order of events (`0 -> 3`
logged as `0 -> 1 -> 3`).

**Queries** (`scripts/tandem/TestQuery.reds`, solo or in a session): a
line our code logs says it did something, not that the game's state
changed (every NPC shot at a guest missed for days, 2026-11-21, while
`npcs-shoot-guest` passed). `cmd test query WHAT [ARGS]` reads the game's
own systems and logs `test query: WHAT ARGS = VALUE` (`none`: nothing to
read):

<!-- prettier-ignore -->
| Query | Value |
| --- | --- |
| `health [points]` | our V's health, percent (`StatPoolsSystem`, 2 decimals); `points`: in health points; the percent starts the watch `health low` reads |
| `health low` | the lowest our V's health went since the last `health`, read every tick: a hit's dip that regeneration fills within a second (a 10-physical hit is ~1 point through armour) |
| `item RECORD` | its count in our inventory, every stack (`TransactionSystem`) |
| `money` | our eddies |
| `fact NAME` | the quest fact's value (`QuestsSystem`, 0: never set) |
| `journal PATH [CLASS]` | the entry's state (`JournalManager`: `Inactive`, `Active`, `Succeeded`…) |
| `position` | our V's X Y Z |
| `mounted [player N \| puppet]` | `RECORD SLOT ENTITYHASH` of the vehicle our V (player N's puppet, the first remote player's) is mounted in (`MountingFacility`) |
| `npc ID health` | a shared NPC's health, percent: our own NPC or our proxy |
| `threat puppet` | the largest threat value an NPC we run near our V holds on the first remote player's puppet (`TargetTrackerComponent`) |

In a scenario: `query VAR WHAT [ARGS]` asks, waits 10 s for this answer
and sets VAR; `expect_value V OP W` (OP `=`, `!=`, `~` an ERE, `<`, `<=`,
`>`, `>=` as numbers); `expect_changed BEFORE AFTER HOW` (HOW `down`, `up`,
`same`, `changed`, or `N`/`+N`/`-N`, the exact difference). E.g.
`query before health` … `query after health`, `expect_changed "$before"
"$after" down`. `task scenarios:check` refuses a query the scripts do not
answer, an unknown operator or change, a compared variable the scenario
never sets, and a VAR that is one of the harness's own (`$out`). The
scenarios that assert effects so: `npcs-shoot-guest`,
`combat-friendly-fire(-off)` (health), `npcs-hurt-guest`,
`combat-melee-guest` (`health` before the host's first hit, then `health
low`), `revive-game`,
`revive-peer` (health after the revive), `trade-peer`, `phone-trade` (item
counts, money unchanged; the mock player keeps no inventory: its `applied`
line is its side), `story-guest` (the reward's money), `drive`, `ride`
(mounted, seat, the same car).

**Launches** (`tools/game.sh launches [N]`, default 5): the cheap crash
check, the first thing to run after a script or plugin change that touches
wraps. Each launch starts the game through Steam with the role's
`tandem.ini` as it is and ends on tandem's `] playing` (the save loaded;
20 s more, then fine), on a crash (REDGraph's `[crash] thread` line in
`red4ext/logs/redgraph-*.log`, or `CrashReporter.exe`), or after 240 s
(neither: the menus were not passed); the game is killed each time. It
prints `N launches, C crashes (…)` and fails unless C is 0 and one
played. Wraps that changed script state on job threads while the save
loaded crashed 5 launches in 6, and 0 in 7 without them (2026-11-20). It
also fails, naming them, when a launch's log has a `threads: hook … ran
beside the script tick` line (below).

**The thread probe** (`plugin/src/threads.hpp`): whether the hooks of
`tools/scripts-threads.known` run beside the script tick. Each starts with
`TandemNative.HookRan(n"File.Class.Method");` (`.2`: a second overload;
`task threads:check` refuses a listed hook without it, and the call anywhere
but first in a function of that name). `TandemSystem.OnTick` is bracketed by
`TandemNative.ScriptTickBegin()` and `ScriptTickEnd()`, which hold the
ticking thread in an atomic (0 between ticks). A hook's call is, by that
atomic at the moment it begins:

- **beside** the tick: on another thread while a tick runs. Two threads in
  script state at once, what corrupted the heap on 2026-11-20: unsafe.
- **inside** the tick: on the tick's own thread while it runs (called from
  something the tick did): safe.
- **between** ticks: no tick runs. Not judged: the game's own scripts may
  run then (a save's load, before the first tick), and a hook that
  began just before a tick and still runs when it starts is missed.

The thread ids alone say nothing: on anya (2.50, 2026-11-21) the tick
itself runs on the job workers and changes thread at any time. In the
game's log:

- `threads: hook NAME ran beside the script tick (thread A, the tick on B)`:
  a warning, once per hook and launch, written from that thread at once.
  `tools/game.sh launches`, `threads` and every scenario fail on it.
- `threads: N hooks beside the script tick: A×12, B×3` (calls so far): every
  60 s when one ran since the last, at a session's end and on
  `tools/game.sh threads` (`cmd threads`, then the newest log's `threads:`
  lines; exit 1 on a hook beside the tick; works after the game has quit
  too), followed there by `threads: N hooks only inside the script tick:
C×40, …`, `threads: N hooks between ticks, never beside one: D×7, …` (the
  calls between ticks) and `threads: N script ticks, on thread B last, it
changed thread M times`.
- `threads: the script tick runs on thread A, the frame update on B`, once
  per world (`: not the same` when they differ), and `the script tick moved
from thread A to B` the first time it moves: facts, not findings.
- `N hook calls before the first script tick (between ticks)`: the load
  before the first tick.
- `N hook calls beside a tick that never ended, not judged`: a tick marked
  begun 5 s ago and not ended (a script that stopped halfway); `a script
tick began on thread A while one ran on thread B` (a warning, once) and
  `N script ticks began while another ran`: two ticks at once.

Reading a played session (`tools/game.sh threads` at its end):

- a hook **beside the script tick**: rework it to the queue pattern (it only
  reads, calls the wrapped method and enqueues, `TandemStreaming.QueueStreamed`
  or a native of its own; its system acts on the tick), drop its `HookRan`
  and its line of `.known`;
- a hook **only inside the script tick**, with a count that shows the
  session exercised it (a fight for the NPC hooks, a drive, the menus), over
  a whole session: it may take the opt-out above it,
  `// game thread only: seen inside the script tick only, <date>`, and lose
  its `HookRan` and its line of `.known`. One clean session is evidence, not
  proof: the hooks per NPC, vehicle or projectile also want a session with
  many of them at once;
- a hook **between ticks, never beside one**: not proven either way; it
  stays listed (a session that runs it while ticks run, or a rework,
  decides);
- a hook in **none** of the lists never ran: it stays listed.

`tandem-peer` modes (in every one the referee checks what the others send,
`sim.md`; a `referee:` line fails any scenario):

- **mirror** — copies your V back as a second player, offset and optionally
  delayed: your own movements on a puppet, through the whole network path.
  The main tool for puppet, animation and vehicle sync.
- **ghost** — circles around your V at walking speed.
- **front** — stands 3 m in front of your V, facing it, and echoes your
  look, held weapon (unless it holds its own: `--hold`, `--held-record`,
  `--strike blade`), shots, pose (with `--pose FILE`, a recorded pose).
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
- **region** — a player with a region of its own (`regions.md`): it
  stands `--region-far M` (default 1000) from your V, or around
  `--region-at X,Y,Z`, with `--region-npcs N` (default 3) NPCs of
  `--region-record R` it runs itself (announced, moved, hurt by your hits,
  killed); it walks up to your V at `--region-walk-at S` (default 10; -1:
  stays away) and off again at `--region-leave-at S` (default -1: never),
  leaving its NPCs with you, at `--region-speed` m/s (default 25). It gives
  and takes NPCs as a game would and logs every step (`region: …`).
  `--region-cars N` runs N world vehicles of its own round its V (of
  `--traffic-vehicle`, announced once it owns its area, `traffic.md`), left
  behind with the NPCs; `--region-driver` seats its last NPC at the wheel of
  the first, `--region-chase` has that one chase the first player it meets.
  It gives and takes vehicles as a game would (`street: …`).
  `--region-placed HEX` says its NPCs are placed in the world (static
  entity ids HEX, HEX+1…: `NpcPlaced`), `--region-kill-at S` has its own V
  kill its first NPC S seconds in: a placed one goes into the session's
  death ledger (`ledger: …`, `regions.md` §14).
  Scenarios `regions-apart`, `regions-meet`, `regions-vanish`,
  `wheels-guest-area`, `ledger-guest`; two of them meet in `tests/peer_regions.sh`.
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
- **sim** — `tandem-peer sim --role host|join --persona P --seed N
--minutes M`: a simulated second game that plays (`sim.md`); `sim-fuzz`
  runs simulated games against each other. Scenarios `soak-guest`,
  `soak-host`.
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
  `host --restrict REC[,REC]` (`--restrict-at S`, default 2; `--restrict-for
S`, lifted after S, default never; or the story verb `restrict
REC[,REC]|off`): the host's story restrictions on its V, which a guest in
  its moment wears (`story.md`, "Restrictions"). The story verb `moment on|off`
  (`moment on [watch] [TIER]`): the host's V in a scripted moment
  (`kStoryMoment`, its tier TIER), one to watch with `watch`
  (`kStoryWatch`); with `camera
on` its camera's views go out, scene or not, for a guest to spectate
  (`story.md`, "Spectating"; `tools/scenarios/spectate-guest.story`).
- **quickhacks**: `join --hack-npcs N` uploads `--hack-action` on one of
  the host's NPCs every N s (1.5 s) and then lands `--hack-effect`;
  `host --replay FILE --hack-player N` has a replayed NPC hack the guest's
  V with `--npc-hack` (default the trace), interrupted `--hack-interrupt S`
  seconds after each (`HackingInterrupted` on the netrunner for 2 s);
  `--hack-car N` brakes the car another player drives. The game side's
  test commands: `tools/game.sh cmd hack npc [RECORD]` (our V quickhacks
  the nearest proxy), `hack car`, and `npc weakspot [N]` (our V's hit of
  N, default 40, on the nearest proxy's first whole weak spot that is not
  shut, flagged `WeakspotHit` as a shot's).
- `--region-immortal said|silent` (`--mode region`): the mock's own NPCs
  cannot die (health floors at 1 %); `said` tells the others with
  `NpcHealth`'s `kNpcProtected`, `silent` does not (scenario
  `npcs-protected`).
- `--drop P`, `--dup P` (any mode): a share P of what the peer sends never
  leaves, reliable messages too (as a bug would lose them), and a share of
  its state goes twice: the game must converge anyway (Consistency;
  scenarios `desync-npcs`, `desync-rejoin`). Resyncs and epochs are logged.
- `--vanish-at N` (any mode): N s after another player is in the session
  the peer ends with no goodbye, as a killed game does (scenarios
  `vanish-down`, `vanish-drive`, `vanish-story`, `story-session-items`).
- `--pause-at N [--pause-for M]` (any mode) opens "its pause menu" N s after
  another player is in the session, for M s (default 3): every game pauses.
- `--sandevistan-at N` (any mode) switches "its Sandevistan" on N s after
  another player is in the session, for `--sandevistan-for` s (default 5)
  at `--sandevistan-scale` (default 0.25): a time bubble around its V
  (scenario `time`); `--scan-at N` does the same with its scanner's slow
  motion (scenario `time-scan`). A guest logs the host's clock at each of
  its `World` updates with the pace since the last one
  (`host clock T s: D game s in R s (rate X)`, 8 at normal speed;
  scenario `time-menus`).
- **Fights**: `join --hit-npcs N` hits one of the host's NPCs every N s
  as a strike, `--strike KIND` its outcome (`plain`, `nonlethal`, `cap`,
  `poise`, `head`, `blade`); as the host of a replay it applies strikes by
  the game's rules (`StrikeLoss`) and answers with health, poise,
  knock-outs, wounds and deaths; `host --hit-you N` has one of its NPCs (a
  replayed one if any) hit every guest's V for 10 physical every N s; a
  guest logs the NPC hits it gets. `host --difficulty D` sets the
  session's difficulty; `--area-hit N` (either) throws "a grenade" (20
  physical, `AreaHit`) at every other V every N s, which only friendly
  fire lets through: `host --friendly-fire on|off` sets the session's rule
  (`Rules`). `host --hit-you-melee on` makes `--hit-you`'s hits melee
  (the guest holds them for its proxy's swing). `--throw N [--throw-item
REC]` (either) throws a grenade 10 m ahead every N s and explodes it 2 s
  later (`Throw`, `ThrowEnd`: the others launch a copy; `--throw-bounce`:
  a second in it is 3 m off its arc and says so, `ThrowPath`); `--smart N` fires
  a smart round at an NPC it knows; `join --defend N` parries, then
  blocks, one of the host's NPCs' attacks every N s (`Defended`).
  Enemy scaling (`npcs.md`): `join --near-npc` stands 4 m from one of the
  host's NPCs (the first heard that is still there), so the host counts it
  near; a guest logs each `NpcScale` ("npc N is scaled for P players");
  `host --enemy-scaling on|off [--enemy-scaling-health F]` sets the rule
  (`Rules`). Scenario `enemy-scaling`.
- **Down and out** (any mode): `--go-down N` puts its V down N s after
  another player is there (it lies still until someone revives it);
  `--go-out N` puts it out instead, up again 10 s later; `--revive N`
  revives another V that goes down N s later (its V walks beside it and
  holds for 10 s). The lives, revives and losses it hears are logged.
- **Stealth** (any mode): as host, `--npc RECORD` stands an NPC 6 m in
  front of the guest and `--detect-at N [--detect-for M]` makes the NPCs
  it sends notice the guests (meter rising over 2 s), detect them for M s
  (default 10), then lose them; `--camo N` turns "its optical camo" on for
  5 s (`--camo-record REC` names its status effect, as a V's camo sends
  it); `--shoot N` fires a gunshot every N s (the shot, where its bullet
  ended, and as guest its noise); `--footsteps` sends its V's footsteps
  as noise while it runs or sprints (stims 25 and 26, 4 a second).
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
- **Activities** (any mode; `activities.md`): `--ask-skip N` asks the
  others to skip `--skip-hours H` (default 3) N s after another player is
  there, `--skip-reason wait|sleep|quest` (quest, as host: said, never
  asked); requests it hears are answered by `--skip-answer
yes|no|ignore|sleep` (sleep: it goes to bed too, a sleep of its own to
  the same hour, which the host counts as its yes: two beds at once); an
  applied skip moves the clock it sends as host. `--activity KIND`
  (`workspot`, `sleep`, `screen`, `minigame`, `shop`, `menu`, `photo`,
  `contest`) makes it busy `--activity-at N` s (default 3) after another
  player is there, anchored where it stands, for `--activity-for S` s
  (default 15; 0: for good); `--contest duel|race|range` makes that a
  contest of a sort (a duel names the first NPC it knows as its opponent).
  A host story's `activity KIND [anchored]
[lying] [frozen]` and `skip HOURS [wait|sleep|quest]` do the same.
  The game side: `tools/game.sh cmd skip ask HOURS`, `skip accept`, `skip
decline`, `busy KIND|off`, `busy report`, `apartment report`. Scenarios
  `skip-wheel`, `skip-asked`, `busy-puppet`, `look-mirror`,
  `apartment-shower`, `apartment-bed`, `apartment-tour`.
- **Comms** (any mode, `comms.md`): `--link auto|ask|off` (its consent;
  ask: every request accepted), `--link-at N` (asks everyone N s in),
  `--unlink-at N` (breaks every link N s in: the party HUD's entry goes,
  scenario `hud-linked`),
  `--ping N` (every N s: a spot before the other V, the nearest shared NPC
  as an enemy and at once again as a danger, then taken back), `--tag N`
  (scan tag on and off of the nearest shared NPC), `--reveal N` (up to 4
  shared NPCs for 10 s, as the Ping quickhack), `--waypoint X,Y,Z`,
  `--waypoint-kind pin|point|objective` and
  `--waypoint-clear N`, `--mark-target ID|nearest` (a team target, as the
  "Mark target" quickhack), `--hack-npc ID|nearest` (host: our V uploads
  `--hack-action` on that NPC for 3 s, then `--hack-effect` is on it for
  10 s, every 20 s). It logs every mark, tag, link and waypoint heard.
- **A bad network** (any mode): `--latency MS`, `--jitter MS`,
  `--loss PERCENT` (`--burst N`: in bursts of N datagrams on average),
  `--reorder PERCENT`, each way. A UDP relay with a link
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
- **Presence** (any mode; `presence.md`): `--hold RECORD` puts an item in
  its V's right hand (a weapon, so `--shoot` and `--melee` are seen and
  heard), `--holster-at N` puts it away N s after another player is there
  and draws it 5 s later; `--shoot N` also sends where the bullet ended (a
  `Cue` 6 m past the other V), `--melee N` swings every N s and lands it (a
  `Cue`); `--hurt N` takes a quarter of its V's health every N s (full
  again under 30%), `--menu-at N` is "in a menu" N s in, for 5 s (both as
  `Presence`); `host --react-npcs N` has one of the NPCs it sends play a
  hit reaction every N s (twitch, impact, stagger, knockdown, ragdoll, its
  end); `--status REC@FROM-TO[,REC@FROM-TO]` puts status effects on its V
  from FROM to TO s after another player is there (no TO: kept), sent as
  `StatusVisuals` for the others' puppet of it to show. Presence, cues,
  status visuals and non-twitch reactions it hears are logged.
- **Traffic** (any mode): each report has a `traffic:` line (kbit/s each
  way with headers, datagrams/s, the largest message types), `--traffic-report`
  the whole table, the exit the whole run's; `--legacy-wire` sends as
  before the netcode work. `tandem-peer traffic FILE...` measures what a
  recording takes on the wire, legacy and compact (`netcode.md`).
- Loot (any mode): `--loot-quest RECORD` has "its V" loot a quest item
  (every other player gets it); `--drop-item RECORD` drops one RECORD as a shared world
  item 1.5 m in front of the other V; `--take-items N` picks up (claims)
  every stack the others drop N s after it appears; as the host of a
  replay, `--remove-corpses N` takes an NPC a guest killed away N s after
  its death (`NpcGone` reason 0).
- **Devices** (any mode): `--device ENTITY:CLASS:WORDS[@SECONDS]`
  operates a device by id (a guest asks the host; a host says its own is
  that, and takes a guest's request as said: it has no world);
  `--device-answer WORDS --device-after N` changes back every device
  another player just changed, N s later. Every device it hears of is
  logged as the game logs it. `tests/peer_devices.sh`; scenarios
  `devices-door`, `devices-lift`, `devices-camera`, `devices-barrel`,
  `devices-guest` (`architecture/devices.md`, Testing).
- **Breach Protocol** (any mode): `--breach ENTITY[:CLASS][@SECONDS]`
  (an access point by id, its class `AccessPointControllerPS` unless
  named) or `--breach npc=N[@SECONDS]` (the quickhack on NPC N) sends a won
  breach SECONDS into the session (5) with `--programs LIST`
  (`MinigameAction` records by name or hex), or a lost one with
  `--breach-failed`. The host's answer and every other player's breach it
  would apply are logged (it has no world: taken as run).
  `tests/peer_breach.sh`; scenarios `breach-guest`, `breach-host`
  (`architecture/devices.md`, "Breach Protocol").
- **Vendors** (host; `progression.md`, "Vendors"): `--vendor
REC@X,Y,Z#ENTITY` shares one NPC of REC standing at X,Y,Z once a guest
  is there, said to be the placed vendor ENTITY (hex, as the game's
  `tools/game.sh cmd vendor near` logs its own nearest vendor), so the
  game as a guest opens its own vendor of it; `--vendor-dies N` kills it N
  s after a guest is there (every game hears that vendor died). What it
  hears of vendors is logged. In game: `tools/game.sh cmd vendor`,
  `vendor near`, `vendor goto`, `vendor open`, `vendor close`; scenario
  `vendor-guest`.
- **Quest interactions** (any mode): `--interact
ENTITY:CHOICE[:DETAIL][@SECONDS]` has a guest's V use the host's object
  (asked of the host; a known host-only or local one is answered at once);
  `--interact-answer performed|host-only|local|missing|busy|too-far|none`
  is what a host answers every guest (local by default: it has no world).
  `tests/peer_interact.sh`; scenarios `interact-guest-pickup`,
  `interact-guest-hostonly`, `interact-apart-own-gig`
  (`architecture/story.md`, "Quest interactions").

- **Vehicle combat** (`vehicle-combat.md`; `tests/peer_wheels.sh`):
  `--mode drive --car-damage N` dents our car N s after it sits (a flat
  tyre, the front bumper off), burns it 4 s later and blows it up 4 s after
  that; `--exit-at N --exit-kind bail|thrown|knocked|dead|exploded` leaves
  the car the hard way (it rolls on to a stop, its state still sent);
  `--bump N` pushes the car another player drives; `--shoot-cars N`
  (any mode) shoots it (every other shot a tyre); `--gunner N
[--gunner-seat SLOT]` takes the gun of another player's car, aims and
  fires for 20 s; `join --ram-npcs N` runs into one of the host's NPCs at
  12 m/s; `host --traffic N --chase N` has the first car chase the guest
  for 20 s. As the owner of a car it applies the hits it gets (1000 health
  points a car) and says how the car stands; everything heard is logged.
  The game side's test commands: `tools/game.sh cmd wheels`, `wheels
shoot`, `wheels gun`, `wheels ungun`.
- **Other mods** (any mode; `mod-compat.md`): records a mod would add,
  with their hints (the game's enum names): `--held-record
NAME:TYPE[:CATEGORY]` (held, as `--hold`), `--vehicle-record
NAME:TYPE[:SEATS]` (driven, as `--vehicle`), `--look-piece NAME:TYPE`
  (worn, again for more), `--npc-record NAME:TYPE[:GENDER]` (the host's
  NPC, as `--npc`). `--unknown NAME,...` gives the mock player a "game"
  that lacks those records: what it hears is resolved as a game does,
  logged (`mods: unknown ... shown as ...`, `mods notice: ...`) and told to
  its owner (`Substitutes`); a `Substitutes` about ours is logged. Scenarios
  `mods-unknown-held`, `mods-unknown-vehicle`, `mods-unknown-look`; the
  game's commands `coop mods` (every comparison and replacement, in the
  log) and `coop mods names` (ask for the others' mod names).
  `tandem-peer mods PATH` prints what tandem makes of a game folder's mods
  (names and contents) or of a list of file names (`tests/data/mods-sample.tsv`).

## Busy machines

The loopback and session tests run real endpoints against the wall clock,
and they used to fail now and then on a loaded machine (a build beside
them, three test runs at once, anya at load ~24: a join that took longer
than the test's 5 s, `budget_test`; `peer-regions` and loopback cases the
same), then pass on a rerun. Since 2026-11-19:

- **Waits for a state stretch** (`tests/slowdown.hpp`): `test::Patience
(limit)` is the limit times `test::Slowdown()`, the machine's load per
  core (`/proc/loadavg`, at least 1) or
  `TANDEM_TEST_SLOWDOWN=F` (1 to 20), whichever is larger, read at each
  wait (a build next door starts and stops). Every test's
  pump helper ("until the guest is joined, at most 3 s") goes through it,
  and the waits of the pumps that also pace frames. A wait ends as soon as
  its state is reached, so the longer bound costs nothing when the test
  passes; what it checks is unchanged.
- **Fixed windows do not**: a window that checks something does _not_
  happen, or paces frames, keeps its length when it calls a pump that does
  not stretch (`relay_test.cpp`'s `Pump(…, false)`), or stretches only
  where a longer window checks the same thing (a helper's "nothing more
  arrived in 200 ms").
- `tests/relay_loopback.sh` waits up to 15 s for each line it expects.
- **Ports apart per run** (`tests/ports.hpp`): each test binary starts its
  ports (41000 to 61000) at a place spread from its process id, and skips
  a port someone holds. Before, each process had 50 ports from
  `41000 + (pid % 400) * 50`: runs started together got consecutive ids,
  and the first one past its 50 ports bound the next one's (three runs of
  the loopback suites at once failed the same seven region cases each
  time).

## Driving the game and the builds

- `tools/scenario.sh` deploys first. Scripts and tweaks alone are
  hot-reloaded with RedHotTools (1.3.0-rc.7, MIT, installed on anya
  2026-11-21: `red4ext/plugins/RedHotTools`, its CET mod beside it;
  `tools/game.sh reload`); a new DLL or role restarts the game. A failure's
  evidence is kept under `.cache/scenarios/`.
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
- `tools/game.sh bt` (a frozen game: Proton's crash handler waits) writes
  every thread's stack to `.cache/re/bt.txt`, prints tandem's frames, then
  a `known crash site:` line for each game frame inside a function listed
  in `bt_known` (`tools/game.sh`: the crash handler, the transform update
  and AI workspot sites of `engine/re.md` §13.98). A new crash site found
  goes there, with its section.
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
  there, runs the task in devenv in one of that machine's three build slots at low
  priority, and copies `build/windows/plugin/tandem.{dll,pdb}` (and, after
  `build`, `build:linux` or `test`, `build/linux/peer/tandem-peer`) back.
  Then `TANDEM_PREBUILT=1 tools/scenario.sh …` deploys that DLL as is
  (`task deploy:files`) instead of rebuilding it here, and refuses a DLL
  older than the last commit to `plugin/`, `core/` or a `*Natives.reds`
  (RED4ext would refuse every script naming a native it lacks and close
  the game).
  It refuses while the game runs on anya (our own scenario runs set
  `TANDEM_REMOTE_WHILE_PLAYING=1` if they must build);
  `tools/build.sh TASK…` picks: anya when nobody plays there, else here
  through the laptop's lock.
- Through the relay by hand: `task relay:run`, then
  `task peer -- host --relay 127.0.0.1` (it logs `join code K7Q-2M9`) and
  `task peer -- join --code K7Q-2M9 --relay 127.0.0.1`. `task test` does the
  same (`relay-loopback`, `tests/relay_loopback.sh`: it builds the relay
  with Go and runs the `relay` suite of `tandem-tests` against it).

## CI

GitHub Actions (`.github/workflows/ci.yaml`) builds and tests every push to
any branch of `RPCU/tandem` and every pull request, and when run by hand
("Run workflow"; `tests: false` only builds); a newer push cancels the
older run of the same ref. Branches are merged into `main` with
`gh pr merge --rebase` once every job is green. The laptop does not run the
C++ builds (`AGENTS.md`, Git).

- **Jobs:** `go` (relay and pilot: vet, race tests), `kubernetes` (the
  kustomize base and overlays, kubeconform), `docs, natives, strings`
  (`tools/docs-check.py links`, `check-natives.py`, `check-strings.py`,
  `scripts-threads.py`, `scripts-refs.py`, `scenarios-check.py`,
  `scenarios-patterns.py`),
  `core build (Linux)` (`task build:linux` with GCC 16, the devenv's: the
  tests' seeded links are measured with its libstdc++), then `core tests`
  in four jobs side by side on its binaries (`tandem-tests` in three
  slices, `--order-by=name --first/--last`; the scripts' suites), and
  `plugin (Windows,
clang-cl)` (`task build:plugin`: Ubuntu's LLVM 20, the MSVC CRT and
  Windows SDK 10.0.22621 from xwin 0.9.0, cached; `tandem.dll` and
  `tandem.pdb` are uploaded as the run's artifact `tandem-<commit>`).
  `task scripts:check` compiles against the game's own scripts and is not
  run: it needs the game machine.
- **Memory** (`.github/actions/build-host`): the runner (4 cores, 16 GB)
  gets 16 GB of swap and one compile job per 3 GB of RAM, at most one per
  core (4). Swap only
  slows down a compile heavier than planned instead of losing the runner
  (run 44616559671 was killed at 2 jobs, no swap, before "Compile memory"
  below).
- **ccache:** ccache 4.14 (devenv's version), the same settings as
  `devenv.nix` (base dir the checkout, no hashed dir, depend mode), 1 GB
  per job, saved with `actions/cache` under a new key each run and restored
  from the newest earlier one (`ccache-linux-`, `ccache-plugin-`), even when
  the build or the tests fail. Caches are per branch, falling back to
  `main`'s: a branch's first run starts from `main`'s cache.
- **Build report** (`.github/actions/build-report`, the job summary):
  ccache's hits and misses, and the 15 heaviest compiles of the run (peak
  memory, seconds; misses only: a wrapper in `CCACHE_PREFIX` times each).

## The compiler cache

Every compile, Windows and Linux, goes through ccache (from devenv), one
cache per machine shared by every checkout and worktree: a fresh worktree's
first build is mostly hits on what another worktree compiled.

- **Off:** `TANDEM_CCACHE=0` (read at each configure, which every
  `task build*` runs; `tools/remote.sh` passes it on to the game machine).
- **Where:** `~/.cache/tandem/ccache` (`TANDEM_CCACHE_DIR`). Capped at 6 GB
  on the laptop and 15 GB on the game machine (`TANDEM_CCACHE_SIZE`; on the
  game machine `TANDEM_REMOTE_CCACHE_SIZE`). One commit's full build
  (plugin and Linux) takes 0.4 GB; a change to a widely included header
  (`protocol.hpp`) misses nearly everything, so each such variant costs
  that again. 15 GB holds some 35 variants for the game machine's 30-odd
  worktrees; the laptop builds only when the game machine is busy, and 6 GB
  (15 variants) of its smaller free disk is enough.
- **Settings** (environment, `devenv.nix` `enterShell`; no user-wide
  `ccache.conf`): `base_dir` is the directory holding every checkout (the
  main checkout on the laptop, its worktrees live inside it;
  `~/.cache/tandem/src` on the game machine), so paths under it are hashed
  relative to the build directory; `hash_dir = false`; depend mode (the
  compiler's own `-MD` or `/showIncludes` list, no second preprocessor run
  on a miss); `compiler_check = echo %compiler%` (every Nix store file has
  the same mtime: the compiler's store path stands for its version).
- **Safety:** no sloppiness is set: a hit needs the same compiler (store
  path), flags and the content of every included file. A cache it cannot
  read or write, or a compile it cannot cache, makes ccache run the
  compiler as is; no ccache in the shell, no launcher. Concurrent builds
  share the cache safely (ccache locks its files). If a build looks wrong,
  `TANDEM_CCACHE=0` rebuilds without it.
- **Measured** on the game machine (2026-11-19, 12 jobs): a fresh
  worktree's `build:plugin` took 190 s without the cache and 2.2 s warm from
  another worktree, `build:linux` 305 s and 15 s (mostly linking the tests).
  Through `tools/build.sh` under load, a fresh `build:plugin build:linux`:
  72 s, 193 of 195 compiles hits (the misses: the files that include the
  commit's build name). A one-file change still compiles that file (18 s
  for a plugin file); changing it back is a hit (0.5 s).
- **`task ccache:stats`**, **`task ccache:clear`**; `tools/remote.sh
ccache:stats` for the game machine's.
- **What the build does for it:** ccache refuses every clang-cl `/clang:`
  option, so the C++23 flag is `/std:c++23preview`, the depfile is
  `/showIncludes` (top-level `CMakeLists.txt`). C++ module scanning is off: nothing here is a
  module, and it compiles with GCC's `-fmodules-ts`, which ccache refuses.
  The debug info is `/Z7` (in each object;
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, `cmake/clang-cl-msvc.cmake`), not
  cl's uncacheable `/Zi`: clang-cl writes the same objects for both, and
  lld-link still makes `tandem.pdb` from them (same 21363 publics and 11581
  procedures as with `/Zi`). The plugin has no precompiled header.
- **Caveat, debug paths:** a hit is the object another worktree compiled,
  byte for byte, so its debug info names that worktree's directory
  (CodeView file names, DWARF `comp_dir`). Recompiling a hit's source in
  another worktree gives the same code and differs only in those strings.
  Function names in `tandem.pdb` (`tools/game.sh bt`) are unaffected; a
  debugger looking for source lines may open the other worktree's file, or
  none if it is gone.

## Compile memory

`proto::Message` has ~140 alternatives and `SessionEvent` ~110. libstdc++'s
`std::variant` compiles construction from an alternative, copy, move,
assignment and `emplace` with one template recursion level per alternative:
O(N²) instantiations for each, and GBs of compiler memory in every
translation unit that copies a message. In CI (GCC 14, 16 GB runner) the
heaviest units took 8–9 GB each (`protocol.cpp`, `endpoint.cpp`, the sim's
`game.cpp`, `play_test.cpp`) and three in parallel had the runner killed.

- **`ClosedVariant<V>`** (`core/include/tandem/closed_variant.hpp`) derives
  from the variant and only declares those operations; they are defined in
  `core/src/closed_variant_impl.hpp` and explicitly instantiated, one or two
  per translation unit: `message.cpp` (copy, destruction, assignment as
  destroy-and-construct, `==`), `message_move.cpp`, `message_make.cpp`
  (from an alternative, through a table of `in_place_index` constructions),
  and `session_event.cpp`, `session_event_make.cpp`. Everything else sees a
  class with out-of-line members; `std::visit`, `std::get_if`,
  `std::holds_alternative` and `index()` work on it as on its base
  (`std::variant_size` and `std::variant_alternative` are specialized).
- **Encoding** (`protocol.cpp`) writes zpp_bits' variant form itself (the
  index in one byte, then the alternative) and decodes through a table of
  readers, one per alternative. zpp_bits' own variant support checked
  that every alternative has its own id at compile time, in O(N²) constexpr
  steps (it needed `-fconstexpr-steps` and GCC's ops limit, both gone), and
  decoded through `emplace`. The bytes are the same.
- **Measured** in CI (peak memory per compile, cold cache, 16 GB runner):
  before, GCC 14 took 8.8 GB for `protocol.cpp`, 8.3 GB for
  `endpoint.cpp`, 8.0 GB for the sim's `game.cpp` and `play_test.cpp`, and
  the Linux build 25 minutes at 3 jobs, deep in swap (with GCC 16 it did not
  finish in 59); clang-cl 3.2 GB for `protocol.cpp` and the plugin 7.5
  minutes. After: the heaviest Linux compile is `message_make.cpp` at
  2.4 GB (`protocol.cpp` 1.4, `endpoint.cpp` 1.7, `play.cpp` 1.0), the
  Linux build 3.5 minutes; clang-cl at most 1.7 GB, the plugin 3.5 minutes.
  Locally (GCC 16, one file): `play.cpp` 3.3 → 1.0 GB, `session.cpp`
  1.7 → 0.3 GB, copying a `Message` alone 1.5 GB → nothing. CI's build
  report (`.github/actions/build-report`) lists the heaviest compiles of
  each run.
