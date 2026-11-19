# tandem: installing and playing

tandem is a co-op mod for Cyberpunk 2077: two to four players in one Night
City, each with their own V. One player hosts with one of their saves; the
others join with a V of their own.

Every player installs the **same tandem release**. A game without tandem
cannot join, and two different releases refuse each other with a message.

## Requirements

| What                    | Version                   | Notes                                                                                        |
| ----------------------- | ------------------------- | -------------------------------------------------------------------------------------------- |
| Cyberpunk 2077 (PC)     | patch **2.50** exactly    | the executable's file version is `3.0.88.46235`                                              |
| **Phantom Liberty**     | owned, installed, enabled | the expansion is required on **every** player's game (see below)                             |
| [RED4ext][red4ext]      | **1.31.0** or newer       | loads `tandem.dll`                                                                           |
| [redscript][redscript]  | **0.5.32** or newer       | compiles tandem's scripts                                                                    |
| [Codeware][codeware]    | **1.21.2** or newer       | tandem's scripts and menus use it                                                            |
| [TweakXL][tweakxl]      | **1.12.0** or newer       | loads the records of the other players' bodies                                               |
| Windows 10/11, or Linux | Linux: Steam with Proton  | Steam launch option `WINEDLLOVERRIDES="winmm,version=n,b" %command%`, as for any RED4ext mod |

**Phantom Liberty is required, for the host and for every guest.** A V
brings what it owns into the other players' games: its weapons, clothes
and cyberware, its vehicles, its perks. With the expansion a V has items
and vehicles whose records only exist in a game that has it, and the Relic
skill tree. A game without it could not show or apply what the other one
sends, and the two would fall out of step. So tandem does not try: without
Phantom Liberty installed and enabled (the game's main menu shows it as
part of the game), co-op stays off and your game is the single-player one;
a player who lacks it cannot join, and both players are told why.

ArchiveXL is **not** needed: tandem ships no archive. Cyber Engine Tweaks is
not needed either.

These are the versions this release was built and tested with (the set that
moved together for patch 2.5). The end of this file, written when the
release was packaged, lists the exact build.

tandem checks all of this when the game starts. If the game is another
patch, has no Phantom Liberty, or a mod above is missing or too old, tandem
turns itself off (your
game runs as if it were not installed) and the **Co-op** entry of the main
menu tells you what to fix.

[red4ext]: https://github.com/wopss/RED4ext
[redscript]: https://github.com/jac3km4/redscript
[codeware]: https://github.com/psiberx/cp2077-codeware
[tweakxl]: https://github.com/psiberx/cp2077-tweak-xl

## Install

1. Install the four mods above, each by its own instructions, and start
   the game once to check it still runs.
2. Extract the tandem zip **into the game's folder** (the one that holds
   `bin`, `r6` and `red4ext`; in Steam: right-click the game → Manage →
   Browse local files). You get:

   ```
   red4ext/plugins/tandem/   tandem.dll, tandem.default.ini, tandem-report.exe,
                             INSTALL.md, LICENSE.txt, NOTICE.txt,
                             THIRD-PARTY-LICENSES.txt
   r6/scripts/tandem/        the scripts (*.reds)
   r6/tweaks/tandem/         the records (*.tweak)
   ```

3. Start the game. The main menu has a **Co-op** entry. The first start
   creates `red4ext/plugins/tandem/tandem.ini` from `tandem.default.ini`.

You do not need to edit anything to play: without a session started from
the Co-op menu, tandem does nothing to your game.

### Updating

Delete `r6/scripts/tandem` and `r6/tweaks/tandem` (a file left over from an
older release can stop every script mod from loading), then extract the new
zip over the old one. Your `tandem.ini` is kept: a release never contains
that file. Every player updates to the same release.

### Uninstall

Delete these three folders; nothing else was written anywhere:

```
red4ext/plugins/tandem
r6/scripts/tandem
r6/tweaks/tandem
```

tandem is young: keep a copy of your saves (`Saved Games/CD Projekt
Red/Cyberpunk 2077` in your user folder) before your first session.

## Playing together

A session has one **host** and up to three **guests**. The host's game runs
the world (story, enemies, time, weather); every guest keeps their own V,
inventory and save.

- **Host:** main menu → **Co-op** → **Host a game** → **Continue** or
  **Load game**. Or, already in game: pause menu → **Co-op** → **Host this
  game**.
- **Guest:** main menu → **Co-op** → **Join a game** → pick a recent host
  or **Enter a code or an address** → choose the save your V comes from.
  Or in game: pause menu → **Co-op** → **Join a game**.
- **Leaving:** pause menu → **Co-op** → **Leave session**, or just quit.

