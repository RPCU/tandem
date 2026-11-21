#!/usr/bin/env bash
# A sweep of in-game scenarios (tools/scenario.sh, one at a time), ordered
# for few restarts, each failure classed for triage.
#
#   tools/sweep.sh [OPTIONS] [NAME...]   these scenarios (default: all but
#                                        the by-hand ones and, without
#                                        PILOT=1, the pilot's)
#   tools/sweep.sh --rerun-failed FILE   the failures of an earlier results file
#   tools/sweep.sh --list [NAME...]      the order, with each one's group
#
#   --out DIR      results.txt and each scenario's output (NAME.log) there
#                  (default .cache/sweeps/YYYYMMDD-HHMMSS)
#   --timeout S    one scenario's limit (default 900, SWEEP_TIMEOUT)
#   --by-hand      keep the by-hand ones ("# by hand", or an expect of 120 s
#                  and more in a file that says "by hand" or "waits for")
#
# Order: grouped by the scenario's ensure line (role and tandem.ini keys:
# one restart per group), in a group those that stop the game at their end
# last, "# runs last" ones after every group.
# After a failure, a game that crashed (REDGraph's "[crash] thread" in the
# newest red4ext/logs/redgraph-*.log, or CrashReporter.exe) or hangs (the
# newest tandem log does not grow for 20 s after a `coop status` while the
# game process lives): every thread's backtrace first (tools/game.sh bt, into
# the evidence folder), then tools/game.sh stop (the next ensure starts it).
# results.txt: one line per scenario, tab-separated: name, PASS|FAIL,
# seconds, class, the first FAIL line, the evidence folder. The end prints
# the failures grouped by class (docs/architecture/testing.md, "Sweeps").
# Never run this while someone plays (AGENTS.md).
# shellcheck disable=SC2029
set -euo pipefail
cd "$(dirname "$0")/.."

host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
game=tools/game.sh
scenario=tools/scenario.sh
remote() { ssh "$host" "$@"; }

out='' limit=${SWEEP_TIMEOUT:-900} by_hand='' list='' rerun='' names=()
while [ $# -gt 0 ]; do
  case $1 in
  --out) out=${2:?--out DIR} && shift ;;
  --timeout) limit=${2:?--timeout SECONDS} && shift ;;
  --by-hand) by_hand=1 ;;
  --list) list=1 ;;
  --rerun-failed) rerun=${2:?--rerun-failed FILE} && shift ;;
  -h | --help)
    sed -n '2,28p' "$0"
    exit 0
    ;;
  -*)
    echo "sweep: unknown option $1" >&2
    exit 2
    ;;
  *) names+=("$1") ;;
  esac
  shift
done

file_of() { echo "tools/scenarios/$1.sh"; }

# A person in game must do something: an explicit "# by hand", or a long
# wait (an expect of 120 s or more) with words for a person.
is_by_hand() {
  local f
  f=$(file_of "$1")
  grep -q '^# by hand' "$f" && return 0
  grep -qE '^ *expect(_peer)? .* (1[2-9][0-9]|[2-9][0-9]{2}|[0-9]{4,})$' "$f" && grep -qE 'by hand|waits for' "$f"
}

# NAME... in sweep order: "NAME<TAB>GROUP" lines.
order() {
  local name f last stops group
  for name in "$@"; do
    f=$(file_of "$name")
    last=0 stops=0
    grep -q '^# runs last' "$f" && last=1
    # shellcheck disable=SC2016 # (a scenario's own words)
    grep -qE '^[^#]*\$game stop' "$f" && stops=1
    group=$(sed -n 's/^ensure //p' "$f" | head -1)
    printf '%s\t%s\t%s\t%s\n' "$last" "${group:--}" "$stops" "$name"
  done | LC_ALL=C sort -t$'\t' -k1,1n -k2,2 -k3,3n -k4,4 | awk -F'\t' '{ print $4 "\t" ($1 == 1 ? "last, " : "") $2 }'
}

