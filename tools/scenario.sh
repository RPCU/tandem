#!/usr/bin/env bash
# In-game scenarios: repeatable tests of the real game against the mock
# player, each a few steps with checks on what the game logs.
#
#   tools/scenario.sh list          the scenarios (tools/scenarios/*.sh)
#   tools/scenario.sh NAME...       run these, in order
#   tools/scenario.sh all           run every one, in tools/sweep.sh's order
#                                   (a sweep: tools/sweep.sh, with results)
#
# Every scenario starts from one known state: the mock player of the one
# before is stopped and its session's end waited for (teardown), what test
# reset cannot undo is repaired ("harness:" lines: repair), then
# tools/game.sh test-reset (V on foot, up, healed, at the test field, the
# tests' leftovers gone); a game that is not "ready" fails the scenario. A
# game started by `ensure` is reset too.
#
# A scenario is a bash file of steps:
#   ensure host|join [KEY=VALUE...]  the game runs this build in this role,
#                          with these extra tandem.ini keys (restarts
#                          only when needed: tools/game.sh ensure)
#   peer ARGS...           (re)start the mock player
#   peer2 ARGS...          a second mock player beside the first (more than
#                          two players; its log .cache/peer2.log; an
#                          instance's .cache/instances/NAME/peer2.log)
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
#   at NAME|"X Y Z [YAW]"  our V teleported to a named spot (fixture:
#                          tools/scenarios/fixtures.txt) and settled there;
#                          spot NAME prints its X Y Z YAW, fixtures the names
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
#   query VAR WHAT [ARGS]  the game's own state (test query: health, item
#                          RECORD, money, fact NAME, journal PATH, position,
#                          mounted [player N|puppet], npc ID health) into VAR
#   expect_value V OP W    V = != ~ (ERE) < <= > >= W
#   expect_changed A B HOW B against A: down, up, same, changed, N, +N, -N
#   pause SECS             let time pass (still watching for a stall)
#   shot NAME              a screenshot kept with the results
#   note TEXT              a line in the report
#   expect_degraded ID     the game runs without ID on purpose here (a state
#                          of docs/architecture/degradations.md: tandem.ini
#                          switched a path off): it must be in the game's
#                          registry at the end
#   allow_fallback ID      ID may be in it (a fallback this scenario takes on
#                          purpose, or cannot rule out), and the game may
#                          tell the mock player so
# Every scenario also rejects the mock player's `referee:` lines: the game
# broke a protocol or authority rule (docs/architecture/sim.md), and the game
# log's `threads: hook NAME ran beside the script tick` lines (the thread
# probe, docs/architecture/testing.md).
# And no silent fallback (docs/architecture/degradations.md): at its end the
# game's registry (tools/game.sh degraded) holds no state and no fallback the
# scenario did not declare, and the game told the mock player of none
# (`degraded: player N's game runs without: …` in its log).
# A failed check stops the scenario and keeps the evidence under
# .cache/scenarios/NAME-TIME/: the game's log since the start, the mock
# player's log, a screenshot and, when the game thread went silent, every
# thread's backtrace (tools/game.sh bt).
#
# The game restarts only when the build or the role changed (scripts
# are hot-reloaded: RedHotTools is installed on anya). Never run this while
# someone plays (AGENTS.md).
#
# TANDEM_INSTANCE=NAME runs them on instance NAME of tools/instance.sh
# (tools/game.sh): its folder and port, the mock players' logs in
# .cache/instances/NAME/, evidence in .cache/scenarios/NAME-TIME-on-NAME/. A
# scenario's ADDRESS:27450 for the mock player becomes this game's port; its
# `pkill` stops only this game's mock players (with TANDEM_PARALLEL too).
# shellcheck disable=SC2029,SC2034 # (SC2034: $played, $peer_log are read by the scenarios it sources)
set -euo pipefail
cd "$(dirname "$0")/.."

