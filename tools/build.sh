#!/usr/bin/env bash
# Run a build or test task where there is room for it:
#
#   tools/build.sh test            tools/build.sh build:plugin test
#
# On the game machine (tools/remote.sh: more cores, its own build lock) when
# nobody plays there and its lock is free; otherwise here, through the
# laptop's lock with limited parallelism. So several agents build at once:
# one on each machine, the rest queue on whichever lock they picked.
# TANDEM_BUILD=local|remote forces one side (remote still refuses while the
# game runs: a build would take its frames).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
host=${GAME_HOST:-anya}
[ $# -gt 0 ] || set -- build test

playing() { ssh -o ConnectTimeout=5 "$host" 'pgrep -f "[C]yberpunk2077.exe" >/dev/null'; }
reachable() { ssh -o ConnectTimeout=5 "$host" true 2>/dev/null; }
remote_busy() { ssh -o ConnectTimeout=5 "$host" '! flock -n /tmp/tandem-build.lock true'; }
local_busy() { ! flock -n /tmp/tandem-build.lock true; }

local_run() {
  echo "build: here, through the laptop's lock" >&2
  CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8} \
    exec flock /tmp/tandem-build.lock direnv exec . task "$@"
}
remote_run() {
  echo "build: on $host" >&2
  exec tools/remote.sh "$@"
}

case ${TANDEM_BUILD:-auto} in
local) local_run "$@" ;;
remote)
  if ! reachable || playing; then
    echo "build: $host is unreachable or the game runs there" >&2
    exit 1
  fi
  remote_run "$@"
  ;;
esac

if ! reachable || playing; then local_run "$@"; fi
# Both free, or both busy: the game machine (it is the faster one). Only one
# busy: the other.
if remote_busy && ! local_busy; then local_run "$@"; fi
remote_run "$@"
