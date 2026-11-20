#!/usr/bin/env bash
# Devices through tandem-peer (docs/architecture/devices.md): a mock guest
# operates a device by its id (--device), the mock host takes its word and
# everyone hears who did it; the host changes it back (--device-answer) and
# the guest hears that; a second guest that joins later gets the device as
# it is, quietly.
# Usage: peer_devices.sh PATH/tandem-peer
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
  echo "peer_devices: $1" >&2
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

"$peer" host --port "$port" --mode idle --device-answer closed,locked --device-after 2 >"$dir/host.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" 'hosting on UDP' || fail "the mock host did not start"

# A guest opens a door, by id, 1 s into its session.
door='1a2b3c4d5e6f7788:DoorControllerPS'
"$peer" join "127.0.0.1:$port" --mode idle --device "$door:open@1" \
  --fracture '1a2b3c4d5e6f7700:glass_dst:10,20,3@1' >"$dir/guest.log" 2>&1 &
pids+=($!)
wait_for "$dir/guest.log" 'asked the host for device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, open, value -1 \(--device\)' ||
  fail "the guest did not ask for the door"
wait_for "$dir/host.log" 'device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, open, value -1 operated by player 2: taken as said' ||
  fail "the host did not take the guest's word"
# The host's word comes back to the guest, with its name on it.
wait_for "$dir/guest.log" 'device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, open, value -1 by player 2, played' ||
  fail "the guest did not hear the door is open"

# A window breaks in the guest's game: the host hears which part, and where.
wait_for "$dir/host.log" 'player 2 broke part 0x[0-9a-f]+ of 1a2b3c4d5e6f7700 at \(10\.0 20\.0 3\.0\)' ||
  fail "the host did not hear the window break"

# 2 s later the host closes and locks it: the guest hears the host did.
wait_for "$dir/host.log" 'our device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, locked, value -1 \(--device-answer\)' ||
  fail "the host did not change the door back"
wait_for "$dir/guest.log" 'device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, locked, value -1 by player 1, played' ||
  fail "the guest did not hear the host's change"

# A late joiner: the door as it is, set, not played; then it sends a lift
# to floor 3 and the first guest hears it.
"$peer" join "127.0.0.1:$port" --mode idle --name late \
  --device '99aabbccddeeff00:LiftControllerPS:floor=3@1' >"$dir/late.log" 2>&1 &
pids+=($!)
wait_for "$dir/late.log" 'device 1a2b3c4d5e6f7788 \(0x[0-9a-f]+\): on, locked, value -1 by player 1, set' ||
  fail "the late joiner did not get the door as it is"
wait_for "$dir/guest.log" 'device 99aabbccddeeff00 \(0x[0-9a-f]+\): on, plain, value 3 by player 3, played' ||
  fail "the first guest did not hear of the lift"
echo "peer_devices: ok"
