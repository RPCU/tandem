#!/usr/bin/env bash
# Run a command holding one of this machine's build slots:
#
#   tools/build-slot.sh SLOTS COMMAND...   wait for a free slot, run COMMAND in it
#   tools/build-slot.sh --free SLOTS       exit 0 if a slot is free now
#
# Slot 0 is /tmp/tandem-build.lock (the lock older scripts take with flock),
# slot N /tmp/tandem-build.lock.N. The slot is held by an inherited file
# descriptor until COMMAND exits. Waiting polls every 2 s: no queue order.
# With TANDEM_JOBS=auto, COMMAND gets CMAKE_BUILD_PARALLEL_LEVEL = this
# machine's cores shared among the slots busy when it starts (ours too): a
# build alone takes every core, three take a third each, and never more
# jobs than free memory allows (TANDEM_JOB_GB per job, default 3: the
# protocol's translation units take several GB each; parallel builds were
# OOM-killed at 4 jobs). With TANDEM_SCOPE=low, COMMAND runs in a systemd
# user scope with a low CPU and IO weight when one can be made (a user bus:
# someone logged in), else just niced.
set -euo pipefail

# TANDEM_SLOT_POOL picks the pool: build (default; slot 0 is the old lock)
# or test (/tmp/tandem-test.lock.N: the tests mostly wait, they need no cores).
pool=${TANDEM_SLOT_POOL:-build}
lock() { if [ "$1" = 0 ]; then echo "/tmp/tandem-$pool.lock"; else echo "/tmp/tandem-$pool.lock.$1"; fi; }

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
      if [ "${TANDEM_JOBS:-}" = auto ]; then
        busy=0
        for ((j = 0; j < slots; j++)); do
          flock -n "$(lock "$j")" true || busy=$((busy + 1))
        done
        jobs=$(($(nproc) / busy))
        mem=$(awk '/^MemAvailable:/ { print int($2 / 1048576) }' /proc/meminfo 2>/dev/null || echo 0)
        bymem=$((mem / ${TANDEM_JOB_GB:-3}))
        [ "$bymem" -lt "$jobs" ] && jobs=$bymem
        export CMAKE_BUILD_PARALLEL_LEVEL=$((jobs > 1 ? jobs : 1))
      fi
      if [ "${TANDEM_SCOPE:-}" = low ]; then
        if systemd-run --user --scope --quiet true 2>/dev/null; then
          set -- systemd-run --user --scope --quiet -p CPUWeight=10 -p IOWeight=10 nice -n 19 "$@"
        else
          set -- nice -n 19 "$@"
        fi
      fi
      echo "build-slot: slot $i of $slots${CMAKE_BUILD_PARALLEL_LEVEL:+, $CMAKE_BUILD_PARALLEL_LEVEL jobs}" >&2
      exec "$@"
    fi
    exec {fd}>&-
  done
  sleep 2
done
