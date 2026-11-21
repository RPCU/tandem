#!/usr/bin/env bash
# In-game scenarios: repeatable tests of the real game against the mock
# player, each a few steps with checks on what the game logs.
#
#   tools/scenario.sh list          the scenarios (tools/scenarios/*.sh)
#   tools/scenario.sh NAME...       run these, in order
#   tools/scenario.sh all           run every one, host scenarios first
#
# Every scenario starts from one known state: the mock player of the one
# before is stopped, then tools/game.sh test-reset (V on foot, up, healed,
# at the test field, the tests' leftovers gone); a game that is not
# "ready" fails the scenario. A game started by `ensure` is reset too.
#
# A scenario is a bash file of steps:
#   ensure host|join [KEY=VALUE...]  the game runs this build in this role,
#                          with these extra tandem.ini keys (restarts
#                          only when needed: tools/game.sh ensure)
#   peer ARGS...           (re)start the mock player
#   peer2 ARGS...          a second mock player beside the first (more than
#                          two players; its log .cache/peer2.log)
#   play NAME ARGS...      the mock player joins and replays a real V's play,
#                          recordings/play/NAME.tplay (tools/game.sh
#                          fetch-play), our V first teleported beside where it
#                          starts; without that file, it joins with ARGS (a
#                          synthetic --mode) instead. $played says which (1, 0)
#   host_world REPLAY ARGS...  the mock host the game (a guest) joins: a real
#                          host's recorded session, recordings/session/$SESSION
#                          .tsession (default city; tandem-peer join --record-
#                          session), our V first teleported beside its host's
#                          start, else the NPC recording REPLAY (- for none); the
#                          scenario's own ARGS on top. $sessioned says which
#   cmd TEXT               a tandem command in the game
#   pilot ROUTINE [FLAGS]  the test pilot plays ROUTINE on our real V
#                          (tools/pilot/routines/ROUTINE.pilot, through
#                          tools/game.sh pilot run): it walks, drives, fights
#                          while the mock player plays the other side; the
#                          scenario fails when the routine does. A scenario
#                          that uses it says "# needs the pilot" (all leaves
#                          those out unless PILOT=1)
#   expect REGEX [SECS]    a game log line since the scenario began (default
#                          30 s); fails at once if the watchdog reports a
#                          silent game thread
#   expect_peer REGEX [SECS]  the same in the mock player's log
#   expect_peer2 REGEX [SECS] the same in the second mock player's log
#   reject REGEX           no such game log line so far
#   reject_peer REGEX      no such mock player log line so far
#   pause SECS             let time pass (still watching for a stall)
#   shot NAME              a screenshot kept with the results
#   note TEXT              a line in the report
# Every scenario also rejects the mock player's `referee:` lines: the game
# broke a protocol or authority rule (docs/architecture/sim.md), and the game
# log's `threads: hook NAME ran beside the script tick` lines (the thread
# probe, docs/architecture/testing.md).
# A failed check stops the scenario and keeps the evidence under
# .cache/scenarios/NAME-TIME/: the game's log since the start, the mock
# player's log, a screenshot and, when the game thread went silent, every
# thread's backtrace (tools/game.sh bt).
#
# The game restarts only when the build or the role changed (scripts would
# be hot-reloaded if RedHotTools were installed). Never run this while
# someone plays (AGENTS.md).
# shellcheck disable=SC2029,SC2034 # (SC2034: $played is read by the scenarios it sources)
set -euo pipefail
cd "$(dirname "$0")/.."

game=tools/game.sh
host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}

remote() { ssh "$host" "$@"; }

# The game log's lines since the mark (its file and line count at the start).
mark_file='' mark_line=0 referee_from=0 peer_mark=0
mark() {
  read -r mark_file mark_line < <(remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); echo \"\$f \$(wc -l < \"\$f\")\"")
  peer_mark=$(wc -l <.cache/peer.log 2>/dev/null || echo 0)
}
# (Not the test reset's own lines: "test reset: ready (on foot, …)" would
# answer a scenario's 'on foot'.)
since() { remote "cd '$dir'; tail -n +$((mark_line + 1)) '$mark_file'" | { grep -av '] \[script\] test reset: ' || true; }; }

out=''
fail() {
  echo "  FAIL: $1"
  mkdir -p "$out"
  since >"$out/game.log" 2>/dev/null || true
  tail -n +$((peer_mark + 1)) .cache/peer.log >"$out/peer.log" 2>/dev/null || true
  [ -z "$piloted" ] || cp .cache/pilot.log "$out/pilot.log" 2>/dev/null || true
  $game shot "scenario-fail" >/dev/null 2>&1 && cp .cache/shots/scenario-fail.png "$out/" || true
  if grep -q 'watchdog: game thread silent' "$out/game.log" 2>/dev/null; then
    $game bt >/dev/null 2>&1 && cp .cache/re/bt.txt "$out/bt.txt" || true
    echo "  the game thread stopped: backtraces in $out/bt.txt"
  fi
  echo "  evidence: $out"
  # Ends the scenario (it runs in a subshell of its own).
  exit 1
}

