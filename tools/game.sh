#!/usr/bin/env bash
# Drive the game on the test machine for in-game tests, from this machine.
#
#   tools/game.sh start        launch through the running Steam, pass the title
#                              screen, Continue (latest save), confirm the mod
#                              warning, wait until tandem reports a loaded game
#   tools/game.sh stop         kill the game
#   tools/game.sh shot NAME    screenshot to .cache/shots/NAME.png
#   tools/game.sh key CODE...  press keys (Linux input codes: 57 Space, 28 Enter, 1 Esc)
#   tools/game.sh hold SECONDS CODE...  hold keys down together, then release
#                              (physical key codes; anya is AZERTY: 31 walked back,
#                              46 crouched, 17 opened the radio menu)
#   tools/game.sh log [N]      the last N lines of tandem's log (default 40)
#   tools/game.sh role [host | join [ADDRESS] | off | player]
#                              which tandem.ini the game reads (the player's
#                              own is kept as tandem.ini.player; player puts
#                              it back)
#   tools/game.sh ensure host|join [KEY=VALUE...]  this build running in this
#                              role (with these extra tandem.ini keys): deploys,
#                              hot-reloads scripts-only changes, restarts only
#                              when the DLL, tweaks or role changed
#   tools/game.sh healthy      the game plays and its game thread ticks
#   tools/game.sh test-reset   one known state (cmd test reset: on foot, up, healed,
#                              at the test field, tests' leftovers gone); prints
#                              its result, fails unless "ready"
#   tools/game.sh bt           every game thread's backtrace (winedbg inside the
#                              game's container) to .cache/re/bt.txt, tandem's
#                              frames named from its .pdb. A game that "freezes"
#                              has often crashed: Proton's crash handler waits.
#   tools/game.sh peer [ARGS]  (re)start the mock player here, in the background
#                              (default: join GAME_HOST, front mode, male body;
#                              log in .cache/peer.log)
#   tools/game.sh reload       hot-reload scripts and tweaks (RedHotTools)
#   tools/game.sh fetch-play NAME  a gameplay recording made in game (cmd record
#                              start NAME ... record stop) to recordings/play/NAME.tplay
#   tools/game.sh fetch-recordings [--since YYYYMMDD]  the recorder's segments
#                              (recorder = on) and their index to recordings/auto/
#   tools/game.sh pilot ...    the test pilot plays our V without a human
#                              (tools/pilot, docs/architecture/autopilot.md):
#                              run ROUTINE [FLAGS] | record NAME ROUTINE |
#                              session NAME ROUTINE | dry ROUTINE | check |
#                              start [FLAGS] (hold the pad between routines,
#                              from before the game starts) | stop | status |
#                              pad [SECONDS] | build
#                              (ROUTINE: a name in tools/pilot/routines or a
#                              file; PILOT_FLAGS). Only on a game this script
#                              started, or with PILOT_INSTANCE=NAME on
#                              instance NAME of tools/instance.sh (its game
#                              folder, keys on its own gamescope display;
#                              cmd, log and fetch-play go to it too)
#   tools/game.sh cmd TEXT     a command for tandem in the running game:
#                              respawn | inspect | bodies | probe | probe stop | despawn |
#                              pose stream | pose loop [off] | vehicle probe [stop] |
#                              vehicle ride | vehicle unride | vehicle clear | where | npcs |
#                              npc count | npc anims | npc seek [range] | npc scale |
#                              markers |
#                              region (apart or grouped, leader, area owner) |
#                              region hold on | region hold off (a gig of ours here) |
#                              ride (our ride, the vehicle's seats, the others' rides) |
#                              ride hide | ride show (every puppet out of sight and back) |
#                              ride npc [RECORD] (an NPC into a free seat of our vehicle) |
#                              teleport home | teleport X Y Z [YAW] | test reset |
#                              record start NAME | record stop (our V's play, for
#                              tandem-peer --play) |
#                              proxy test [record] | proxy clear |
#                              pose hook | pose report | pose watch [3] | pose copy [off] |
#                              device (counts) | device near [KIND] | device goto [KIND] |
#                              device tell [KIND] | device open|close|lock|unlock|on|off|
#                              explode|distract|friendly|hostile [KIND] |
#                              device floor N|other | device station N
#                              (the nearest device of a KIND: door lift camera turret
#                              barrel ap radio any; near prints its id for
#                              tandem-peer --device, goto puts V beside it) |
#                              coop status | coop host [PORT] | coop join ADDRESS[:PORT] |
#                              coop leave | coop menu PAGE | coop pick TEXT
#                              (the co-op menu: in game any time; menu and pick drive
#                              an open main or pause menu, pages root coop host join save) |
#                              coop name [reset] (the name; reset: the menu asks again) |
#                              coop name answer TEXT | coop name cancel (the open name popup)
#
# The game machine runs Steam in a Sway session (headless output for Sunshine)
# and ydotoold for input; GAME_HOST and GAME_DIR override the defaults.
# Commands in single quotes run, and expand, on the game machine.
# shellcheck disable=SC2016,SC2029
set -euo pipefail

