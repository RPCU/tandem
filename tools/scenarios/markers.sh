# A player joins: their marker (world, minimap, map) comes and goes with them
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'marker: player [0-9]+ \(.*\) on the puppet, slot .*, registered true' 30
pause 3
cmd markers
expect 'markers: definition .*: in world true, minimap true, map true' 10
expect 'markers: player [0-9]+ \(.*\) on the puppet at ' 10
shot markers
# The mock player goes silent: the host times it out (up to ~30 s).
pkill -x tandem-peer || true
expect 'marker: player [0-9]+ removed' 45
cmd markers
expect 'markers: none' 10
reject 'watchdog: game thread silent'
