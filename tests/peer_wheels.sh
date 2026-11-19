#!/usr/bin/env bash
# Vehicle combat through tandem-peer (docs/architecture/vehicle-combat.md): a
# mock host drives a car that a mock guest shoots and mans the gun of; the
# car is wrecked and the host bails out of it on the move; the host's street
# car chases the guest, who runs into the host's NPC. A late joiner finds
# the car as it stands by then: a wreck, not an explosion.
# Usage: peer_wheels.sh PATH/tandem-peer
set -euo pipefail
peer=$1
dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$dir"
}
trap cleanup EXIT
port=$((46000 + ($$ % 500) * 4))
fail() {
  echo "peer_wheels: $1" >&2
  for log in "$dir"/*.log; do
    echo "--- $log" >&2
    cat "$log" >&2
  done
  exit 1
}

wait_for() {
  local _
  for _ in $(seq 1 150); do
    grep -qE "$2" "$1" 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

"$peer" host --port "$port" --mode drive --car-damage 2 --exit-at 13 --traffic 2 --chase 2 \
  --npc Character.Tester >"$dir/host.log" 2>&1 &
pids+=($!)
wait_for "$dir/host.log" 'hosting on UDP' || fail "the mock host did not start"

timeout -s TERM 18 "$peer" join "127.0.0.1:$port" --name gunner --mode ghost --shoot-cars 2 \
  --gunner 1 --ram-npcs 3 >"$dir/gunner.log" 2>&1 || true

# The gunner seat: asked for, given, aimed and fired from.
grep -qE 'gunner: the seat is ours' "$dir/gunner.log" || fail "the guest did not get the gunner seat"
grep -qE 'vehicle 0x1: gunner 2' "$dir/host.log" || fail "the host did not give its seat"
grep -qE "player 2 aims vehicle 0x1's gun" "$dir/host.log" || fail "the host heard no aim"
grep -qE "player 2 fires vehicle 0x1's weapon" "$dir/host.log" || fail "the host heard no gunfire"
grep -qE 'player 2 in vehicle 0x1: gunner' "$dir/host.log" || fail "the host did not hear the guest's seat combat"
# Shots on the host's car go to its driver's game, which applies them.
grep -qE 'our car: player 2 hit it for 150' "$dir/host.log" || fail "the guest's shots did not reach the driver"
grep -qE 'our car: player 2 hit it for 150, tyre 1 flat' "$dir/host.log" || fail "no tyre was shot out"
# The car is damaged, burns, blows up: the copy follows each step.
grep -qE 'vehicle 0x1 \(of player 1\): health 0\.[0-9]+, flags 0x0, tyres 0x[0-9a-f]+, 1 part\(s\) off' \
  "$dir/gunner.log" || fail "the guest's copy did not get the dents and the bumper"
grep -qE 'vehicle 0x1 \(of player 1\): .*: it blows up' "$dir/gunner.log" || fail "the guest's copy did not blow up"
# The host bails out on the move; the car rolls on to a stop.
grep -qE 'out of the car the hard way \(kind 1\) .*: it rolls on' "$dir/host.log" || fail "the host did not bail out"
grep -qE 'player 1 bailed out of vehicle 0x1 at .*: it rolls on' "$dir/gunner.log" || fail "the guest did not hear the bail-out"
# The street car chases the guest; the guest's car hits the host's NPC.
grep -qE 'street: vehicle 0x[0-9a-f]+ chases player 2 \(us\)' "$dir/gunner.log" || fail "the guest did not hear the chase"
grep -qE 'our car ran into npc' "$dir/gunner.log" || fail "the guest rammed nothing"
grep -qE 'player 2 ran into npc [0-9]+ at 12\.0 m/s \(knocked down\)' "$dir/host.log" || fail "the ram did not reach the NPCs' owner"

# A late joiner: the car is a wreck, put there.
wait_for "$dir/host.log" 'our car rolled to a stop' || fail "the car never stopped"
timeout -s TERM 4 "$peer" join "127.0.0.1:$port" --name late --mode ghost >"$dir/late.log" 2>&1 || true
grep -qE 'vehicle 0x1 \(of player 1\): .*: a wreck' "$dir/late.log" || fail "the late joiner did not see a wreck"
grep -qE 'it blows up' "$dir/late.log" && fail "the late joiner saw the car blow up again"
echo "peer_wheels: ok"
