# An elevator ride together: V sends the lift to another floor with the other player aboard; they send it back
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT ("X Y Z", default V's flat: the megabuilding's lifts are 200 m around).
# To look at: the other player stands in the cabin for the whole ride (no
# sliding, no falling through), the doors open for both at the end.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --device-answer floor=0 --device-after 25
expect 'joined \(player [0-9]+' 20
cmd teleport "${DEVICES_AT:-home}"
pause 4
cmd device
expect 'devices: [0-9]+ followed, [1-9][0-9]* in our world' 10
cmd device goto lift
expect 'devices: at lift [0-9a-f]+ LiftControllerPS: ' 10
shot devices-lift-aboard
mark
# V picks another floor: the lift leaves here, and the session says where it goes.
cmd device floor other
expect 'devices: floor [0-9a-f]+ LiftControllerPS at ' 5
expect 'devices: [0-9a-f]+ LiftControllerPS is now on, [a-z+]+, value [0-9]+' 15
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): on, [a-z+]+, value [0-9]+ by player 1, played' 15
pause 8
shot devices-lift-riding
# The other player sends it back 25 s after it left: ours follows.
expect_peer 'asked the host for device [0-9a-f]+ .* value 0 \(--device-answer\)' 30
expect 'devices: player [0-9]+ operated [0-9a-f]+' 10
expect 'devices: [0-9a-f]+ LiftControllerPS is now on, [a-z+]+, value 0' 30
shot devices-lift-back
reject 'devices: [0-9a-f]+ LiftControllerPS will not become'
reject 'watchdog: game thread silent'