The status panel at the top right of the Co-op pages shows who is in the
session and their ping. How the guest reaches the host depends on where
you both are:

### On the same network (LAN, or a VPN such as Tailscale or ZeroTier)

The host opens pause menu → **Co-op** → **Invite**: it shows the host's
addresses and port, for example `192.168.1.20:27450`. The guest types that
address in **Enter a code or an address**. The port can be left out when it
is the default (27450).

The first time, Windows may ask the host whether the game may accept
connections: allow it (private networks). If the guest cannot connect, see
[Troubleshooting](#troubleshooting).

### Across the internet, with a port forward

The host forwards **UDP port 27450** on their router to their PC (the
router's "port forwarding" or "virtual server" page; UDP, not TCP), and
gives the guest their public address (what a "what is my IP" site shows).
The guest types `that.address:27450`.

To host on another port, set `port = …` in the host's `tandem.ini` and
forward that one; the guest then types `address:port`.

Only the host forwards a port. Some connections cannot do it at all
(carrier-grade NAT, mobile, some fibre boxes): use a relay.

### Across the internet, with a relay and a join code

A relay is a small server (`tandem-relay`) that passes the traffic between
players who cannot reach each other. tandem comes set to the public RPCU
relay (`relay = relay.rpcu.io:27460` in `red4ext/plugins/tandem/tandem.ini`),
open to anyone: there is nothing to set up.

1. The host hosts as above; the Co-op panel and the **Invite** popup show
   a **join code** such as `K7Q-2M9`.
2. The guest types the code in **Enter a code or an address**.

Another relay (one your group runs: `docs/relay.md` in tandem's source
explains how; a small VPS or a home server with one forwarded port is
enough) goes in the same line, for both players. If its operator gave the
host a **host token** (`relay_token = 64 hex digits`), it lifts the relay's
limits on that host's sessions, and a relay that takes only token holders
needs it. An empty `relay =` turns relays off.

A code is valid for ten minutes and the host's panel always shows the
current one; once a guest is in, their game finds the session again by
itself after a disconnection. The host's panel shows the relay's state if
something is wrong ("the relay did not answer", "the relay hosts as many
sessions from your address as it may", "the relay refused our host
token").

A relayed session is a little slower than a direct one: everything makes a
detour through the relay. With a relay set, a host is still reachable
directly on its port too.

## Settings: `tandem.ini`

`red4ext/plugins/tandem/tandem.ini`, read when the game starts. Unknown
keys and bad values are reported in tandem's log and ignored.

| Key                        | Default                               | What                                                                                                                                                                                                                                                  |
| -------------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`                     | `V`                                   | The name the other players see over your V and in their party list: 1 to 16 characters, no `;` or `#`. While it is `V`, the Co-op menu asks for one the first time you host or join. A host shows a second player of the same name as "V (2)".        |
| `nameplates`               | `near`                                | The name over the other players: `near` (full to 12 m, gone at 20 m), `always` (any distance in view, smaller and fainter far away), `off`. A partner you are linked with (`link`) also has an entry with their health in your party list, on screen. |
| `party_hud`                | `on`                                  | The party list under your health bar: each linked partner's name, health and state. It moves below a holocall and hides with the game's HUD. `off` hides it.                                                                                          |
| `role`                     | `off`                                 | `off`: solo until you start a session in the Co-op menu. `host`: host at every load. `join`: join `address` or `join` at every load.                                                                                                                  |
| `port`                     | `27450`                               | UDP port the host listens on, and the port a guest calls when none is typed.                                                                                                                                                                          |
| `address`                  | none                                  | `role = join`: the host's address.                                                                                                                                                                                                                    |
| `relay`                    | `relay.rpcu.io:27460` (shipped)       | A tandem-relay, `host` or `host:port` (default port 27460): the public RPCU relay, open to anyone, by default; several, comma-separated: a host takes the nearest; empty: none.                                                                       |
| `code`                     | `new`                                 | Hosts with a token: `keep` gives the same join code every session (the Co-op menu's New join code changes it).                                                                                                                                        |
| `relay_token`              | none                                  | Optional: a host token of that relay (64 hex digits), lifting an open relay's limits on your sessions (needed on a token-only relay). Hosts only. Keep it to yourself.                                                                                |
| `join`                     | none                                  | `role = join`: the host's join code.                                                                                                                                                                                                                  |
| `direct`                   | `auto`                                | Through a relay, also try to reach the other player directly (lower latency, the relay as fallback). `off`: always the relay.                                                                                                                         |
| `pose`                     | `on`                                  | Exact body poses of the other players. `off`: stock animations only.                                                                                                                                                                                  |
| `story`                    | `on`                                  | Share the host's story (journal, calls, texts, subtitles, choices, rewards). `off`: each game keeps its own.                                                                                                                                          |
| `quest_freeze`             | `off`                                 | Guest: your own story waits while you play in the host's. Experimental.                                                                                                                                                                               |
| `quest_freeze_allow`       | none                                  | More open-world quest phases that keep running while frozen (resource paths, comma-separated).                                                                                                                                                        |
| `scene_privacy`            | `on`                                  | Romance and private one-to-one scenes are yours alone; the others see you busy. `off`: every scene can be watched.                                                                                                                                    |
| `private_actors`           | `judy, panam, river, kerry, meredith` | Whose scenes are private.                                                                                                                                                                                                                             |
| `private_scenes`           | none                                  | Scenes to treat as private (resource paths, comma-separated).                                                                                                                                                                                         |
| `public_scenes`            | none                                  | Scenes to treat as watchable, whoever is in them.                                                                                                                                                                                                     |
| `time_host`                | `full`                                | Host's own Sandevistan: `full` slows the host's game as in single player; `bubble` slows only the NPCs around the host.                                                                                                                               |
| `enemy_scaling`            | `on`                                  | Host: a hostile NPC is tougher for each extra player near it (health x1.6 for two players, x2.2 for three, x2.8 for four; guard, stagger and armour a little). Its damage, XP and loot are not scaled. Every game plays by the host's.                |
| `enemy_scaling_health`     | `0.6`                                 | Host, advanced: the health `enemy_scaling` adds per extra player (0 to 2.5).                                                                                                                                                                          |
| `recorder`                 | `off`                                 | `on`: record your play in `recordings/auto/` next to `tandem.ini`, for tandem's developers. Changes nothing in game.                                                                                                                                  |
| `recorder_max_mb`          | `2048`                                | Megabytes of recordings kept; the oldest go first.                                                                                                                                                                                                    |
| `recorder_segment_minutes` | `10`                                  | Minutes per recorded segment.                                                                                                                                                                                                                         |
| `recorder_pose`            | `off`                                 | Also record body poses.                                                                                                                                                                                                                               |

Developer keys, not needed to play (they can crash the game or flood the
log): `puppet_female`, `puppet_male` (the records puppets are spawned
from), `probe`, `trace`, `mirror` (animation experiments), `compat = off`
(start sessions whatever the startup check found).

## Troubleshooting

### Where the logs are

All under the game's folder:

| File                              | What it tells                                                   |
| --------------------------------- | --------------------------------------------------------------- |
| `red4ext/logs/tandem-<date>.log`  | tandem's own log: what it found at startup, the session, errors |
| `red4ext/logs/red4ext-<date>.log` | RED4ext's: whether `tandem.dll` was loaded, and why not         |
| `r6/logs/redscript_rCURRENT.log`  | redscript's: whether the scripts compiled                       |

The first lines of tandem's log say what it found:

```
tandem 0.2.0+2fa03ca loaded (protocol 7)
game 3.0.88.46235 (built for patch 2.50, 3.0.88.46235)
RED4ext: 1.31.0
Codeware: 1.21.2
TweakXL: 1.12.0
```

and, once the main menu is up, `Phantom Liberty: enabled`.

### There is no Co-op entry in the main menu

- **A popup at launch says "REDScript compilation has failed"**: the
  scripts did not compile, and the game then runs with _no_ script mod at
  all (not only tandem's). `r6/logs/redscript_rCURRENT.log` says where:

  ```
  [ERROR - Thu, 19 Nov 2026 12:14:43 +0100] [UNRESOLVED_REF] At …\r6\scripts\tandem\Coop.reds:71:13:
      let x = Missing.Thing();
              ^^^^^^^
  unresolved reference 'Missing'

  [ERROR - Thu, 19 Nov 2026 12:14:44 +0100] REDScript compilation has failed.
  This error has been caused by mods listed below:
  - tandem
  ```

  When the mod named is tandem: Codeware is missing or older than 1.21.2
  (unresolved references to its types), or files of an older tandem
  release are left in `r6/scripts/tandem` (delete that folder and extract
  the release again). When another mod is named, that mod is the one to
  update or remove: tandem cannot load until every script mod compiles.

- **No popup, and no `red4ext/logs/tandem-*.log` either**: RED4ext is not
  installed or not loading (on Linux: the launch option above is missing), or
  `tandem.dll` is not at `red4ext/plugins/tandem/tandem.dll` (the zip was
  extracted somewhere else, or into a folder of its own).
- **No popup, tandem's log exists**: redscript is not installed
  (`engine/tools/scc.exe` is missing), or `r6/scripts/tandem` is empty.

### The Co-op page says "Co-op is off"

The startup check found a problem and says which in a popup, for example:

- _This tandem is built for Cyberpunk 2077 patch 2.50 (3.0.88.46235); this
  game is 3.0.80.51928._ Update the game, or install the tandem release
  made for your patch. After a game update, wait for RED4ext, redscript,
  Codeware, TweakXL **and** tandem to be updated.
- _Codeware 1.20.5 is too old: tandem needs 1.21.2 or newer._ Update it.
- _TweakXL is not installed._ Install it.
- _This game does not have the Phantom Liberty expansion._ tandem needs
  it on every player's game; there is no setting to play without it.
- _Phantom Liberty is owned but not installed or not enabled in this
  game._ Install it from your store's library (Steam: the game's
  properties → DLC; GOG and Epic: the game's add-ons), start the game and
  check the main menu shows it.
- _tandem's tweaks are missing (r6/tweaks/tandem)._ Extract the whole zip
  again.

While it is off, tandem installs nothing in the game: playing solo is
unaffected.

### A guest cannot connect

- "could not reach the host": the address or port is wrong, the host has
  not loaded a save as host yet, the host's firewall blocks the game
  (allow `Cyberpunk2077.exe` for UDP on private networks), or the router
  does not forward UDP 27450. Test on the same network first.
- "the session is full": a session holds four players.
- "no session has this join code (wrong, or expired)": ask the host for
  the code their panel shows now.
- "too many join attempts: try again in a minute": the relay limits wrong
  codes; wait.
- "Joining by code needs a relay": write the same `relay = …` as the host
  in your `tandem.ini` and restart the game.

### Version mismatches between players

- **One player has no Phantom Liberty**: they are refused. They read
  _Could not join: your game has no Phantom Liberty: tandem needs the
  expansion on every player's game_ and the host _NAME could not join:
  their game has no Phantom Liberty_. (Normally it does not come to that:
  a game without it keeps co-op off from its own menu.)
- **Different tandem releases** (another protocol): the guest is refused
  and reads _Could not join: your tandem speaks protocol 6, the host's
  (0.2.0+2fa03ca) protocol 7: both players need the same tandem release_;
  the host reads _NAME could not join: their tandem (…) speaks protocol 6,
  yours (…) protocol 7_. Install the same release on both games.
- **Same protocol, something else differs**: the session goes on, and each
  player is told once what differs: the tandem build, the game's version,
  tandem's scripts or tweaks (extract the whole release again, on both
  games), or the list of other mods.
- **Other mods**: tandem compares the _names_ of the mods installed in
  each game (a fingerprint of the list; the names themselves never leave
  your PC) and warns when they differ. It never refuses a session for it.
  Mods that only change looks or the interface are harmless. Mods that
  change gameplay (weapon stats, enemy health, AI, cyberware, quests,
  vehicles, time, new items) should be the same on every game, or each
  player will see a slightly different fight. When something looks out of
  step, try again without them before reporting a bug.

## Reporting a bug

Make a report right after the problem, on **each** player's game:

- in game or in the main menu: **Co-op** → **Write a bug report**;
- or, when the game does not start: run
  `red4ext/plugins/tandem/tandem-report.exe` (on Linux, the co-op menu's
  entry is the easy way; the files it gathers are listed below if you want
  to collect them by hand).

Both write `red4ext/plugins/tandem/reports/tandem-report-<date>.zip` and
send nothing anywhere. The zip holds exactly:

| File                  | What                                                                                                                                        |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `versions.txt`        | tandem's build; the versions of the game, RED4ext, Codeware, TweakXL, ArchiveXL; fingerprints of tandem's files; the startup check's result |
| `tandem.log`          | the newest `red4ext/logs/tandem-*.log` (its last 2 MB)                                                                                      |
| `tandem.previous.log` | the one before it (the run that crashed, when you restarted to make the report)                                                             |
| `red4ext.log`         | the last 200 lines of the newest `red4ext/logs/red4ext-*.log`                                                                               |
| `redscript.log`       | the last 200 lines of `r6/logs/redscript_rCURRENT.log`                                                                                      |
| `tandem.ini`          | your settings, with `relay_token` and `join` removed                                                                                        |
| `last-session.txt`    | the last session in a few lines: role, how long, how many players, why it ended, ping, resyncs                                              |

Before they go in, every file is cleaned: the game's folder becomes
`<game>`, the user name in a path becomes `<user>`, and IPv4 addresses keep
only their first half (`203.0.x.x`). What stays and you may want to know
about: the players' names as typed in `tandem.ini`, the relay's host name,
and, in the two log tails of RED4ext and redscript, the file names of your
other mods. The number of your other mods and a fingerprint of their names
are included; the list itself is not. Saves, screenshots, recordings and
the list of recent hosts are never included.

With the zip, say what you did, what you expected, what happened, and
roughly when (the logs have times).
