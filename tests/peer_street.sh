#!/usr/bin/env bash
# World vehicles and police through tandem-peer (docs/architecture/traffic.md,
# police.md): a mock host shares a street (--traffic), a mock guest rams a
# car, takes one and is wanted; a crime is told to the guest; a late joiner
# finds the street as it is by then.
# Usage: peer_street.sh PATH/tandem-peer
set -euo pipefail
peer=$1
dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$dir"
}
trap cleanup EXIT
port=$((45000 + ($$ % 500) * 4))
fail() {
  echo "peer_street: $1" >&2
  for log in "$dir"/*.log; do
    echo "--- $log" >&2
    cat "$log" >&2
  done
  exit 1
}

# wait_for FILE REGEX: until the line is in the log (15 s at most).
wait_for() {
  local _
  for _ in $(seq 1 150); do
    grep -qE "$2" "$1" 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

"$peer" host --port "$port" --mode idle --traffic 3 --npc Character.Tester --wreck-at 4 \
  --crime-at 2 --heat 2 --heat-at 3 --heat-for 8 >"$dir/host.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" 'hosting on UDP' || fail "the mock host did not start"

timeout -s TERM 20 "$peer" join "127.0.0.1:$port" --name thief --mode ghost --ram-traffic 2 --steal-at 10 \
  --drive-seconds 3 --heat 1 --heat-at 1 --heat-for 6 >"$dir/thief.log" 2>&1 || true

# The street: three cars driving and one parked, a driver in the first.
grep -qE 'street: 3 cars of .* round player 2, one parked' "$dir/host.log" || fail "the host shared no street"
grep -qE 'street: npc [0-9]+ at the wheel of vehicle' "$dir/host.log" || fail "no driver in the first car"
grep -qE 'street: vehicle 0x[0-9a-f]+ .*, 4 shared' "$dir/thief.log" || fail "the guest did not get the four vehicles"
# A ram reaches the owner, which applies it and says how the car stands.
grep -qE 'street: rammed vehicle' "$dir/thief.log" || fail "the guest rammed nothing"
grep -qE 'street: player 2 hit vehicle 0x[0-9a-f]+ for 300 \(a ram\)' "$dir/host.log" || fail "the ram did not reach the owner"
grep -qE 'street: vehicle 0x[0-9a-f]+ health 0\.[0-9]+' "$dir/thief.log" || fail "the guest heard no status"
# The first car burns and blows up: a wreck for everyone.
grep -qE 'blew up: a wreck' "$dir/host.log" || fail "no car blew up"
grep -qE 'health 0\.00, flags 0x[0-9a-f]+ \(a wreck\)' "$dir/thief.log" || fail "the guest saw no wreck"
# The guest takes the nearest that is no wreck: it is its car for both.
grep -qE 'street: getting into vehicle' "$dir/thief.log" || fail "the guest took no car"
grep -qE "taken: it is player 2's car now \(ours\)" "$dir/thief.log" || fail "the claim did not come back to the guest"
grep -qE "taken: it is player 2's car now" "$dir/host.log" || fail "the owner did not hear whose car it is"
grep -qE 'street: out of the car we took' "$dir/thief.log" || fail "the guest never left the car"
# Police: each is wanted in its own name, and the crime is told to the guest.
grep -qE 'police: player 2 wanted level 1 \(state 1\)' "$dir/host.log" || fail "the host did not hear the guest's stars"
grep -qE 'police: player 2 wanted level 0' "$dir/host.log" || fail "the guest's stars never went"
grep -qE 'police: player 1 wanted level 2 ' "$dir/thief.log" || fail "the guest did not hear the host's stars"
grep -qE 'police: crime of player 2 reported to its game$' "$dir/host.log" || fail "the crime was not sent"
grep -qE "police: the area's owner saw our V commit a crime \(kind 1" "$dir/thief.log" || fail "the crime did not reach the guest"

# A late joiner: three vehicles left (one was taken), one of them a wreck.
timeout -s TERM 4 "$peer" join "127.0.0.1:$port" --name late --mode ghost >"$dir/late.log" 2>&1 || true
grep -qE 'street: vehicle 0x[0-9a-f]+ .*, 3 shared' "$dir/late.log" || fail "the late joiner did not get the street"
grep -qE '4 shared' "$dir/late.log" && fail "the late joiner got the car that was taken"
grep -qE '\(a wreck\)' "$dir/late.log" || fail "the late joiner saw no wreck"
echo "peer_street: ok"
