# AI agents guide — tandem

From-scratch co-op mod for Cyberpunk 2077 patch 2.50: a RED4ext plugin (C++,
Windows DLL cross-compiled on Linux), a mock second player and tests on
Linux, a Go UDP relay shipped as a Nix OCI image for Kubernetes.

**Read first:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), the core
(authority table, threading, protocol, roadmap, index), then only the
`docs/architecture/<topic>.md` files your task touches. Engine facts, each
with a source and a tag: `docs/research/engine/<topic>.md`, indexed by
section number in `docs/research/engine-knowledge.md`.

Where things are (`core/`, `plugin/`, `peer/`, `tests/`, `relay/`, `nix/`,
`deploy/`, `scripts/`, `tweaks/`, `dist/`, `cmake/`, `docs/`, `tools/`,
`vendor/`, `.github/workflows/`) and the toolchain: ARCHITECTURE.md,
Components. The repository is published (MIT, `LICENSE`): keep personal data,
real addresses and credentials out of it (tests use documentation
addresses, 192.0.2.x / 198.51.100.x / 203.0.113.x, and fake names).

## Environment

- **Linux only.** `direnv allow` loads devenv (`devenv.nix`); outside
  direnv: `devenv shell -- task <name>`.
- Entering the shell **runs treefmt over the tree**: files you wrote get
  reformatted (`.clang-format`: LLVM style, 100 columns).
- Every compile goes through **ccache**, one cache per machine shared by
  all worktrees (`~/.cache/tandem/ccache`); `TANDEM_CCACHE=0` turns it off.
  Settings, sizes and the debug-path caveat:
  `docs/architecture/testing.md`, "The compiler cache".

## Commands (Taskfile.yml)

| Task                    | What                                                         |
| ----------------------- | ------------------------------------------------------------ |
| `task build`            | plugin + Linux build                                         |
| `task build:plugin`     | `build/windows/plugin/tandem.dll` (+ `.pdb`)                 |
| `task test`             | core tests (unit + loopback UDP + through a real relay)      |
| `task peer -- …`        | run the mock player (`host --mode mirror` by default)        |
| `task relay:run`        | a local relay open to any host (testing only)                |
| `task relay:test`       | relay vet + tests with the race detector                     |
| `task pilot:test`       | the test pilot: vet, tests, every routine on its simulator   |
| `task relay:image`      | relay OCI image via Nix (`result-relay-image`; `:arm64` too) |
| `task k8s:check`        | render the kustomize base and overlays, validate them        |
| `task check`            | all of the above, and the four checks below                  |
| `task natives:check`    | each `*Natives.reds` matches its `.cpp` (types, fields)      |
| `task scripts:check`    | compile `scripts/tandem` as the game does (seconds, no game) |
| `task strings:check`    | player-facing texts only from `Text.reds`, English + French  |
| `task menu:check`       | co-op menu: settings rows aligned, texts EN + FR, no dupes   |
| `task threads:check`    | script hooks that may run on job threads only read and queue |
| `task refs:check`       | script calls of ours and our TweakDB records exist           |
| `task scenarios:check`  | scenarios name real peer flags, commands, helpers, log lines |
| `task degraded:check`   | degradation ids: catalogue, texts EN + FR, doc rows, uses    |
| `task docs:check`       | doc links and § numbers resolve; no fact lost since HEAD     |
| `task mods:check DIR=…` | other mods' script hooks against tandem's (conflicts)        |
| `task scenario -- …`    | in-game scenarios against the mock player (NAME, all, list)  |
| `task sweep -- …`       | scenarios grouped by role, results file, failures by class   |
| `task sim -- …`         | the simulated second game against the real one (`sim.md`)    |
| `task sim:fuzz K=N`     | N seeds of simulated games against each other, refereed      |
| `task deploy`           | copy the DLL to the game machine (`GAME_HOST`, default anya) |
| `task logs`             | follow the plugin's log on the game machine                  |
| `task package`          | the release zip and its symbols zip (`build/package/`)       |
| `task ccache:stats`     | this machine's compiler cache: size, hits (`:clear` empties) |

