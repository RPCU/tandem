#!/usr/bin/env bash
# Run a command holding one of this machine's build slots:
#
#   tools/build-slot.sh SLOTS COMMAND...   wait for a free slot, run COMMAND in it
#   tools/build-slot.sh --free SLOTS       exit 0 if a slot is free now
#
# Slot 0 is /tmp/tandem-build.lock (the lock older scripts take with flock),
# slot N /tmp/tandem-build.lock.N. The slot is held by an inherited file
# descriptor until COMMAND exits. Waiting polls every 2 s: no queue order.
set -euo pipefail

lock() { if [ "$1" = 0 ]; then echo /tmp/tandem-build.lock; else echo "/tmp/tandem-build.lock.$1"; fi; }

if [ "${1:-}" = --free ]; then
  for ((i = 0; i < $2; i++)); do
    flock -n "$(lock "$i")" true && exit 0
  done
  exit 1
fi

slots=$1
shift
while :; do
  for ((i = 0; i < slots; i++)); do
    exec {fd}>>"$(lock "$i")"
    if flock -n "$fd"; then
      echo "build-slot: slot $i of $slots" >&2
      exec "$@"
    fi
    exec {fd}>&-
  done
  sleep 2
done
