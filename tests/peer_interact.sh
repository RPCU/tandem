#!/usr/bin/env bash
# Quest interactions through tandem-peer (docs/architecture/story.md,
# "Quest interactions"): a mock guest's V uses the host's object
# (--interact), the mock host answers as its game would (--interact-answer);
# a host-only answer is known for the session, so the same use asked again
# is answered at once by the guest's own endpoint; a performed one is asked
# each time.
# Usage: peer_interact.sh PATH/tandem-peer
set -euo pipefail
peer=$1
dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$dir"
}
trap cleanup EXIT
port=$((47000 + ($$ % 500) * 4))
fail() {
  echo "peer_interact: $1" >&2
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

# A host whose game says every use is the host's to do.
"$peer" host --port "$port" --mode idle --interact-answer host-only >"$dir/host.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" 'hosting on UDP' || fail "the mock host did not start"

# A guest jacks in twice into the same access point, 1 s and 2 s in.
link='5eed00000000aa02:TogglePersonalLink:TogglePersonalLink'
"$peer" join "127.0.0.1:$port" --mode idle --interact "$link@1" --interact "$link@2" \
  >"$dir/guest.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" "interact: player 2's use of 5eed00000000aa02: host only" ||
  fail "the host did not answer the guest's use"
wait_for "$dir/guest.log" 'interact: the host on our use of 5eed00000000aa02 \(question 1\): host only' ||
  fail "the guest did not hear the host's verdict"
# The second: known, answered by the guest's own endpoint, never asked.
wait_for "$dir/guest.log" 'interact: the host on our use of 5eed00000000aa02 \(question 2\): host only' ||
  fail "the guest's second use was not answered"
sleep 0.5
[ "$(grep -c "interact: player 2's use of" "$dir/host.log")" = 1 ] ||
  fail "the host was asked twice about a use it said is its own"

# A second host whose game does every use: asked each time.
port2=$((port + 2))
"$peer" host --port "$port2" --mode idle --interact-answer performed >"$dir/host2.log" 2>&1 &
pids+=($!)
wait_for "$dir/host2.log" 'hosting on UDP' || fail "the second mock host did not start"
switch='5eed00000000aa01:QuestCustomAction:0x1234'
"$peer" join "127.0.0.1:$port2" --mode idle --interact "$switch@1" --interact "$switch@2" \
  >"$dir/guest2.log" 2>&1 &
pids+=($!)
wait_for "$dir/guest2.log" 'interact: the host on our use of 5eed00000000aa01 \(question 2\): performed' ||
  fail "the guest's second press was not performed"
[ "$(grep -c "interact: player 2's use of 5eed00000000aa01: performed" "$dir/host2.log")" = 2 ] ||
  fail "the host did not perform both presses"
echo "peer_interact: ok"