host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
# PILOT_INSTANCE=NAME: an instance of tools/instance.sh instead of the Steam
# game (docs/architecture/autopilot.md, "Two real games").
instance=${PILOT_INSTANCE:-main}
if [ "$instance" != main ] && [ -z "${GAME_DIR:-}" ]; then
  dir=$(tools/instance.sh dir "$instance") || exit 1
fi
shots=.cache/shots

remote() { ssh "$host" "$@"; }

running() { remote 'pgrep -f "[C]yberpunk2077.exe" >/dev/null'; }

keys() {
  local args=()
  for code in "$@"; do args+=("$code:1" "$code:0"); done
  remote "YDOTOOL_SOCKET=/run/ydotoold/socket ydotool key ${args[*]}"
}

# The newest tandem log, then wait until it shows a line matching $1 (ERE),
# for at most $2 seconds. Prints the matching line.
# wait_log PATTERN SECONDS OLD: PATTERN in the newest tandem log that is not
# OLD (the log of the game before this launch: a killed game's log can be
# written in the very second of the launch, so times cannot tell them apart).
wait_log() {
  local pattern=$1 timeout=$2
  remote "cd '$dir'; for i in \$(seq 1 $timeout); do f=\$(ls -t red4ext/logs/tandem-*.log 2>/dev/null | head -1); \
    if [ -n \"\$f\" ] && [ \"\$f\" != '$3' ] && grep -m1 -E '$pattern' \"\$f\"; then exit 0; fi; sleep 1; done; exit 1"
}

# ---- whose game it is ----
# The test pilot (below) only ever drives a game this script started: start
# writes that game's pid and start time to the pilot's cache folder on the
# game machine (never in the game's folder), stop removes it, and the pilot
# refuses a running game that is not the one written there (the player's).
pilot_dir=.cache/tandem/pilot/$instance # on the game machine, under $HOME
pilot_bin=build/linux/pilot/tandem-pilot

own() {
  remote "mkdir -p $pilot_dir; p=\$(pgrep -f '^[A-Z]:.*Cyberpunk2077[.]exe' | head -1); [ -n \"\$p\" ] || exit 1; \
    echo \"\$p \$(sed 's/.*) //' /proc/\$p/stat | awk '{ print \$20 }')\" > $pilot_dir/game.owner"
}
unown() { remote "rm -f $pilot_dir/game.owner" || true; }

# (keep_pilot: start's own stop leaves a serving pilot up, with its pad:
# the game about to start must see it.)
stop() {
  [ -n "${keep_pilot:-}" ] || pilot_stop >/dev/null 2>&1 || true
  unown
  if running; then
    remote 'pkill -KILL -f "[C]yberpunk2077.exe"; for i in $(seq 1 15); do pgrep -f "[C]yberpunk2077.exe" >/dev/null || exit 0; sleep 1; done; exit 1'
    echo "game stopped"
  fi
}