game=tools/game.sh
host=${GAME_HOST:-anya}
# Which game: instance, dir, port, cache (the local folder of its mock
# players' logs), scoped (stop only its own mock players).
instance='' dir='' port='' cache='' scoped=''
where=$($game where) || exit 2
eval "$where"
export GAME_DIR=$dir TANDEM_PEER_OF=$instance
peer_log=$cache/peer.log
suffix=''
[ "$instance" = main ] || suffix=-on-$instance

remote() { ssh "$host" "$@"; }

# This game's mock players (pgrep ARGS), as tools/game.sh's own_peers.
own_peers() {
  local p
  for p in $(pgrep "$@" 2>/dev/null); do
    tr '\0' '\n' <"/proc/$p/environ" 2>/dev/null | grep -qxF "TANDEM_PEER_OF=$instance" && echo "$p"
  done
  return 0
}
# Scenarios stop their mock players with pkill -x tandem-peer (or -f a
# pattern): scoped, only this game's, with pkill's exit status.
pkill() {
  [ -n "$scoped" ] || {
    command pkill "$@"
    return
  }
  local pids
  pids=$(own_peers "$@")
  [ -n "$pids" ] || return 1
  # shellcheck disable=SC2086 # a list of pids
  kill $pids
}
peers_running() {
  if [ -n "$scoped" ]; then [ -n "$(own_peers -x tandem-peer)" ]; else pgrep -x tandem-peer >/dev/null 2>&1; fi
}

# The game log's lines since the mark (its file and line count at the start).
mark_file='' mark_line=0 referee_from=0 peer_mark=0
mark() {
  read -r mark_file mark_line < <(remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); echo \"\$f \$(wc -l < \"\$f\")\"")
  peer_mark=$(wc -l <"$peer_log" 2>/dev/null || echo 0)
}
# (Not the test reset's own lines: "test reset: ready (on foot, …)" would
# answer a scenario's 'on foot'.)
since() { remote "cd '$dir'; tail -n +$((mark_line + 1)) '$mark_file'" | { grep -av '] \[script\] test reset: ' || true; }; }