# The game thread is stalled when its last watchdog line since the mark is
# "silent" and has stayed so for 45 s: a loading screen (fast travel, a
# death and reload, a dive) silences it for a while and "back" follows.
stall_since=''
stalled() {
  if [ "$(since | grep -o 'watchdog: game thread [a-z]*' | tail -1)" != 'watchdog: game thread silent' ]; then
    stall_since=''
    return 1
  fi
  [ -n "$stall_since" ] || stall_since=$SECONDS
  ((SECONDS - stall_since >= 45))
}

ensure() {
  local output status=0
  output=$($game ensure "$@" 2>&1) || status=$?
  while IFS= read -r line; do echo "  $line"; done <<<"$output"
  if grep -q 'redscript compile errors' <<<"$output"; then
    echo "  ABORT: the scripts do not compile, every scenario would fail"
    exit 3
  fi
  [ "$status" = 0 ] || fail "the game is not running this build as $1"
  # A fresh start: everything in the new log belongs to this scenario. Else
  # the mark before the test reset stays: what the reset has the game say
  # again (a session's rules, comms, our level, the HUD's layout) is the
  # scenario's to read.
  if grep -q '^launched' <<<"$output"; then
    mark
    mark_line=0
  elif [ -z "$mark_file" ]; then
    mark
  fi
}
peer() {
  $game peer "$@" | sed 's/^/  /'
  peer_mark=0 referee_from=0
}
peer2() {
  mkdir -p .cache
  nohup build/linux/peer/tandem-peer "$@" >.cache/peer2.log 2>&1 &
  echo "  second mock player: $* (.cache/peer2.log)"
}
cmd() { $game cmd "$*" >/dev/null || fail "command not taken: $*"; }
# The pilot's own log (each step, its checks, the pad's safety) is kept in
# .cache/pilot.log and goes with a failure's evidence.
piloted=''
pilot() {
  local status=0
  piloted=1
  mkdir -p .cache
  echo "  pilot: $*"
  $game pilot run "$@" >.cache/pilot.log 2>&1 || status=$?
  grep -E 'WARN|PASS|FAIL|SAVE' .cache/pilot.log | sed 's/^/  /' || true
  [ "$status" = 0 ] || fail "the pilot's routine $1 did not pass (.cache/pilot.log)"
}
reset() {
  local result status=0
  result=$($game test-reset 2>&1) || status=$?
  echo "  ${result:-test reset: no answer}"
  [ "$status" = 0 ] || fail "the game is not in its test state"
}
sessioned=0
host_world() {
  local replay=$1 file=recordings/session/${SESSION:-city}.tsession beside
  shift
  if [ ! -f "$file" ]; then
    echo "  no $file: the synthetic host"
    sessioned=0
    if [ "$replay" = - ]; then peer host "$@"; else peer host --replay "$replay" "$@"; fi
    return
  fi
  beside=$(build/linux/peer/tandem-peer play-info "$file" | sed -n 's/^beside: //p')
  [ -n "$beside" ] || fail "$file holds no state of its host"
  echo "  hosting the recorded session $file"
  cmd "$beside"
  expect 'teleport: ' 10
  pause 5
  sessioned=1
  peer host --play-session "$file" "$@"
}
# at_most N REGEX: no more than N such game log lines so far.
at_most() {
  local count
  count=$(since | grep -cE "$2" || true)
  if [ "$count" -le "$1" ]; then
    echo "  ok, $count (at most $1): $2"
  else
    fail "$count lines (more than $1): $2"
  fi
}
played=0
play() {
  local name=$1 file=recordings/play/$1.tplay address beside
  shift
  address=$(getent ahostsv4 "$host" | awk 'NR == 1 { print $1 }')
  if [ ! -f "$file" ]; then
    echo "  no $file (tools/game.sh fetch-play $name): synthetic motion"
    played=0
    peer join "$address:27450" "$@"
    return
  fi
  beside=$(build/linux/peer/tandem-peer play-info "$file" | sed -n 's/^beside: //p')
  [ -n "$beside" ] || fail "$file holds no state of its V"
  echo "  replaying $file"
  cmd "$beside"
  expect 'teleport: ' 10
  # A far teleport loads the world around V before the replay walks it.
  pause 5
  played=1
  peer join "$address:27450" --play "$file" "$@"
}
note() { echo "  $*"; }
shot() { mkdir -p "$out" && $game shot "$1" >/dev/null && cp ".cache/shots/$1.png" "$out/"; }