start() {
  keep_pilot=1 stop
  local since
  since=$(remote "cd '$dir'; ls -t red4ext/logs/tandem-*.log 2>/dev/null | head -1")
  remote 'pid=$(pgrep -f "steam.sh" | head -1)
    eval "$(tr "\0" "\n" < /proc/$pid/environ | grep -E "^(DISPLAY|WAYLAND_DISPLAY|XDG_RUNTIME_DIR|DBUS_SESSION_BUS_ADDRESS|SWAYSOCK)=" | sed "s/^/export /")"
    nohup steam steam://rungameid/1091500 >/dev/null 2>&1 &'
  echo "launched, loading the latest save"
  # Scripts compile within a minute of launch; a compile error disables
  # every script, so stop at once instead of waiting for lines that will
  # never come.
  local launched
  launched=$(remote 'date +%s')
  if remote "cd '$dir/r6/logs'; for i in \$(seq 1 90); do \
      if [ -f redscript_rCURRENT.log ] && [ \$(stat -c %Y redscript_rCURRENT.log) -ge $launched ]; then \
        sleep 2; grep -q '\[ERROR' redscript_rCURRENT.log && exit 0; exit 1; fi; sleep 1; done; exit 1"; then
    echo "redscript compile errors (every script is off):" >&2
    remote "grep -A4 '\[ERROR' '$dir/r6/logs/redscript_rCURRENT.log' | cut -c1-200 | head -30" >&2 || true
    exit 1
  fi

  # Load Begone (Nexus 8144; Mod Settings' user.ini, section
  # [LoadBegone.LoadBegoneConfig]: enabled, skipRecapVideos) skips the intro,
  # the title screen, the mod warning and the load screen's prompt. When it
  # does not load the save within 25 s of the menu, Continue by keys.
  if ! wait_log 'main menu world|in game' 240 "$since" >/dev/null; then
    echo "the game did not start (redscript error?):" >&2
    remote "grep -A4 '\\[ERROR' '$dir/r6/logs/redscript_rCURRENT.log' | head -20" >&2 || true
    exit 1
  fi
  # The game process exists now: it is ours, for the pilot.
  own || echo "the game's process was not found: the pilot will refuse this game" >&2
  if ! wait_log 'in game' 25 "$since" >/dev/null; then
    start_by_keys
  fi
  # "in game" comes as the save loads; "playing" once the world runs, past
  # the loading screen, whose last prompt Space answers.
  local tries=0
  until wait_log 'script. playing' 8 "$since" >/dev/null; do
    keys 57
    tries=$((tries + 1))
    if [ "$tries" -gt 30 ]; then
      echo "the save did not finish loading in 4 minutes" >&2
      exit 1
    fi
  done
  echo "in game"
}

# Without Load Begone: through the title screen and the menu with keys.
start_by_keys() {
  echo "still on the menu: Continue by keys"
  keys 57 # the title screen's "press Space", if Load Begone did not pass it
  sleep 5
  # (Not Esc for a "changes in patch" popup: on the bare menu it asks to quit.)
  # The menu starts in mouse mode with nothing selected: an arrow key switches
  # it to keyboard selection, and Up from anywhere ends on Continue (the top).
  keys 103 103 103 103
  sleep 1
  keys 28 # Continue (latest save)
  sleep 3
  keys 28 # "this save was made with mods": confirm (nothing when skipped)
}

hold() {
  local seconds=${1:?hold SECONDS CODE...}
  shift
  local down=() up=()
  for code in "$@"; do
    down+=("$code:1")
    up+=("$code:0")
  done
  remote "export YDOTOOL_SOCKET=/run/ydotoold/socket; ydotool key ${down[*]}; sleep $seconds; ydotool key ${up[*]}"
}

shot() {
  local name=${1:?shot NAME}
  mkdir -p "$shots"
  remote 'XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-1 grim -s 0.5 /tmp/tandem-shot.png'
  scp -q "$host:/tmp/tandem-shot.png" "$shots/$name.png"
  echo "$shots/$name.png"
}

