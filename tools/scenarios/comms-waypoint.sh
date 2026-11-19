# Waypoints: the mock player's map pin shows here as "tandem-peer's waypoint" and goes when it takes it away
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --waypoint -1000,1900,36 --waypoint-clear 30
expect 'joined \(player [0-9]+' 20
expect 'comms: waypoint of tandem-peer \(kind 1\) at .*, registered true' 30
# A person checks: the world marker and label, the minimap, the map and its
# tooltip ("tandem-peer's waypoint", "The map pin tandem-peer placed").
shot comms-waypoint
cmd comms report
expect 'comms: .* 1 waypoint marker\(s\) here' 5
expect 'comms: waypoint of tandem-peer gone' 40
cmd comms report
expect 'comms: .* 0 waypoint marker\(s\) here' 5
# Ours goes the other way when a person places a map pin (by hand: the
# mock player logs "player 1's waypoint: kind 1 at ...").
note "by hand: place a map pin; the mock player's log says player 1's waypoint"
reject 'watchdog: game thread silent'
