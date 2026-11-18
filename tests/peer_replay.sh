#!/usr/bin/env bash
# The NPC recording and replay the guest scenarios stand on (npcs-host
# records, npcs-guest and the others replay): a recording made next to a
# host with an NPC holds its spawn, a replay of it gives a guest that spawn,
# and a recording of an empty street never replaces a good one (a replay
# with no spawn gives a guest no proxies at all: the 2026-11-18 regression).
# Usage: peer_replay.sh PATH/tandem-peer
set -euo pipefail
peer=$1
dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$dir"
}
trap cleanup EXIT
port=$((42000 + ($$ % 500) * 4))
fail() {
  echo "peer_replay: $1" >&2
  for log in "$dir"/*.log; do
    echo "--- $log" >&2
    cat "$log" >&2
  done
  exit 1
}

# host ARGS...: a mock host in the background on the next port.
host() {
  port=$((port + 1))
  "$peer" host --port "$port" --mode idle "$@" >"$dir/host-$port.log" 2>&1 &
  pids+=($!)
  sleep 0.3
}
# join SECONDS ARGS...: a mock guest for that long, its log on stdout's file.
join() {
  local seconds=$1
  shift
  timeout -s TERM "$seconds" "$peer" join "127.0.0.1:$port" "$@" >"$dir/join-$port.log" 2>&1 || true
}

# A host with one NPC (it stands it in front of the guest once it moves).
host --npc Character.Tester
join 4 --mode ghost --record "$dir/good.rec"
grep -qE 'recording saved: [0-9]+ NPC messages, [1-9][0-9]* spawns' "$dir/join-$port.log" ||
  fail "a recording next to an NPC holds no spawn"
[ -s "$dir/good.rec" ] || fail "the recording is not there"
cp "$dir/good.rec" "$dir/good.copy"

# An empty street: nothing to record, the good recording stays.
host
join 3 --mode ghost --record "$dir/good.rec"
grep -q 'recording not saved: no NPC spawns' "$dir/join-$port.log" ||
  fail "an empty recording was not refused"
cmp -s "$dir/good.rec" "$dir/good.copy" || fail "an empty recording replaced a good one"

# Its replay gives a guest the NPC (recorded again on the guest's side).
host --replay "$dir/good.rec"
join 4 --mode ghost --record "$dir/again.rec"
grep -qE 'recording saved: [0-9]+ NPC messages, [1-9][0-9]* spawns' "$dir/join-$port.log" ||
  fail "the replay sent the guest no NPC spawn"

# A replay with no spawn says so.
: >"$dir/empty.rec"
host --replay "$dir/empty.rec"
sleep 0.5
grep -q 'replay: no NPC spawns' "$dir/host-$port.log" || fail "an empty replay was not reported"
echo "peer_replay: ok"