Run what your change can break before saying it works. The C++ builds and
tests run in **CI** (`.github/workflows/ci.yaml`, on every push and pull
request): `core build (Linux)` and the `core tests` jobs are `task test`,
`plugin (Windows, clang-cl)` is `task build:plugin` (its `tandem.dll` and
`.pdb` are the run's artifact). Do not run them on the laptop (it runs out
of memory); push the branch and read the run (Git, below). Locally, the
cheap ones: `task relay:test` for `relay/`, `task pilot:test` for
`tools/pilot/`, `task k8s:check` for `deploy/`,
`task natives:check`, `task strings:check`, `task menu:check`,
`task threads:check`, `task refs:check`, `task scenarios:check`,
`task degraded:check` and `task docs:check`;
`task scripts:check` for `scripts/` needs the game machine.

`tools/build.sh TASK…` builds where there is room (Git, below), when a
build must happen outside CI (an in-game test on the game machine).
`tools/remote.sh TASK…` runs a task on the game machine (more cores, its own
build slots) and copies the DLL, `.pdb` and `tandem-peer` back; then
`TANDEM_PREBUILT=1 tools/scenario.sh …` deploys that DLL as is. Details:
`docs/architecture/testing.md`.

## Rules

### Code provenance

- **Never copy code from CyberpunkMP, Night City Online or the campaign
  fork** (Tilted Phoques' source-available licence). Facts (hashes, offsets,
  measured numbers, what crashed) are fine and are cited in
  `docs/research/`. Codeware, RED4ext.SDK, RedLib and SharedPunk are MIT:
  their code may be used with attribution.

### Protocol (`core/include/tandem/protocol.hpp`)

- Messages are alternatives of `MessageVariant`: **append new ones at the
  end**, never reorder or remove. Any change to an existing message bumps
  `kProtocolVersion`. Add the message to `NameOf`, pick its channel in
  `ChannelOf`, add a round-trip case to `tests/protocol_test.cpp`, and
  give it its referee rules (`peer/sim/src/referee.cpp`): its `SaidBy`
  line (who may send it: without one, the referee does not compile) and
  its checks, with a case in `tests/referee_test.cpp`. A violation fails
  any test that does not expect it (`test::ExpectReferee`).
- Code uses `proto::Message` (and `SessionEvent`), a `ClosedVariant`
  (`tandem/closed_variant.hpp`): std::visit, get_if, holds_alternative and
  index() work as on the variant, but its copies, moves and conversions are
  compiled only in `core/src/message*.cpp` (`session_event*.cpp`). Keep it
  that way: no `std::variant<…every message…>` by value elsewhere, no
  `emplace`, no `in_place_index`; libstdc++ compiles each of those in O(N²)
  for N alternatives, GBs of compiler memory per translation unit
  (`docs/architecture/testing.md`, "Compile memory").
- High-rate messages stay small (`PlayerState` < 64 bytes is tested). A
  stream sent every frame or tick goes in `tests/budget_test.cpp` (64
  kbit/s each way) with a relevance rate; compact wire forms are encoded
  and decoded inside `Endpoint` (`docs/architecture/netcode.md`).
- `Hello`'s first two fields (magic, version) never move: a host reads them
  from a `Hello` it cannot decode and refuses it in words
  (`PeekHelloVersion`).
- Phantom Liberty is required on every game, checked at startup and at a
  join; why and how: `docs/architecture/release.md`.
- Consistency (`docs/architecture/consistency.md`): a state message is
  idempotent (the receiver drops a repeat); a new domain of host-owned state
  goes into the host's digests and its resync (`Endpoint::DigestFor`,
  `SendDomainsTo`). A one-shot event (a hit, a reward) is listed in
  `consistency::IsOneShot`; a time-sensitive one the game queues gets an
  age limit (`consistency::DropStale`).
- Absolute times are `uint64_t` microseconds on the host clock; never store
  them in a float.

### Plugin threading

- The net thread owns the `Endpoint` and never calls into the engine.
- The game thread never touches the socket. Exchange data through queues.
- Engine callbacks reached from redscript may run on job threads: enqueue
  only; change state on the game thread's frame tick.
