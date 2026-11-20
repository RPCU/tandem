#!/usr/bin/env bash
# Per-player regions through two mock players (docs/architecture/regions.md):
# each runs NPCs of its own (--mode region). Far apart they share nothing;
# the guest walks up to the host and both show the other's NPCs; it walks off
# again, leaving its NPCs, and the host takes them over; then the same with a
# lossy link, and a guest that vanishes next to the host.
# Usage: peer_regions.sh PATH/tandem-peer
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
  echo "peer_regions: $1" >&2
  for log in "$dir"/*.log; do
    echo "--- $log" >&2
    cat "$log" >&2
  done
  exit 1
}

# wait_for FILE REGEX [TENTHS]: until the line is in the log (15 s at most).
wait_for() {
  local _
  for _ in $(seq 1 "${3:-150}"); do
    grep -qE "$2" "$1" 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

# host ARGS...: a mock host standing at the origin with two NPCs of its own.
host() {
  port=$((port + 1))
  hostlog="$dir/host-$port.log"
  "$peer" host --port "$port" --mode region --region-at 0,0,0 --region-far 0 --region-walk-at -1 \
    --region-npcs 2 "$@" >"$hostlog" 2>&1 &
  pids+=($!)
  wait_for "$hostlog" 'hosting on UDP' || fail "the mock host did not start"
}
# guest ARGS...: a mock guest with three NPCs of its own, in the background.
guest() {
  guestlog="$dir/guest-$port.log"
  "$peer" join "127.0.0.1:$port" --name far --mode region --region-npcs 3 "$@" >"$guestlog" 2>&1 &
  pids+=($!)
  guestpid=$!
}

# ---- apart, then together, then apart again: a hand-over ----
host
guest --region-far 400 --region-walk-at 2 --region-leave-at 11 --region-speed 60
wait_for "$guestlog" 'region: running 3 NPC' || fail "the guest runs no NPC of its own"
wait_for "$hostlog" 'region: running 2 NPC' || fail "the host runs no NPC of its own"
# Apart: each on its own.
wait_for "$guestlog" 'region: linked with nobody' || fail "a guest 400 m away is linked"
wait_for "$hostlog" 'region: linked with nobody' || fail "the host is linked with a guest 400 m away"
# Together.
wait_for "$guestlog" 'region: linked with player 1' || fail "the guest never met the host"
wait_for "$hostlog" 'region: linked with player 2' || fail "the host never met the guest"
# The guest walks off, leaving its three NPCs 8 m from the host: handed over.
wait_for "$guestlog" 'region: handed npc 16777217 over to player 1' 300 ||
  fail "the guest was not asked to hand its NPCs over"
wait_for "$hostlog" 'region: took npc 16777217 over from player 2 \(health 1\.00, 0 effect\(s\), target player 1\)' ||
  fail "the host did not take the guest's NPC over with its state"
wait_for "$hostlog" 'region: took npc 16777219 over from player 2' || fail "not every NPC was handed over"
wait_for "$guestlog" 'region: linked with nobody' || fail "the guest did not leave the host's region"
# The host's own two never moved: its V stayed with them.
if grep -q 'region: handed npc' "$hostlog"; then fail "the host gave an NPC away while standing by it"; fi
kill "$guestpid" 2>/dev/null || true
kill "${pids[0]}" 2>/dev/null || true

# ---- the same through a link that loses a quarter of everything ----
# The guest walks off once it knows it is linked (--region-leave-linked, not
# a time): an owner that believes it is linked with nobody sends no NPC
# states, and the host decides a hand-over from where its NPCs are. What
# the loss takes from the players domain (the roster at join, a Regions)
# comes back with a players resync: 3 s digests, two in a row, a 5 s
# cooldown, the request and the answer lossy too, so one every 6-12 s that
# fails one time in three or four. 120 s is ten of them. Then 3 s of
# states, 2 s to walk past kKeep, and the hand-over's four tries 1 s apart.
host --drop 0.25
guest --drop 0.25 --region-far 300 --region-walk-at 2 --region-leave-linked 3 --region-speed 40
wait_for "$hostlog" 'region: linked with player 2' 300 || fail "lossy: the host never met the guest"
wait_for "$guestlog" 'region: walking off' 1200 ||
  fail "lossy: the guest never learned it is linked by the host's V"
wait_for "$hostlog" 'region: took npc 1677721[789] over from player 2' 600 ||
  fail "lossy: no NPC was handed over"
kill "$guestpid" 2>/dev/null || true
kill "${pids[2]}" 2>/dev/null || true

# ---- a guest that vanishes next to the host: its NPCs are inherited ----
host
guest --region-far 6 --region-walk-at -1 --vanish-at 4
wait_for "$hostlog" 'region: linked with player 2' || fail "vanish: the host never met the guest"
wait_for "$hostlog" 'region: took npc 16777217 over from player 2 .*inherited' 300 ||
  fail "vanish: the host did not inherit the vanished guest's NPCs"
wait_for "$hostlog" 'region: took npc 16777219 over from player 2 .*inherited' ||
  fail "vanish: not every NPC was inherited"
echo "peer_regions: ok"
