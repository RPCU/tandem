# A barrel the other player shoots blows up here: the blast, its sound, and it stays gone
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT ("X Y Z": a place with an explosive barrel within 200 m and
# not within 10 m of V; V's flat has none, so name one).
# To look at and listen to: the barrel goes up with its explosion and sound a
# moment after the other player's request, V is hurt only when close, and the
# barrel's wreck is what is left.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --device-answer exploded --device-after 3
expect 'joined \(player [0-9]+' 20
cmd teleport "${DEVICES_AT:-home}"
pause 4
cmd device
expect 'devices: [0-9]+ followed, [1-9][0-9]* in our world' 10
cmd device near barrel
expect 'devices: nearest barrel [0-9a-f]+ [A-Za-z]+ at ' 10
shot devices-barrel-before
mark
# V points it out (the session learns of it), the other player shoots it.
cmd device tell barrel
expect 'devices: told the session of barrel' 5
expect_peer 'asked the host for device [0-9a-f]+ .*exploded.* \(--device-answer\)' 10
expect 'devices: player [0-9]+ operated [0-9a-f]+' 10
# From its puppet: its kills and its crime are the guest's.
expect 'devices: [0-9a-f]+ [A-Za-z]+ set off by player [0-9]+.s puppet' 5
pause 2
shot devices-barrel-blast
# It went up in our game (its state says so, from its own explosion).
expect 'devices: [0-9a-f]+ [A-Za-z]+ (is now|after player [0-9]+.s request:) [a-z]+, [a-z+]*exploded' 10
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): [a-z]+, [a-z+]*exploded[a-z+]*, value -?[0-9]+ by player [0-9]+, played' 10
# And stays gone: asked again, it is still exploded.
cmd device near barrel
shot devices-barrel-after
reject 'watchdog: game thread silent'
