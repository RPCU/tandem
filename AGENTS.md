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
`vendor/`) and the toolchain: ARCHITECTURE.md, Components.

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

| Task                 | What                                                         |
| -------------------- | ------------------------------------------------------------ |
| `task build`         | plugin + Linux build                                         |
| `task build:plugin`  | `build/windows/plugin/tandem.dll` (+ `.pdb`)                 |
| `task test`          | core tests (unit + loopback UDP + through a real relay)      |
| `task peer -- …`     | run the mock player (`host --mode mirror` by default)        |
| `task relay:run`     | a local relay open to any host (testing only)                |
| `task relay:test`    | relay vet + tests with the race detector                     |
| `task pilot:test`    | the test pilot: vet, tests, every routine on its simulator   |
| `task relay:image`   | relay OCI image via Nix (`result-relay-image`)               |
| `task k8s:check`     | render the kustomize base and validate it (kubeconform)      |
| `task check`         | all of the above, and the four checks below                  |
| `task natives:check` | each `*Natives.reds` matches its `.cpp` (types, fields)      |
| `task scripts:check` | compile `scripts/tandem` as the game does (seconds, no game) |
| `task strings:check` | player-facing texts only from `Text.reds`, English + French  |
| `task docs:check`    | doc links and § numbers resolve; no fact lost since HEAD     |
| `task scenario -- …` | in-game scenarios against the mock player (NAME, all, list)  |
| `task sim -- …`      | the simulated second game against the real one (`sim.md`)    |
| `task sim:fuzz K=N`  | N seeds of simulated games against each other, refereed      |
| `task deploy`        | copy the DLL to the game machine (`GAME_HOST`, default anya) |
| `task logs`          | follow the plugin's log on the game machine                  |
| `task package`       | the release zip and its symbols zip (`build/package/`)       |
| `task ccache:stats`  | this machine's compiler cache: size, hits (`:clear` empties) |

Run what your change can break before saying it works: `task test` for
`core/`, `task build:plugin` for anything compiled into the DLL,
`task relay:test` for `relay/`, `task k8s:check` for `deploy/`,
`task scripts:check` for `scripts/`, `task docs:check` for docs.

`tools/build.sh TASK…` builds where there is room (Git, below).
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

- Messages are `std::variant` alternatives: **append new ones at the end**,
  never reorder or remove. Any change to an existing message bumps
  `kProtocolVersion`. Add the message to `NameOf`, pick its channel in
  `ChannelOf`, add a round-trip case to `tests/protocol_test.cpp`.
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
  Codeware's `scripts/` are good references): a name that compiles can
  still be the wrong one.
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
  for what the change touches. `tools/scenario.sh` deploys, sets the game's
  role, restarts only when the DLL, scripts, tweaks or role changed, drives
  the mock player and checks the game's log; a failure keeps its evidence
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
  `tools/game.sh` and `tools/scenario.sh` know one game and kill it **by
  name**: never run them while instances run.
- **Every scenario starts from `test reset`** (`tools/game.sh test-reset`,
  `scripts/tandem/TestReset.reds`), which `tools/scenario.sh` runs before
  each one: the save loads as the player left it (in a car, mid-mission,
  wanted, downed) and each scenario leaves state behind. A game that is not
  `test reset: ready` fails the scenario; one that needs another spot
  teleports after the reset. State a test adds must be cleared by it.
- `tools/game.sh role host|join|player` picks the game's `tandem.ini`;
  **leave the machine in `player`** when testing is over (it restores the
  player's own, kept as `tandem.ini.player`).
- A game that freezes (0% CPU) has usually **crashed** (under Proton the
  crash handler waits forever): run `tools/game.sh bt` before restarting it
  (every thread's stack, tandem's frames named from its .pdb). The plugin's
  watchdog logs `watchdog: game thread silent` after 5 s without a tick
  (`plugin/src/runtime.hpp`).
- anya's keyboard is AZERTY: simulated keys land on other letters. Restart
  the game only for script or plugin changes; `tools/game.sh cmd …` needs
  none.

### Engine access

- Prefer RTTI/script names and Codeware; then address-library hashes; raw
  offsets or vtable slots only behind a startup layout check that disables
  the feature on a mismatch. Write down the game version a layout was
  measured on.
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

- The repository is local only. **Commit locally** (small, focused commits,
  hooks passing: treefmt, shellcheck); **never push or create a remote**
  unless the user asks.
- Parallel work: independent tasks (research, reverse engineering, a
  feature that needs no game) can run in separate agents, each in its own
  git worktree; the game machine is one shared resource, so in-game testing
  (deploys, `tools/scenario.sh`) stays with one agent at a time.
- **Builds go through build slots** (`tools/build-slot.sh`): one on the
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
