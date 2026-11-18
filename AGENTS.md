# AI agents guide — tandem

tandem is a from-scratch co-op mod for Cyberpunk 2077 patch 2.50: a RED4ext
plugin (C++, Windows DLL cross-compiled on Linux), a mock second player and
tests on Linux, and a Go UDP relay shipped as a Nix OCI image for Kubernetes.

Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) before changing anything:
it holds the authority model, the threading rules and the roadmap. Engine
facts gathered from earlier projects are in
[`docs/research/engine-knowledge.md`](docs/research/engine-knowledge.md),
each with its source and a tag.

## Layout

```
core/          protocol + transport + session, platform-independent (C++23)
plugin/        RED4ext plugin (tandem.dll): entry points, config, net thread
peer/          tandem-peer, the mock player (Linux)
tests/         doctest tests of core/ (Linux)
relay/         tandem-relay (Go, stdlib only): cmd/tandem-relay, internal/relay
nix/           relay package and OCI image, redscript (the game's scc, built for Linux); default.nix exposes them
deploy/        kubernetes/base: kustomize base for the relay
scripts/       redscript, installed to r6/scripts/tandem (Natives.reds mirrors plugin/src/natives.cpp, StoryNatives.reds story_natives.cpp, CombatNatives.reds combat_natives.cpp, SyncNatives.reds sync_natives.cpp)
tweaks/        TweakXL records, installed to r6/tweaks/tandem (the puppet records, their attitude groups, the player marker)
dist/          files installed next to the DLL (tandem.ini)
cmake/         clang-cl-msvc.cmake: the Windows cross toolchain
docs/          ARCHITECTURE.md, research/ (engine facts, sources; data/: RTTI and skeleton dumps)
tools/         game.sh (drive the game), scenario.sh + scenarios/ (in-game tests), check-natives.py, scripts-check.sh, re/ (read-only reverse engineering, see its README)
vendor/        submodules: RED4ext.SDK, RedLib, enet, zpp_bits, doctest
```

## Environment

- **Linux only.** `direnv allow` loads devenv (`devenv.nix`): CMake, Ninja,
  clang-cl/lld-link (unwrapped LLVM 20, through `TANDEM_*` variables), xwin,
  Go, go-task, kustomize, kubeconform. nixbook's shared hooks (treefmt with
  clang-format, nixfmt, prettier, gofumpt, shfmt; shellcheck) come from
  `npins` → `nixbook/devenvModules/devenv.nix`.
- Entering the shell **runs treefmt over the tree**: expect files you wrote
  to be reformatted (`.clang-format`: LLVM style, 100 columns).
- The Windows sysroot (MSVC CRT + Windows SDK 10.0.22621) lives in
  `$TANDEM_XWIN` (default `~/.cache/tandem/xwin`); `task sysroot` fetches it.
  SDK 10.0.26100 does not work with xwin.
- Outside direnv: `devenv shell -- task <name>`.

## Commands (Taskfile.yml)

| Task                 | What                                                         |
| -------------------- | ------------------------------------------------------------ |
| `task build`         | plugin + Linux build                                         |
| `task build:plugin`  | `build/windows/plugin/tandem.dll` (+ `.pdb`)                 |
| `task test`          | core tests (unit + loopback UDP + through a real relay)      |
| `task peer -- …`     | run the mock player (`host --mode mirror` by default)        |
| `task relay:run`     | a local relay open to any host (testing only)                |
| `task relay:test`    | relay vet + tests with the race detector                     |
| `task relay:image`   | relay OCI image via Nix (`result-relay-image`)               |
| `task k8s:check`     | render the kustomize base and validate it (kubeconform)      |
| `task check`         | all of the above                                             |
| `task natives:check` | `Natives.reds` matches `natives.cpp` (types, struct fields)  |
| `task scripts:check` | compile `scripts/tandem` as the game does (seconds, no game) |
| `task deploy`        | copy the DLL to the game machine (`GAME_HOST`, default anya) |
| `task logs`          | follow the plugin's log on the game machine                  |

`tools/remote.sh TASK…` runs a task on the game machine instead (more cores):
it syncs the working tree to `~/.cache/tandem/src/<worktree>` there, runs
the task in devenv under that machine's own build lock at low priority, and
copies `build/windows/plugin/tandem.{dll,pdb}` (and, after `build`,
`build:linux` or `test`, `build/linux/peer/tandem-peer`) back. Then
`TANDEM_PREBUILT=1 tools/scenario.sh …` deploys that DLL as is
(`task deploy:files`) instead of rebuilding it here.

Run the checks your change can break before saying it works: `task test` for
`core/`, `task build:plugin` for anything compiled into the DLL,
`task relay:test` for `relay/`, `task k8s:check` for `deploy/`,
`task scripts:check` for `scripts/`.

