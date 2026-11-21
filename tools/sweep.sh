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
#   --instances N  N games at once: the Steam game (instance 0) and the first
#                  N-1 instances of tools/instance.sh named in SWEEP_INSTANCES
#                  (default "a b c"), each a slice of the list (whole groups
#                  where it can), in parallel; results.txt merged in the
#                  sweep's order, with a 7th column, the game. Those that
#                  need the Steam game (needs_main, below) stay on it.
#   --weights FILE a results.txt whose seconds balance the slices (default:
#                  one per scenario)
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
# Never run this while someone plays (AGENTS.md). Several games at once:
# docs/architecture/two-instances.md, "Sweeps on two games" (not yet run in
# game).
# shellcheck disable=SC2029
set -euo pipefail
cd "$(dirname "$0")/.."

host=${GAME_HOST:-anya}
game=tools/game.sh
scenario=tools/scenario.sh
remote() { ssh "$host" "$@"; }
# This shell's game, from tools/game.sh where (sweep_slice).
instance='' dir='' cache='' pids_fn=''

out='' limit=${SWEEP_TIMEOUT:-900} by_hand='' list='' rerun='' names=() instances=1 weights=''
while [ $# -gt 0 ]; do
  case $1 in
  --out) out=${2:?--out DIR} && shift ;;
  --timeout) limit=${2:?--timeout SECONDS} && shift ;;
  --by-hand) by_hand=1 ;;
  --list) list=1 ;;
  --rerun-failed) rerun=${2:?--rerun-failed FILE} && shift ;;
  --instances) instances=${2:?--instances N} && shift ;;
  --weights) weights=${2:?--weights FILE} && shift ;;
  -h | --help)
    sed -n '2,38p' "$0"
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