# RedHotTools watches these files next to its DLL: touching one recompiles
# the scripts / reloads the tweak records in the running game.
reload() {
  running || {
    echo "the game is not running" >&2
    exit 1
  }
  # RedHotTools logs "Scripts reload completed" when the new scripts run; a
  # compile error leaves the old ones running and shows in redscript's log.
  remote "cd '$dir'; [ -d red4ext/plugins/RedHotTools ] || { echo 'RedHotTools is not installed: no hot reload' >&2; exit 1; }; \
    rht=\$(ls -t red4ext/plugins/RedHotTools/RedHotTools-*.log 2>/dev/null | head -1); [ -n \"\$rht\" ] || exit 1; \
    before=\$(grep -c 'reload completed' \"\$rht\"); \
    touch red4ext/plugins/RedHotTools/.hot-tweaks; sleep 1; touch red4ext/plugins/RedHotTools/.hot-scripts; \
    for i in \$(seq 1 30); do sleep 1; \
      if [ \$(grep -c 'reload completed' \"\$rht\") -gt \$before ]; then echo 'scripts and tweaks reloaded'; exit 0; fi; \
      if [ r6/logs/redscript_rCURRENT.log -nt red4ext/plugins/RedHotTools/.hot-scripts ] && grep -q ERROR r6/logs/redscript_rCURRENT.log; then \
        echo 'compile error, the old scripts still run:' >&2; grep -A6 ERROR r6/logs/redscript_rCURRENT.log | cut -c1-200 >&2; exit 1; fi; \
    done; echo 'no answer from RedHotTools' >&2; exit 1"
}

