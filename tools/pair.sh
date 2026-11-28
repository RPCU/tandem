#!/usr/bin/env bash
# Two real games in one session, unattended: instance PAIR_HOST (a) hosts,
# PAIR_GUEST (c) joins it over loopback (tools/instance.sh), and each pair
# script (tools/pairs/NAME.sh) acts on both games and checks both logs. One
# line per script, "pair NAME: pass" or "pair NAME: FAIL (why)"; its evidence
# (both logs since the script began, its measures, screenshots of both
# screens) in .cache/pairs/NAME-TIME/.
#
#   tools/pair.sh NAME [NAME...]   deploy, (re)start both when the build or
#                                  scripts changed, test reset both, run each
#   tools/pair.sh --list           the pair scripts and what each proves
#   tools/pair.sh --stop           stop both instances
#
# TANDEM_PREBUILT=1: deploy the DLL in build/ as it is (tools/remote.sh). A
# pair script runs in this shell with:
#   on h|g TEXT...           a tandem command on the host's or the guest's game
#   expect h|g REGEX [S]     wait S s (20) for a log line of that game since
#                            the script began; none: the script fails
#   reject h|g REGEX         at the end: such a line fails the script
#   lines h|g REGEX          the matching lines so far (for measures)
#   measure TEXT             a line of the evidence's measures.txt
#   fail WHY                 the script fails now
#   shot LABEL               both screens, LABEL-host.png and LABEL-guest.png
# It runs on the machine the games are on through tools/instance.sh (GAME_HOST).
# shellcheck disable=SC2029,SC2329 # expanded here on purpose; the helpers are for the pair scripts
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
host_name=${PAIR_HOST:-a} guest_name=${PAIR_GUEST:-c}
inst() { tools/instance.sh "$@"; }
remote() { ssh "${GAME_HOST:-anya}" "$@"; }

case ${1:-} in
--list)
  for f in tools/pairs/*.sh; do
    printf '%-24s %s\n' "$(basename "$f" .sh)" "$(sed -n '1s/^# //p' "$f")"
  done
  exit 0
  ;;
--stop)
  inst stop "$host_name" || true
  inst stop "$guest_name" || true
  exit 0
  ;;
"" | -*)
  sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
  exit 2
  ;;
esac

# Never beside the player's game (tools/instance.sh refuses too).
if inst who 2>/dev/null | grep -q "the player's"; then
  echo "pair: the player's game runs on ${GAME_HOST:-anya}: nothing done" >&2
  exit 1
fi

dir_of() { inst dir "$1"; }
log_of() { # the game's newest tandem log
  remote "ls -t '$(dir_of "$1")'/red4ext/logs/tandem-*.log 2>/dev/null | head -1"
}

# ---- both games up, this build, in one session ----
stamp=$( (
  sha256sum build/windows/plugin/tandem.dll
  find scripts/tandem tweaks/tandem -type f -print0 | sort -z | xargs -0 cat
) | sha256sum | cut -c1-16)
for name in "$host_name" "$guest_name"; do
  d=$(dir_of "$name")
  running=$(inst status 2>/dev/null | grep -E "^ +$name: " || true)
  if [[ $running == *running* ]] &&
    [ "$(remote "cat '$d/red4ext/plugins/tandem/.pair-started' 2>/dev/null" || true)" = "$stamp" ]; then
    continue
  fi
  inst stop "$name" >/dev/null 2>&1 || true
  if [ -n "${TANDEM_PREBUILT:-}" ]; then
    GAME_DIR=$d task deploy:files >/dev/null
  else
    GAME_DIR=$d task deploy >/dev/null
  fi
  if [ "$name" = "$host_name" ]; then
    inst role "$name" host >/dev/null
  else
    inst role "$name" join 127.0.0.1 >/dev/null
  fi
  inst start "$name" >/dev/null
  inst load "$name" >/dev/null
  remote "echo '$stamp' > '$d/red4ext/plugins/tandem/.pair-started'"
done
guest_log=$(log_of "$guest_name")
for _ in $(seq 1 60); do
  remote "grep -q 'in the session as player' '$guest_log'" && break
  sleep 2
done
remote "grep -q 'in the session as player' '$guest_log'" || {
  echo "pair: FAIL (the guest never joined the host's session)"
  exit 1
}

# ---- the scripts ----
status=0
for script in "$@"; do
  file=tools/pairs/$script.sh
  [ -f "$file" ] || {
    echo "pair $script: FAIL (no $file)"
    status=1
    continue
  }
  for name in "$host_name" "$guest_name"; do
    TANDEM_INSTANCE=$name tools/game.sh test-reset >/dev/null 2>&1 || true
  done
  evidence=.cache/pairs/$script-$(date +%Y%m%d-%H%M%S)
  mkdir -p "$evidence"
  declare -A log mark
  log[h]=$(log_of "$host_name") log[g]=$(log_of "$guest_name")
  mark[h]=$(remote "wc -l < '${log[h]}'") mark[g]=$(remote "wc -l < '${log[g]}'")
  why=''
  name_of() { [ "$1" = h ] && echo "$host_name" || echo "$guest_name"; }
  since() { remote "tail -n +$((${mark[$1]} + 1)) '${log[$1]}'"; }
  on() {
    local who=$1
    shift
    inst cmd "$(name_of "$who")" "$@" >/dev/null
  }
  fail() { # in the script's subshell: its last stderr line is the why
    echo "$*" >&2
    return 1
  }
  expect() {
    local who=$1 regex=$2 wait=${3:-20} i
    for ((i = 0; i < wait; i += 2)); do
      since "$who" | grep -qE -- "$regex" && return 0
      sleep 2
    done
    fail "no '$regex' on the $([ "$who" = h ] && echo host || echo guest) within $wait s"
  }
  reject() { echo "$1:$2" >>"$evidence/rejects"; }
  lines() { since "$1" | grep -E -- "$2" || true; }
  measure() { echo "$*" >>"$evidence/measures.txt"; }
  shot() {
    local label=$1 who name
    for who in h g; do
      name=$(name_of "$who")
      inst shot "$name" "pair-$label.png" >/dev/null 2>&1 &&
        scp -q "${GAME_HOST:-anya}:/data/Steam/tandem-instances/$name/run/pair-$label.png" \
          "$evidence/$label-$([ "$who" = h ] && echo host || echo guest).png" || true
    done
  }
  ok=1
  # Not in an && or || list: errexit would be off inside the subshell, and
  # a failed check would go on (a 0.46 m body passed so, 2026-11-28).
  set +e
  (
    set -e
    # shellcheck disable=SC1090 # the pair script, by name
    source "$file"
  ) 2>"$evidence/stderr.txt"
  rc=$?
  set -e
  [ "$rc" = 0 ] || ok=0
  [ $ok = 1 ] || why=$(tail -1 "$evidence/stderr.txt" 2>/dev/null || true)
  while IFS= read -r r; do
    if since "${r%%:*}" | grep -qE -- "${r#*:}"; then
      ok=0
      why="'${r#*:}' seen"
    fi
  done < <(cat "$evidence/rejects" 2>/dev/null || true)
  since h >"$evidence/host.log"
  since g >"$evidence/guest.log"
  grep -h 'referee:\|watchdog: game thread silent' "$evidence/host.log" "$evidence/guest.log" >/dev/null &&
    ok=0 why=${why:-a referee or watchdog line}
  if [ $ok = 1 ]; then
    echo "pair $script: pass ($evidence)"
  else
    echo "pair $script: FAIL (${why:-see $evidence}) ($evidence)"
    status=1
  fi
done
exit $status