out=''
fail() {
  echo "  FAIL: $1"
  mkdir -p "$out"
  since >"$out/game.log" 2>/dev/null || true
  tail -n +$((peer_mark + 1)) "$peer_log" >"$out/peer.log" 2>/dev/null || true
  [ -z "$piloted" ] || cp "$cache/pilot.log" "$out/pilot.log" 2>/dev/null || true
  $game shot "scenario-fail" >/dev/null 2>&1 && cp "$cache/shots/scenario-fail.png" "$out/" || true
  # The stall report (plugin/src/stall.hpp): the net thread stopped too, so
  # no watchdog line came; its stacks are in it.
  remote "cat '$dir/red4ext/plugins/tandem/stall.log'" >"$out/stall.log" 2>/dev/null || true
  [ -s "$out/stall.log" ] || rm -f "$out/stall.log"
  if grep -q 'watchdog: game thread silent' "$out/game.log" 2>/dev/null ||
    { [ -f "$out/stall.log" ] && ! tail -1 "$out/stall.log" | grep -q ' is back after '; }; then
    $game bt >/dev/null 2>&1 && cp "$cache/re/bt.txt" "$out/bt.txt" || true
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
  local args=("$@") i
  mkdir -p "$cache"
  # This game's port, as tools/game.sh peer gives the first.
  if [ "$port" != 27450 ]; then
    for i in "${!args[@]}"; do args[i]=${args[i]/%:27450/:$port}; done
    [ "${args[0]:-}" != host ] || [[ " ${args[*]} " == *' --port '* ]] || args+=(--port "$port")
  fi
  nohup build/linux/peer/tandem-peer "${args[@]}" >"$cache/peer2.log" 2>&1 &
  echo "  second mock player: ${args[*]} ($cache/peer2.log)"
}
# The scenarios name the Steam game's port (27450); on an instance the game
# hosts on its own (game.sh: TANDEM_GAME_PORT, default 27550), which the
# mock player's arguments already follow: the game's lines and commands too.
game_port=${TANDEM_GAME_PORT:-$([ "${TANDEM_INSTANCE:-a}" = main ] || [ "${TANDEM_INSTANCE:-a}" = 0 ] && echo 27450 || echo 27550)}
cmd() {
  local text="$*"
  text=${text//27450/$game_port}
  $game cmd "$text" >/dev/null || fail "command not taken: $text"
}
# The pilot's own log (each step, its checks, the pad's safety) is kept in
# $cache/pilot.log (.cache/pilot.log) and goes with a failure's evidence.
piloted=''
pilot() {
  local status=0
  piloted=1
  mkdir -p "$cache"
  echo "  pilot: $*"
  $game pilot run "$@" >"$cache/pilot.log" 2>&1 || status=$?
  grep -E 'WARN|PASS|FAIL|SAVE' "$cache/pilot.log" | sed 's/^/  /' || true
  [ "$status" = 0 ] || fail "the pilot's routine $1 did not pass ($cache/pilot.log)"
}
reset() {
  local result status=0 silent back moves
  silent=$(watchdog_lines silent)
  back=$(watchdog_lines back)
  moves=$(since | grep -c 'elsewhere: a teleport' || true)
  result=$($game test-reset 2>&1) || status=$?
  echo "  ${result:-test reset: no answer}"
  [ "$status" = 0 ] || fail "the game is not in its test state"
  # The reset's teleport to the test field from far away goes through a
  # loading screen (the watchdog's "silent" line, which scenarios reject):
  # wait it out and mark after it (comms-objective on instance b,
  # 2026-11-22).
  # The watchdog speaks 5 s into the silence: after a teleport, wait for it.
  [ "$(since | grep -c 'elsewhere: a teleport' || true)" = "$moves" ] || sleep 7
  past_loading "$silent" "$back"
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
    peer join "$address:$port" "$@"
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
  peer join "$address:$port" --play "$file" "$@"
}
note() { echo "  $*"; }
# Named spots in the world (tools/scenarios/fixtures.txt, testing.md
# "Fixtures"). `spot NAME` prints its "X Y Z YAW" ("X Y Z [YAW]" given as
# it is passes through: a scenario's *_AT variable may hold either);
# `fixtures` the names. `at NAME|"X Y Z [YAW]"` teleports our V there and
# waits until it stands there: a far teleport goes through a loading screen
# (up to 30 s), so its position is asked every 2 s until it is within 4 m of
# the spot, then 3 s more for the world around it to stream in. A loading
# screen silences the game thread (the watchdog's "silent" line, which the
# scenarios reject): when one came, the log is marked after it.
# `moved COMMAND…` is `cmd` for a game command that teleports our V itself
# (`ledger goto`, `vendor goto`: up to 200 m, out of a basement): the same
# wait and mark (`ledger goto` went 99.5 m up from Viktor's clinic, the game
# thread silent for 19.9 s, and ledger-guest's last reject tripped on it,
# 2026-11-21). What the command logged may be before the mark: ask again.
# The scenarios resolve the game machine's name with getent: a lookup that
# comes back empty for a moment left the mock player with no address ("join
# needs an address", 2026-11-21). Tried again, 10 s at most.
getent() {
  local i out
  for i in 1 2 3 4 5 6 7 8 9 10; do
    out=$(command getent "$@") && [ -n "$out" ] && {
      echo "$out"
      return 0
    }
    sleep 1
  done
  return 2
}
# The watchdog's lines so far (silent, back), and the loading screen that
# came since those counts, waited out and the log marked after it.
watchdog_lines() { since | grep -c "watchdog: game thread $1" || true; }
past_loading() {
  [ "$(watchdog_lines silent)" != "$1" ] || return 0
  # Our V's position is set before the loading screen ends: the world
  # around it streams in only after (fixtures-survey found nothing at
  # three spots, each shot still the loading screen). Its end is the
  # watchdog's "back" line; then the streaming's few seconds.
  local waited=$SECONDS
  while [ "$(watchdog_lines back)" = "$2" ] && ((SECONDS - waited < 90)); do
    sleep 2
  done
  pause 5
  echo "  (a loading screen on the way, $((SECONDS - waited)) s: the log marked after it)"
  mark
}
moved() {
  local silent back
  silent=$(watchdog_lines silent)
  back=$(watchdog_lines back)
  cmd "$@"
  # The watchdog says "silent" after 5 s without a tick.
  sleep 7
  # The watchdog speaks 5 s into the silence: after a teleport, wait for it.
  [ "$(since | grep -c 'elsewhere: a teleport' || true)" = "$moves" ] || sleep 7
  past_loading "$silent" "$back"
}
fixture_file=tools/scenarios/fixtures.txt
spot() {
  if [[ $1 =~ ^-?[0-9.]+\ +-?[0-9.]+\ +-?[0-9.]+ ]]; then
    echo "$1"
    return 0
  fi
  awk -v n="$1" '$1 == n { print $2, $3, $4, $5; found = 1; exit } END { exit !found }' "$fixture_file"
}
fixtures() { awk '!/^#/ && NF >= 4 { print $1 }' "$fixture_file"; }
at() {
  local where x y z start=$SECONDS line silent back
  where=$(spot "$1") || fail "no fixture '$1' in $fixture_file"
  read -r x y z _ <<<"$where"
  silent=$(watchdog_lines silent)
  back=$(watchdog_lines back)
  cmd teleport "$where"
  while ((SECONDS - start < 45)); do
    sleep 2
    if stalled; then fail "game thread silent after the teleport to $1"; fi
    cmd test query position
    sleep 1
    line=$(since | grep -F 'test query: position = ' | tail -1 || true)
    line=${line%$'\r'}
    if [ -n "$line" ] && awk -v w="${line#*position = }" -v x="$x" -v y="$y" -v z="$z" 'BEGIN {
      split(w, p, " "); dx = p[1] - x; dy = p[2] - y; dz = p[3] - z
      exit !(dx * dx + dy * dy + dz * dz < 16) }'; then
      echo "  at $1 ($where) in $((SECONDS - start)) s"
      pause 3
      past_loading "$silent" "$back"
      return 0
    fi
  done
  fail "our V did not come to $1 ($where) within 45 s (last: ${line:-no position})"
}
shot() { mkdir -p "$out" && $game shot "$1" >/dev/null && cp "$cache/shots/$1.png" "$out/"; }