peer() {
  # The old one must be gone, and its port free, before the new one binds.
  pkill -x tandem-peer || true
  for _ in $(seq 1 30); do
    pgrep -x tandem-peer >/dev/null || break
    sleep 0.1
  done
  local args=("$@")
  if [ ${#args[@]} -eq 0 ]; then
    # ENet speaks IPv4; the name does not always resolve to it here: ask the
    # game machine for its own address then.
    local address
    address=$(getent ahostsv4 "$host" | awk 'NR == 1 { print $1 }')
    [ -n "$address" ] || address=$(remote "ip -4 -o route get 1.1.1.1" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
    args=(join "$address:27450" --mode front --body female)
  fi
  mkdir -p .cache
  nohup build/linux/peer/tandem-peer "${args[@]}" >.cache/peer.log 2>&1 &
  echo "mock player: ${args[*]} (.cache/peer.log)"
}

# tandem reads (and removes) tandem.cmd twice a second while in a game.
cmd() {
  running || {
    echo "the game is not running" >&2
    exit 1
  }
  # One at a time: wait until tandem took the previous one (and this one;
  # up to 30 s, as a command that starts a loading screen is taken only once
  # the world runs again).
  printf '%s\n' "$*" | remote "f='$dir/red4ext/plugins/tandem/tandem.cmd'; \
    for i in \$(seq 1 20); do [ -e \"\$f\" ] || break; sleep 0.25; done; cat > \"\$f\"; \
    for i in \$(seq 1 120); do [ -e \"\$f\" ] || exit 0; sleep 0.25; done; echo 'not taken (in a game?)' >&2; exit 1"
  echo "sent: $*"
}

# A recording of our V's play (plugin/src/net_thread.cpp writes it next
# to the DLL) to recordings/play/, where scenarios and tandem-peer --play
# find it.
fetch_play() {
  local name=${1:?a recording name}
  mkdir -p recordings/play
  scp -q "$host:$dir/red4ext/plugins/tandem/recordings/$name.tplay" "recordings/play/$name.tplay"
  echo "recordings/play/$name.tplay ($(du -h "recordings/play/$name.tplay" | cut -f1))"
  build/linux/peer/tandem-peer play-info "recordings/play/$name.tplay" 2>/dev/null || true
}

# The recorder's segments (plugin/src/recording.hpp), newer than those we
# have (or than --since YYYYMMDD), and its index, to recordings/auto/.
fetch_recordings() {
  local since=''
  if [ "${1:-}" = --since ]; then since=${2:?--since YYYYMMDD}; fi
  mkdir -p recordings/auto
  local from="$dir/red4ext/plugins/tandem/recordings/auto"
  local have newest=''
  have=$(find recordings/auto -maxdepth 1 \( -name '*.tsession' -o -name '*.tplay' \) -printf '%f\n' | sort | tail -1)
  [ -n "$have" ] && newest=${have%%.*}
  [ -n "$since" ] && newest=$since
  local list
  list=$(remote "cd '$from' 2>/dev/null && ls -1 *.tsession *.tplay 2>/dev/null | sort" || true)
  local copied=0
  while IFS= read -r name; do
    [ -n "$name" ] || continue
    # Names are times: newer than what we have (the newest again: it may
    # have grown since).
    if [ -z "$newest" ] || [[ ${name%%.*} > $newest ]] || [ "${name%%.*}" = "$newest" ]; then
      scp -q "$host:$from/$name" "recordings/auto/$name" && copied=$((copied + 1))
    fi
  done <<<"$list"
  scp -q "$host:$from/index.jsonl" recordings/auto/index.jsonl 2>/dev/null || true
  echo "recordings/auto: $copied segment(s) copied, $(find recordings/auto -maxdepth 1 -name '*.ts*' -o -name '*.tplay' | wc -l) here"
}

# ---- the test pilot ----
# tandem-pilot is built here (a static binary), copied with the routine to
# the pilot's cache folder on the game machine and run there, next to the
# game: its control loop reads the plugin's state file and holds a virtual
# gamepad with no network in between. It exists there only while it runs.

pilot_build() {
  mkdir -p "$(dirname "$pilot_bin")"
  CGO_ENABLED=0 go build -C tools/pilot -trimpath -o "$PWD/$pilot_bin" ./cmd/tandem-pilot
}

# A routine by name (tools/pilot/routines/NAME.pilot) or by file.
pilot_routine() {
  if [ -f "$1" ]; then
    echo "$1"
  elif [ -f "tools/pilot/routines/$1.pilot" ]; then
    echo "tools/pilot/routines/$1.pilot"
  else
    echo "no routine $1 (a file, or a name in tools/pilot/routines)" >&2
    return 1
  fi
}

# The Sway session's environment on the game machine (the pilot asks sway
# which window has the focus), as start takes it for Steam.
session_env='pid=$(pgrep -f "steam.sh" | head -1); eval "$(tr "\0" "\n" < /proc/$pid/environ | grep -E "^(DISPLAY|WAYLAND_DISPLAY|XDG_RUNTIME_DIR|DBUS_SESSION_BUS_ADDRESS|SWAYSOCK)=" | sed "s/^/export /")"'

# pilot_run ROUTINE [FLAGS]: the routine on the game's V; its log on stdout,
# its result as the exit status. -tt: Ctrl-C here, or a lost connection,
# reaches the pilot as a hang-up: it releases the pad and stops.
pilot_run() {
  local routine
  routine=$(pilot_routine "${1:?pilot run ROUTINE [FLAGS]}")
  shift
  pilot_build
  "$pilot_bin" check "$routine" >/dev/null
  running || {
    echo "the game is not running" >&2
    exit 1
  }
  remote "mkdir -p $pilot_dir"
  rsync -t "$pilot_bin" "$routine" "$host:$pilot_dir/"
  ssh -tt "$host" "$session_env; cd $pilot_dir && exec ./tandem-pilot run --instance '$instance' --game-dir '$dir' ${PILOT_FLAGS:-} $* '$(basename "$routine")'" </dev/null | tr -d '\r'
}

# pilot_start [FLAGS]: a pilot that stays up between routines, holding the
# pad (tandem-pilot serve): start it before the game when the game only
# takes a pad it saw at launch. "pilot run" then goes through it, with these
# flags. It ends on "pilot stop", "stop", or 15 minutes without a routine.
pilot_start() {
  pilot_build
  remote "mkdir -p $pilot_dir"
  rsync -t "$pilot_bin" "$host:$pilot_dir/"
  remote "$session_env; cd $pilot_dir && nohup ./tandem-pilot serve --instance '$instance' --game-dir '$dir' ${PILOT_FLAGS:-} $* >serve.log 2>&1 & sleep 3; cat $pilot_dir/serve.log"
}

# The pilot of this instance, if one runs: asked to stop (it releases the
# pad), then killed.
pilot_stop() {
  # An instance's keys: let go of anything a pilot killed outright held.
  if [ "$instance" != main ] && [ -x "$pilot_bin" ]; then
    remote "$session_env; cd $pilot_dir 2>/dev/null && ./tandem-pilot release --instance '$instance' --game-dir '$dir' ${PILOT_FLAGS:-}" || true
  fi
  remote "p=\$(cat $pilot_dir/lock 2>/dev/null); [ -n \"\$p\" ] && [ \"\$(cat /proc/\$p/comm 2>/dev/null)\" = tandem-pilot ] || { echo 'no pilot runs'; exit 0; }; \
    kill \$p; for i in \$(seq 1 30); do [ -e /proc/\$p ] || { echo 'pilot stopped'; exit 0; }; sleep 0.1; done; kill -KILL \$p; echo 'pilot killed'"
}

pilot_status() {
  remote "$session_env; cd $pilot_dir 2>/dev/null || { echo 'no pilot was ever installed here'; exit 0; }; \
    p=\$(cat lock 2>/dev/null); if [ -n \"\$p\" ] && [ \"\$(cat /proc/\$p/comm 2>/dev/null)\" = tandem-pilot ]; then echo \"a pilot runs (pid \$p)\"; else echo 'no pilot runs'; fi; \
    ./tandem-pilot owner --instance '$instance' --game-dir '$dir' 2>&1; ./tandem-pilot state --game-dir '$dir' 2>&1; true"
}

# pilot_record NAME ROUTINE: a gameplay recording with no human (a session
# is needed: role host records alone): recordings/play/NAME.tplay.
pilot_record() {
  local name=${1:?pilot record NAME ROUTINE} status=0
  shift
  cmd "record start $name" >/dev/null
  pilot_run "$@" || status=$?
  cmd "record stop" >/dev/null
  sleep 1
  fetch_play "$name"
  return "$status"
}

# pilot_session NAME ROUTINE: a recorded host session with no human: the
# recording guest joins from here, out of sight, while the pilot plays the
# host's V: recordings/session/NAME.tsession.
pilot_session() {
  local name=${1:?pilot session NAME ROUTINE} status=0 address guest
  shift
  address=$(getent ahostsv4 "$host" | awk 'NR == 1 { print $1 }')
  mkdir -p recordings/session .cache
  build/linux/peer/tandem-peer join "$address:27450" --mode watch --name watcher \
    --record-session "recordings/session/$name.tsession" >.cache/pilot-session.log 2>&1 &
  guest=$!
  for _ in $(seq 1 20); do
    grep -q 'recording the session' .cache/pilot-session.log && break
    sleep 0.5
  done
  if ! grep -q 'recording the session' .cache/pilot-session.log; then
    kill "$guest" 2>/dev/null || true
    echo "the recording guest did not join (.cache/pilot-session.log)" >&2
    exit 1
  fi
  cmd "ride hide" >/dev/null
  pilot_run "$@" || status=$?
  kill -INT "$guest" 2>/dev/null || true
  wait "$guest" 2>/dev/null || true
  cmd "ride show" >/dev/null
  grep 'session saved' .cache/pilot-session.log || echo "no session saved (.cache/pilot-session.log)" >&2
  return "$status"
}

pilot() {
  local what=${1:-}
  [ $# -gt 0 ] && shift
  case $what in
  build) pilot_build && echo "$pilot_bin" ;;
  check)
    pilot_build
    if [ $# -eq 0 ]; then set -- tools/pilot/routines/*.pilot; fi
    "$pilot_bin" check "$@"
    ;;
  dry)
    pilot_build
    "$pilot_bin" dry "$(pilot_routine "${1:?pilot dry ROUTINE}")"
    ;;
  run) pilot_run "$@" ;;
  start) pilot_start "$@" ;;
  record) pilot_record "$@" ;;
  session) pilot_session "$@" ;;
  stop) pilot_stop ;;
  status) pilot_status ;;
  pad)
    pilot_build
    remote "mkdir -p $pilot_dir"
    rsync -t "$pilot_bin" "$host:$pilot_dir/"
    ssh -tt "$host" "cd $pilot_dir && exec ./tandem-pilot pad --instance '$instance' ${PILOT_FLAGS:-} ${1:-10}" </dev/null | tr -d '\r'
    ;;
  *)
    echo "pilot: run ROUTINE [FLAGS] | record NAME ROUTINE | session NAME ROUTINE | dry ROUTINE | check [ROUTINE...] | start [FLAGS] | stop | status | pad [SECONDS] | build" >&2
    exit 2
    ;;
  esac
}

