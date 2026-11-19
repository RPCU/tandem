# Two games on one machine

Can two copies of Cyberpunk 2077 run on anya at once, one hosting and one
joining over loopback, so that tests put two real games against each other?

**Status: researched offline (2026-11-19), nothing tried on anya.** The
tooling (`tools/instance.sh`) was run only against a fake game tree. Every
claim below carries a tag:

- `[sourced]` — read in documentation or source code, linked.
- `[reported]` — users say so (forums, a project's README), not confirmed.
- `[inferred]` — follows from how Wine, Proton or the engine work.
- `[offline]` — checked here, on the fake tree or in the unit tests.
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
cloud and no "running" state `[inferred]`.

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

Not measured. What is known:

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
  gets each game from launch to a loaded save without a key.
- **Keyboard and mouse: through gamescope's own X server.** Each nested
  gamescope runs an Xwayland of its own (`DISPLAY=:1`, `:2`…); XTEST events
  sent there (`DISPLAY=:2 xdotool key …`) reach that game only, focused in
  Sway or not ([foxclick](https://github.com/samarslan/foxclick),
  [pindu-launcher #423](https://github.com/Tim-van-Oudheusden/pindu-launcher/issues/423))
  `[reported]`. `ydotool` (what `tools/game.sh key` uses) is a kernel input
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

### 5. Risks to the player's install

| Risk                                                     | How the setup removes it                                                                                                                                    |
| -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A test game saves over the player's saves                | its own prefix, holding a copy of one save; the player's prefix is hidden from it (an empty tmpfs in its sandbox)                                           |
| Steam cloud uploads a test save, or downloads over one   | not launched by Steam, no app id, the client's folder hidden; Steam's local cloud folder (`userdata/*/1091500`) hidden as well                              |
| Achievements, playtime, "in game" status on the account  | the same: the game cannot reach the client                                                                                                                  |
| The script cache, logs or mod state of the player's game | the instance's game folder is a copy; the player's is mounted read only in the sandbox                                                                      |
| A game update or a mod change while an instance is built | nothing is written to the player's folder; `instance.sh sync` refreshes the copy                                                                            |
| The tools kill the player's game                         | `instance.sh` kills only processes carrying its mark, never by name. **`tools/game.sh stop` still kills by name** (`pkill -f Cyberpunk2077.exe`): see below |
| Two games starve the player's                            | every changing verb refuses while a game we did not start runs; a memory limit per instance; `stop` always works                                            |
| A half-copied prefix                                     | the prefix is copied only while the player's game is not running                                                                                            |

What the sandbox does **not** do: it is not a jail. The game can still write
elsewhere in the home directory (as any game can). It protects the three
things named: the install, the prefix, the cloud folder.

## Recommended setup

```
/data/tandem-instances/NAME/           (TANDEM_INSTANCES; same filesystem as the game)
  .tandem-instance                     marks it as ours (destroy refuses without it)
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
tools/instance.sh start a; tools/instance.sh start b
tools/instance.sh log a; tools/instance.sh cmd b where
tools/instance.sh stop a; tools/instance.sh destroy a
```

`-n` prints every command instead of running it. From the laptop the script
sends itself to anya over ssh; `GAME_HOST=local` runs it on the machine at
hand.

**`tools/game.sh` and `tools/scenario.sh` are not instance-aware.** They
find, start and kill "the game" by name and through Steam. Do not run them
while instances run. The next piece of work is a two-game scenario runner
on top of `instance.sh` (its `cmd` and `log`, per instance).

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

Paths are the game's: `Z:/data/tandem-instances/b/tandem.ini` under Wine. A
relative path starts at the plugin's folder. Built and unit-tested; not run
in a game `[unknown]`.

## Costs

| Per instance | Estimate                                                                                                                                                                                     |
| ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Disk         | the game folder without `archive/` plus the prefix: a few GB `[unknown]`; `create` prints the number and checks the room before copying. A full copy instead: the whole game, 70 GB and more |
| RAM          | 8 to 10 GB `[unknown]` (limit: 14 GB)                                                                                                                                                        |
| VRAM         | 3 to 5 GB at 720p low `[unknown]`                                                                                                                                                            |
| CPU          | a few cores at 30 fps `[unknown]`                                                                                                                                                            |
| Start        | as long as the player's game, plus shader compilation at the first start (a cache of its own)                                                                                                |

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
  scenarios do today) and one instance. Works with the same script
  (`INSTANCE_ALLOW_FOREIGN=1`), but the tests then use the player's saves
  and prefix again, and `tools/game.sh` kills by name. Not recommended.
- **A second Steam app entry** (a non-Steam shortcut with its own prefix).
  Means editing the player's Steam configuration, and the game then talks to
  the client. Rejected.
- **A second machine or a VM with a GPU.** The clean way, with hardware we
  do not have.

## Open unknowns

Ranked by how likely each is to stop the plan.

1. **Proton outside Steam on anya.** Which Proton, where the Steam Linux
   Runtime is, whether the machine is NixOS (then `steam-run`), whether
   the runtime, `steam-run` and our bubblewrap nest. `instance.sh` guesses
   from the player's prefix (`config_info`) and takes `PROTON_DIR`,
   `INSTANCE_RUNTIME`, `INSTANCE_FHS`, `INSTANCE_SANDBOX`,
   `INSTANCE_LAUNCHER=umu` to peel layers off one at a time.
2. **The game without the Steam client in reach.** Reported to run; under
   Proton with `~/.steam` hidden it is `[unknown]`. If it stalls, try
   `INSTANCE_HIDE_STEAM=0` (the app id variables stay removed).
3. **Capacity.** RAM, VRAM and CPU for two. Measure with one, then two.
4. **Linked archives.** Whether the engine and ArchiveXL read `archive/`
   through symbolic links under Wine. Mod managers deploy this way on Linux
   `[reported]`; if not, reflinks or `INSTANCE_MIRROR=copy`.
5. **The copied prefix**: that RED4ext and every mod load in it as they do in
   the player's.
6. **The mark.** `stop` and the "is it ours" check rely on
   `TANDEM_INSTANCE_ID` reaching every process's environment through the
   runtime and Wine. If it does not, the systemd scope
   (`tandem-instance-NAME.scope`) is the fallback for `stop`, and our own
   game would look foreign (the tools then refuse: the safe side).
7. **gamescope on anya**: installed? works nested on that GPU and that Sway?
8. **Input**: XTEST into gamescope's X server reaching the game's raw mouse
   input; what an unfocused game does without gamescope.
9. **Sound**: two games play at once. A null sink per instance
   (`INSTANCE_ENV='PULSE_SINK=…'`) if it matters.
10. **tandem with two real games**: found by doing it. That is the point.

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
   `/data/tandem-instances/b`.
2. **`tools/instance.sh create b`.** _Observe:_ "archives: reflink" or
   "link"; the save it took; then that nothing of the player's changed:
   `find "/data/Steam/steamapps/common/Cyberpunk 2077" /data/Steam/steamapps/compatdata/1091500 -newer /data/tandem-instances/b/.tandem-instance | head`
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

Afterwards: `tools/instance.sh status` shows nothing running; `destroy` each
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
