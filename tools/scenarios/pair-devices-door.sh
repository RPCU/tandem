# Two games: a door our V opens opens on the partner's screen; the partner closes it and it closes here; both games show the same door
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a door within 200 m; default
# v-door, V's own door).
# To look at and listen to: on each screen the door opens and closes with
# its own animation and sound, a moment after the other V's doing; neither
# V is pushed or stuck in it.
ensure host
at "${DEVICES_AT:-v-door}"
partner_join
cmd device goto door
expect 'devices: at door [0-9a-f]+ [A-Za-z]+: ' 10
read -r door x y z < <(since | sed -nE 's/.*devices: at door ([0-9a-f]+) [A-Za-z]+: \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3 \4/p' | tail -1)
note "the door: $door"
# The partner beside our V, a metre aside: the same door is its nearest.
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pmark
pcmd device near door
expect_partner "devices: nearest door $door " 10
# A known start: closed on both.
cmd device close door
pause 4
mark
pmark
# Our V opens it: the session's word, played on the partner's copy.
cmd device open door
expect "devices: $door [A-Za-z]+ is now [a-z]+, [a-z+]*open" 10
expect_partner "devices: $door [A-Za-z]+ by player 1: [a-z]+, [a-z+]*open[a-z+]*, value -?[0-9]+, played" 10
pause 2
shot pair-door-open
compare devices "$door"
# The partner closes it: its copy at once, asked of us, ours follows.
pcmd device close door
expect_partner "devices: $door [A-Za-z]+ changed here \\([a-z]+, (plain|[a-z+]*), value -?[0-9]+\\): asked of the host" 10
expect "devices: player [0-9]+ operated $door" 10
expect "devices: $door [A-Za-z]+ after player [0-9]+'s request: [a-z]+, (plain|[a-z+]*), value" 10
reject "devices: $door [A-Za-z]+ after player [0-9]+'s request: [a-z]+, [a-z+]*open"
pause 2
shot pair-door-closed
compare devices "$door"
reject "devices: $door [A-Za-z]+ will not become"
reject_partner "devices: $door [A-Za-z]+ will not become"
reject 'watchdog: game thread silent'
