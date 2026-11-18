#!/usr/bin/env bash
# Every option tandem-peer's usage lists must parse (a listed but unparsed
# --record cost an in-game test run). Usage: peer_flags.sh PATH/tandem-peer
set -euo pipefail
peer=$1
declare -A value=(
  ["--port"]=27451 ["--look"]=random ["--seed"]=7 ["--name"]=test ["--body"]=male ["--mode"]=idle ["--offset"]="1,2,3"
  ["--delay"]=100 ["--rate"]=30 ["--pose"]=x.tpose ["--vehicle"]=Vehicle.x
  ["--drive-seconds"]=5 ["--radio"]=radio_station_09_downtempo ["--time"]=06:30 ["--weather"]=24h_weather_rain
  ["--record"]=x.rec ["--replay"]=x.rec ["--hit-npcs"]=2 ["--hit-you"]=2 ["--pause-at"]=10 ["--pause-for"]=3
  ["--drive-seconds"]=5 ["--time"]=06:30 ["--weather"]=24h_weather_rain
  ["--record"]=x.rec ["--replay"]=x.rec ["--hit-npcs"]=2 ["--pause-at"]=10 ["--pause-for"]=3
  ["--relay"]=relay.example.org:27460 ["--code"]=k7q-2m9
  ["--relay-token"]=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
  ["--hack-npcs"]=4 ["--hack-action"]=QuickHack.BlindHack ["--hack-effect"]=BaseStatusEffect.Blind
  ["--hack-player"]=6 ["--npc-hack"]=AIQuickHack.HackRevealPosition ["--hack-car"]=5
  ["--npc"]=Character.x ["--detect-at"]=15 ["--detect-for"]=10 ["--camo"]=10 ["--shoot"]=3
  ["--story"]=x.story ["--answer"]=1 ["--reply"]=12345 ["--suggest"]=0
  ["--sandevistan-at"]=10 ["--sandevistan-for"]=5 ["--sandevistan-scale"]=0.25
  ["--go-down"]=10 ["--go-out"]=10 ["--revive"]=3
)
flags=$("$peer" --help 2>&1 | grep -oE '^  --[a-z-]+' | tr -d ' ' | sort -u) || true
[ -n "$flags" ] || {
  echo "no options in the usage text" >&2
  exit 1
}
status=0
for flag in $flags; do
  if [ -z "${value[$flag]:-}" ]; then
    echo "$flag: listed in the usage, no test value here (add one)" >&2
    status=1
  elif ! "$peer" host --check "$flag" "${value[$flag]}" >/dev/null 2>&1; then
    echo "$flag: listed in the usage, rejected by the parser" >&2
    status=1
  fi
done
exit $status