expect() {
  local pattern=${1//27450/$game_port} timeout=${2:-30} start=$SECONDS
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
  local pattern=$1 timeout=${2:-30} start=$SECONDS log=${3:-$peer_log} from=${4:-$peer_mark}
  while ((SECONDS - start < timeout)); do
    if tail -n +$((from + 1)) "$log" 2>/dev/null | grep -qE "$pattern"; then
      echo "  ok ($(basename "$log" .log)): $pattern"
      return 0
    fi
    sleep 1
  done
  fail "no '$pattern' in $log within $timeout s"
}
expect_peer2() { expect_peer "$1" "${2:-30}" "$cache/peer2.log" 0; }
reject() {
  local pattern=${1//27450/$game_port}
  if since | grep -qE "$pattern"; then
    fail "unexpected: $(since | grep -E "$pattern" | head -1 | cut -c1-160)"
  else
    echo "  ok, none: $1"
  fi
}
reject_peer() {
  local from=${2:-$peer_mark}
  if tail -n +$((from + 1)) "$peer_log" 2>/dev/null | grep -qE "$1"; then
    fail "unexpected (peer): $(tail -n +$((from + 1)) "$peer_log" | grep -E "$1" | head -1 | cut -c1-160)"
  else
    echo "  ok, none (peer): $1"
  fi
}
# The game's own state (scripts/tandem/TestQuery.reds), not what our code
# logged it did: `query VAR WHAT [ARGS…]` asks `test query WHAT ARGS`, waits
# for this answer ("test query: WHAT ARGS = VALUE", 10 s) and sets VAR.
query() {
  local _var=$1 _asked _from _line _start=$SECONDS
  shift
  _asked="$*"
  _from=$(since | grep -cF "test query: $_asked = " || true)
  cmd test query "$_asked"
  while ((SECONDS - _start < 10)); do
    _line=$(since | grep -F "test query: $_asked = " | sed -n "$((_from + 1))p" || true)
    if [ -n "$_line" ]; then
      _line=${_line%$'\r'}
      printf -v "$_var" '%s' "${_line#*"test query: $_asked = "}"
      echo "  query $_asked: ${!_var}"
      return 0
    fi
    if since | grep -qF "test query: unknown: $_asked"; then fail "no such query: $_asked"; fi
    sleep 1
  done
  fail "no answer to 'test query $_asked' within 10 s"
}
# expect_value VALUE OP WANT: OP = or != (text), ~ (an ERE), < <= > >=
# (numbers).
expect_value() {
  local ok=0
  case $2 in
  =) if [ "$1" = "$3" ]; then ok=1; fi ;;
  !=) if [ "$1" != "$3" ]; then ok=1; fi ;;
  '~') if grep -qE -- "$3" <<<"$1"; then ok=1; fi ;;
  '<' | '<=' | '>' | '>=')
    if ! numeric "$1" || ! numeric "$3"; then fail "not numbers: '$1' $2 '$3'"; fi
    if awk -v a="$1" -v b="$3" "BEGIN { exit !(a + 0 $2 b + 0) }"; then ok=1; fi
    ;;
  *) fail "expect_value: no operator '$2'" ;;
  esac
  [ "$ok" = 1 ] || fail "value '$1', expected $2 '$3'"
  echo "  ok: '$1' $2 '$3'"
}
# expect_changed BEFORE AFTER HOW: two answers of one query, as numbers;
# HOW: down, up, same, changed, or N / +N / -N (AFTER - BEFORE, exactly).
expect_changed() {
  if ! numeric "$1" || ! numeric "$2"; then fail "not numbers: '$1' -> '$2'"; fi
  case $3 in
  down | up | same | changed | [+-][0-9]* | [0-9]*) ;;
  *) fail "expect_changed: no such change '$3'" ;;
  esac
  awk -v b="$1" -v a="$2" -v how="$3" 'BEGIN {
    d = a - b
    if (how == "down") exit !(d < 0)
    if (how == "up") exit !(d > 0)
    if (how == "same") exit !(d == 0)
    if (how == "changed") exit !(d != 0)
    exit !(d - how < 0.005 && how - d < 0.005)
  }' || fail "'$1' -> '$2' is not $3"
  echo "  ok: '$1' -> '$2' ($3)"
}
numeric() { [[ $1 =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; }
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
# No silent fallback (docs/architecture/degradations.md). What a scenario
# declares (expect_degraded, allow_fallback: ids of core/src/degraded.cpp's
# catalogue, checked by task degraded:check), and the check at its end: the
# game's registry, then what the game told the mock player.
# Allowed everywhere: the mock player sends no look unless --look, so its
# puppet wears the stand-in body ("look not received", and "stand-in look"
# when no V body is at hand).
degraded_expected=() degraded_allowed=('look not received' 'stand-in look')
expect_degraded() {
  degraded_expected+=("$1")
  echo "  declared: the game runs without '$1'"
}
allow_fallback() { degraded_allowed+=("$1"); }
no_silent_fallback() {
  local args=() id output status=0
  # (A scenario that ends out of the game, in the main menu or with the game
  # stopped, has no registry to ask.)
  if ! $game healthy >/dev/null 2>&1; then
    echo "  the game is not playing: its registry of what it runs without was not read"
    return 0
  fi
  for id in "${degraded_expected[@]}"; do args+=(--expect "$id"); done
  for id in "${degraded_allowed[@]}"; do args+=(--allow "$id"); done
  output=$($game degraded "${args[@]}" 2>&1) || status=$?
  if [ "$status" != 0 ]; then
    while IFS= read -r line; do echo "  $line"; done <<<"$output"
    fail "degraded: $(grep -m1 -E '^(unexpected|missing): ' <<<"$output" || echo 'the registry could not be read') (docs/architecture/degradations.md)"
  fi
  [ ! -f "$peer_log" ] || output=$(tail -n +$((referee_from + 1)) "$peer_log" | tools/degraded.py told "${args[@]}") || status=$?
  [ "$status" = 0 ] || fail "degraded: $(head -1 <<<"$output") (docs/architecture/degradations.md)"
  echo "  ok: nothing degraded that was not declared"
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

# The last scenario's session ends in the game after its mock player stops:
# "player N left", "marker: player N removed" (host), "session ended",
# "save: leave cleaning -> solo" (guest). Those lines belong to the last
# scenario, not to the next one's mark: wait until a host's "left" or a
# guest's "cleaning -> solo" and 1 s without such a line, or 2 s without
# one, or nothing of it in the first 3 s; 8 s at most. Never fails.
teardown() {
  remote "bash -s -- '$dir'" <<'REMOTE' || true
cd "$1" || exit 0
f=$(ls -t red4ext/logs/tandem-*.log 2>/dev/null | head -1)
[ -n "$f" ] || exit 0
from=$(wc -l <"$f")
lines='session ended|player [0-9]+ left|left the session|save: leave |saves: |save: back from co-op|marker: player [0-9]+ removed|revive: (the session ended|we left)'
seen=0 last=0 still=0 done=0
for i in $(seq 1 16); do
  sleep 0.5
  new=$(tail -n +$((from + 1)) "$f")
  seen=$(grep -cE "$lines" <<<"$new" || true)
  grep -qE 'player [0-9]+ left|save: leave cleaning -> solo' <<<"$new" && done=1
  if [ "$seen" = "$last" ]; then still=$((still + 1)); else still=0; last=$seen; fi
  [ "$seen" = 0 ] && [ "$i" -ge 6 ] && break
  [ "$seen" -gt 0 ] && [ "$still" -ge 4 ] && break
  [ "$done" = 1 ] && [ "$still" -ge 2 ] && break
done
[ "$seen" = 0 ] || echo "the last session's end: $seen lines in $((i / 2)) s"
REMOTE
}

# What an earlier scenario can leave that test reset cannot undo, repaired
# before this one (each repair a "harness:" line): V dead on the death
# screen, saving locked after a story that could not be put back, or the
# game in the main menu (exit-host; all until a save loads: the game is
# stopped, the scenario's ensure starts it again); co-op left off (coop leave) in the role this scenario runs in,
# which ensure would not restart: hosted or joined again from the co-op
# menu, at tandem.ini's address.
repair() {
  local want dead locked menu chosen started address why
  want=$(sed -n 's/^ensure //p' "$1" | head -1)
  want=${want// /,}
  # (Since the last load, "playing": dead, locked, in the main menu; the
  # last session chosen; the role the game was started in; the join
  # address.)
  read -r dead locked menu chosen started address < <(remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); \
    awk '/\\[script\\] playing/ { dead = 0; locked = 0; menu = 0 } /revive: our V dies: death screen/ { dead = 1 } \
      /save: stays locked/ { locked = 1 } /save: unlocked|saving unlocked/ { locked = 0 } \
      /\\[script\\] main menu world: not a game/ { menu = 1 } \
      /session: chosen / { sub(/.*session: chosen /, \"\"); chosen = \$1 } \
      END { printf \"%d %d %d %s \", dead, locked, menu, chosen == \"\" ? \"-\" : chosen }' \"\$f\"; \
    s=\$(sed 's/^[^ ]* //' red4ext/plugins/tandem/.started 2>/dev/null); a=\$(sed -n 's/^address *= *//p' red4ext/plugins/tandem/tandem.ini); \
    echo \"\${s:--} \${a:--}\"") || return 0
  if [ "$dead" = 1 ] || [ "$locked" = 1 ] || [ "$menu" = 1 ]; then
    why="our V is dead on the death screen"
    [ "$locked" = 1 ] && why="saving stays locked (a story not put back)"
    [ "$menu" = 1 ] && why="the game is in the main menu"
    echo "  harness: $why since the last load: the game is stopped, ensure starts it again"
    $game stop >/dev/null 2>&1 || true
    return 0
  fi
  [ "$chosen" = off ] && [ "$started" = "$want" ] || return 0
  local words lines
  case ${want%%,*} in
  join)
    if [ "$address" = - ]; then
      echo "  harness: co-op is off as a guest and tandem.ini has no address: left as it is"
      return 0
    fi
    words="coop join $address:$port"
    echo "  harness: co-op was left off as a guest (an earlier scenario's coop leave): $words"
    ;;
  host)
    words="coop host"
    echo "  harness: co-op was left off as the host (an earlier scenario's coop leave): $words"
    ;;
  *) return 0 ;;
  esac
  lines=$(remote "cd '$dir'; wc -l <\"\$(ls -t red4ext/logs/tandem-*.log | head -1)\"") || return 0
  $game cmd "$words" >/dev/null 2>&1 || echo "  harness: $words not taken"
  # The new choice's session starts.
  remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); \
    for i in \$(seq 1 20); do tail -n +$((lines + 1)) \"\$f\" | grep -q 'session: starting as' && exit 0; sleep 0.5; done; exit 1" ||
    echo "  harness: no session started within 10 s"
}