log() {
  remote "cd '$dir'; tail -n ${1:-40} \"\$(ls -t red4ext/logs/tandem-*.log | head -1)\""
}

# winedbg from the game's own Proton, run in the game's pressure-vessel
# namespaces (its wineserver and libraries); attaches, dumps, detaches.
bt() {
  mkdir -p .cache/re
  remote 'bash -s' >.cache/re/bt.txt <<'REMOTE'
p=$(pgrep -f "^[A-Z]:.*Cyberpunk2077[.]exe" | head -1)
[ -n "$p" ] || { echo "no game" >&2; exit 1; }
w=$(dirname "$(readlink -f "/proc/$(pgrep -x wineserver | head -1)/exe")")
# The game's whole environment (its prefix, libraries, sync mode), in its
# namespaces and root: the same wineserver as the game.
env=()
while IFS= read -r -d "" kv; do
  case $kv in WINESERVERSOCKET=* | LD_PRELOAD=* | WINELOADERNOEXEC=*) ;; *) env+=("$kv") ;; esac
done <"/proc/$p/environ"
run() { timeout 120 nsenter --preserve-credentials -U -m -r -t "$p" /usr/bin/env -i "${env[@]}" WINEDEBUG=-all "$w/wine" winedbg "$@"; }
pid=$(run --command "info process" 2>/dev/null | awk '/Cyberpunk2077.exe/ {print $1}')
[ -n "$pid" ] || { echo "winedbg does not see the game" >&2; exit 1; }
printf 'attach 0x%s\nbt all\ndetach\nquit\n' "$pid" | run 2>&1 |
  grep -v "could not attach\|can.t debug several\|Either .detach"
