# As host: a guest's V standing on foot in a lane of our traffic: our cars stop for it as they do for our own V, and drive on once it is clear (a guest's car ran a host's V over and carried it 750 m, 2026-11-22)
# shellcheck shell=bash disable=SC2317,SC2154
# Needs a street with traffic and the mock in a lane of it: our V at
# YIELD_AT (a fixture or "X Y Z [YAW]", default drake-ave), the mock
# YIELD_OFFSET metres from it (world X,Y,Z; default 6,0,0). Not yet run: the
# default may put the mock on a pavement. `cmd street lanes` lists our driven
# cars (place, heading, distance): pick a spot in front of one.
ensure host
at "${YIELD_AT:-drake-ave}"
pause 5
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset "${YIELD_OFFSET:-6,0,0}" --body female
expect 'joined \(player [0-9]+' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
pause 3
cmd street count
expect 'street count: [0-9]+ \([1-9][0-9]* run by us' 10
cmd street lanes
expect 'street lanes: [0-9]+ driven car\(s\) of ours listed' 5
# A car of ours comes up to the puppet and stops (the crowd's own stop
# pattern, its horn), then drives on when the lane is clear or goes round.
expect 'street: vehicle [0-9]+ stops for a player on foot in its lane' 120
shot traffic-yield
cmd street lanes
expect 'street lanes: [0-9]+ driven car\(s\) of ours listed, [1-9][0-9]* stop\(s\) for a player on foot' 5
reject 'watchdog: game thread silent'
# By hand: the car stops short of the puppet and honks; the puppet is never
# pushed; the cars behind queue up as behind our own V.
