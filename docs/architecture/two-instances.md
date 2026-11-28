# Two games on one machine

Can two copies of Cyberpunk 2077 run on anya at once, one hosting and one
joining over loopback, so that tests put two real games against each other?

**Status: tried on anya (2026-11-19), steps 0 to 8 of the
[checklist](#first-try-lowest-risk-first).** One instance plays a save and
hosts the mock player; two instances run at once and join over loopback,
but of four starts of the guest one hung at start and one froze right
after loading (see
[Results](#results-on-anya-2026-11-19)). Every claim below carries a tag:

- `[sourced]` — read in documentation or source code, linked.
- `[reported]` — users say so (forums, a project's README), not confirmed.
- `[inferred]` — follows from how Wine, Proton or the engine work.
- `[offline]` — checked here, on the fake tree or in the unit tests.
- `[anya]` — seen on anya, 2026-11-19 (patch 2.5, Proton CachyOS 11.0
  through the Steam Linux Runtime 4, gamescope 3.17, an RX 6800 XT).
- `[unknown]` — needs a try on anya; ranked in [Open unknowns](#open-unknowns).

## Verdict

**Likely feasible.** Nothing found forbids it, and the pieces exist:

- The game has no known single-instance guard, and a guard built on Windows
  objects could not see across two Wine prefixes anyway.
- The Steam build runs without the Steam client, so the second game need not
  go through Steam, whose client would refuse a second launch of the app.
- What two games would fight over is all files, and all of it is solved by
  giving each instance its own game folder and its own prefix.
- Split-screen launchers on Linux (PartyDeck) already run several copies of
  one Windows game this way: gamescope, bubblewrap, Proton outside Steam, a
  prefix per player.

What decides it in practice is capacity (two games' memory, VRAM and CPU on
one machine) and a handful of launch details that only a try will settle.

## Results on anya (2026-11-19)

**Works** `[anya]`:

- **One instance, no tandem** (step 4): Proton CachyOS outside Steam,
  through the Steam Linux Runtime 4 and `steam-run`, inside bubblewrap and a
  nested gamescope, from a game folder whose archives are symbolic links.
  The save loads; every mod loads (13 RED4ext plugins, redscript compiles
  with no error); Steam's `content_log.txt` logs no "App Running" for any
  start (it does for the player's own launches); nothing of the player's
  changes (the `find` of step 2 prints nothing after every step), the cloud
  folder included.
- **The mark**: `TANDEM_INSTANCE_ID` reaches every process, the game's
  included (its name in `/proc` is `GameThread`, not `Cyberpunk2077.exe`);
  `status`, `stop` and the foreign-game check work on it.
- **tandem in an instance** (step 5): deployed with
  `GAME_DIR=… task deploy:files`; `b` hosts the mock player from the laptop
  (`tandem-peer join anya --mode front`): the puppet walks, `cmd b where`
  is taken.
- **Two instances over loopback** (step 6): `a` hosts, `b` joins
  `127.0.0.1:27450`: rtt 4 to 8 ms, no loss, each V sees the other (puppet,
  marker), the host's story reaches the guest (Panam's call on both).
- **Input per instance** (step 7): `tools/instance.sh xdo b key c` crouches
  b's V only (a's log sees player 3 crouch, b's log sees no change of a's),
  and the other way round. A click works the same way (Continue on the
  menu); keys alone did not pick Continue (Return took the item under the
  pointer: New Game).
- **Headless** (step 8): `INSTANCE_DISPLAY=headless` (gamescope's headless
  backend: no window in Sway) renders: `shot` takes the picture, `xdo`
  reaches the game, `load` gets it in, it joined a's session (rtt 2 to 3 ms)
  and tandem's watchdog stayed quiet. The pointer-only report of
  [gamescope #1984](https://github.com/ValveSoftware/gamescope/issues/1984)
  did not happen here.
- **What the launch needed**, all in `tools/instance.sh` now: the game's
  `bin/x64/GameServicesSteam.dll` left out of the copy (with it and no Steam
  client the game exits at start or hangs on its logo); the Steam Linux
  Runtime told the instance's folders (`STEAM_COMPAT_INSTALL_PATH`,
  `PRESSURE_VESSEL_FILESYSTEMS_RW/RO`, else the game quits without a word);
  `UMU_ID=0` and `PROTONFIXES_DISABLE=1` for Proton CachyOS without an app
  id; gamescope, bubblewrap, xdotool and jq built from nixpkgs per instance
  (`run/nix-*`), nothing installed on the system. The archives as symbolic
  links load (ext4: no reflinks), and the copied prefix runs every mod.
- **The game's resolution must be gamescope's**: the player's
  `UserSettings.json` says 1152x720, which the game kept inside a 1280x720
  gamescope, a window 64 pixels in (clicks offset). `settings` now writes
  `Resolution` from `INSTANCE_SIZE` (it also failed before: jq is a
  multi-output package, its link is `run/nix-jq-bin`).
- **From launch to a loaded save** without a key from anywhere else:
  `tools/instance.sh load NAME` (Load Begone, then a click on Continue, then
  Space at the loading screen, as tandem's log tells).

**Blocks or bites** `[anya]`:

- **The guest froze once right after "playing"** (b joining a, its second
  start of four): every thread of the game waits on its wineserver
  (`anon_pipe_read`), 4% CPU, its log and net thread silent, so the host
  drops it after 5 s ("b left the session"). Not tandem's watchdog line
  either: the whole process stopped. Stacks in b's `run/bt.txt`
  (`tools/instance.sh bt b`); no tandem frame in them but its idle net
  thread. The next start of b, same roles, played. Cause unknown: the
  machine was swapping at the time (below).
- **Starting under load trips the engine's watchdog**: one start of b (with
  a running and builds on all cores) stalled between the script compile and
  the audio's start for over 120 s: `Watchdog timeout!` (engineWatchdog.cpp
  224), a minidump in the instance's `ReportQueue`, the process left hanging
  (the crash handler waits under Proton). The same phase takes about 50 s
  alone and 2 min under load. b also hung once this way at its very first
  start (the previous session, 12:39). 2 of about 10 starts.
- **CPU: builds beat the games.** Other agents' builds on anya (three slots
  at `-j5`, niced) run in ssh session scopes; the instances run in the user
  manager's scopes. `nice` does not reach across cgroups, so the builds took
  most of the CPU (pressure 72%) and both games fell to 9-10 fps, the GPU
  10% busy. The scope's `CPUWeight=50` made it worse (now 100,
  `INSTANCE_CPU_WEIGHT`): set to 400 on the running scopes, they rose to
  13 and 17 fps at once, then 20 and 29 as the builds wound down (the two
  effects not separated). A build slot that puts its build in a scope of low weight
  (`systemd-run --user --scope -p CPUWeight=10`) would fix it at the source.
- **Memory**: two games and the builds filled the 31 GB: 4 GB swapped, the
  host's resident set fell from 6.4 to 2 GB. Two games alone fit: about
  13 GB resident for both.

**Measured** `[anya]` (720p, textures Low, 30 fps cap; idle machine: 5.7 GB
of RAM used, 1.4 GB of VRAM):

| What                                | one game, no tandem, textures High, 1152x720 | one game, tandem host + mock player | two games (each), builds running       |
| ----------------------------------- | -------------------------------------------- | ----------------------------------- | -------------------------------------- |
| game resident memory                | 6.4 GB                                       | 6.5 GB                              | 6.4 GB (b); a swapped to 2 to 3.3      |
| scope memory (with page cache)      | 7.6 GB                                       | 7.4 GB                              | 7.8 GB (b)                             |
| machine RAM used (over idle)        | +6.4 GB                                      | not separable (builds)              | 27 GB in all with builds, 4 GB swapped |
| VRAM (amdgpu, the game's clients)   | 6.3 GB                                       | 3.2 GB                              | 3.5 GB; 8.4 GB used in all             |
| CPU (5 s)                           | 3.9 cores                                    | 2.4 cores                           | 3.2 and 4.1 cores                      |
| frame rate (Wine's present counter) | 30                                           | 29-30                               | 9-10, then 20 and 29 (see above)       |
| start to the menu                   | 35 s                                         | 40 s                                | 3.5 min under load                     |
| start to "playing"                  | not timed                                    | not timed                           | 4.5 to 5.5 min under load              |

The VRAM is the textures: High at 1152x720 took 6.3 GB, Low at 1280x720
3.2 GB. Two games fit the 16 GB card with room; RAM fits too without the
builds. Frame rates come from `WINEDEBUG=-all,+fps` (Wine logs each present
in `run/launch.log`; `tools/instance.sh stats` reads the last).

## Findings

### 1. Single-instance guards

**The game.** No source describes a mutex or lock file that stops a second
`Cyberpunk2077.exe`. A fork of CyberpunkMP documents "Test with two games on
one PC": a script starts a server and two game processes from the same
executable on Windows, and its only warning is that both share the save
folder ([oxylol/cp2077-coop](https://github.com/oxylol/cp2077-coop), for
patch 2.31; the README does not say the setup was verified) `[reported]`.

Even if a guard existed: Windows named objects (mutexes, events) live in the
wineserver, and each Wine prefix has its own wineserver. Two games in two
prefixes cannot see each other's objects `[inferred]`. A guard through a
lock on a shared file is the one kind that would cross prefixes; separate
game folders remove the shared files.

**Steam.** The client tracks an app as running and does not start it twice
`[inferred]` (general Steam behaviour, not tried). It does not matter: the
second game is not launched through Steam.

**DRM.** The Steam build is listed as DRM-free once installed, and runs from
`bin/x64/Cyberpunk2077.exe` with the client closed; what is lost without
REDprelauncher is the rewards DLC check and CDPR's cross-progression sync
([PCGamingWiki](https://www.pcgamingwiki.com/wiki/Cyberpunk_2077)) `[sourced]`.
Steam forum posts say the same, with one dissenting report `[reported]`.
Steam's part is the Steamworks API (cloud, achievements), not a wrapper.

**Steam identity.** A game launched outside Steam still signs in to a
running client when it finds an app id: `SteamAppId` / `SteamGameId` in the
environment, or `steam_appid.txt` next to the executable; the client then
shows it as running and counts playtime `[reported]`. So the instance is
started with those variables removed, its copy of the game folder has no
`steam_appid.txt`, and the Steam client's own folder (`~/.steam`) is hidden
from it. With no way to reach the client, there are no achievements, no
cloud and no "running" state `[inferred]`; on anya Steam logged no "App
Running" for any instance start `[anya]`. The game also needs its
`GameServicesSteam.dll` gone: with it and no client it exits or hangs on
its logo `[anya]`.

**Launching Proton without Steam.** The `proton` script runs by hand with
`STEAM_COMPAT_DATA_PATH` (any folder: the prefix) and the client's folder in
`STEAM_COMPAT_CLIENT_INSTALL_PATH`, as these show:
[gist](https://gist.github.com/michaelbutler/f364276f4030c5f449252f2c4d960bd2),
[protonlaunch FAQ](https://github.com/h0m3/protonlaunch/wiki/FAQ)
`[reported]`. [umu-launcher](https://github.com/Open-Wine-Components/umu-launcher)
packages the same thing (`WINEPREFIX=… PROTONPATH=… GAMEID=… umu-run game.exe`,
inside the Steam Linux Runtime, no Steam needed) `[sourced]`. Two programs
in one prefix is the disputed case; one prefix per game is not.

RED4ext is a `winmm.dll` next to the executable, the ASI loader a
`version.dll`: Wine must be told to prefer them
(`WINEDLLOVERRIDES=winmm,version=n,b`), which the player has in Steam's
launch options and the instance must set itself `[inferred]`.

### 2. What two games would share

In the game folder (found in tandem's own notes and in redscript's and
RED4ext's sources):

| Path                                                         | Written by                 | Shared by two games in one folder                                                                                                                                                                        |
| ------------------------------------------------------------ | -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `r6/cache/final.redscripts.modded`, `.ts`, `.bk`             | redscript, at every launch | the compiler takes a write lock on `final.redscripts.ts` ([scc/lib/src/lib.rs](https://github.com/jac3km4/redscript)): two launches queue, then both rewrite the bundle the other is loading `[sourced]` |
| `red4ext/redscript_paths.txt`                                | RED4ext, at every launch   | rewritten ([RED4ext #165](https://github.com/wopss/RED4ext/issues/165) shows the command line) `[sourced]`                                                                                               |
| `r6/logs/redscript_rCURRENT.log`                             | redscript                  | one log for both; `tools/game.sh` reads it for compile errors                                                                                                                                            |
| `red4ext/logs/*.log`                                         | RED4ext and every plugin   | names carry the start time: two starts in one second collide; the tools take "the newest tandem log"                                                                                                     |
| `red4ext/plugins/tandem/tandem.ini`                          | us                         | **the role**: one file cannot say host and join                                                                                                                                                          |
| `red4ext/plugins/tandem/tandem.cmd`                          | us                         | the command file: whichever game polls first takes the command                                                                                                                                           |
| `red4ext/plugins/tandem/recordings/`, `recent.txt`           | tandem                     | mixed                                                                                                                                                                                                    |
| other mods' state (Mod Settings' `user.ini`, CET's files, …) | the player's mods          | last writer wins                                                                                                                                                                                         |
| `archive/**`                                                 | nobody (read only)         | safe to share: most of the game's size                                                                                                                                                                   |

In the prefix (`compatdata/1091500/pfx/drive_c/users/steamuser/`): the saves
(`Saved Games/CD Projekt Red/Cyberpunk 2077/`), the settings
(`AppData/Local/CD Projekt Red/Cyberpunk 2077/UserSettings.json`), crash
reports (`AppData/Local/REDEngine/ReportQueue/`). The cp2077-coop fork's
warning is exactly this: two games in one prefix save into one folder.

On the machine: tandem's UDP port. The host binds 27450; a guest sends from
a port the system picks, so a host and a guest do not collide. Two hosts
would: `port` in `tandem.ini`, or `TANDEM_PORT`.

**Conclusion: a shared game folder cannot work** (the role and the script
cache alone rule it out). Each instance gets its own folder and prefix.

### 3. Resources

Measured on anya: [Results](#results-on-anya-2026-11-19). What was known
before:

- CD Projekt's minimum for 2.x is 12 GB of RAM and a 6 GB card for 1080p low
  at 30 fps
  ([requirements](https://www.cyberpunk.net/en/news/48271/update-to-pc-system-requirements))
  `[sourced]`. Two games at 720p low, 30 fps: plan **8 to 10 GB of RAM and 3
  to 5 GB of VRAM each** `[unknown]`, on a machine with 31 GB and one GPU.
  Two test games fit on paper; two test games plus the player's own do not,
  which is one more reason `tools/instance.sh` refuses while the player's
  game runs.
- CPU: the engine spreads over the cores it finds. Two games on 12 cores
  depend on the frame cap `[unknown]`.
- Frame cap: `VKD3D_FRAME_RATE=30` (the game is Direct3D 12; vkd3d-proton
  reads it, [README](https://github.com/HansKristian-Work/vkd3d-proton))
  `[sourced]`, and gamescope's `--framerate-limit`.
- Resolution: gamescope gives the game a screen of the size asked for
  (`-w 1280 -h 720 -W 1280 -H 720`), so "fullscreen" is 720p whatever
  `UserSettings.json` says. The settings file can be edited (options found
  by `name`: `Resolution`, `MaximumFPS_Value`…;
  [example](https://gist.github.com/MoreOrLessSoftware/746c841ae1804e4947779fe400b0837c)),
  and PCGamingWiki lists `-windowed -width -height -fpsClamp` launch
  arguments `[reported]`; simpler is to set low graphics by hand once in each
  instance: it keeps its own settings.
- A memory limit per instance (`systemd-run --user --scope -p MemoryMax=14G`)
  makes a leaking game die alone.

Reading anya's GPU, when allowed (read only; not run):

```sh
lspci -nnk | grep -A3 -E 'VGA|3D'            # which GPU, which driver
nvidia-smi --query-gpu=name,memory.total,memory.used,utilization.gpu --format=csv
nvidia-smi --query-compute-apps=pid,used_memory --format=csv   # per game
cat /sys/class/drm/card*/device/mem_info_vram_total             # AMD, bytes
cat /sys/class/drm/card*/device/mem_info_vram_used
vulkaninfo --summary                                            # the Vulkan device
free -m; nproc; df -h /data; stat -f -c %T /data                # RAM, cores, room, filesystem
```

**Displays.** anya's Sway session already runs on a headless output for
Sunshine (`tools/game.sh`). Options, simplest first:

1. **A nested gamescope per instance** (a normal window in Sway). The game
   sees one screen of its own and believes it is focused and fullscreen at
   all times. Recommended.
2. **`gamescope --backend headless`**: no window at all. One report of the
   headless backend rendering only the pointer
   ([gamescope #1984](https://github.com/ValveSoftware/gamescope/issues/1984))
   `[reported]`: try after option 1 works.
3. **A second headless output** in Sway (`swaymsg create_output`), a
   workspace per instance. Without gamescope, focus is Sway's: one game has
   the keyboard, and what the unfocused game does (pause? keep rendering?)
   is `[unknown]`.

The windows appear in the session Sunshine streams: put them on a workspace
of their own.

### 4. Input for two bots

- **Best: no OS input.** tandem's command file (`tandem.cmd`) and scripts
  drive a game from inside, per instance, whatever has focus. Load Begone
  gets each game from launch to the main menu without a key; Continue
  takes one click (`tools/instance.sh load`) `[anya]`.
- **Keyboard and mouse: through gamescope's own X server.** Each nested
  gamescope runs an Xwayland of its own (`DISPLAY=:1`, `:2`…); XTEST events
  sent there (`DISPLAY=:2 xdotool key …`) reach that game only, focused in
  Sway or not ([foxclick](https://github.com/samarslan/foxclick),
  [pindu-launcher #423](https://github.com/Tim-van-Oudheusden/pindu-launcher/issues/423))
  `[reported]`; on anya keys and clicks reach that game only, the pointer in
  the game window's coordinates `[anya]`. `ydotool` (what `tools/game.sh key` uses) is a kernel input
  device: it goes to whatever Sway has focused, one game at a time, and to
  the player's own windows. Not usable for two.
- **Gamepads: one uinput device per bot**, each game seeing only its own.
  Gamescope cannot select devices yet
  ([gamescope #1771](https://github.com/ValveSoftware/gamescope/issues/1771));
  the launchers that do this hide the other devices with bubblewrap (a tmpfs
  over `/dev/input`, the instance's own nodes bound back:
  [PartyDeck](https://github.com/partydeck/partydeck),
  [SplitScreenWrapper](https://github.com/dscrawford/SplitScreenWrapper))
  `[sourced]`. Pads are read from the device, not through window focus, and
  unfocused gamescope windows keep receiving them (PartyDeck's README).
  `tools/instance.sh` already wraps the game in bubblewrap: the device
  binds are a few more arguments, left for when a bot needs a pad.
- Without gamescope, Wayland focus decides: only the focused game gets keys.
- The test pilot uses the same path: `PILOT_INSTANCE=NAME tools/game.sh
pilot run ROUTINE` plays an instance's V by XTEST on that display
  (`autopilot.md`, "Two real games"), or by a pad only that instance reads
  (an SDL hint for now; the bubblewrap device binds above are the sturdier
  way).

### 5. Risks to the player's install

| Risk                                                     | How the setup removes it                                                                                                                                                                                                                  |
| -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A test game saves over the player's saves                | its own prefix, holding a copy of one save; the player's prefix is hidden from it (an empty tmpfs in its sandbox)                                                                                                                         |
| Steam cloud uploads a test save, or downloads over one   | not launched by Steam, no app id, the client's folder hidden; Steam's local cloud folder (`userdata/*/1091500`) hidden as well                                                                                                            |
| Achievements, playtime, "in game" status on the account  | the same: the game cannot reach the client                                                                                                                                                                                                |
| The script cache, logs or mod state of the player's game | the instance's game folder is a copy; the player's is mounted read only in the sandbox                                                                                                                                                    |
| A game update or a mod change while an instance is built | nothing is written to the player's folder; `instance.sh sync` refreshes the copy                                                                                                                                                          |
| The tools kill the player's game                         | `instance.sh` kills only processes carrying its mark, never by name; `tools/game.sh stop` kills the Steam game's unmarked processes by pid, no longer by name (`pkill -f Cyberpunk2077.exe`): [Sweeps on two games](#sweeps-on-two-games) |
| Two games starve the player's                            | every changing verb refuses while a game we did not start runs; a memory limit per instance; `stop` always works                                                                                                                          |
| A half-copied prefix                                     | the prefix is copied only while the player's game is not running                                                                                                                                                                          |

What the sandbox does **not** do: it is not a jail. The game can still write
elsewhere in the home directory (as any game can). It protects the three
things named: the install, the prefix, the cloud folder.

## A test game never saves

`tools/instance.sh role` and `tools/game.sh role` write `saves = off`: the
game locks saving from its first tick (`TandemSystem.LockTestSaves`, as the
pilot does) and logs `saves: off`. A hosting instance autosaved in the fight
that killed its V, and every start after loaded that save, dead
(2026-11-22: its two newest autosaves were moved to
`tandem-instances/a/run/saves-aside`). `tools/instance.sh load` asks
`test query health` once the save plays and fails when the V is dead or the
game takes no command (`playing (health 100)` otherwise). Resetting two
games in the same seconds left both Vs 28 m from the test field and failed
both resets: reset one, wait for `test reset: ready`, then the other.

## Recommended setup

```
/data/Steam/tandem-instances/NAME/     (TANDEM_INSTANCES; same filesystem as the game)
  .tandem-instance                     marks it as ours (destroy refuses without it); its pad
  game/                                the game folder: archive/ linked, the rest copied
  compatdata/                          its Proton prefix (STEAM_COMPAT_DATA_PATH)
  run/                                 pid, launch.log, shader cache
```

- **Game folder: a copy with the archives linked.** `archive/` (most of the
  game) is reflinked where the filesystem can (btrfs, xfs: free, independent
  copies), else a tree of symbolic links, one per file, into the player's
  install. Everything else (`bin`, `engine`, `r6`, `red4ext`, `tools`,
  `mods`) is copied: that is where games and mods write. The player's mods
  come along, so an instance runs what the player's game runs; its logs and
  its tandem are its own.
- **Prefix: a copy of the player's**, without the saves and crash reports:
  it carries the runtimes RED4ext and the mods need and the game's settings.
  Then one save is copied in (by default the newest).
- **Launch: Proton outside Steam**, the Proton the player's prefix names,
  through the Steam Linux Runtime (and `steam-run` when the machine has it,
  as NixOS needs), with no Steam identity. umu-launcher is the alternative.
- **Display: a nested gamescope**, 1280×720, 30 fps.
- **Guard: bubblewrap** around the game: the player's install read only;
  their prefix, cloud folder and `~/.steam` not there.
- **Both test games are instances** (`a` hosts, `b` joins `127.0.0.1`). The
  player's own install is then never deployed to, never launched and never
  killed by tests: tandem need not even be installed in it.

`tools/instance.sh` does this:

```sh
tools/instance.sh create a            # and b; reads the player's install
GAME_DIR="$(tools/instance.sh dir a)" task deploy:files
tools/instance.sh role a host
tools/instance.sh role b join         # 127.0.0.1
tools/instance.sh start a; tools/instance.sh load a   # to "playing"
tools/instance.sh start b; tools/instance.sh load b   # once a plays
tools/instance.sh log a; tools/instance.sh cmd b where
tools/instance.sh stats a; tools/instance.sh shot b x.png; tools/instance.sh xdo b key c
tools/instance.sh bt b                # a frozen game's stacks, to b's run/bt.txt
tools/instance.sh who                 # each running game: ours (steam, instance a) or the player's
tools/instance.sh stop a; tools/instance.sh destroy a
```

**Changed from the plan**: the instances live in
`/data/Steam/tandem-instances` (the default of `TANDEM_INSTANCES`, beside
the Steam library), not in `/data/tandem-instances/b` as first written
(`/data/tandem-instances/NAME/ (TANDEM_INSTANCES; same filesystem as the game)`):
`/data` is root-owned. Paths under Wine follow
(`Z:/data/tandem-instances/b/tandem.ini` is now under `Z:/data/Steam/`), and
so does the check of step 2 (it read
`find "/data/Steam/steamapps/common/Cyberpunk 2077" /data/Steam/steamapps/compatdata/1091500 -newer /data/tandem-instances/b/.tandem-instance | head`).
And `tools/instance.sh start a; tools/instance.sh start b` became a start
and a `load` each, the second once the first plays.

`-n` prints every command instead of running it. From the laptop the script
sends itself to anya over ssh; `GAME_HOST=local` runs it on the machine at
hand.

**`tools/game.sh` and `tools/scenario.sh` take an instance**,
`TANDEM_INSTANCE=NAME` ([Sweeps on two games](#sweeps-on-two-games));
`PILOT_INSTANCE=NAME` is its older name. `tools/game.sh pilot …` on an
instance plays our V there by keys sent to the instance's own gamescope
display, or by a pad only that instance reads (`.tandem-instance` records
it as `pad = 0x02d1`, `INSTANCE_PAD` picks it, and `start` passes
`SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT`): `autopilot.md`, "Two real
games".

## Sweeps on two games

**Not yet run in game: the coordinator will try it** (written 2026-11-21;
checked by shellcheck, `--list` and a run against stubs of `game.sh` and
`scenario.sh`). Doubles the sweep's throughput: the Steam game and an
instance each run a slice of the list, against a mock player each.

```sh
tools/instance.sh create a                          # once; b too for --instances 3
tools/sweep.sh --list --instances 2                 # which game runs what
TANDEM_PREBUILT=1 tools/sweep.sh --instances 2      # the Steam game and instance a
TANDEM_INSTANCE=a tools/scenario.sh three-players   # one scenario on a, by hand
```

- **Which game**: `TANDEM_INSTANCE=NAME` for `tools/game.sh`,
  `tools/scenario.sh` and a one-game `tools/sweep.sh`; unset, `0` or `main`
  is the Steam game, everything as before. `tools/game.sh where` prints the
  game's instance, folder, port and local cache folder. `launches` and
  `threads` refuse anything but the Steam game alone.
- **Its processes**: an instance's carry `TANDEM_INSTANCE_ID` (its folder),
  the Steam game's none; `running`, `stop`, `own`, `bt` (each game has its
  own wineserver) and the sweep's crash and hang check find only the
  game's own, by pid, never every `Cyberpunk2077.exe` by name. An instance
  starts, loads (`tools/instance.sh load`: a click on Continue) and stops
  through `tools/instance.sh`. Everything else follows its folder: logs,
  `tandem.cmd`, `tandem.ini` roles, `.started`, the deploy (`GAME_DIR`).
- **Whose game**: `tools/instance.sh` refuses to change anything (create,
  sync, role, settings, start, cmd, destroy) while the player's game runs.
  The Steam game it may run beside is the one `tools/game.sh` started:
  `start` and `launches` write that game's pid and kernel start time to
  `~/.cache/tandem/pilot/main/game.owner` on the game machine as soon as
  its process exists (the pilot's owner record, `autopilot.md`); `stop`
  removes it once the game is gone. A game is ours when it carries an
  instance's mark, or when it is that recorded pid with that start time (a
  reused pid does not match), a launcher above it (Steam's reaper, Proton)
  or a process below it. Any other is the player's, and nothing is done; one
  under a minute old is waited for (a minute at most) until `tools/game.sh`
  records it. `tools/instance.sh who` lists each running game as
  `ours (steam, started by game.sh)`, `ours (instance a)` or `the player's`
  (a launcher of the player's too); `status` names the recorded Steam game.
  A Steam game started by hand or by Steam is the player's: stop it, or
  start it with `tools/game.sh start`. `INSTANCE_ALLOW_FOREIGN=1` (by hand
  only, never set by the sweep) takes any running game for ours.
- **Ports**: game k listens on 27450 + 100 k (`TANDEM_GAME_PORT`; an
  instance alone defaults to 27550), in its `tandem.ini`. The scenarios name
  27450: `tools/game.sh peer` and `peer2` turn a mock player's
  `ADDRESS:27450` into the game's port and give a mock host `--port`.
- **Mock players** carry `TANDEM_PEER_OF=GAME`; beside another game
  (`TANDEM_PARALLEL`, which the sweep sets) or on an instance, the harness
  and the scenarios' `pkill` (a function of `tools/scenario.sh`) stop only
  the game's own. Their logs, the pilot's, screenshots and `bt.txt` go to
  `.cache/instances/NAME/` (`$peer_log` in a scenario); evidence to
  `.cache/scenarios/SCENARIO-TIME-on-NAME/`.
- **One at a time**: game starts (until "playing": a start under load
  passed the engine's 120 s watchdog, above) and deploys (the build,
  `scripts:check`), by `flock` on `.cache/game-start.lock` and
  `.cache/deploy.lock`.
- **The split** (`tools/sweep.sh --instances N`): whole `ensure` groups
  where it can (one start per group and game), a group cut where it is more
  than a game's share, heaviest first; `--weights FILE` takes an earlier
  `results.txt`'s seconds instead of one per scenario. 181 scenarios
  (2026-11-21): 91 and 90 on two games, 61, 60 and 60 on three. Each game
  writes `results-GAME.txt`; `results.txt` merges them in the sweep's
  order, a 7th column naming the game.

**Keys and screenshots.** On anya's Sway session `ydotool` (a kernel input
device) and `grim` act on the focused window and the output, one game at a
time: unusable for two. An instance has its own gamescope (nested, or
headless: the sweep's default, `INSTANCE_DISPLAY`), whose Xwayland takes
XTEST input for that game only and whose screen `gamescopectl screenshot`
takes `[anya]` (above). So `shot` works on an instance. `key` and `hold`
are sent there as X keycodes (the Linux code + 8; Esc by name), physical
keys as ydotool's; whether xdotool takes a bare keycode, and what the
instance's keymap (not anya's AZERTY) makes of the codes the scenarios
chose, is `[unknown]`. The test reset's ChoiceApply (F) and the loading
screen's Space are the same key on both layouts. **Routed to the Steam
game** (instance 0) by the sweep, `needs_main`: `# needs the main game`;
the by-hand ones (a person watches that screen); the pilot's (its pad and
keys); `tools/game.sh key`/`hold` (`names-prompt`); the port 27450 named
outside a mock player's address (`coop`, `devices-guest`, `join-address`,
`recorder`, `release`, `settings`, `solo`: a log line, a `coop join`, `ss`).

**Resources** (proposed, from the measures above): two games fit anya's
31 GB and 16 GB of VRAM only without builds, so **no build on anya during
a parallel sweep** (build here, or `tools/remote.sh` before:
`TANDEM_PREBUILT=1`). The instance at 1280x720, textures Low, no sound
(`create`'s `settings`), 30 fps (`INSTANCE_FPS`: vkd3d and gamescope),
headless (no window in the streamed session), `MemoryMax=14G` and
`INSTANCE_MIN_FREE_MB=12000` (the start refuses with less), CPU weight 100
(400 if anything else competes). Cores apart, when the frame rates say the
two fight: `INSTANCE_CPUS=6-11` (`taskset`, inherited by every process of
the instance; check `nproc` first; the user manager's scopes have no cpuset
controller for `AllowedCPUs`). The Steam game keeps the player's own
settings: the tests change none of them.

**First try**: `tools/instance.sh status` (a stopped, deployed or not),
then `TANDEM_INSTANCE=a tools/scenario.sh three-players` alone (start, load,
port, mock players, evidence), then `tools/sweep.sh --instances 2 coop
three-players combat-defend markers` beside the Steam game, watching
`tools/instance.sh stats a` and the guest freeze above (the first open
unknown).

### What tandem needed

With a folder per instance, nothing: each has its own `tandem.ini`,
`tandem.cmd`, `recordings/` and logs. For setups that do share a plugin
folder (the overlay below), and to tell a game its port without a file, the
plugin now reads four variables at load (`core/include/tandem/instance.hpp`,
tested in `tests/instance_test.cpp`; Wine passes the Unix environment to
the game):

| Variable        | Effect                                                                  |
| --------------- | ----------------------------------------------------------------------- |
| `TANDEM_CONFIG` | the settings file instead of `tandem.ini`; `recent.txt` stays beside it |
| `TANDEM_CMD`    | the command file instead of `tandem.cmd`; `recordings/` stays beside it |
| `TANDEM_PORT`   | the UDP port, over the file's                                           |
| `TANDEM_LOG`    | a log file of tandem's own, besides RED4ext's `tandem-*.log`            |

Paths are the game's: `Z:/data/Steam/tandem-instances/b/tandem.ini` under
Wine. A relative path starts at the plugin's folder. Built and unit-tested;
not run in a game `[unknown]` (the instances on anya did not need them).

## Scenarios on two games

**Written 2026-11-28, not yet run in game.** The sweeps above put each game
against a mock player; these put two real games in one session, so what a
row of `parity.md` says is "same" is read off both games at once. A
scenario that says `# needs two games` (line 3) runs only with the other
game named, and `tools/sweep.sh` leaves it out otherwise:

```sh
TANDEM_INSTANCE=a TANDEM_PARTNER=c tools/scenario.sh pair-devices-door
TANDEM_INSTANCE=a TANDEM_PARTNER=c tools/sweep.sh pair-devices-door pair-devices-lift pair-devices-camera pair-devices-media pair-glass pair-police
```

- **Roles**: the scenario's game hosts (`ensure host`); `partner_join` has
  the partner run the same build with co-op off (`tools/game.sh ensure
off` on it: started once ours plays), leave any session, take its test
  reset and `coop join 127.0.0.1:PORT` (the game machine's loopback, our
  port); it arrives beside our V. Its own port is
  `TANDEM_PARTNER_PORT` (default ours + 100: two instances default to
  27550 both). After the scenario it leaves (`coop leave`), so the next
  one's mock player is alone with our game.
- **Helpers** (`tools/scenario.sh`): `pcmd TEXT` (a command in the
  partner), `pat SPOT` (its V there, settled, as `at`), `pmark`,
  `expect_partner`/`reject_partner` (its log since its mark), `pquery VAR
WHAT` (its test query), `pnear KIND ID` (it follows that device: its
  `device near KIND` asked every 3 s, 45 s at most, as its devices stream
  in), and `compare CATEGORY [ENTITY]`: a parity dump
  of both games at one moment (`tools/world-compare.py --instances OURS
PARTNER --only CATEGORY [--entity HEX]`, within 60 m), failing on any
  difference in that category (for `devices` with an entity, also when
  either game does not follow it), with a screenshot of each game at that
  moment (`compare-CATEGORY-TIME-INSTANCE.png` in each instance's `run/`); each comparison goes to the evidence's
  `compare.txt`. A failure keeps the partner's log as `partner.log`; at
  the end the partner, too, must have run no hook beside its script tick
  and hold nothing degraded the scenario did not declare.
- **The scenarios** (parity.md rows 7, 11–14, 16, 23): `pair-devices-door`,
  `pair-devices-lift` (the partner carried by its own cabin, more than 2 m
  up or down), `pair-devices-camera` (turned by the partner: friendly to
  both Vs on both games, marked on ours, then back), `pair-devices-barrel`
  (`DEVICES_AT` a spot 10 to 40 m from a barrel: none is known),
  `pair-devices-media`, `pair-glass`, `pair-turret` (`TURRET_AT` near a
  sentry: none is known), `pair-police` (the partner wanted: our units and
  car after its puppet, none on our V, its own stars, standing down).
  Automatic doors and mines have no scenario: no fixture is known for
  either.

## Costs

| Per instance | Measured on anya `[anya]`                                                                                                                        |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| Disk         | 3.5 GB (the game folder without `archive/`, the prefix, one save); copied in about a minute. A full copy instead: the whole game, 70 GB and more |
| RAM          | 6.5 GB resident, 7.5 GB with its page cache (limit: 14 GB)                                                                                       |
| VRAM         | 3.2 to 3.5 GB at 720p, textures Low (6.3 GB with the player's High)                                                                              |
| CPU          | 2.5 to 4 cores at 30 fps                                                                                                                         |
| Start        | 35 to 40 s to the menu alone, 3.5 min under load; the first start compiles shaders (a cache of its own in `run/`)                                |

## Alternatives

- **An overlay** (overlayfs: the player's folder below, the instance's
  writes on top), by `bwrap --overlay-src … --overlay …` (bubblewrap 0.11,
  [release notes](https://github.com/containers/bubblewrap/releases/tag/v0.11.0))
  or fuse-overlayfs (what PartyDeck uses). No copy at all, and always the
  player's current mods. But the merged folder exists only inside the
  sandbox: the tools outside cannot use it as `GAME_DIR`, and writing into
  the upper layer from outside while it is mounted is undefined. tandem's
  files would move out through `TANDEM_CONFIG`, `TANDEM_CMD` and
  `TANDEM_LOG` (hence those variables). A game or mod update under a
  mounted overlay is undefined too. Kept as plan B if the copy is too large.
- **A full copy of the game.** Simplest and slowest to build: 70 GB and more
  per instance. `INSTANCE_MIRROR=copy`.
- **The player's game as one of the two** (launched by Steam, as the
  scenarios do today) and one instance. What the sweep does (the Steam game
  `tools/game.sh` recorded counts as ours, "Sweeps on two games"), but that
  game uses the player's saves and prefix again, and `tools/game.sh` stops
  any Steam game (every one without a mark). Not recommended for two
  instances' worth of tests.
- **A second Steam app entry** (a non-Steam shortcut with its own prefix).
  Means editing the player's Steam configuration, and the game then talks to
  the client. Rejected.
- **A second machine or a VM with a GPU.** The clean way, with hardware we
  do not have.

## Open unknowns

Ranked by how likely each is to stop the plan. Settled on anya `[anya]`:
Proton outside Steam (CachyOS, Steam Linux Runtime 4, `steam-run`,
bubblewrap: they nest; `PROTON_DIR`, `INSTANCE_RUNTIME`, `INSTANCE_FHS`,
`INSTANCE_LAUNCHER=umu` stay to peel layers off elsewhere); the game with
the Steam client out of reach (once `GameServicesSteam.dll` is out); linked
archives; the copied prefix; the mark (the systemd scope,
`tandem-instance-NAME.scope`, is still stopped first); gamescope nested and
headless on that GPU; XTEST input, mouse and keys.

1. **The guest's freeze after "playing"** (once in three): a whole-process
   stop, every thread waiting on the wineserver. Is it the swapping machine,
   the two wineservers, or tandem's guest path? Next: two games with no
   build running, then the same roles swapped, `bt` at once when it stops.
2. **Starts under load and the engine's 120 s watchdog**: start the second
   game only once the first plays, and with no build on anya. A test runner
   should detect it (no "system attached" within 3 minutes), `stop` and
   start again.
3. **Builds against games**: the build slots' processes need a scope of low
   CPU weight (or the tests a time without builds); `nice` is not enough.
4. **Sound**: two games play at once (unheard: anya's session streams
   video only). A null sink per instance (`INSTANCE_ENV='PULSE_SINK=…'`) if
   it matters.
5. **Input without gamescope**: what an unfocused game does. Moot while
   gamescope works.

## First try, lowest risk first

Each step needs the player's go-ahead and the game not running, and stops
at the first surprise. Steps 0 and 1 change nothing on anya.

0. **Facts, read only.** The commands of [Resources](#3-resources), and:
   `ls /data/Steam/steamapps/common | grep -i -E 'proton|runtime'`;
   `cat /data/Steam/steamapps/compatdata/1091500/config_info | head -3`;
   `command -v gamescope bwrap umu-run steam-run rsync systemd-run`;
   `du -sh --exclude=archive "/data/Steam/steamapps/common/Cyberpunk 2077"`;
   the player's launch options (Steam, the game's properties).
   _Observe:_ the GPU and its memory, the filesystem of `/data`, the room,
   the Proton in use, what is installed.
1. **`tools/instance.sh -n create b`.** _Observe:_ the paths are the right
   ones, the size and the room it reports, no command outside
   `/data/Steam/tandem-instances/b`.
2. **`tools/instance.sh create b`.** _Observe:_ "archives: reflink" or
   "link"; the save it took; then that nothing of the player's changed:
   `find "/data/Steam/steamapps/common/Cyberpunk 2077" /data/Steam/steamapps/compatdata/1091500 -newer /data/Steam/tandem-instances/b/.tandem-instance | head`
   prints nothing.
3. **`tools/instance.sh -n start b`**, and read the command: the Proton,
   the runtime, the sandbox's binds.
4. **`tools/instance.sh start b`**, tandem not deployed, role off: the
   player's mods only. _Observe:_ `run/launch.log`; a gamescope window;
   `game/red4ext/logs/red4ext-*.log` lists the plugins;
   `game/r6/logs/redscript_rCURRENT.log` has no error; the save loads; the
   Steam client does **not** show the game as running; `status` counts the
   game among b's processes; memory, VRAM and frame rate with one game;
   the `find` of step 2 still prints nothing; nothing new under
   `~/.local/share/Steam/userdata/*/1091500`. Then `stop b`: every process
   gone, nothing else killed.
   If it does not start: remove one layer at a time (`INSTANCE_DISPLAY=window`,
   then `INSTANCE_HIDE_STEAM=0`, then `INSTANCE_RUNTIME=none`, then
   `INSTANCE_LAUNCHER=umu`; `INSTANCE_SANDBOX=0` last, and only for a
   diagnosis).
5. **One instance against the mock player.** Deploy into b
   (`GAME_DIR="$(tools/instance.sh dir b)" task deploy:files`), `role b
host`, `start b`, `task peer -- join <anya>:27450 --mode front`.
   _Observe:_ `tools/instance.sh log b` shows the session as the scenarios
   expect; `cmd b where` is taken. The instance now stands in for the game
   the scenarios used, without the player's install.
6. **Two.** `create a`, deploy, `role a host`, `role b join`, start a, wait
   for its "playing", start b. _Observe:_ both logs show the session; each
   V sees the other; memory, VRAM, frame rates with two; the machine stays
   usable. A failed second start with the first still fine is a capacity
   answer, not a dead end: lower `INSTANCE_SIZE`, `INSTANCE_FPS`.
7. **Input.** `DISPLAY=:N xdotool key …` into each gamescope: only that game
   reacts. Then a uinput pad per instance if the bots need one.
8. **Headless.** `INSTANCE_DISPLAY=headless` for one instance: does it
   render (a screenshot from tandem or gamescope), does the engine's
   watchdog stay quiet.

Done on 2026-11-19 (steps 0 to 8, [Results](#results-on-anya-2026-11-19));
instances `a` and `b` stay on anya, roles host and join. Afterwards:
`tools/instance.sh status` shows nothing running; `destroy` each
instance when the disk is wanted back. Record what was learnt here, tag by
tag, and in `docs/research/engine-knowledge.md` §12.

## Sources

- Two games on one PC (Windows, CyberpunkMP fork): <https://github.com/oxylol/cp2077-coop>
- DRM, paths, launch arguments: <https://www.pcgamingwiki.com/wiki/Cyberpunk_2077>
- redscript's lock and the files it writes: <https://github.com/jac3km4/redscript> (`scc/lib/src/lib.rs`, v0.5.27)
- RED4ext's script compilation command line: <https://github.com/wopss/RED4ext/issues/165>
- Proton by hand: <https://gist.github.com/michaelbutler/f364276f4030c5f449252f2c4d960bd2>, <https://github.com/h0m3/protonlaunch/wiki/FAQ>
- umu-launcher: <https://github.com/Open-Wine-Components/umu-launcher>
- PartyDeck (gamescope, bubblewrap, a prefix per player): <https://github.com/partydeck/partydeck>
- SplitScreenWrapper (bubblewrap over `/dev/input`): <https://github.com/dscrawford/SplitScreenWrapper>
- bubblewrap overlays: <https://github.com/containers/bubblewrap/releases/tag/v0.11.0>
- gamescope: device selection <https://github.com/ValveSoftware/gamescope/issues/1771>, headless backend <https://github.com/ValveSoftware/gamescope/issues/1984>
- Input into a nested gamescope: <https://github.com/samarslan/foxclick>, <https://github.com/Tim-van-Oudheusden/pindu-launcher/issues/423>
- vkd3d-proton's frame limit: <https://github.com/HansKristian-Work/vkd3d-proton>
- System requirements: <https://www.cyberpunk.net/en/news/48271/update-to-pc-system-requirements>
- Editing `UserSettings.json`: <https://gist.github.com/MoreOrLessSoftware/746c841ae1804e4947779fe400b0837c>
- tandem's own notes: `docs/research/engine-knowledge.md` §12 (startup hangs, the compile-error dialog on a headless display), §13.8 (a crash freezes the game under Proton)