Through the relay by hand: `task relay:run`, then `task peer -- host --relay
127.0.0.1` (it logs `join code K7Q-2M9`) and `task peer -- join --code
K7Q-2M9 --relay 127.0.0.1`. `task test` does the same (`relay-loopback`,
`tests/relay_loopback.sh`: it builds the relay with Go and runs the
`relay` suite of `tandem-tests` against it).

## Rules

### Code provenance

- **Never copy code from CyberpunkMP, Night City Online or the campaign
  fork** (Tilted Phoques' source-available licence). Facts (hashes, offsets,
  measured numbers, what crashed) are fine and are cited in
  `docs/research/`. Codeware, RED4ext.SDK, RedLib and SharedPunk are MIT:
  their code may be used with attribution.

### Protocol (`core/include/tandem/protocol.hpp`)

- Messages are `std::variant` alternatives: **append new ones at the end**,
  never reorder or remove. Any change to an existing message bumps
  `kProtocolVersion`. Add the message to `NameOf`, decide its channel in
  `ChannelOf`, and add a round-trip case to `tests/protocol_test.cpp`.
- High-rate messages stay small (`PlayerState` < 64 bytes is tested).
- Shared state stays consistent (`docs/ARCHITECTURE.md`, Consistency): a
  state message must be idempotent (the receiver drops a repeat), and a new
  domain of host-owned state goes into the host's digests and its resync
  (`Endpoint::DigestFor`, `SendDomainsTo`). A one-shot event (a hit, a
  reward) is listed in `consistency::IsOneShot`; a time-sensitive one the
  game queues gets an age limit (`consistency::DropStale`).
- Absolute times are `uint64_t` microseconds on the host clock; never store
  them in a float.

### Plugin threading

- The net thread owns the `Endpoint` and never calls into the engine.
- The game thread never touches the socket. Exchange data through queues.
- Engine callbacks reached from redscript may run on job threads: enqueue
  only; change state on the game thread's frame tick.

### Plugin sources

- Every file that includes RED4ext SDK headers starts with
  `#include <RED4ext/RED4ext.hpp>` in **its own include group** (a blank line
  after it): the SDK's headers only compile after it, and clang-format would
  sort it below the others inside a shared group.
- After a merge, look for a script method kept twice: redscript compiles a
  class with two definitions of one method silently, and the game then dies
  in the script VM (frozen after the loading screen) at its first call.
  `task scripts:check` refuses it (`tools/scripts-duplicates.py`).
- Use `__builtin_offsetof` in `static_assert`s: the MSVC CRT's `offsetof` is
  not a constant expression under clang.
- `scripts/tandem/Natives.reds` must match `plugin/src/natives.cpp` exactly
  (and `StoryNatives.reds` `story_natives.cpp`, `CombatNatives.reds`
  `combat_natives.cpp`, `SyncNatives.reds` `sync_natives.cpp`; `task
natives:check` reads every pair).
  A redscript compile error disables **every** script in the game, so run
  `task scripts:check` after every `.reds` change (`task deploy`, `reload`
  and `check` run it too): it compiles `scripts/tandem` with the game's own
  redscript (scc 0.5.32, `nix/redscript.nix`) against the game's original
  bundle and the other mods' scripts (Codeware, ArchiveXL…), copied read-only
  from the game machine into `~/.cache/tandem/redscript-base`, and prints
  errors as the game's log does. Bump `nix/redscript.nix` when the game's
  `engine/tools/scc.exe` changes. Still check vanilla and Codeware names
  before using them (NCO's working 2.5 scripts and Codeware's `scripts/`
  are good references): a name that compiles can still be the wrong one.
  `task natives:check` (run by `task deploy`) compares the natives; redscript
  itself never sees the DLL. Native structs hold no `Vector4`/`Quaternion` fields: the script VM
  aligns struct locals to 8 and a 16-byte aligned field crashes it.

### In-game testing

- **The loop:** change, `task test`, then `task scenario -- NAME` (or
  `all`) for what the change touches. `tools/scenario.sh` deploys, switches
  the game's role, restarts only when the DLL, scripts, tweaks or role
  changed (scripts alone would be hot-reloaded with RedHotTools, which is
  not installed on anya), drives the mock player and checks the game's
  log. A failure keeps its evidence under `.cache/scenarios/`: logs,
  a screenshot, and every thread's backtrace if the game thread stopped.
  Add a scenario (`tools/scenarios/*.sh`, a few lines) for each feature
  you verify in game, so it stays verified.
- The plugin's watchdog logs `watchdog: game thread silent` when the game
  thread stops ticking for 5 s (`plugin/src/runtime.hpp`).
- `tools/game.sh role host|join|player` picks the game's `tandem.ini` (the
  player's own is kept as `tandem.ini.player`; `player` restores it, and
  is the state to leave the machine in when testing is over).
