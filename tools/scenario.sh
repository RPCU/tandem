#!/usr/bin/env bash
# In-game scenarios: repeatable tests of the real game against the mock
# player, each a few steps with checks on what the game logs.
#
#   tools/scenario.sh list          the scenarios (tools/scenarios/*.sh)
#   tools/scenario.sh NAME...       run these, in order
#   tools/scenario.sh all           run every one, host scenarios first
#
# A scenario is a bash file of steps:
#   ensure host|join       the game runs this build in this role (restarts
#                          only when needed: tools/game.sh ensure)
#   peer ARGS...           (re)start the mock player
#   cmd TEXT               a tandem command in the game
#   expect REGEX [SECS]    a game log line since the scenario began (default
#                          30 s); fails at once if the watchdog reports a
#                          silent game thread
#   expect_peer REGEX [SECS]  the same in the mock player's log
#   reject REGEX           no such game log line so far
#   pause SECS             let time pass (still watching for a stall)
#   shot NAME              a screenshot kept with the results
#   note TEXT              a line in the report
# A failed check stops the scenario and keeps the evidence under
# .cache/scenarios/NAME-TIME/: the game's log since the start, the mock
# player's log, a screenshot and, when the game thread went silent, every
# thread's backtrace (tools/game.sh bt).
#
# The game restarts only when the build or the role changed (scripts would
# be hot-reloaded if RedHotTools were installed). Never run this while
# someone plays (AGENTS.md).
# shellcheck disable=SC2029
set -euo pipefail
cd "$(dirname "$0")/.."

game=tools/game.sh
host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}

remote() { ssh "$host" "$@"; }

# The game log's lines since the mark (its file and line count at the start).
mark_file='' mark_line=0
mark() {
  read -r mark_file mark_line < <(remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); echo \"\$f \$(wc -l < \"\$f\")\"")
  peer_mark=$(wc -l <.cache/peer.log 2>/dev/null || echo 0)
}
since() { remote "cd '$dir'; tail -n +$((mark_line + 1)) '$mark_file'"; }

out=''
fail() {
  echo "  FAIL: $1"
  mkdir -p "$out"
  since >"$out/game.log" 2>/dev/null || true
  tail -n +$((peer_mark + 1)) .cache/peer.log >"$out/peer.log" 2>/dev/null || true
  $game shot "scenario-fail" >/dev/null 2>&1 && cp .cache/shots/scenario-fail.png "$out/" || true
  if grep -q 'watchdog: game thread silent' "$out/game.log" 2>/dev/null; then
    $game bt >/dev/null 2>&1 && cp .cache/re/bt.txt "$out/bt.txt" || true
    echo "  the game thread stopped: backtraces in $out/bt.txt"
  fi
  echo "  evidence: $out"
  # Ends the scenario (it runs in a subshell of its own).
  exit 1
}

stalled() { since | grep -q 'watchdog: game thread silent'; }

ensure() {
  local output
  output=$($game ensure "$1")
  while IFS= read -r line; do echo "  $line"; done <<<"$output"
  mark
  # A fresh start: everything in the new log belongs to this scenario.
  if grep -q '^launched' <<<"$output"; then mark_line=0; fi
}
peer() {
  $game peer "$@" | sed 's/^/  /'
  peer_mark=0
}
cmd() { $game cmd "$*" >/dev/null || fail "command not taken: $*"; }
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
  local pattern=$1 timeout=${2:-30} start=$SECONDS
  while ((SECONDS - start < timeout)); do
    if tail -n +$((peer_mark + 1)) .cache/peer.log 2>/dev/null | grep -qE "$pattern"; then
      echo "  ok (peer): $pattern"
      return 0
    fi
    sleep 1
  done
  fail "no '$pattern' in the mock player's log within $timeout s"
}
reject() {
  if since | grep -qE "$1"; then
    fail "unexpected: $(since | grep -E "$1" | head -1 | cut -c1-160)"
  else
    echo "  ok, none: $1"
  fi
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
  # stops it on any other failing command: not run as an if condition,
  # where bash would ignore set -e).
  local status=0
  (
    set -e
    # shellcheck source=/dev/null
    source "$file"
  ) || status=$?
  if [ "$status" = 0 ]; then
    echo "  PASS ($((SECONDS - start)) s)"
    return 0
  fi
  echo "  FAILED ($((SECONDS - start)) s)"
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
    mapfile -t names < <(
      grep -l '^ensure host' tools/scenarios/*.sh | xargs -n1 basename | sed 's/\.sh$//'
      grep -L '^ensure host' tools/scenarios/*.sh | xargs -n1 basename | sed 's/\.sh$//'
    )
  fi
  passed=0 total=0 failures=()
  for name in "${names[@]}"; do
    total=$((total + 1))
    if run "$name"; then passed=$((passed + 1)); else failures+=("$name"); fi
  done
  pkill -x tandem-peer 2>/dev/null || true
  echo "== $passed/$total passed${failures:+, failed: ${failures[*]}}"
  [ "$passed" = "$total" ]
  ;;
esac
