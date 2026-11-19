# The autopilot: our V played without a human

The real game's V walks, drives, fights, hacks and takes down on its own,
so that gameplay recordings (`.tplay`) and host sessions (`.tsession`) are
made without anyone at the controls, and scenarios test the real V acting
while the mock player (or a replayed session) plays the other side.

**State on 2026-11-19: built and tested offline, never run in the game.**
The game machine was in use. Everything below that touches the game is to
be proven by the [first-run checklist](#first-run-checklist); the engine
names it relies on are tagged in
[`docs/research/engine/autopilot.md`](../research/engine/autopilot.md).

## Layers

```
 laptop                              game machine (next to the game)
 tools/game.sh pilot run ROUTINE ──► ~/.cache/tandem/pilot/INSTANCE/tandem-pilot
 tools/scenario.sh: pilot ROUTINE         │  reads   red4ext/plugins/tandem/pilot.state   (20 Hz)
                                          │  writes  red4ext/plugins/tandem/tandem.cmd    (pilot N ACTION)
                                          │  tails   red4ext/logs/tandem-*.log
                                          └─ holds   a uinput Xbox 360 pad (only while it runs)
                                     the game: scripts/tandem/Pilot.reds, plugin/src/pilot_natives.cpp
```

1. **Script-level actions** (`scripts/tandem/Pilot.reds`): what the game
   lets a script make V do through its own gameplay path. No device,
   deterministic, each acknowledged. Preferred whenever it exists.
2. **A virtual gamepad** (`tools/pilot/internal/pad`): for what no script
   can do: walking, jumping, driving by hand, melee, dialogue choices. A
   uinput Xbox 360 pad: no keyboard layout, analog sticks, and invisible
   to the compositor (it cannot type into a window).
3. **A closed loop** (`tools/pilot/internal/routine`): the plugin writes
   V's state twenty times a second; the pilot, running on the game machine
   itself, steers on it: the left stick points at the waypoint in the
   view's frame, the right stick turns the view until it is on the target,
   every action is confirmed on the state or the game's log, every wait
   has a time limit.
4. **Routines** (`tools/pilot/routines/*.pilot`): small scripts of steps
   with checks.

### Which layer does what

| Action                                   | Layer                                           |
| ---------------------------------------- | ----------------------------------------------- |
| walk, sprint, crouch, jump, dodge, slide | pad (`walk`, `press`, `stick`)                  |
| look, aim                                | script (`act look`), else pad (`face`)          |
| draw, holster                            | script (`act equip`)                            |
| fire                                     | pad's trigger; script (`act fire`) once proven  |
| reload, melee, grenade                   | pad                                             |
| quickhack                                | script (`act hack`)                             |
| grab, takedown                           | script (`act takedown`)                         |
| Sandevistan / operating system           | script (`act ability`), else pad (LB+RB)        |
| heal, lose health, go down               | script (`act heal`, `act hurt`)                 |
| call the car, get in, get out            | script (`act summon`, `act mount`, `act exit`)  |
| drive                                    | script (`act autodrive on`), else pad (`drive`) |
| door, terminal, lift                     | script (`act device`, `act lift`), else pad's X |
| dialogue choice                          | pad only (`choose`)                             |
| answer a call, loot, buy                 | script (`act call`, `act loot`, `act vendor`)   |
| teleport, spawn test NPCs, record        | existing tandem commands (`cmd …`)              |

## The plugin's side

`pilot on` (a tandem command, like every `tools/game.sh cmd`) turns the
pilot on: `TandemSystem` then ticks it every frame, takes commands every
frame instead of twice a second, and saving is locked. `pilot off`, a
test reset, or **3 s without `pilot beat`** turn it off: firing and
looking stop, the state file is removed. It works with or without a
session.

**The state**: one line of `key=value` words in
`red4ext/plugins/tandem/pilot.state`, written whole (a temporary file,
then a rename) at 20 Hz.

| Field                                               | Meaning                                                                                          |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `seq`, `t`                                          | sample number (grows), real time in seconds                                                      |
| `pilot`, `ack`, `res`                               | on; the last action's number and its answer (`ok…`, `fail:REASON`)                               |
| `x y z`                                             | V's position                                                                                     |
| `fx fy fz`, `cx cy cz`                              | where the view looks (unit vector) and from where                                                |
| `hx hy`, `speed`                                    | heading of V's body, or of the vehicle; speed in m/s                                             |
| `veh`, `seat`, `vstate`                             | in a vehicle; `driver`, `passenger`, `none`; the vehicle state                                   |
| `hp`                                                | health, percent                                                                                  |
| `loco`, `detail`, `crouch`, `sprint`, `air`, `swim` | how V moves                                                                                      |
| `combat`, `ads`, `weapon`, `firing`                 | in combat, aiming, a weapon in hand, the pilot's trigger held                                    |
| `tier`, `menu`, `takedown`, `carry`                 | scene tier, a menu open, grappling, carrying a body                                              |
| `dead`, `down`, `dilated`                           | not alive, down (revive), time dilation active                                                   |
| `call`, `choices`, `sel`, `prompt`                  | 1 a call rings; dialogue choices on screen and the selected one; an interaction prompt's choices |
| `auto`, `autoavail`, `car`                          | AutoDrive on / available; metres to the called car (−1: none)                                    |
| `savelock`, `session`, `role`                       | saving locked; in a session; `host`, `guest`, `none`                                             |
| `npcs`, `hostile`                                   | living NPCs within 30 m, and the hostile ones                                                    |
| `tid`, `tx ty tz`, `td`, `thp`, `tdead`             | the target: where to aim, distance, health, down or dead                                         |

**The actions** (`pilot N ACTION ARGS`, answered by `ack=N`):
`target nearest|hostile [RANGE] | proxy | view | none`, `look target [S] |
X Y Z [S] | stop`, `equip weapon|slot1..slot4|fists|holster`,
`fire [S] | stop`, `reload`, `ability [sandevistan|off]`, `heal`,
`hurt PERCENT|all`, `hack [RECORD]`, `takedown grab|kill|nonlethal`,
`summon`, `mount [SLOT]`, `exit`, `autodrive on|off|roam`,
`device [ACTION]`, `lift FLOOR`, `call answer|reject`, `loot`,
`vendor list|buy`. `pilot probe` logs what could replace the pad one day
(the game's own functional-test input system) without calling it.

## The pilot (`tools/pilot`, Go, standard library only)

`tandem-pilot` is built on the laptop as a static binary, copied with the
routine to `~/.cache/tandem/pilot/INSTANCE/` on the game machine and run
there for the length of one routine. It is never installed: nothing in the
game's folder, no service, and the pad exists only while it runs.

```
tandem-pilot check ROUTINE...     parse
tandem-pilot dry ROUTINE...       play on the built-in simulator, printing every
                                  pad event and command it would send
tandem-pilot run [FLAGS] ROUTINE  play on the running game
tandem-pilot serve [FLAGS]        hold the pad between routines: "run" hands them over
tandem-pilot owner | state | pad [SECONDS]
  --pad uinput|none|dry   --focus require|off   --instance NAME   --product 0x028e
```

By default each `run` makes its pad and removes it. `serve`
(`tools/game.sh pilot start`) is for a game that only takes a pad it saw
when it launched: it holds the pad from before the game starts, plays the
routines `run` hands it through files in its cache folder
(`internal/spool`), with every guard of a single run each time, and ends on
`pilot stop`, on `tools/game.sh stop` or `role player`, or after 15 minutes
without a routine: it is never left behind.

`internal/sim` is a toy game (a V that walks where the stick points, a
view, a car, NPCs, a door, a call, dialogue choices, the plugin's pilot
with its answers and heartbeat): the interpreter's tests and every shipped
routine run on it (`task pilot:test`). It proves a routine's logic and the
control loops, not what the game does.

### The routine language

One step a line; `#` starts a comment. A failed step ends the routine
(`FAIL at line N: reason`) unless it is `optional` (a warning).

```
limit SECONDS                  the whole routine's time limit (300)
needs COND                     checked before the first step
origin X Y Z                   where ~ coordinates are measured from (default: where V stands)
sim FIXTURE                    the simulator's world for a dry run (ignored on the game)

cmd TEXT                       a tandem command (teleport, npc spawn, record start, …)
act ACTION [ARGS] [timeout S]  a script-level action; fails when the game answers fail:REASON
press BUTTON [SECONDS]         a b x y lb rb l3 r3 up down left right (0.1 s)
hold BUTTON... | release BUTTON... | release all
stick left|right X Y [SECONDS] -1..1, y forward/up; stays until changed without SECONDS
trigger lt|rt VALUE [SECONDS]  0..1
walk POS [within R] [pace walk|jog|sprint] [timeout S]   on foot; a jump when stuck, then a failure
drive POS [within R] [speed MPS] [timeout S]             at the wheel; brakes to a stop there
face POS [tolerance DEG] [timeout S]                     the view, by the right stick
choose NUMBER [timeout S]      a dialogue choice: d-pad to it, X, the choices must go
calibrate [move|look|steer]    proves the pad reaches the game; learns reversed axes
wait SECONDS | until COND [timeout S] | check COND
expect REGEX [timeout S] | reject REGEX | mark    the game's log since the start (or the mark)
show | say TEXT                the state line, or a note, in the pilot's log
optional STEP                  a failure is a warning
if COND / else / end, repeat COUNT / end

POS:  X Y [Z] | ~DX ~DY [~DZ] (from the origin) | target
COND: KEY OP VALUE [and …] (== != < <= > >=; a word with == or !=) | near POS RADIUS
```

Start, Back and Guide are not in the language: the pilot cannot open the
pause menu, the map or Steam's overlay.

### Routines shipped

| Routine                              | What                                                        | Needs                             |
| ------------------------------------ | ----------------------------------------------------------- | --------------------------------- |
| `state`                              | first contact: the state line twice, saving locked          | no pad                            |
| `script-actions`                     | every script-level action once, all optional: a report      | no pad                            |
| `calibrate`                          | the pad reaches the game; axis directions                   |                                   |
| `survey`                             | 10 m out and back along each axis: where the ground is open |                                   |
| `street`                             | walk, jog, sprint, crouch, back                             | open ground (survey)              |
| `movement`                           | jumps, double jump, dodges, a slide                         |                                   |
| `drive`                              | call the car, get in, AutoDrive or the pad, stop, get out   | a road nearby                     |
| `fight`                              | gun, quickhack, fists on a hostile NPC (a proxy as a guest) |                                   |
| `stealth`, `grab`                    | crouch up to an NPC, non-lethal takedown; grab then kill    |                                   |
| `sandevistan`                        | the operating system on, a sprint in slowed time            | V wears one                       |
| `down`                               | health to nothing, revived by the other player              | a session, `tandem-peer --revive` |
| `call`                               | a test call comes in, V answers                             | a session                         |
| `door`, `lift`, `vendor`, `dialogue` | the device, vendor or conversation in front of V            | V placed there first              |

## Safety

- **Never on a game someone else started.** `tools/game.sh start` writes
  the game's pid and kernel start time to
  `~/.cache/tandem/pilot/INSTANCE/game.owner` on the game machine; `stop`
  removes it. `tandem-pilot run` finds the running game process and
  refuses unless it is exactly that one; it checks again every second. A
  game the player launched has no such file.
- **One pilot per instance**: a lock file (`flock`) in the same folder; it
  cannot outlive its process.
- **It never saves.** From the first `pilot on`, saving is locked until
  the game exits, three ways: a flag in the plugin that
  `TandemSystem.IsSavingLocked` answers from (it survives loaded saves),
  the game's `SaveLocksManager` (asked again every 5 s), and pending
  autosaves invalidated. The pad has no button that opens a menu. And the
  pilot lists the save folder before and after a run: any file written,
  changed or removed there fails the run loudly (`SAVE …`). A game the
  pilot has played in cannot save any more: stop it (`tools/game.sh stop`)
  before anyone plays.
- **It lets go.** Every exit path releases everything: the end of a
  routine, a failure, a panic, `SIGINT`/`SIGTERM`/`SIGHUP` (`ssh -tt`:
  Ctrl-C on the laptop or a lost connection is a hang-up there). A
  dead-man timer releases the pad when the control loop has not run for
  0.5 s; the loop itself releases it when the game's state stops for 1.5 s
  (a loading screen, a pause, a crash) and fails after 90 s. Killed
  outright, the kernel removes the device: the game sees the pad unplugged.
  The plugin's own dead-man (3 s without heartbeat) stops firing and
  looking.
- **It cannot type elsewhere.** Only a gamepad: the compositor does not
  route it to windows. Because Steam can route a pad to its own interface
  when the game is not in front, the game's window must have the focus
  (asked of sway once, checked every second; `--focus off` to measure
  without).

## In the workflow

```
tools/game.sh pilot check                 every routine parses
tools/game.sh pilot dry street            on the simulator, here
tools/game.sh pilot run street            on the game's V
tools/game.sh pilot record street street  recordings/play/street.tplay, no human
tools/game.sh pilot session city street   recordings/session/city.tsession (the recording guest runs here)
tools/game.sh pilot start [FLAGS]          a pilot that keeps its pad between routines (see above)
tools/game.sh pilot stop | status | pad [SECONDS]
PILOT_FLAGS='--pad none --focus off' tools/game.sh pilot run script-actions
```

In a scenario, `pilot ROUTINE` plays a routine on the real V while the
mock player started before it plays the other side; a scenario that uses
it carries `# needs the pilot` and `tools/scenario.sh all` leaves it out
unless `PILOT=1` (`pilot-street`, `pilot-fight`, `pilot-takedown`).

The recordings the two manual sessions of `docs/ARCHITECTURE.md` ask a
player for map to: `pilot record street street`, `pilot record movement
movement`, `pilot record drive drive`, and, with the game as a guest of
`tandem-peer host --replay recordings/npcs.rec`, `pilot record fight
fight`.

## Two real games

Planned, not built: each instance has its own pilot, and nothing in the
pilot is global. `PILOT_INSTANCE` names the instance: its own cache folder
(lock, owner file), its own game folder (`--game-dir`: state, commands and
log are that game's), its own pad with its own USB product id
(`--product`), and the game process is matched by its folder. What is
left for the day two instances run: `tools/game.sh` drives one game
(`GAME_DIR`, one `pgrep`), each instance must be told to read only its pad
(`SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT`), and only one window has the
focus (research §14.2.5).

## First-run checklist

For the first time the game machine is free. In this order: each step
needs the ones before it. Stop at the first surprise and write it down in
`docs/research/engine/autopilot.md`.

**0. Before anything**

- Nobody plays: `ssh anya 'pgrep -af Cyberpunk2077.exe'` prints nothing.
- A copy of the saves, once (the safety net if a save lock does not hold):
  `ssh anya 'cp -a "/data/Steam/steamapps/compatdata/1091500/pfx/drive_c/users/steamuser/Saved Games/CD Projekt Red/Cyberpunk 2077" ~/.cache/tandem/saves-before-pilot'`.
  If that path does not exist, find the real one
  (`find /data/Steam -name 'sav.dat' | head`) and pass it to every run as
  `PILOT_FLAGS='--saves DIR'`: otherwise the pilot warns that saves are not
  watched.
- `tools/build.sh build:plugin test`, `task pilot:test`, then
  `tools/game.sh ensure host` (it deploys this build and starts the game).
  - `redscript compile errors` → the scripts do not compile against the
    game as it is now (a mod changed): `task scripts:check`.

**1. The game is ours** — `tools/game.sh pilot status`

- Expect `no pilot runs`, `the running game (pid N) was started by our
tools`, `no pilot state`.
- `was not started by tools/game.sh` → `start` did not find the process:
  compare `ssh anya 'pgrep -af Cyberpunk2077.exe'` with the pattern in
  `tools/game.sh` (`own`) and `tools/pilot/internal/guard` (`isGame`).

**2. The probe** — `tools/game.sh cmd pilot probe`, then read its lines:
run `tools/game.sh log 200 | grep 'pilot probe'`.

- Copy the lines into the research notes (§14.1.2). `FunctionalTestsGameSystem
… instance present` would mean input with no device at all is possible:
  the next thing to build. `instance absent` → the pad stays.
- No `pilot probe` line at all → the DLL or scripts are not this build
  (`tools/game.sh log`, `r6/logs/redscript_rCURRENT.log`).

**3. The state** — `PILOT_FLAGS='--pad none --focus off' tools/game.sh pilot run state`

- Expect two `state:` lines with `seq` growing, V's position, a unit
  `fx fy fz`, and `PASS`.
- `no pilot state from the plugin within 15 s` → on anya, look in
  `red4ext/plugins/tandem/`: only `pilot.state.tmp` → the rename fails
  under Wine (write the file in place in `pilot_natives.cpp`); neither
  file → `pilot on` was not taken (a menu is open: the script tick is
  paused) or the script threw (tandem's log).
- `savelock is 0` → neither lock holds: **stop**, do not go on to steps
  that change the world.
- `SAVE …` lines → the game wrote a save during those seconds: stop, and
  restore from the copy if a wanted save rotated out.

**4. Script-level actions** — `PILOT_FLAGS='--pad none --focus off' tools/game.sh pilot run script-actions`

- Every `WARN` is one action that does not work as read: note which, with
  the game's answer (`the game refused: …`) or the check that timed out.
  What each means:
  - `act look` ok but `face target` warns → `TargetingSystem.LookAt` does
    not turn the view outside a scene: aiming falls back to the pad (`face`).
  - `act fire` ok but `thp` does not drop → `QuestForceShoot` does not fire
    from script: firing stays the pad's trigger (the routines already do).
  - `act hack` fails or no `hacks: our upload` → deck, RAM, or the event
    does not run for our V on its own NPCs.
  - `act ability` refused → V wears no operating system, or it is on
    cooldown.
  - `act summon` / `until car` → no car to call, or the blackboard's
    entity is not the called car.
  - `act mount` ok but `veh` stays 0 → the animated mount needs more
    (compare with `TandemVehicles.Seat`, instant).
  - `act autodrive on` refused → not on a road, or not V's own car.
- The game freezes → `tools/game.sh bt` before anything else.

**5. The pad exists** — `tools/game.sh pilot pad 20`, and meanwhile, on
the game machine: `ssh anya 'grep -B2 -A8 "X-Box 360" /proc/bus/input/devices'`.

- Expect the device with `Handlers=eventN jsN`, and Steam noticing a
  controller. Nothing is pressed.
- `open /dev/uinput: permission denied` → the ssh session lacks the
  `uinput` group (`ssh anya id`).

**6. The pad reaches the game** — `tools/game.sh pilot run calibrate`

- Expect V to step right, then forward, the view to turn right and up and
  back, `PASS`, and the game's glyphs switching to a gamepad.
- `the focus is on steam` (or another class) → the window is not what the
  pilot expects: `ssh anya 'swaymsg -t get_tree'` (with the session's
  `SWAYSOCK`) for the game's class, then `PILOT_FLAGS='--focus-class NAME'`.
- `swaymsg: … SWAYSOCK` → the session's environment was not found
  (`session_env` in `tools/game.sh`).
- `the left stick did not move V: the game does not read the pad` → in
  this order: (a) the pad may have to exist when the game launches:
  `tools/game.sh stop`, `tools/game.sh pilot start`, `tools/game.sh ensure
host`, then `calibrate` again (it now goes through the serving pilot,
  whose pad the game saw at launch); if that works, always `pilot start`
  before the game; (b) Steam Input: see what Steam shows for the pad, and
  try it off for the game; (c) `PROTON_PREFER_SDL=1` in the game's launch
  options. The focus is not the cause here: it was checked.
- `moved V sideways of it` → the view vector or the stick axes are not
  what the pilot assumes (`control.go`: right is `(fy, -fx)`).
- Steam's overlay or Big Picture reacted → stop; the pad is being routed
  to Steam (focus, or Steam Input's desktop layout).

**7. The dead-man** — `tools/game.sh pilot run street`, and during a walk:

- Ctrl-C here → V stops within half a second; the log ends `FAIL … stopped`.
- Again, and `ssh anya 'pkill -KILL tandem-pilot'` → V stops (the kernel
  removed the pad); 3 s later tandem's log says `pilot: off (no heartbeat
for 3 s)`.
- Again, and `tools/game.sh pilot stop` from another terminal.
- V keeps walking after any of these → **stop using the pad** until it is
  understood (Steam Input may hold the last stick value of a vanished pad).

**8. Refusal** — `ssh anya 'mv ~/.cache/tandem/pilot/main/game.owner /tmp/'`,
then `tools/game.sh pilot run calibrate` must refuse (`was not started by
tools/game.sh`); move the file back.

**9. Walking** — `tools/game.sh pilot run survey`, then `street` and
`movement` the same way.

- `stuck at …` → the ground is not open there: move the waypoints
  (`survey`'s log says which directions are free).
- `crouch is 0`, `air is 0` → the button is not the default (the player's
  bindings) or the state field does not mean what it should: `show`.

**10. Recording** — with the game as host: `tools/game.sh pilot record
street street`, then `build/linux/peer/tandem-peer play-info
recordings/play/street.tplay`, then `tools/scenario.sh front`.

**11. The rest**, one at a time: `sandevistan`; `drive` (first on a road:
teleport there); with the game as a guest of the replay host, `fight`,
`stealth`, `grab`; with V placed in front of each, `door`, `lift`,
`vendor`, `dialogue`; in a session with `tandem-peer --revive 5`, `down`;
`call`. Then `PILOT=1 tools/scenario.sh pilot-street pilot-fight
pilot-takedown`, and `tools/game.sh pilot session city street`.

**12. After** — every run's last lines carry no `SAVE`; `tools/game.sh
stop`; `tools/game.sh role player`; compare the save folder with the copy
of step 0 once.

## Risks

- **Every engine name is untested** (step 4 exists for that). The least
  certain: script firing, the look-at outside scenes, the lift's panel as
  a look-at object, the second takedown event from a grab.
- **Steam Input** may keep the pad from the game or route it to Steam's
  own interface; the focus requirement and the absent menu buttons bound
  the damage, step 6 measures it.
- **Autosaves** are native: that they honour script save locks is read,
  not proven. The save-folder comparison detects a failure, it does not
  prevent one: hence the copy in step 0.
- **A test changes the loaded world** (ammo, money, a dead ganger, a car
  called): never saved, gone with the next load, but visible to whoever
  watches the stream.
- **Waypoints are relative to the test field** and unmeasured: `survey`
  first.
- **The simulator proves the interpreter, not the game.** A routine that
  passes there can fail on the first real step.
