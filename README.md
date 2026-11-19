# tandem

Co-op multiplayer for Cyberpunk 2077 (patch 2.50): two to four players in
one shared Night City — same story, same enemies, each with their own V.

**Status:** milestone 0. The plugin builds and the network session works
between the mock player and itself. Nothing is synced in game yet. See the
[roadmap](docs/ARCHITECTURE.md#roadmap).

## Build (Linux)

```sh
direnv allow        # or: devenv shell
task sysroot        # once: MSVC CRT + Windows SDK headers via xwin
task build          # tandem.dll + the mock player + tests
task test
```

## Try it

```sh
task deploy                      # install tandem.dll in the game (GAME_HOST=anya)
task peer -- host --mode mirror  # a mock second player that mirrors you
# in red4ext/plugins/tandem/tandem.ini: role = join, address = <this machine>
task logs
```

## Relay (NAT fallback)

```sh
task relay:test
task relay:image                 # OCI image built with Nix
task k8s:check                   # deploy/kubernetes/base
```

## Release

```sh
tools/build.sh --force build:plugin test # build (here or on anya), test
task package:files               # build/package/tandem-<build>.zip + symbols zip
```

## Docs

- [Installing and playing](dist/INSTALL.md) — for players: requirements, hosting and joining, troubleshooting
- [Running a relay](docs/relay.md) — for whoever operates a tandem-relay
- [Releases](docs/architecture/release.md) — packaging, compatibility checks, bug reports
- [First two-player session](docs/playtest.md) — the checklist
- [NOTICE](NOTICE) — third-party code and credits
- [Architecture](docs/ARCHITECTURE.md) — authority model, threading, roadmap, and the index of the topic files in `docs/architecture/`
- [Engine knowledge](docs/research/engine-knowledge.md) — what earlier projects learned about the engine, with sources: the index of `docs/research/engine/`
- [AGENTS.md](AGENTS.md) — rules for AI agents working on this repository
