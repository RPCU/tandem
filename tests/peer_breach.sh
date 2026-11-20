#!/usr/bin/env bash
# Breach Protocol through tandem-peer (docs/architecture/devices.md, "Breach
# Protocol"): a mock guest breaches an access point with daemons
# (--breach, --programs), the mock host grants it and applies it, a second
# guest hears it for its own NPCs; that guest's own breach of the same
# network is refused, naming the first breacher; a late joiner finds the
# access point breached.
# Usage: peer_breach.sh PATH/tandem-peer
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
  echo "peer_breach: $1" >&2
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

"$peer" host --port "$port" --mode idle >"$dir/host.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" 'hosting on UDP' || fail "the mock host did not start"

ap='a0b1c2d3e4f50617'
# A second guest first, so it is there when the network is breached.
"$peer" join "127.0.0.1:$port" --mode idle --name second --breach "$ap@4" \
  --programs NetworkLowerICEMajor >"$dir/second.log" 2>&1 &
pids+=($!)
wait_for "$dir/second.log" 'in the session: player 2' || fail "the second guest did not join"

# The breacher jacks in 1 s into its session: three daemons.
"$peer" join "127.0.0.1:$port" --mode idle --name breacher --breach "$ap@1" \
  --programs NetworkCameraShutdown,NetworkLowerICEMedium,NetworkDataMineLootAll >"$dir/breacher.log" 2>&1 &
pids+=($!)
wait_for "$dir/breacher.log" "breached network $ap npc 0 with 3 daemon\(s\) \(--breach\)" ||
  fail "the guest did not breach"
wait_for "$dir/breacher.log" "our breach of network $ap npc 0: granted, by player 3" ||
  fail "the breacher did not hear its breach stands"
wait_for "$dir/host.log" "breach by player 3: network $ap npc 0, 3 daemon\(s\), 0 trap\(s\): applied to its network and our NPCs" ||
  fail "the host did not apply the breach"
wait_for "$dir/second.log" "breach by player 3: network $ap npc 0, 3 daemon\(s\), 0 trap\(s\): applied to our NPCs" ||
  fail "the second guest did not hear the breach for its NPCs"
# Breached for everyone: the access point's record, with the breacher's name.
wait_for "$dir/second.log" "device $ap \(0x[0-9a-f]+\): on, exposed\+breached, value -1 by player 3, played" ||
  fail "the second guest did not hear the access point is breached"

# The second guest's own jack-in, later: refused, the first breacher named.
wait_for "$dir/second.log" "our breach of network $ap npc 0: refused \(breached already\), by player 3" ||
  fail "the second breach was not refused"
if grep -qE 'breach by player 2' "$dir/host.log"; then
  fail "the refused breach was applied"
fi

# A late joiner: the access point breached, set, not played.
"$peer" join "127.0.0.1:$port" --mode idle --name late >"$dir/late.log" 2>&1 &
pids+=($!)
wait_for "$dir/late.log" "device $ap \(0x[0-9a-f]+\): on, exposed\+breached, value -1 by player 3, set" ||
  fail "the late joiner did not get the access point as it is"
if grep -qE 'referee: ' "$dir"/*.log; then
  fail "the referee found a breach of the rules"
fi
echo "peer_breach: ok"