if [ -n "$rerun" ]; then
  [ -f "$rerun" ] || {
    echo "sweep: no results file $rerun" >&2
    exit 2
  }
  mapfile -t names < <(awk -F'\t' '$2 == "FAIL" { print $1 }' "$rerun")
  [ ${#names[@]} -gt 0 ] || {
    echo "sweep: no failure in $rerun"
    exit 0
  }
elif [ ${#names[@]} -eq 0 ]; then
  for f in tools/scenarios/*.sh; do
    name=$(basename "$f" .sh)
    if [ -z "${PILOT:-}" ] && grep -q '^# needs the pilot' "$f"; then continue; fi
    if [ -z "$by_hand" ] && is_by_hand "$name"; then continue; fi
    names+=("$name")
  done
fi
for name in "${names[@]}"; do
  [ -f "$(file_of "$name")" ] || {
    echo "sweep: no scenario $name" >&2
    exit 2
  }
done
mapfile -t ordered < <(order "${names[@]}")

if [ -n "$list" ]; then
  printf '%s\n' "${ordered[@]}" | awk -F'\t' '{ printf "%-28s %s\n", $1, $2 }'
  exit 0
fi

out=${out:-.cache/sweeps/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
results=$out/results.txt
: >"$results"

# After a failure: "crash LINE", "hang", "ok" or "down" (no game).
# The hang check asks `coop status` (a line in a game that runs) and waits
# up to 20 s for the log to grow.
health() {
  remote "bash -s -- '$dir'" <<'REMOTE' || echo unknown
cd "$1" || { echo down; exit 0; }
pgrep -f '[C]yberpunk2077.exe' >/dev/null || { echo down; exit 0; }
rg=$(ls -t red4ext/logs/redgraph-*.log 2>/dev/null | head -1)
if [ -n "$rg" ] && grep -aq '\[crash\] thread' "$rg"; then
  echo "crash $(grep -a -m1 '\[crash\] thread' "$rg" | cut -c1-200)"
  exit 0
fi
if pgrep -f '[C]rashReporter.exe' >/dev/null; then
  echo "crash CrashReporter.exe runs"
  exit 0
fi
f=$(ls -t red4ext/logs/tandem-*.log 2>/dev/null | head -1)
[ -n "$f" ] || { echo ok; exit 0; }
size=$(stat -c %s "$f")
c=red4ext/plugins/tandem/tandem.cmd
[ -e "$c" ] || { printf 'coop status\n' >"$c.new" && mv -f "$c.new" "$c"; }
for _ in $(seq 1 20); do
  sleep 1
  [ "$(stat -c %s "$f")" = "$size" ] || { echo ok; exit 0; }
done
rm -f "$c"
echo hang
REMOTE
}

# The REDGraph crash lines, for the evidence.
crash_lines() {
  remote "cd '$dir'; grep -a '\\[crash\\]' \"\$(ls -t red4ext/logs/redgraph-*.log | head -1)\"" 2>&1 || true
}

# class RC LOG EVIDENCE HEALTH NAME: the failure's class (testing.md, "Sweeps").
classify() {
  local rc=$1 log=$2 ev=$3 health=$4 name=$5 line prefix where
  line=$(grep -m1 -E '^  (FAIL|ABORT): ' "$log" | sed 's/^  [A-Z]*: //' || true)
  if [ "$rc" = 3 ] || grep -q '^  ABORT: ' "$log"; then
    echo abort
    return
  fi
  case $health in crash*) echo crash && return ;; hang) echo hang && return ;; esac
  if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
    echo timeout
    return
  fi
  if is_by_hand "$name"; then
    echo by-hand
    return
  fi
  if grep -qE '(mock player: join|coop join) :[0-9]' "$log"; then
    echo dns
    return
  fi
  if [ -n "$ev" ] && grep -qE 'needs a value|^usage: |unknown option|unknown flag' "$ev/peer.log" 2>/dev/null; then
    echo peer-args
    return
  fi
  case $line in
  *'game thread silent'*) echo hang && return ;;
  *'referee: ['*) echo referee && return ;;
  *'script hooks ran'*) echo threads && return ;;
  *'not in its test state'*) echo reset && return ;;
  *'is not running this build'*) echo launch && return ;;
  *"pilot's routine"*) echo pilot && return ;;
  esac
  if [ -n "$ev" ] && grep -qE ': no [^:]* within [0-9]+ m$|: nothing there$|: no ground near|nobody in a workspot within' "$ev/game.log" 2>/dev/null; then
    echo location
    return
  fi
  local expected="^no '(.*)' in (the game log|[^ ]+) within"
  if [[ $line =~ $expected ]]; then
    # A line that begins as the pattern does (its text up to the first
    # regex sign, 8 characters at least) is there: reworded, or other
    # values. None: the game never said it.
    where=$ev/game.log
    [ "${BASH_REMATCH[2]}" = 'the game log' ] || where=$ev/peer.log
    # shellcheck disable=SC2001 # (a bracket of regex signs)
    prefix=$(sed 's/[][\\.()*+?{|^$].*//' <<<"${BASH_REMATCH[1]}")
    if [ ${#prefix} -ge 8 ] && grep -qF -- "$prefix" "$where" 2>/dev/null; then echo wording; else echo missing; fi
    return
  fi
  case $line in
  unexpected*) echo unexpected ;;
  *'command not taken'*) echo command ;;
  *) echo other ;;
  esac
}

total=${#ordered[@]} i=0 began=$SECONDS
echo "== sweep: $total scenarios, at most $limit s each: $results"
for entry in "${ordered[@]}"; do
  name=${entry%%$'\t'*}
  i=$((i + 1))
  log=$out/$name.log
  start=$SECONDS rc=0
  # timeout signals its whole process group: the scenario's subshells and
  # mock players too.
  # (No terminal: a background process group reading it would stop.)
  timeout -k 15 "$limit" "$scenario" "$name" </dev/null >"$log" 2>&1 || rc=$?
  seconds=$((SECONDS - start))
  if [ "$rc" = 0 ]; then
    printf '%s\tPASS\t%s\tpass\t\t\n' "$name" "$seconds" >>"$results"
    printf '[%d/%d] %-28s PASS %4d s\n' "$i" "$total" "$name" "$seconds"
    continue
  fi
  ev=$(sed -n 's/^  evidence: //p' "$log" | tail -1)
  state=$(health)
  case $state in
  crash* | hang)
    if [ -z "$ev" ]; then
      ev=.cache/scenarios/$name-$(date +%H%M%S)
    fi
    mkdir -p "$ev"
    cp "$log" "$ev/scenario.log"
    # The stacks first: a stopped game's are gone.
    if [ ! -s "$ev/bt.txt" ]; then
      timeout 240 "$game" bt </dev/null >/dev/null 2>&1 && cp .cache/re/bt.txt "$ev/bt.txt" || echo "  (no backtrace)" >>"$log"
    fi
    [ "${state%% *}" != crash ] || crash_lines >"$ev/crash.txt"
    timeout 90 "$game" stop </dev/null >/dev/null 2>&1 || true
    echo "  sweep: $state: backtraces in $ev/bt.txt, the game stopped" >>"$log"
    ;;
  esac
  class=$(classify "$rc" "$log" "$ev" "$state" "$name")
  # Not in its test state: what the last one left (a menu, the phone) would
  # fail every later one too; the next ensure starts a fresh game.
  if [ "$class" = reset ] && [ "${state%% *}" = ok ]; then
    timeout 90 "$game" stop </dev/null >/dev/null 2>&1 || true
    echo "  harness: not in its test state: the game stopped, the next one starts it" >>"$log"
  fi
  line=$(grep -m1 -E '^  (FAIL|ABORT): ' "$log" | sed 's/^  //' | tr '\t' ' ' | cut -c1-300 || true)
  [ -n "$line" ] || line="exit $rc$([ "$rc" = 124 ] && echo ", stopped after $limit s")"
  printf '%s\tFAIL\t%s\t%s\t%s\t%s\n' "$name" "$seconds" "$class" "$line" "${ev:--}" >>"$results"
  printf '[%d/%d] %-28s FAIL %4d s  %-10s %s\n' "$i" "$total" "$name" "$seconds" "$class" "${line:0:120}"
  [ "$class" != abort ] || {
    echo "== the scripts do not compile: the sweep stops"
    break
  }
done

# The summary: failures by class, and what the harness did.
passed=$(awk -F'\t' '$2 == "PASS"' "$results" | wc -l)
ran=$(wc -l <"$results")
starts=$(cat "$out"/*.log 2>/dev/null | grep -c '^  launched' || true)
repairs=$(cat "$out"/*.log 2>/dev/null | grep -c '^  harness: ' || true)
echo "== $passed/$ran passed in $(((SECONDS - began) / 60)) min, $starts game starts, $repairs harness repairs: $results"
awk -F'\t' '$2 == "FAIL" { n[$4]++; s[$4] = s[$4] " " $1 } END { for (c in n) printf "%-11s %3d %s\n", c, n[c], s[c] }' "$results" | sort -k2,2nr
[ "$passed" = "$ran" ]
