#!/usr/bin/env bash
# The player's own sessions on the Steam game, recorded for debugging: the
# game hosts (debug log, recorder, observer), a simulated companion joins
# from this machine, and everything is fetched afterwards.
#
#   tools/play-session.sh start    the companion bot (it joins whenever the
#                                  game is in a session, again after a leave)
#   tools/play-session.sh stop     the bot stopped, the session's logs fetched
#   tools/play-session.sh status   the game's settings, the bot, the logs
#   tools/play-session.sh restore  the player's own tandem.ini back (no host)
#
# Never starts, stops or saves the game: the player launches it as usual.
# shellcheck disable=SC2029 # the remote paths expand here on purpose
set -euo pipefail
cd "$(dirname "$0")/.."
host=${GAME_HOST:-anya}
dir=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
plugin="$dir/red4ext/plugins/tandem"
state=.cache/play-sim
mkdir -p "$state"

runner() { pgrep -f "^bash $state/run[.]sh" || true; }

write_runner() {
  cat >"$state/run.sh" <<'E'
#!/usr/bin/env bash
cd "$(dirname "$0")/../.."
n=0
while [ ! -f .cache/play-sim/stop ]; do
  addr=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
  [ -n "$addr" ] || addr=$(getent ahostsv4 anya.local | awk 'NR == 1 { print $1 }')
  [ -n "$addr" ] || { sleep 5; continue; }
  n=$((n + 1))
  build/linux/peer/tandem-peer sim --role join "$addr:27450" --persona companion --seed "$n" --minutes 240 >>".cache/play-sim/sim-$n.log" 2>&1
  sleep 15
done
E
  chmod +x "$state/run.sh"
}

case ${1:-status} in
start)
  [ -x build/linux/peer/tandem-peer ] || {
    echo "no build/linux/peer/tandem-peer: tools/build.sh build:linux" >&2
    exit 1
  }
  if [ -n "$(runner)" ]; then
    echo "the companion is already running"
    exit 0
  fi
  write_runner
  rm -f "$state/stop"
  setsid "$state/run.sh" >/dev/null 2>&1 &
  echo "companion started: it joins as soon as your game is in a session (logs: $state/sim-*.log)"
  ;;
stop)
  touch "$state/stop"
  for p in $(runner); do kill "$p" 2>/dev/null || true; done
  for p in $(pgrep -f '^build/linux/peer/tandem-peer sim --role join' || true); do kill "$p" 2>/dev/null || true; done
  out=.cache/play-sessions/$(date +%Y%m%d-%H%M%S)
  mkdir -p "$out"
  ssh "$host" "cd '$plugin/../../logs' && ls -t tandem-*.log redgraph-*.log 2>/dev/null | head -6" | while read -r f; do
    scp -q "$host:$plugin/../../logs/$f" "$out/" || true
  done
  scp -qr "$host:$plugin/observer" "$out/" 2>/dev/null || true
  cp "$state"/sim-*.log "$out/" 2>/dev/null || true
  echo "companion stopped; logs in $out"
  ;;
status)
  ssh "$host" "grep -v '^;' '$plugin/tandem.ini' | grep -E '^(role|log|recorder|observer|vehicle_move) '" || true
  if [ -n "$(runner)" ]; then echo "companion: running"; else echo "companion: stopped"; fi
  ssh "$host" "pgrep -af Cyberpunk2077.exe | grep -v tandem-instances | grep -q . && echo 'game: running' || echo 'game: not running'"
  ;;
restore)
  ssh "$host" "cd '$plugin' && [ -f tandem.ini.player ] && cp tandem.ini.player tandem.ini && echo 'your own tandem.ini is back'"
  ;;
*)
  sed -n '2,13p' "$0"
  exit 2
  ;;
esac
