#!/usr/bin/env bash
# Gameplay recording and replay through tandem-peer (tandem/play.hpp): a
# mock player saves its own play as the game does (--save-play), play-info
# reads it and says where to put our V, and a mock host replays it
# (--play) to a guest, which sees the replayed V move.
# Usage: peer_play.sh PATH/tandem-peer
set -euo pipefail
peer=$1
dir=$(mktemp -d)
pids=()
cleanup() {
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  rm -rf "$dir"
}
trap cleanup EXIT
port=$((43000 + ($$ % 500) * 4))
fail() {
  echo "peer_play: $1" >&2
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

# host ARGS...: a mock host in the background on the next port, once it listens.
host() {
  port=$((port + 1))
  "$peer" host --port "$port" --mode idle "$@" >"$dir/host-$port.log" 2>&1 &
  pids+=($!)
  wait_for "$dir/host-$port.log" 'hosting on UDP' || fail "the mock host did not start"
}
# join SECONDS ARGS...: a mock guest for that long.
join() {
  local seconds=$1
  shift
  timeout -s TERM "$seconds" "$peer" join "127.0.0.1:$port" "$@" >"$dir/join-$port.log" 2>&1 || true
}

# A guest circling, saving its play.
host
join 7 --mode ghost --body male --save-play "$dir/ghost.tplay"
grep -qE 'play saved: [1-9][0-9]+ messages' "$dir/join-$port.log" || fail "no play saved"

"$peer" play-info "$dir/ghost.tplay" >"$dir/info.log" 2>&1 || fail "play-info failed"
grep -qE '^play: [0-9.]+ s, [0-9]+ messages \(.*[1-9][0-9]* PlayerState' "$dir/info.log" ||
  fail "play-info shows no states"
grep -q 'body male' "$dir/info.log" || fail "the recording lost its body"
grep -qE '^beside: teleport -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ -?[0-9.]+$' "$dir/info.log" ||
  fail "play-info gives no teleport"

# Its replay, by a mock host, to a guest: it starts when the guest is in,
# sends every message, and the guest hears the states.
host --play "$dir/ghost.tplay" --body female
join 9 --mode idle
wait_for "$dir/host-$port.log" 'play: started' || fail "the replay did not start"
wait_for "$dir/host-$port.log" 'play: done, [1-9][0-9]* messages sent, 0 not for this session' ||
  fail "the replay did not end"
grep -qE '[1-9][0-9]* states' "$dir/join-$port.log" || fail "the guest heard no replayed state"
grep -qE 'look from player 1: [0-9]+ keys' "$dir/join-$port.log" || fail "the guest got no look"

# A host's session: a mock host whose V circles and with an NPC; a guest
# watching unseen records what it is sent (--record-session); a mock host
# replays it (--play-session) to another guest, which records it again.
host --mode ghost --npc Character.Tester
join 8 --mode watch --record-session "$dir/city.tsession"
grep -qE 'session saved: [1-9][0-9]* messages' "$dir/join-$port.log" || fail "no session saved"
"$peer" play-info "$dir/city.tsession" >"$dir/info.log" 2>&1 || fail "play-info failed on a session"
grep -qE '^session: host 1, recorded by player 2' "$dir/info.log" || fail "the session lost its players"
grep -qE '[1-9][0-9]* NpcSpawn' "$dir/info.log" || fail "the session holds no NPC spawn"
grep -qE '^beside: teleport ' "$dir/info.log" || fail "the session gives no teleport"
host --play-session "$dir/city.tsession"
join 10 --mode idle --record-session "$dir/again.tsession"
wait_for "$dir/host-$port.log" 'session: started' || fail "the session replay did not start"
wait_for "$dir/host-$port.log" 'session: done, [1-9][0-9]* messages sent \([1-9][0-9]* NPCs\)' ||
  fail "the session replay did not end, or sent no NPC"
"$peer" play-info "$dir/again.tsession" >"$dir/again.log" 2>&1 || fail "the replay was not recorded"
grep -qE '[1-9][0-9]* NpcSpawn' "$dir/again.log" || fail "the replayed session gave the guest no NPC"
grep -qE '[1-9][0-9]* PlayerState' "$dir/again.log" || fail "the replayed session gave the guest no host V"

# A missing recording: the mode runs instead.
host --play "$dir/missing.tplay"
wait_for "$dir/host-$port.log" "play: cannot read .*: the mode's own motion instead" ||
  fail "a missing recording was not reported"
echo "peer_play: ok"