- No lock of ours is held across an engine call: one that waits stops
  every thread taking that lock, the net thread too (`ModRecords`). An
  engine hook logs after releasing its lock (`engine/quests.cpp`).

### Plugin sources and scripts

- Every file that includes RED4ext SDK headers starts with
  `#include <RED4ext/RED4ext.hpp>` in **its own include group** (a blank line
  after it): the SDK's headers only compile after it, and clang-format would
  sort it below the others inside a shared group.
- Use `__builtin_offsetof` in `static_assert`s: the MSVC CRT's `offsetof` is
  not a constant expression under clang.
- **A redscript compile error disables every script in the game.** Run
  `task scripts:check` after every `.reds` change (`task deploy`, `reload`
  and `check` run it too): it compiles `scripts/tandem` with the game's own
  redscript (scc 0.5.32, `nix/redscript.nix`; bump it when the game's
  `engine/tools/scc.exe` changes) against the game's original bundle and the
  other mods' scripts (Codeware, ArchiveXL…), copied read-only from the game
  machine into `~/.cache/tandem/redscript-base`, and prints errors as the
  game's log does. It also refuses the two traps below. Still check vanilla
  and Codeware names before using them (NCO's working 2.5 scripts and
  Codeware's `scripts/` are good references; the game's own scripts as
  text at its version: `tools/scripts-decompile.sh`): a name that compiles
  can still be the wrong one.
- **Delete a body Codeware spawned with `TandemEntities.Delete`**
  (`Entities.reds`), never `DynamicEntitySystem.DeleteEntity`: deleted
  before its stub exists, it is freed under the population system
  (`docs/research/engine/vehicles.md` §13.140).
- **No script method defined twice** (look after every merge): redscript
  compiles a class with two definitions of one method silently, and the game
  then dies in the script VM (frozen after the loading screen) at its first
  call (`tools/scripts-duplicates.py`).
- **Give every value-type local a value where it is declared**
  (`let id = TDBID.None();`, `n"None"`, `EntityID.FromHash(0ul)`, `0.0`,
  `new Vector4(…)`): the script VM does not clear `let x: TweakDBID;`, and a
  path that reads it before assigning it gets whatever the slot held (a
  perk's record sent as V's car, 2026-11-18;
  `tools/scripts-uninitialized.py`). A local assigned on every path before
  any read may keep its bare declaration with the comment
  `// assigned on every path` on that line.
- **No array intrinsic or struct field on a call's result**: put it in a
  local first (`let players = system.PuppetPlayers();`). The VM reads
  `ArrayContains(f(), x)`, `ArraySize(f())`, `f()[i]` or
  `GetWorldPosition().X` from some other variable: the police forgot
  every remote V's level on the tick it was learnt (2026-11-21;
  `tools/scripts-temporaries.py`).
- **Keep `TandemSystem.RunCommand` from growing**: a test command goes in
  a command function of its own (`RideCommand`, `CoopCommand`): eight more
  branches there and the game died at its next command (a call through a
  null pointer on the script thread, 2026-11-22).