- `task deploy` installs the DLL, scripts and tweaks on the game machine.
  A test `tandem.ini` with `role = host` and `autoload = true` loads the last
  save on its own; run `task peer -- join anya --mode mirror` here.
- Test without a second player or input: `tandem-peer` modes (`front`
  echoes your V in view; `drive` drives a car round you; `host --time
--weather` plays the host while the game joins as a guest with
  `role = join`, `address = <this machine>`; `--latency`, `--jitter`,
  `--loss`, `--reorder` put a bad link in between; `--travel-at` and
  `--offer-regroup` test fast travel together), and `tools/game.sh cmd`
  commands (`teleport home`, `where`, `vehicle ride`, `pose loop`, `travel`,
  `regroup accept`…) that
  need no restart. Restart only for script or plugin changes. anya's
  keyboard is AZERTY: simulated keys land on other letters.
- The game is launched through the Steam already running in anya's Sway
  session (`steam steam://rungameid/1091500` with that session's
  environment). Read `red4ext/logs/tandem-*.log` for the outcome.
- A game that freezes (0% CPU) has usually **crashed**: under Proton the
  game's crash handler waits forever. Run `tools/game.sh bt` before
  restarting it: every thread's stack, tandem's frames named from its .pdb.

### Engine access

- Prefer RTTI/script names and Codeware; then address-library hashes; raw
  offsets or vtable slots only behind a startup layout check that disables
  the feature on a mismatch. Write down the game version a layout was
  measured on.
- Before using a fact from `docs/research/engine-knowledge.md`, read its tag.
  When you verify something in game, update its tag and source there.
- `ExecuteFunction` on a function that returns a value needs an out slot,
  or it silently does nothing.
- To find a layout, a vtable or a function, use `tools/re/` (live RTTI dump,
  memory reader, disassembler, Ghidra decompiler): it only reads, so it can
  run while the game is being played. Reference functions by
  address-library hash (`tools/re/addr.py`), and record findings in
  `docs/research/engine-knowledge.md` §13 with their tag.

### Authority

- Follow the table in `docs/ARCHITECTURE.md`: one owner per piece of state.
  Do not introduce "each game simulates its own copy and we sync the
  result" for shared world state.

### The game machine

- The game runs on **anya** (`ssh anya`, Proton), installed at
  `/data/Steam/steamapps/common/Cyberpunk 2077`. The player's other mods
  live there too.
- Only ever write inside `red4ext/plugins/tandem/` (and later
  `r6/scripts/tandem/`). Never overwrite the player's `tandem.ini`
  (`task deploy` uses `--ignore-existing`).
- **Launch, kill or restart the game only with the user's go-ahead** (a request
  for in-game work counts): they may be playing or streaming from it. Check
  `pgrep -af Cyberpunk2077.exe` first.
- RED4ext logs are in `red4ext/logs/`; the plugin's own log is
  `red4ext/logs/tandem-*.log`.

### Relay and deployment

- Standard library only unless there is a strong reason; `vendorHash`
  becomes non-null the day a dependency is added.
- Keep the container non-root, read-only, capability-free, with no shell.
- Keep the NetworkPolicy's egress empty unless a feature needs outbound
  traffic, and say why in the manifest.
- The relay must never answer an unregistered address's data and never send
  a reply larger than the request that caused it; there are tests for both.
  Only registration-type requests (register, resolve a join code), padded
  to 64 bytes, are answered from an unregistered address.
- Join codes must stay expensive to guess: keep the per-source and total
  lookup limits (`TestCodeGuessingIsSlow`, `TestCodeFailuresFromEveryoneAreCapped`)
  and the code space (6 characters of base32, `crypto/rand`).
- The relay's wire format (`relay/internal/relay/wire.go`) has a C++ twin in
  `core/src/relay.cpp`: change both, and only by appending message types.

### Git

- The repository is local only. **Commit locally** (small, focused
  commits, hooks passing: treefmt, shellcheck); **never push or create a
  remote** unless the user asks.
- Parallel work: independent tasks (research, reverse engineering, a
  feature that needs no game) can run in separate agents, each in its own
  git worktree; the game machine is one shared resource, so in-game
  testing (deploys, `tools/scenario.sh`) stays with one agent at a time.
- **One build at a time** on the laptop: every build or test goes through
  the shared lock, with limited parallelism:
  `CMAKE_BUILD_PARALLEL_LEVEL=8 flock /tmp/tandem-build.lock direnv exec . task test`
  (same for `task build`, `task check`). Prefer `tools/remote.sh build:plugin
test` on anya when the laptop's lock has a queue: anya has its own lock.
- Vendored dependencies are submodules; don't edit files under `vendor/`.
  Work around upstream issues in our own build (see the `sdk-casefix` link
  in `plugin/CMakeLists.txt`).

### Documentation

- Keep `docs/ARCHITECTURE.md` and this file true: update them in the same
  change as the code they describe (layout, commands, rules, roadmap state).
