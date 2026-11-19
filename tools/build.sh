#!/usr/bin/env bash
# Run a build or test task where there is room for it:
#
#   tools/build.sh test            tools/build.sh build:plugin test
#
# On the game machine (tools/remote.sh: more cores, its own build lock) when
# nobody plays there and one of its build slots is free (3 by default:
# tools/build-slot.sh); otherwise here, in the laptop's one slot
# (TANDEM_LOCAL_SLOTS) with limited parallelism. Our own test instances on
# the game machine (tools/instance.sh) are not someone playing.
# TANDEM_BUILD=local|remote forces one side (remote still refuses while the
# game runs: a build would take its frames).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
host=${GAME_HOST:-anya}
[ $# -gt 0 ] || set -- build test

remote_slots=${TANDEM_REMOTE_SLOTS:-3}
local_slots=${TANDEM_LOCAL_SLOTS:-1}
playing() { ssh -o ConnectTimeout=5 "$host" 'pgrep -af "[C]yberpunk2077.exe" | grep -v tandem-instances | grep -q .'; }
reachable() { ssh -o ConnectTimeout=5 "$host" true 2>/dev/null; }
# shellcheck disable=SC2029 # expanded here on purpose
remote_busy() {
  ssh -o ConnectTimeout=5 "$host" "for i in \$(seq 0 $((remote_slots - 1))); do
    f=/tmp/tandem-build.lock; [ \$i = 0 ] || f=\$f.\$i; flock -n \$f true && exit 1; done; exit 0"
}
local_busy() { ! tools/build-slot.sh --free "$local_slots"; }

local_run() {
  echo "build: here, in the laptop's build slot" >&2
  CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8} \
    exec tools/build-slot.sh "$local_slots" direnv exec . task "$@"
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
