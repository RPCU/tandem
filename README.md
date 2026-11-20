# tandem

Co-op multiplayer for Cyberpunk 2077 (patch 2.50): two to four players in
one shared Night City — same story, same enemies, each with their own V.

tandem is a RED4ext plugin (C++, a Windows DLL cross-compiled on Linux)
with redscript and TweakXL records, a mock second player and tests that
run on Linux, and a small Go UDP relay for players behind NAT.

**Status: in development, not yet playable.** The plugin builds, loads in
the game and runs sessions against the mock player; a second V, poses,
looks, driving, time and weather, and NPC proxies have been seen working
in game on the developer's machine, and much more is written but not yet
tested there. No two-player session between two real machines has been
played yet, and there is no release for players. See the
[roadmap](docs/ARCHITECTURE.md#roadmap) for what each milestone has
verified.

tandem is not made, endorsed or supported by CD PROJEKT RED.

## Build (Linux)

```sh
direnv allow        # or: devenv shell
task sysroot        # once: MSVC CRT + Windows SDK headers via xwin
task build          # tandem.dll + the mock player + tests
task test
```

The Windows sysroot is downloaded by `xwin`, which accepts Microsoft's
licence for the CRT and SDK on your behalf: read it first.

## Try it

```sh
task deploy                      # install tandem.dll in the game (GAME_HOST=anya)
task peer -- host --mode mirror  # a mock second player that mirrors you
# in red4ext/plugins/tandem/tandem.ini: role = join, address = <this machine>
task logs
```

`anya` is the developer's game machine; set `GAME_HOST` and `GAME_DIR` to
yours (`Taskfile.yml`).

## Relay (NAT fallback)

`tandem-relay` (`relay/`, Go standard library only) forwards a session's
traffic when the players cannot reach each other directly. The image is
built by CI on every push to `main` and on version tags:

```
zot.rpcu.io/public/tandem-relay:<8-char commit>   # every push to main
zot.rpcu.io/public/tandem-relay:<x.y.z>           # tags
```

```sh
task relay:test
task relay:image                 # OCI image built with Nix
docker build -t tandem-relay relay/   # the same image without Nix
task k8s:check                   # deploy/kubernetes/base
```

Running one (host tokens, systemd, the Kubernetes base and its overlay,
firewall, replicas): [docs/relay.md](docs/relay.md).

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

## Licence

tandem is released under the [MIT licence](LICENSE). The third-party code
it is built with, all MIT, is listed in [NOTICE](NOTICE) with its own
licence files under `vendor/`.