expect() {
  local pattern=$1 timeout=${2:-30} start=$SECONDS
  while ((SECONDS - start < timeout)); do
    if since | grep -qE "$pattern"; then
      echo "  ok: $pattern"
      return 0
    fi
    if stalled; then
      fail "game thread silent while waiting for: $pattern"
    fi
    sleep 1
  done
  fail "no '$pattern' in the game log within $timeout s"
}
expect_peer() {
  local pattern=$1 timeout=${2:-30} start=$SECONDS log=${3:-.cache/peer.log} from=${4:-$peer_mark}
  while ((SECONDS - start < timeout)); do
    if tail -n +$((from + 1)) "$log" 2>/dev/null | grep -qE "$pattern"; then
      echo "  ok ($(basename "$log" .log)): $pattern"
      return 0
    fi
    sleep 1
  done
  fail "no '$pattern' in $log within $timeout s"
}
expect_peer2() { expect_peer "$1" "${2:-30}" .cache/peer2.log 0; }
reject() {
  if since | grep -qE "$1"; then
    fail "unexpected: $(since | grep -E "$1" | head -1 | cut -c1-160)"
  else
    echo "  ok, none: $1"
  fi
}
reject_peer() {
  local from=${2:-$peer_mark}
  if tail -n +$((from + 1)) .cache/peer.log 2>/dev/null | grep -qE "$1"; then
    fail "unexpected (peer): $(tail -n +$((from + 1)) .cache/peer.log | grep -E "$1" | head -1 | cut -c1-160)"
  else
    echo "  ok, none (peer): $1"
  fi
}
# The thread probe (docs/architecture/testing.md): no script hook ran beside
# the script tick (on another thread while it ran) since the mark; each is
# logged the first time, then summed up.
on_game_thread() {
  local names
  names=$(since | sed -n 's/.*threads: hook \([^ ]*\) ran beside the script tick.*/\1/p' | sort -u | tr '\n' ' ' || true)
  [ -n "$names" ] || names=$(since | sed -n 's/.*threads: [1-9][0-9]* hooks beside the script tick: //p' | tail -1 || true)
  [ -n "$names" ] || return 0
  fail "script hooks ran beside the script tick (queue them, testing.md): $names"
}
pause() {
  local start=$SECONDS
  while ((SECONDS - start < $1)); do
    if stalled; then
      fail "game thread silent"
    fi
    sleep 1
  done
}

run() {
  local name=$1 file=tools/scenarios/$1.sh
  [ -f "$file" ] || {
    echo "no scenario $name" >&2
    return 2
  }
  out=.cache/scenarios/$name-$(date +%H%M%S)
  echo "== $name: $(sed -n 's/^# //p;q' "$file")"
  local start=$SECONDS
  # The first failed step ends the scenario (fail exits this subshell; set -e
  # stops it on any other failing command). Not run as an if condition or
  # on the left of ||: bash ignores set -e there, in the whole subshell.
  local status
  set +e
  (
    set -e
    # The last scenario's player leaves, and the game comes to its test state.
    pkill -x tandem-peer 2>/dev/null || true
    if $game healthy >/dev/null 2>&1; then
      mark
      reset
    fi
    referee_from=$(wc -l <.cache/peer.log 2>/dev/null || echo 0)
    # shellcheck source=/dev/null
    source "$file"
    # What the mock player heard broke no protocol or authority rule.
    reject_peer 'referee: \[' "$referee_from"
    on_game_thread
  )
  status=$?
  set -e
  if [ "$status" = 0 ]; then
    echo "  PASS ($((SECONDS - start)) s)"
    return 0
  fi
  echo "  FAILED ($((SECONDS - start)) s)"
  [ "$status" = 3 ] && return 3
  return 1
}

case ${1:-list} in
list)
  for f in tools/scenarios/*.sh; do printf '%-16s %s\n' "$(basename "$f" .sh)" "$(sed -n 's/^# //p;q' "$f")"; done
  ;;
*)
  names=("$@")
  if [ "$1" = all ]; then
    # Host scenarios first, then guest ones: one role switch, fewer restarts.
    # A scenario marked "# runs last" (it leaves the game on a death screen)
    # comes after all the others.
    # Scenarios that need the pilot (a virtual pad on the game machine) only
    # with PILOT=1.
    mapfile -t names < <(
      grep -l '^ensure host' tools/scenarios/*.sh | xargs grep -L '^# runs last' | xargs -n1 basename | sed 's/\.sh$//'
      grep -L '^ensure host' tools/scenarios/*.sh | xargs grep -L '^# runs last' | xargs -n1 basename | sed 's/\.sh$//'
      grep -l '^# runs last' tools/scenarios/*.sh | xargs -n1 basename | sed 's/\.sh$//'
    )
    if [ -z "${PILOT:-}" ]; then
      kept=()
      for name in "${names[@]}"; do
        grep -q '^# needs the pilot' "tools/scenarios/$name.sh" || kept+=("$name")
      done
      names=("${kept[@]}")
    fi
  fi
  passed=0 total=0 failures=()
  for name in "${names[@]}"; do
    total=$((total + 1))
    status=0
    run "$name" || status=$?
    if [ "$status" = 0 ]; then passed=$((passed + 1)); else failures+=("$name"); fi
    [ "$status" = 3 ] && break
  done
  pkill -x tandem-peer 2>/dev/null || true
  echo "== $passed/$total passed${failures:+, failed: ${failures[*]}}"
  [ "$passed" = "$total" ]
  ;;
esac
