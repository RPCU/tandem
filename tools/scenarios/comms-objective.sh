# Objectives: our tracked quest objective reaches the mock player as our waypoint; its own shows here as "tandem-peer's objective" and goes when it takes it away
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
# The save must track a quest whose objective has a quest mappin, and no
# map pin or map point tracked by hand (that one would be shared instead).
cmd comms objective
expect 'comms: our objective at .*, shared' 5
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --waypoint -1000,1900,36 --waypoint-kind objective --waypoint-clear 30
expect 'joined \(player [0-9]+' 20
# Ours, with nothing tracked by hand: the host's objective, which a guest
# that is not frozen (story sync off, the default) sees nowhere else.
expect_peer "player 1's waypoint: objective at" 20
# Theirs: kind 3, labelled "tandem-peer's objective", no notice.
expect 'comms: waypoint of tandem-peer \(kind 3\) at .*, registered true' 30
# A person checks: the world marker in tandem-peer's colour and its label,
# the minimap, the map and its tooltip ("The objective tandem-peer tracks"),
# and that tracking it on the map gives the GPS route.
shot comms-objective
cmd comms report
expect 'comms: waypoint of tandem-peer \(kind 3\) at \(-1000' 5
reject 'comms: objective of tandem-peer (not shown|hidden)'
expect 'comms: waypoint of tandem-peer gone' 40
cmd comms report
expect 'comms: .* 0 waypoint marker\(s\) here' 5
reject 'watchdog: game thread silent'