run() {
  local name=$1 file=tools/scenarios/$1.sh
  [ -f "$file" ] || {
    echo "no scenario $name" >&2
    return 2
  }
  out=.cache/scenarios/$name-$(date +%H%M%S)$suffix
  echo "== $name: $(sed -n 's/^# //p;q' "$file")"
  local start=$SECONDS
  # The first failed step ends the scenario (fail exits this subshell; set -e
  # stops it on any other failing command). Not run as an if condition or
  # on the left of ||: bash ignores set -e there, in the whole subshell.
  local status
  set +e
  (
    set -e
    # The last scenario's player leaves, its session ends in the game, what
    # it left is repaired, and the game comes to its test state.
    local had_peer=''
    peers_running && had_peer=1
    pkill -x tandem-peer 2>/dev/null || true
    if $game healthy >/dev/null 2>&1; then
      if [ -n "$had_peer" ]; then teardown | sed 's/^/  /'; fi
      repair "$file"
    fi
    if $game healthy >/dev/null 2>&1; then
      mark
      reset
    fi
    referee_from=$(wc -l <"$peer_log" 2>/dev/null || echo 0)
    degraded_expected=() degraded_allowed=('look not received' 'stand-in look')
    # shellcheck source=/dev/null
    source "$file"
    # What the mock player heard broke no protocol or authority rule.
    reject_peer 'referee: \[' "$referee_from"
    on_game_thread
    # And the game ran on no lesser path the scenario did not declare.
    no_silent_fallback
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
    # In tools/sweep.sh's order: grouped by ensure line (one restart per
    # role and tandem.ini keys), "# runs last" ones at the end; the pilot's
    # only with PILOT=1. By-hand ones too (tools/sweep.sh leaves them out).
    mapfile -t names < <(tools/sweep.sh --list --by-hand | awk '{ print $1 }')
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