REMOTE
  echo ".cache/re/bt.txt: $(grep -c '^Backtracing' .cache/re/bt.txt) threads"
  grep -n ' in tandem ' .cache/re/bt.txt | grep -v 'enet_\|Session::Poll\|Endpoint::Update\|NetThread\|stop_token\|_Invoke\|thread.cpp\|vector:' | head -20 || true
}

# This machine's address on the route to the game machine (for role join).
here() {
  local to
  to=$(getent ahostsv4 "$host" | awk 'NR == 1 { print $1 }')
  ip -4 -o route get "$to" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }'
}

# Which tandem.ini the game reads: host | join [ADDRESS] | player (the
# player's own, kept aside as tandem.ini.player the first time we switch).
role() {
  local ini="$dir/red4ext/plugins/tandem/tandem.ini" text
  case ${1:-} in
  # log = debug: the scenarios read per-puppet, per-hit lines (log.hpp).
  host) text=$'role = host\nport = 27450\nname = V\nlog = debug\n' ;;
  off) text=$'role = off\nport = 27450\nname = V\nlog = debug\n' ;;
  join)
    local address=${2:-}
    [ -n "$address" ] || address=$(here)
    text="role = join"$'\n'"address = $address"$'\nport = 27450\nname = V\nlog = debug\n'
    ;;
  player)
    # Testing is over: no pilot, no pad left behind.
    pilot_stop >/dev/null 2>&1 || true
    remote "[ -e '$ini.player' ] && mv '$ini.player' '$ini'; head -1 '$ini'"
    return
    ;;
  "")
    remote "head -2 '$ini'"
    return
    ;;
  *)
    echo "role: host | join [ADDRESS] | off | player" >&2
    exit 1
    ;;
  esac
  # Extra keys for a test (ensure ROLE KEY=VALUE...): one per line.
  local extra
  for extra in ${ini_extra:-}; do text+="${extra%%=*} = ${extra#*=}"$'\n'; done
  printf '%s' "$text" | remote "[ -e '$ini.player' ] || cp '$ini' '$ini.player'; cat > '$ini'"
  echo "role: ${text%%$'\n'*}"
}