- **A hook that may run on a job thread only reads, calls the wrapped
  method and enqueues** (`TandemStreaming.QueueStreamed`, or another native
  that only enqueues); its system acts on the tick. Such hooks: a wrap of a
  class that exists per entity, component or persistent state, any
  `cb func` (wrapped or ours), lifecycle methods (`OnGameAttached`,
  `OnDetach`…), Codeware `Entity/*` callbacks. Wraps that pushed into a
  system's array there corrupted the heap while a save loaded (2026-11-20).
  `tools/scripts-threads.py` (`task threads:check`, run by `scripts:check`
  and CI) follows every call a hook makes and refuses a write to a script
  object that is not the hook's own; `-- --list` shows each hook. A hook
  that must act in place says why in the comment above it:
  `// game thread only: <how it was proven>` or
  `// job thread safe: <why>`. `tools/scripts-threads.known` holds the
  hooks older than the check, neither proven nor reworked: it only
  shrinks. Each of its hooks starts with `TandemNative.HookRan(n"File.Class.Method");`,
  the thread probe: the log names those that ran beside the script tick
  (`tools/game.sh threads`; how to read it: `testing.md`, "The thread
  probe"). A wrap of a class new to `tools/scripts-threads.classes` needs
  its ancestry there (`--classes VANILLA.reds`).
- **No player-facing literal**: every text the player sees is a key of
  `scripts/tandem/Text.reds` (English and French), shown through
  `TandemText` (notices, hints with the player's own binding, never a key
  name); `task strings:check` (run by `scripts:check`) refuses the rest
  (`docs/architecture/strings.md`).
- Each `scripts/tandem/*Natives.reds` must match its `plugin/src/*natives.cpp`
  exactly (pairs in Layout): `task natives:check` (run by `task deploy`)
  reads every pair; redscript itself never sees the DLL.
- Native structs hold no `Vector4`/`Quaternion` fields: the script VM aligns
  struct locals to 8 and a 16-byte aligned field crashes it.
- A line per puppet, proxy, hit, shot or frame is `log::Debug` /
  `TandemNative.LogDebug` (`log = debug`, which the test roles set);
  `info` is quiet (`netcode.md`, Frame cost).

### In-game testing

How (scenarios, every `tandem-peer` mode and flag, `tools/game.sh`
commands, recordings): `docs/architecture/testing.md` and `recordings.md`.

- **The loop:** change, `task test`, then `task scenario -- NAME` (or `all`)
  for what the change touches (all, with a results file: `task sweep`).
  `tools/scenario.sh` deploys, sets the game's role, restarts only when
  the DLL, scripts, tweaks or role changed, drives the mock player and checks the game's log; a failure keeps its evidence
  under `.cache/scenarios/`. Add a scenario (`tools/scenarios/*.sh`, a few
  lines) for each feature you verify in game, so it stays verified.
- **The referee** (`docs/architecture/sim.md`) checks every message the
  mock player hears against the protocol and the authority table, in every
  mode: a `referee:` line in its log fails any scenario. A new message or
  rule gets its referee check (`peer/sim/src/referee.cpp`) and a case in
  `tests/referee_test.cpp`.
- **Instances** (`tools/instance.sh`, `docs/architecture/two-instances.md`;
  tried on anya: two games in a session over loopback, a guest froze once):
  more games apart from the player's, in `/data/Steam/tandem-instances`.
  `tools/game.sh` and `tools/scenario.sh` act on one game,
  `TANDEM_INSTANCE=NAME` or the Steam game, its processes by pid;
  `tools/sweep.sh --instances N` runs N at once (`two-instances.md`,
  "Sweeps on two games"; not yet run in game: until it has been, run them
  beside instances only through it). `tools/instance.sh` runs beside a
  Steam game only when `tools/game.sh` started (and recorded) it; any other
  is the player's: `tools/instance.sh who` says whose each game is.
- **Every scenario starts from `test reset`** (`tools/game.sh test-reset`,
  `scripts/tandem/TestReset.reds`), which `tools/scenario.sh` runs before
  each one: the save loads as the player left it (in a car, mid-mission,
  wanted, downed) and each scenario leaves state behind. A game that is not
  `test reset: ready` fails the scenario; one that needs another spot
  teleports after the reset. State a test adds must be cleared by it.
- `tools/game.sh role host|join|player` picks the game's `tandem.ini`;
  **leave the machine in `player`** when testing is over (it restores the
  player's own, kept as `tandem.ini.player`).
- **Starting is tested by the scenarios themselves**: every game start of
  a sweep must reach play; a crash while loading is a failure to chase.
  `tools/game.sh launches N` (each start until the save plays or the game
  crashes; `testing.md`, "Launches") is not a required step any more
  (the user, 2026-11-21): use it only to hunt a loading crash.
- A game that freezes (0% CPU) has usually **crashed** (under Proton the
  crash handler waits forever): run `tools/game.sh bt` before restarting it
  (every thread's stack, tandem's frames named from its .pdb). The plugin's
  watchdog logs `watchdog: game thread silent` after 5 s without a tick
  (`plugin/src/runtime.hpp`); a hang that stops the net thread too (no
  watchdog line) is in `red4ext/plugins/tandem/stall.log`, with the stuck
  threads' stacks (`plugin/src/stall.hpp`).
- anya's keyboard is AZERTY: simulated keys land on other letters. Restart
  the game only for script or plugin changes; `tools/game.sh cmd …` needs
  none.

### No silent fallback (`docs/architecture/degradations.md`)

- **Whatever takes a lesser path than the intended one registers it**, by
  an id of the catalogue (`core/src/degraded.cpp`): a state that holds
  (`TandemDegraded.State(id, why)` … `Over(id)`, `Chosen` for a
  `tandem.ini` switch; `Degradations::Get().Start` in the plugin; an engine
  feature by `engine::FeatureOff`) or a fallback taken for one body
  (`TandemDegraded.Fallback(id, why)`). A log line alone is a silent
  failure: the registry is what the player (a notice, the status panel),
  the other players (`proto::Degraded`), the session's start and end lines
  and the tests read. A core id (co-op cannot be right without it) means no
  session. A new id: its catalogue row, its text in `Text.reds`
  (`Degraded-Id-…`, English and French), its row in `degradations.md`
  (`task degraded:check`).
- **A scenario fails on an entry it did not declare** (`expect_degraded ID`,
  `allow_fallback ID`): one that tests a fallback on purpose says so; any
  other entry is a bug to fix, not to allow.

### Engine access

- Prefer RTTI/script names and Codeware; then address-library hashes; raw
  offsets or vtable slots only behind a startup layout check that disables
  the feature on a mismatch. Write down the game version a layout was
  measured on. A hash is resolved with `engine::Find` (never
  `UniversalRelocFunc` or `UniversalRelocBase::Resolve`: they end the
  process on a missing hash), and the feature gets its check in
  `CheckAccess` and its row in `docs/architecture/engine-access.md`.
- Before using an engine fact from `docs/research/`, read its tag (meanings:
  `engine-knowledge.md`). When you verify something in game, update its tag
  and source there.
- `ExecuteFunction` on a function that returns a value needs an out slot,
  or it silently does nothing.
- To find a layout, a vtable or a function, use `tools/re/` (live RTTI dump,
  memory reader, disassembler, Ghidra decompiler): it only reads, so it can
  run while the game is being played. Reference functions by
  address-library hash (`tools/re/addr.py`). Record findings as a §13.x
  section of the fitting `docs/research/engine/<topic>.md`, tagged, with its
  row in the index (`engine-knowledge.md`).

### Authority

- NPCs are owned per NPC, by the game that runs them, not by the host
  (`docs/architecture/regions.md`): in scripts ask `TandemOwn` or
  `TandemNpcs.Owns`, never `TandemNative.IsHost()`, about an NPC.
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
- **Tests never run on the Steam game: on instances** (`tools/instance.sh`,
  each with its own install, prefix and saves; `tools/game.sh`,
  `tools/scenario.sh` and `tools/sweep.sh` take instance `a` unless
  `TANDEM_INSTANCE` names another, and refuse the Steam game without
  `TANDEM_STEAM_GAME=1`). A game that hosts autosaves: a day of tests on the
  Steam game overwrote the player's 20 autosave slots and a quick save, and
  each start loaded the newest, so the tests' state drifted (2026-11-21).
  The player's saves are theirs: never write, delete or load-and-save them
  without being asked.
- **Launch, kill or restart the game only with the user's go-ahead** (a
  request for in-game work counts): they may be playing or streaming from
  it. Check `pgrep -af Cyberpunk2077.exe` first.
- RED4ext logs are in `red4ext/logs/`; the plugin's own log is
  `red4ext/logs/tandem-*.log`.

### Relay and deployment (`docs/architecture/relay.md`)

- Standard library only unless there is a strong reason (`vendorHash`
  becomes non-null the day a dependency is added). Container: non-root,
  read-only, capability-free, no shell. NetworkPolicy egress: empty unless
  a feature needs outbound traffic; say why in the manifest.
- Never answer an unregistered address's data; never send a reply larger
  than the request that caused it (both tested). Only registration-type
  requests (register, resolve a join code, probe), padded to 64 bytes, are
  answered from an unregistered address (a redirect is such an answer). An
  introduction (two answers to one request) goes only to members of the
  requester's own session, both together smaller than the request.
- Open by default: a host with no token pays a proof of work per session
  (a stateless challenge, answered smaller than its request) and is capped
  (sessions per source and in all, bandwidth per guest link); a token
  bypasses it, `--require-token` closes the relay (`relay/internal/relay/open.go`).
- Behind a PROXY v2 tunnel (`--proxy-protocol`, playit.gg): only
  `--trusted-proxies` speak for clients, and every rule about a player
  (lookup limits, `Observed`, `Introduce`) uses the header's address, the
  size rule the payload without the header (`relay/internal/relay/proxy.go`).
- Replicas and regions share nothing and never talk to each other: the
  peers and regions lists are configuration, addresses not names (no DNS).
  The failed-lookup budget is the whole world's, split between replicas.
- Join codes stay expensive to guess: keep the per-source and total lookup
  limits (`TestCodeGuessingIsSlow`, `TestCodeFailuresFromEveryoneAreCapped`)
  and the code space (6 characters of base32, `crypto/rand`).
- The wire format (`relay/internal/relay/wire.go`) has a C++ twin in
  `core/src/relay.cpp`: change both, and only by appending message types.

### Git

- The public repository is `RPCU/tandem` on GitHub (remote `rpcu`): no
  secrets, personal data or real addresses in it (top of this file). Work
  on a branch in its own worktree, **commit** (small, focused commits,
  hooks passing: treefmt, shellcheck), **push the branch** to `rpcu` and
  open a pull request into `main` (`gh pr create --fill`). CI
  (`.github/workflows/`) runs on every push and pull request: core's build
  (Linux), its tests in four shards, `tandem.dll` (Windows), Go, the
  manifests and the docs checks; a newer push cancels the older run of the
  same ref (`gh run watch`, `gh run view --log-failed`; the job summary
  lists the compiler cache's hits and the heaviest compiles). Merge with
  `gh pr merge N --rebase --admin --delete-branch`: the user allows admin
  merges without waiting for CI, and fast-forward pushes to `main` by the
  agent coordinating the merges (`git push rpcu HEAD:main`). Testing is
  the user's, by hand or in game on anya; run scenarios when asked.
- Parallel work: independent tasks (research, reverse engineering, a
  feature that needs no game) can run in separate agents, each in its own
  git worktree; the game machine is one shared resource, so in-game testing
  (deploys, `tools/scenario.sh`) stays with one agent at a time.
- **Builds outside CI go through build slots** (`tools/build-slot.sh`): one on the
  laptop (14 GB of RAM, `-j8`), three on anya (`-j5` each, niced; one task
  at a time per worktree). Slot 0 is `/tmp/tandem-build.lock`. **Use
  `tools/build.sh TASK…`**: it runs the task on anya (`tools/remote.sh`)
  when nobody plays there and a slot is free, and here otherwise. Our own
  test instances (`tools/instance.sh`, under `tandem-instances/`) are not
  someone playing. `tools/remote.sh` refuses while the user's game runs on
  anya (our own scenario runs set `TANDEM_REMOTE_WHILE_PLAYING=1` if they
  must build).
- Vendored dependencies are submodules; don't edit files under `vendor/`.
  Work around upstream issues in our own build (see the `sdk-casefix` link
  in `plugin/CMakeLists.txt`).

### Documentation

- Keep the docs and this file true: update them in the same change as the
  code they describe (layout, commands, rules, roadmap state). A subsystem's
  design goes in its `docs/architecture/<topic>.md`; `ARCHITECTURE.md`
  changes only for the authority table, the roadmap, a cross-cutting rule or
  an index line.
- Every agent pays for every token here: state a fact once, in the file its
  reader opens, without narrative. Before committing a doc edit:
  `task docs:check`; after a rewrite, `task docs:check -- facts --base REV`
  lists every number, identifier, path and tag that went missing.
  `tools/docs-tables.py` unpads wide tables. An unindented line that is only
  a code span stops the mdsh hook.