# Only the Steam game (instance 0) runs it, beside other games: "# needs the
# main game"; the by-hand ones (a person watches that game's screen); the
# pilot's (its pad and keys are the Steam game's); keys (tools/game.sh key,
# hold: codes chosen on anya's AZERTY session, an instance's own X keymap
# is another); the port 27450 named anywhere but a mock player's address
# (a log line it expects, a coop join, ss): an instance has its own port.
needs_main() {
  local f
  f=$(file_of "$1")
  grep -qE '^# needs the (main game|pilot)' "$f" && return 0
  is_by_hand "$1" && return 0
  # shellcheck disable=SC2016 # (a scenario's own words)
  grep -qE '(\$game|tools/game\.sh) (key|hold)( |$)' "$f" && return 0
  grep -vE '^ *(#|peer |peer2 )' "$f" | grep -q 27450
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

# The games: instance 0 the Steam game, then SWEEP_INSTANCES' first N-1.
[[ $instances =~ ^[1-9][0-9]?$ ]] || {
  echo "sweep: --instances N, 1 to 99" >&2
  exit 2
}
read -r -a spare <<<"${SWEEP_INSTANCES:-a b c}"
games=(main "${spare[@]:0:instances-1}")
[ ${#games[@]} = "$instances" ] || {
  echo "sweep: --instances $instances needs $((instances - 1)) names in SWEEP_INSTANCES (${SWEEP_INSTANCES:-a b c})" >&2
  exit 2
}
[ -z "$weights" ] || [ -f "$weights" ] || {
  echo "sweep: no results file $weights" >&2
  exit 2
}

# The ordered list in N slices, "NAME<TAB>GROUP<TAB>SLICE" lines, each
# slice in the sweep's order: those that need the Steam game on slice 0; the
# others in runs of one group (cut where a run is more than a slice's
# share: one more game start), the heaviest first, each to a slice that has
# its group already and room, else to the lightest. Weights: the seconds of
# --weights (the mean for a scenario not in it), else one each.
slices() {
  local entry
  for entry in "${ordered[@]}"; do
    if [ "$instances" -gt 1 ] && needs_main "${entry%%$'\t'*}"; then
      printf '%s\t1\n' "$entry"
    else
      printf '%s\t0\n' "$entry"
    fi
  done | awk -F'\t' -v n="$instances" -v weights="$weights" '
    BEGIN {
      if (weights != "")
        while ((getline line < weights) > 0) {
          split(line, f, "\t")
          if (f[3] ~ /^[0-9]+$/) { secs[f[1]] = f[3]; sum += f[3]; known++ }
        }
      mean = known ? sum / known : 1
    }
    {
      name[NR] = $1; grp[NR] = $2; w[NR] = ($1 in secs) ? secs[$1] : mean; total += w[NR]
      if ($3 == 1) { at[NR] = 0; load[0] += w[NR]; has[0, $2] = 1 }
    }
    END {
      share = total / n
      for (i = 1; i <= NR; i++) {
        if (i in at) continue
        if (!p || grp[i] != pgrp[p] || (size[p] > 0 && size[p] + w[i] > share)) { p++; pgrp[p] = grp[i] }
        member[p, ++count[p]] = i; size[p] += w[i]
      }
      for (c = 1; c <= p; c++) {
        best = 0
        for (k = 1; k <= p; k++) if (!placed[k] && (!best || size[k] > size[best])) best = k
        placed[best] = 1
        s = -1
        for (t = 0; t < n; t++)
          if (has[t, pgrp[best]] && load[t] + size[best] <= share && (s < 0 || load[t] < load[s])) s = t
        if (s < 0) { s = 0; for (t = 1; t < n; t++) if (load[t] < load[s]) s = t }
        load[s] += size[best]; has[s, pgrp[best]] = 1
        for (j = 1; j <= count[best]; j++) at[member[best, j]] = s
      }
      for (i = 1; i <= NR; i++) printf "%s\t%s\t%d\n", name[i], grp[i], at[i]
    }'
}
mapfile -t sliced < <(slices)

if [ -n "$list" ]; then
  if [ "$instances" = 1 ]; then
    printf '%s\n' "${ordered[@]}" | awk -F'\t' '{ printf "%-28s %s\n", $1, $2 }'
  else
    printf '%s\n' "${sliced[@]}" | awk -F'\t' -v games="${games[*]}" 'BEGIN { ng = split(games, g, " ") }
      { printf "%-28s %-6s %s\n", $1, g[$3 + 1], $2; n[$3]++ }
      END { for (s = 0; s < ng; s++) printf "== %s: %d scenarios\n", g[s + 1], n[s] }'
  fi
  exit 0
fi

out=${out:-.cache/sweeps/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$out"
results=$out/results.txt
: >"$results"

# After a failure: "crash LINE", "hang", "ok" or "down" (no game).
# The hang check asks `coop status` (a line in a game that runs) and waits
# up to 20 s for the log to grow.
# (This game's processes only: game_pids, tools/game.sh's.)
health() {
  {
    echo "$pids_fn"
    cat
  } <<'REMOTE' | remote "bash -s -- '$dir'" || echo unknown
cd "$1" || { echo down; exit 0; }
[ -n "$(game_pids -f '[C]yberpunk2077.exe')" ] || { echo down; exit 0; }
rg=$(ls -t red4ext/logs/redgraph-*.log 2>/dev/null | head -1)
if [ -n "$rg" ] && grep -aq '\[crash\] thread' "$rg"; then
  echo "crash $(grep -a -m1 '\[crash\] thread' "$rg" | cut -c1-200)"
  exit 0
fi
if [ -n "$(game_pids -f '[C]rashReporter.exe')" ]; then
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

# sweep_slice RESULTS TAG ENTRY...: the scenarios one at a time on this
# shell's game (TANDEM_INSTANCE: tools/game.sh where), a line each in
# RESULTS. TAG: empty alone; beside other games, the game's name and a space
# (in its progress lines), and results get a 7th column, the game.
sweep_slice() {
  local results=$1 tag=$2 entry name i=0 total log start rc seconds ev state class line where extra=''
  shift 2
  total=$#
  : >"$results"
  where=$("$game" where) || return 2
  eval "$where"
  local suffix=''
  if [ "$instance" != main ]; then
    suffix=-on-$instance
    # (Its scenarios need not ask the game machine for its folder again.)
    export GAME_DIR=$dir
  fi
  [ -z "$tag" ] || extra=$'\t'$instance
  for entry in "$@"; do
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
      printf '%s\tPASS\t%s\tpass\t\t%s\n' "$name" "$seconds" "$extra" >>"$results"
      printf '[%s%d/%d] %-28s PASS %4d s\n' "$tag" "$i" "$total" "$name" "$seconds"
      continue
    fi
    ev=$(sed -n 's/^  evidence: //p' "$log" | tail -1)
    state=$(health)
    case $state in
    crash* | hang)
      if [ -z "$ev" ]; then
        ev=.cache/scenarios/$name-$(date +%H%M%S)$suffix
      fi
      mkdir -p "$ev"
      cp "$log" "$ev/scenario.log"
      # The stacks first: a stopped game's are gone.
      if [ ! -s "$ev/bt.txt" ]; then
        timeout 240 "$game" bt </dev/null >/dev/null 2>&1 && cp "$cache/re/bt.txt" "$ev/bt.txt" || echo "  (no backtrace)" >>"$log"
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
    printf '%s\tFAIL\t%s\t%s\t%s\t%s%s\n' "$name" "$seconds" "$class" "$line" "${ev:--}" "$extra" >>"$results"
    printf '[%s%d/%d] %-28s FAIL %4d s  %-10s %s\n' "$tag" "$i" "$total" "$name" "$seconds" "$class" "${line:0:120}"
    [ "$class" != abort ] || {
      echo "== ${tag}the scripts do not compile: the sweep stops"
      return 0
    }
  done
}

began=$SECONDS
if [ "$instances" = 1 ]; then
  echo "== sweep: ${#ordered[@]} scenarios, at most $limit s each: $results"
  sweep_slice "$results" '' "${ordered[@]}"
else
  # Beside each other (docs/architecture/two-instances.md, "Sweeps on two
  # games"): the Steam game as instance 0, port 27450; instance k on port
  # 27450 + 100 k, headless unless INSTANCE_DISPLAY says otherwise, started
  # beside the Steam game (INSTANCE_ALLOW_FOREIGN: it is a test game here);
  # every game stops only its own mock players, starts and deploys one at a
  # time (TANDEM_PARALLEL).
  for g in "${games[@]:1}"; do
    tools/instance.sh dir "$g" >/dev/null || {
      echo "sweep: no instance $g on the game machine (tools/instance.sh create $g)" >&2
      exit 2
    }
  done
  echo "== sweep: ${#ordered[@]} scenarios on ${games[*]}, at most $limit s each: $results"
  slice_pids=()
  # Ctrl-C: each slice's scenario (timeout signals its group), then the slice.
  trap 'for p in "${slice_pids[@]}"; do pkill -TERM -P "$p"; kill "$p"; done 2>/dev/null; exit 130' INT TERM
  for k in "${!games[@]}"; do
    mapfile -t mine < <(printf '%s\n' "${sliced[@]}" | awk -F'\t' -v k="$k" '$3 == k { print $1 "\t" $2 }')
    [ ${#mine[@]} -gt 0 ] || continue
    (
      export TANDEM_PARALLEL=1
      unset TANDEM_INSTANCE PILOT_INSTANCE TANDEM_GAME_PORT
      if [ "$k" -gt 0 ]; then
        unset GAME_DIR
        export TANDEM_INSTANCE=${games[k]} TANDEM_GAME_PORT=$((27450 + 100 * k)) INSTANCE_ALLOW_FOREIGN=1
        export INSTANCE_DISPLAY=${INSTANCE_DISPLAY:-headless}
      fi
      sweep_slice "$out/results-${games[k]}.txt" "${games[k]} " "${mine[@]}"
    ) &
    slice_pids+=($!)
  done
  for p in "${slice_pids[@]}"; do wait "$p" || true; done
  trap - INT TERM
  # One results.txt, in the sweep's order.
  printf '%s\n' "${ordered[@]}" | cut -f1 | awk -F'\t' 'FNR == NR { order[++n] = $1; next } { line[$1] = $0 }
    END { for (i = 1; i <= n; i++) if (order[i] in line) print line[order[i]] }' - "$out"/results-*.txt >"$results"
  awk -F'\t' '{ ran[$7]++; if ($2 == "PASS") passed[$7]++ } END { for (g in ran) printf "== %s: %d/%d passed\n", g, passed[g], ran[g] }' "$results" | sort
fi

# The summary: failures by class, and what the harness did.
passed=$(awk -F'\t' '$2 == "PASS"' "$results" | wc -l)
ran=$(wc -l <"$results")
starts=$(cat "$out"/*.log 2>/dev/null | grep -c '^  launched' || true)
repairs=$(cat "$out"/*.log 2>/dev/null | grep -c '^  harness: ' || true)
echo "== $passed/$ran passed in $(((SECONDS - began) / 60)) min, $starts game starts, $repairs harness repairs: $results"
awk -F'\t' '$2 == "FAIL" { n[$4]++; s[$4] = s[$4] " " $1 } END { for (c in n) printf "%-11s %3d %s\n", c, n[c], s[c] }' "$results" | sort -k2,2nr
[ "$passed" = "$ran" ]