# What is deployed: a hash of the DLL, scripts and tweaks (local side).
stamp() { cat build/windows/plugin/tandem.dll scripts/tandem/*.reds tweaks/tandem/*.tweak | sha256sum | cut -c1-16; }

# A running game with this build and this role, restarting only when needed:
#   ensure host|join  deploy if the build changed; reload scripts when only
#                     scripts did (RedHotTools); restart when the DLL, the
#                     tweaks or the role changed, or the game is not playing.
#   ensure ROLE KEY=VALUE...  the same with these keys in tandem.ini (the
#                     plugin reads it at start: other keys mean a restart).
ensure() {
  local role_name=$1 want started now
  shift
  ini_extra="$*"
  # The role and its extra keys, as one word.
  want="$role_name${ini_extra:+,${ini_extra// /,}}"
  now=$(stamp)
  started=$(remote "cat '$dir/red4ext/plugins/tandem/.started' 2>/dev/null" || true)
  local current_role
  current_role=$(remote "sed -n 's/^role *= *//p' '$dir/red4ext/plugins/tandem/tandem.ini'")
  if [ "$current_role" != "$role_name" ] || [ "${started#* }" != "$want" ]; then
    role "$role_name" >/dev/null
  fi
  if [ "$started" = "$now $want" ] && running && healthy; then
    echo "game: running this build as $want"
    return
  fi
  # Only scripts changed, same DLL, tweaks and role: hot reload.
  local dll
  dll=$(sha256sum build/windows/plugin/tandem.dll | cut -c1-16)
  if running && healthy && [ "${started#* }" = "$want" ] &&
    [ "$(remote "cat '$dir/red4ext/plugins/tandem/.started-dll' 2>/dev/null" || true)" = "$dll" ] &&
    [ "$(remote "cat '$dir/red4ext/plugins/tandem/.started-tweaks' 2>/dev/null" || true)" = "$(cat tweaks/tandem/*.tweak | sha256sum | cut -c1-16)" ]; then
    deploy
    if reload 2>/dev/null; then
      remote "echo '$now $want' > '$dir/red4ext/plugins/tandem/.started'"
      return
    fi
  fi
  deploy
  start
  remote "cd '$dir/red4ext/plugins/tandem'; echo '$now $want' > .started; echo '$dll' > .started-dll; \
    echo '$(cat tweaks/tandem/*.tweak | sha256sum | cut -c1-16)' > .started-tweaks"
  # The save loads as the player left it (in a car, mid-mission, wanted...).
  test_reset || echo "the game is not in its test state" >&2
}

# One known state for a test (scripts/tandem/TestReset.reds): V on foot,
# up, healed, at the test field, the tests' leftovers gone. Prints the
# game's answer; fails unless it is "ready".
test_reset() {
  local before line
  before=$(remote "cd '$dir'; grep -cE 'test reset: (ready|failed)' \"\$(ls -t red4ext/logs/tandem-*.log | head -1)\"" || true)
  cmd test reset >/dev/null
  line=$(remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); for i in \$(seq 1 45); do \
    if [ \"\$(grep -cE 'test reset: (ready|failed)' \"\$f\")\" -gt ${before:-0} ]; then \
    grep -E 'test reset: (ready|failed)' \"\$f\" | tail -1; exit 0; fi; sleep 1; done; exit 1") || {
    echo "test reset: no answer within 45 s" >&2
    return 1
  }
  echo "${line#*] }"
  case $line in *'test reset: ready'*) return 0 ;; esac
  return 1
}

# TANDEM_PREBUILT=1: install the DLL already in build/ (tools/remote.sh
# built it on the game machine) instead of building it here.
deploy() {
  if [ -n "${TANDEM_PREBUILT:-}" ]; then task deploy:files >/dev/null; else task deploy >/dev/null; fi
}

# The game runs and its game thread ticks (no watchdog line since the last
# "back", see plugin/src/runtime.hpp).
healthy() {
  remote "cd '$dir'; f=\$(ls -t red4ext/logs/tandem-*.log | head -1); \
    last=\$(grep -n 'watchdog:' \"\$f\" | tail -1); \
    case \"\$last\" in *silent*) exit 1 ;; esac; grep -q 'script. playing' \"\$f\""
}

case ${1:-} in
start) start ;;
stop) stop ;;
shot) shot "${2:-}" ;;
key)
  shift
  keys "$@"
  ;;
hold)
  shift
  hold "$@"
  ;;
log) log "${2:-}" ;;
bt) bt ;;
role)
  shift
  role "$@"
  ;;
ensure)
  shift
  ensure "${@:?host or join}"
  ;;
healthy) healthy && echo healthy ;;
test-reset) test_reset ;;
reload) reload ;;
fetch-play) fetch_play "${2:-}" ;;
fetch-recordings)
  shift
  fetch_recordings "$@"
  ;;
peer)
  shift
  peer "$@"
  ;;
pilot)
  shift
  pilot "$@"
  ;;
cmd)
  shift
  cmd "$@"
  ;;
*)
  sed -n '2,21p' "$0" >&2
  exit 2
  ;;
esac
