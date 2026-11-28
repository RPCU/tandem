# Two games: both Vs in one elevator, our V sends it to another floor and the partner's own cabin carries the partner there; the partner sends it back
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a lift within 200 m; default
# h11-basement, its lift 64 m off, testing.md "Fixtures").
# To look at: on each screen the other V rides in the cabin (no sliding, no
# falling through), both cabins leave together, the doors open for both.
ensure host
at "${DEVICES_AT:-h11-basement}"
partner_join
cmd device goto lift
expect 'devices: at lift [0-9a-f]+ LiftControllerPS: ' 10
read -r lift x y z < <(since | sed -nE 's/.*devices: at lift ([0-9a-f]+) LiftControllerPS: \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3 \4/p' | tail -1)
note "the lift: $lift"
pat "$(awk -v x="$x" 'BEGIN { print x + 0.8 }') $y $z"
pnear lift "$lift"
pquery before position
shot pair-lift-aboard
mark
pmark
# Our V picks another floor: every game's cabin leaves together.
cmd device floor other
expect "devices: $lift LiftControllerPS is now on, [a-z+]+, value [0-9]+" 15
expect_partner "devices: $lift LiftControllerPS by player 1: on, [a-z+]+, value [0-9]+, played" 15
pause 15
pquery after position
# The partner's V went with its own cabin (more than 2 m up or down).
ups=$(awk -v a="$before" -v b="$after" 'BEGIN { split(a, p, " "); split(b, q, " "); d = q[3] - p[3]; print (d < 0 ? -d : d) }')
expect_value "$ups" '>' 2
shot pair-lift-arrived
compare devices "$lift"
# The partner sends it back to the ground floor: asked of us, ours follows.
mark
pmark
pcmd device floor 0
expect_partner "devices: $lift LiftControllerPS changed here \\(on, [a-z+]+, value 0\\): asked of the host" 10
expect "devices: player [0-9]+ operated $lift" 10
expect "devices: $lift LiftControllerPS after player [0-9]+'s request: on, [a-z+]+, value 0" 15
pause 15
compare devices "$lift"
reject "devices: $lift LiftControllerPS will not become"
reject_partner "devices: $lift LiftControllerPS will not become"
reject 'watchdog: game thread silent'
