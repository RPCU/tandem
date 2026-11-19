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
#   tools/game.sh role [host | join [ADDRESS] | player]
#                              which tandem.ini the game reads (the player's
#                              own is kept as tandem.ini.player; player puts
#                              it back)
#   tools/game.sh ensure host|join [KEY=VALUE...]  this build running in this
#                              role (with these extra tandem.ini keys): deploys,
#                              hot-reloads scripts-only changes, restarts only
#                              when the DLL, tweaks or role changed
#   tools/game.sh healthy      the game plays and its game thread ticks
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
#   tools/game.sh cmd TEXT     a command for tandem in the running game:
#                              respawn | inspect | bodies | probe | probe stop | despawn |
#                              pose stream | pose loop [off] | vehicle probe [stop] |
#                              vehicle ride | vehicle unride | vehicle clear | where | npcs |
#                              npc count | npc anims | npc seek [range] | markers |
#                              ride (our ride, the vehicle's seats, the others' rides) |
#                              ride hide | ride show (every puppet out of sight and back) |
#                              ride npc [RECORD] (an NPC into a free seat of our vehicle) |
#                              teleport home | teleport X Y Z [YAW] |
#                              record start NAME | record stop (our V's play, for
#                              tandem-peer --play) |
#                              proxy test [record] | proxy clear |
#                              pose hook | pose report | pose watch [3] | pose copy [off] |
#                              coop status | coop host [PORT] | coop join ADDRESS[:PORT] |
#                              coop leave | coop menu PAGE | coop pick TEXT
#                              (the co-op menu: in game any time; menu and pick drive
#                              an open main or pause menu, pages root coop host join save)
#
# The game machine runs Steam in a Sway session (headless output for Sunshine)
# and ydotoold for input; GAME_HOST and GAME_DIR override the defaults.
# Commands in single quotes run, and expand, on the game machine.
# shellcheck disable=SC2016,SC2029
set -euo pipefail

host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
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

stop() {
  if running; then
    remote 'pkill -KILL -f "[C]yberpunk2077.exe"; for i in $(seq 1 15); do pgrep -f "[C]yberpunk2077.exe" >/dev/null || exit 0; sleep 1; done; exit 1'
    echo "game stopped"
  fi
}

start() {
  stop
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
  host) text=$'role = host\nport = 27450\nname = V\n' ;;
  join)
    local address=${2:-}
    [ -n "$address" ] || address=$(here)
    text="role = join"$'\n'"address = $address"$'\nport = 27450\nname = V\n'
    ;;
  player)
    remote "[ -e '$ini.player' ] && mv '$ini.player' '$ini'; head -1 '$ini'"
    return
    ;;
  "")
    remote "head -2 '$ini'"
    return
    ;;
  *)
    echo "role: host | join [ADDRESS] | player" >&2
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
reload) reload ;;
fetch-play) fetch_play "${2:-}" ;;
peer)
  shift
  peer "$@"
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
