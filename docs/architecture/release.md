# Releases: packaging, compatibility checks, bug reports

Everything between "works on the developer's machine" and "two friends
install it and play". What players read is [`dist/INSTALL.md`](../../dist/INSTALL.md)
(shipped in the zip); the relay operator's guide is [`relay.md`](../relay.md);
the first real session's checklist is [`playtest.md`](../playtest.md). This
page is for whoever changes the code behind them.

**State (2026-11-19):** all of it is built and tested offline (unit tests,
the packer, the scripts' compile check). **None of it has run in the game
yet**: the list at the [end](#not-yet-verified-in-game) says what the first
in-game session must look at.

## One version, one build name

- The **release** number is `project(tandem VERSION …)` in the root
  `CMakeLists.txt`. The plugin's version for RED4ext (`Query` in
  `plugin/src/main.cpp`), the build's name and the package's file name all
  come from it.
- The **build** is `release+commit`, `0.2.0+2fa03ca`
  (`core/CMakeLists.txt` → `tandem_build.hpp` → `compat::Build()`). CMake
  asks git at configure time, which every `task build:*` runs; a tree
  copied without its history (`tools/remote.sh`) is told the commit
  through `TANDEM_COMMIT`; outside both, the build is the release alone
  and compares equal to any build of that release (`compat::SameBuild`).
- `kProtocolVersion` (`core/include/tandem/protocol.hpp`) is the only hard
  gate between two games. It changes when an existing message changes;
  the build name changes with every commit and only ever warns.

## The package (`task package`)

`tools/package.py` writes into `build/package/`:

| File                         | What                                                                |
| ---------------------------- | ------------------------------------------------------------------- |
| `tandem-<build>.zip`         | the game's folder layout; players extract it into the game's folder |
| `tandem-<build>-symbols.zip` | `tandem.pdb`, `tandem-report.pdb`: kept by us, for crash backtraces |
| `INSTALL.md`                 | the one in the zip, to put next to a download                       |
| `SHA256SUMS`                 | of both zips                                                        |

```
red4ext/plugins/tandem/   tandem.dll, tandem-report.exe, tandem.default.ini,
                          INSTALL.md, NOTICE.txt, THIRD-PARTY-LICENSES.txt,
                          LICENSE.txt
r6/scripts/tandem/        *.reds
r6/tweaks/tandem/         *.tweak
```

Rules the packer enforces (it stops with a message):

- **What goes in is a list, not a copy of folders.** A name that looks like
  symbols, a recording, a log, a command file, `recent.txt`,
  `last-session.txt`, `recordings/`, `reports/` or `tandem.ini` itself is
  refused whatever the lists become.
- **The DLL is this commit's**: the build's name must be found in
  `tandem.dll`. A tree with uncommitted changes is refused
  (`--allow-dirty` for trying the packer, never for a release).
- **INSTALL.md says what the plugin enforces**: every version in
  `compat::kRequirements`, the game's and redscript's must appear in
  `dist/INSTALL.md`. The exact build (tandem, protocol, the vendored
  commits) is appended as "This build".
- The zips are the same for the same commit and binaries (sorted, dated by
  the commit).

`task package` builds the plugin here first; `task package:files` packages
the DLL already in `build/` (after `tools/build.sh --force build:plugin`).
Both run `natives:check` and `scripts:check`. The build is named at CMake's
configure from the commit, which `build:plugin`'s sources do not see: after
a commit that touched no source the task is skipped and the DLL keeps the
old name, so the packer refuses it. Hence `--force` (`task package` forces
it itself).

The scripts ship whole, `Probe.reds`, `VehicleProbe.reds` and
`TestReset.reds` included: `System.reds` refers to their classes, so
leaving them out would break the compile for every script mod. They do
nothing without `probe = on` or a command file.

**`tandem.ini` is the player's.** A release ships `tandem.default.ini`
(`dist/tandem.ini` in the tree); the plugin copies it to `tandem.ini` at
the first start (`main.cpp`) and an update never touches it. `task deploy`
still installs `dist/tandem.ini` as `tandem.ini` with `--ignore-existing`.

### Cutting a release

1. Bump `project(tandem VERSION …)` if the release number changes; commit.
2. `tools/build.sh --force build:plugin test`, then `task relay:test k8s:check`.
3. `task package:files` (or `task package` to build here).
4. Install the zip into a clean game folder and go through
   [`playtest.md`](../playtest.md) with a second player.
5. Keep the symbols zip with the release: `tools/game.sh bt` and a crash
   report need the `.pdb` of the exact build.

Nothing here publishes: releases are built locally with `task package` and
not hosted anywhere yet (the user's decision, 2026-11-19).

## Compatibility

`core/include/tandem/compat.hpp` holds the rules and the words;
`plugin/src/installed.cpp` reads the facts (Windows file versions);
`tests/compat_test.cpp` covers both, the wording included.

### At startup

`CheckInstallation` (`main.cpp`) runs once when RED4ext loads the plugin,
logs what it found, and keeps it in `Runtime::installation`.

| Checked                           | How                                                                             | When it fails                                  |
| --------------------------------- | ------------------------------------------------------------------------------- | ---------------------------------------------- |
| the game is patch 2.50            | the executable's file version is `compat::kGame` (3.0.88.46235, the SDK's 2.50) | co-op off                                      |
| RED4ext ≥ 1.31.0                  | file version of `red4ext/RED4ext.dll`                                           | co-op off                                      |
| Codeware ≥ 1.21.2                 | file version of `red4ext/plugins/Codeware/Codeware.dll`                         | co-op off                                      |
| TweakXL ≥ 1.12.0                  | file version of `red4ext/plugins/TweakXL/TweakXL.dll`                           | co-op off                                      |
| redscript is installed            | `engine/tools/scc.exe` exists (it has no version to read)                       | a line in the log (no script runs to say more) |
| tandem's scripts and tweaks exist | `r6/scripts/tandem`, `r6/tweaks/tandem` hold files                              | co-op off                                      |
| a version that cannot be read     | a DLL without a version resource                                                | a line in the log                              |

ArchiveXL is read and reported, never required: tandem ships no archive.

**Phantom Liberty is required on every game** (a user decision,
2026-11-19). A V carries into the other games what it owns: items,
cyberware, vehicles, perks. With the expansion those include records that
only exist in a TweakDB with EP1 loaded, and the Relic tree; a game
without them has nothing to show or apply for what the other one sends,
and the shared world would differ between a game with EP1's content and
one without. Rather than handle every such case, tandem requires it.

- It cannot be read when the plugin loads: the game says so itself, later.
  `TandemCoop.ReportExpansion` (Coop.reds) asks the game's own check, the
  one its main menu uses for the expansion banner and logo:
  `inkISystemRequestsHandler.IsAdditionalContentEnabled(n"EP1")` (owned,
  installed and enabled; engine facts §13.37), else `IsAdditionalContentOwned(n"EP1")`, and
  tells the plugin (`TandemNative.SetExpansion`: 1 enabled, 2 owned but
  not installed or not enabled, 3 not owned). It runs when either menu's
  co-op pages are created and when a V enters a game, before any session
  starts.
- Without it `Runtime::Blocked` is true like for a startup problem, with
  its own sentence in `Runtime::Problems` (`compat::ExpansionProblem`):
  the Co-op page's popup says it, a session `tandem.ini` had chosen is
  dropped (with a notice in game), nothing can be hosted or joined. The
  hooks installed at load stay, passing through as in any solo game.
- Until the game has said (`Expansion::Unknown`) nothing is blocked; no
  session can start before a menu or a V exists, and both report first.
- `versions.txt` of a report says what the game said, and whether
  `archive/pc/ep1` holds files (all `tandem-report.exe` can know).

**Co-op off** (`Runtime::Blocked`) means: no hook is installed (the game
runs as without tandem), `Runtime::Choose` refuses any session (so
`TandemNative.IsConfigured()` stays false and `TandemSystem` stays idle),
the Co-op page lists only "Write a bug report" and opens with a popup of
the problems (`TandemNative.CompatProblems`, `TandemCoop.ProblemsText`),
and the recorder (`recorder = on`, `recordings.md`) stays off: its natives
refuse to start it and the menu hides its switch. `compat = off` in `tandem.ini` (developers) ignores the check.

**Why the plugin declares itself runtime-independent.** With
`info->runtime` set to the SDK's latest version, RED4ext refuses to load
the DLL on any other game version, with a line in _its_ log and nothing in
game, and tandem's scripts are then compiled against natives that do not
exist. So the plugin loads everywhere, compares the executable's file
version itself (the same number RED4ext compares) and stays inert. What
still runs on a foreign version is RedLib's RTTI registration, which goes
through the address library like Codeware's; every address tandem itself
resolves is behind a function that a blocked plugin never calls.

**A redscript compile error** is redscript's own business: it shows its
popup at launch and the game runs no script at all. tandem can only
explain it (INSTALL.md, with the real message).

### At a join

| Compared                          | Carried by                                        | Outcome                                       |
| --------------------------------- | ------------------------------------------------- | --------------------------------------------- |
| protocol version                  | `Hello.version`                                   | **refused**; both players read why; no retry  |
| Phantom Liberty, on both games    | `Hello.flags` (`kHelloExpansion`), the host's own | **refused**; both players read why; no retry  |
| tandem build                      | `Install.build` (and `Hello.gameVersion`)         | warning                                       |
| game version                      | `Install.game`                                    | warning                                       |
| tandem's scripts, tandem's tweaks | `Install.scripts`, `Install.tweaks`               | warning (not repeated when the builds differ) |
| the other mods                    | `Install.mods`, `Install.modCount`                | warning; never blocks                         |
| what the other mods add           | `ModList` (hashes and categories; names asked)    | a notice and a status line; never blocks      |

- **`proto::Install`** (appended, Event channel) is sent once by each side
  after the `Welcome`: guest → host, host → guest (after the roster, so the
  guest can name the host). It is never relayed: guests are compared with
  the host only. A value of 0 or "" is unknown and never compared, which is
  how the mock player (a build, nothing else) stays quiet.
  `Endpoint` raises `events::RemoteInstall` with the differences already in
  words (`compat::Differences`); the net thread logs the other game's
  install, shows each difference once as a notice (`ScriptEvent::Notice`),
  and keeps them for the status panel ("N difference(s)…",
  `TandemNative.CompatWarnings`), `last-session.txt` and the report.
- **Protocol mismatch.** Before: the guest's log said `refused: protocol
version 5, the host runs 6`, nothing on the guest's screen (a session
  never entered ends silently), nothing at all on the host's, and the guest
  tried again every 5 s for ever. Now the host answers
  `compat::RefusalForGuest` and raises `events::JoinRefused`; the guest
  shows "Could not join: your tandem speaks protocol 5, the host's (…)
  protocol 6: both players need the same tandem release", the host "NAME
  could not join: their tandem (…) speaks protocol 5, yours (…) protocol
  6: …", and the guest's net thread stops (`compat::IsFinalRefusal`,
  status `Failed`). A `Hello` with another magic is refused as "not a
  tandem game".
- **Phantom Liberty at a join.** A game without it never gets this far by
  itself (co-op is off), but the host does not take a guest's word for it:
  `Hello.flags` says whether the guest's game has it, and a host refuses a
  guest without it ("your game has no Phantom Liberty: tandem needs the
  expansion on every player's game"; the host reads "NAME could not join:
  their game has no Phantom Liberty …"). A host whose own game lacks it
  (only possible with `compat = off`) refuses everyone, with the words the
  other way round. Final, like a version refusal.
- **`kProtocolVersion` is 13** since `NpcShotAt`'s `flags` and the cues
  `Ricochet` and `Charge` (2026-11-21; each version's change is listed above
  it in `protocol.hpp`; 12: `StoryState`'s `kStoryNoFastTravel`; 9: `NpcHealth` got its `flags`; 8: `NpcShotAt`'s time between hits; 7:
  `Hello`'s `flags`, 2026-11-19): each a change to an existing message. `Install`, appended
  the day of 7, would not have bumped it by the rule; builds of protocol 6
  never left the developers' machines.
- **A `Hello` whose layout changed is still refused in words.** Decoding
  is strict, so a `Hello` of another version with another layout does not
  decode at all (protocol 6's is one byte shorter). The first two fields of
  `Hello`, magic and version, never move: `proto::PeekHelloVersion` reads
  them from the raw packet, `net::Event::helloVersion` carries the version
  of a malformed packet that is a `Hello`, and the host refuses it like
  any other version mismatch (it knows no name: "A player could not
  join…"). Keep those two fields first.
- **The files' fingerprint** (`compat::HashTree`) is FNV-1a over the
  sorted, lower-cased relative names and the contents without carriage
  returns, so a checkout with other line endings is the same install.
- **The mods' fingerprint** (`compat::InstalledMods`) is over names only:
  folders of `red4ext/plugins`, `*.archive` of `archive/pc/mod`, entries of
  `r6/scripts` and `r6/tweaks`, folders of
  `bin/x64/plugins/cyber_engine_tweaks/mods` and of `mods` (REDmod), minus
  tandem's own. The names stay on the player's machine: the wire and the
  report carry the number and the count. It cannot tell a gameplay mod
  from a cosmetic one, nor two versions of one mod; hence a warning.

**What the mods add** (`docs/architecture/mod-compat.md`): each game also
sorts its mods by what they add (`mods::Scan`: local, items, vehicles,
characters, looks, gameplay, unknown) and sends them as `ModList`, hashes
and categories only; the other game words the difference ("Judy has mods
you don't (3): 2 with new items..."), and names travel only when the co-op
menu's Mods popup asks. `Differences` no longer words the mods' fingerprint
(the comparison says more); `Install.mods` and `Install.modCount` stay for
the report. Every record from another game goes through one resolver: what
our game lacks is shown as a vanilla record like it, or not at all, and
told.

What differing gameplay mods do to a session is not solved here, only
said: the host's game decides the world (NPCs, damage dealt to them, the
story), so a guest's gameplay mod mostly changes what that guest's own V
can do; a host's changes the world for everyone. TweakDB differences
(weapon stats, records that exist on one side only) are the likely
trouble: an item id one game does not know. A stricter check (a
fingerprint of TweakDB itself, or of the records tandem sends) is the next
step if playtests show it matters.

## Bug reports

`core/include/tandem/report.hpp` (the list of what goes in is its header
comment, INSTALL.md repeats it for players, `tests/report_test.cpp` pins
both the contents and what is kept out).

- **Three ways in, one writer**: Co-op → Write a bug report
  (`TandemNative.WriteReport`, both menus), `tools/game.sh cmd coop report`,
  and `tandem-report.exe` next to the DLL for a game that does not start
  (`plugin/report/main.cpp`: no engine code, the same `report::Write`).
- It writes `red4ext/plugins/tandem/reports/tandem-report-<UTC>.zip`
  (stored, no compression library) and sends nothing.
- **Cleaning** (`report::Redact`): the game's folder → `<game>`; the folder
  after `Users` or `home` in a path → `<user>`; IPv4 addresses keep two
  parts (loopback and 0.0.0.0 stay); `relay_token` and `join` are removed
  from the ini, commented out or not. IPv6 addresses and host names are
  not masked.
- **`last-session.txt`** (`report::SessionSummary`) is written by the net
  thread when a session ends: role, through the relay or not, start,
  length, most players at once, why it ended, the last ping and loss, the
  consistency counters, and the differences found.

## Settings

`dist/tandem.ini` documents every key the parser knows
(`plugin/src/config.cpp`), players' keys first and developers' keys under
their own heading; INSTALL.md has the same table. The defaults are the end
user's: `role = off`, so tandem is passive until a session is started from
the Co-op menu. Keys not in the parser are not documented (`autoload` is
written by the test harness for itself; the recorder's keys arrive with
its branch and belong under the developers' heading unless it becomes a
player feature).

## Not yet verified in game

Everything above was built and unit-tested; the game has not run it.

1. The plugin still loads on 2.50 now that it is runtime-independent, and
   the log's first lines show `game 3.0.88.46235` and real versions for
   RED4ext, Codeware and TweakXL. If one of those DLLs has no version
   resource the log says "version not read" (a warning, not a block): the
   minimum-version check is then blind for that mod.
2. The versions in `compat::kRequirements` are the 2.5 set recorded in
   `research/engine-knowledge.md`; compare them with what is installed on
   the game machine and raise or lower them.
3. The Co-op page's new entry and both popups (`GenericMessageNotification`
   from the main menu and the pause menu), and `coop report` from
   `tools/game.sh cmd`.
4. The notices at a join between two real games (two notices at most in a
   normal case: other mods; the same release otherwise).
5. A blocked start: set `compat::kGame` wrong in a test build, or move
   TweakXL away, and check the game runs untouched and the popup reads
   well.
6. `tandem-report.exe` on Windows and under Proton (it is a console
   program linked like the DLL; it was only ever compiled).
7. What a game without the DLL does with tandem's scripts installed (the
   case the runtime-independent load avoids, but a player can still delete
   the DLL alone).
8. Phantom Liberty: the log says `Phantom Liberty: enabled` on a game that
   has it, as soon as the main menu shows. On a game without it (or with
   the DLC unticked in the store's client): `not owned` or `owned, not
installed or not enabled`, the Co-op popup, and no session from
   `role = host`. `IsAdditionalContentEnabled` is trusted to be right in
   the main menu already (the game's own menu calls it there).
