# A camera the other player hacks: turned friendly here too, and switched off for everyone
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT ("X Y Z": a place with a surveillance camera within 200 m;
# V's flat has none in sight, so name one).
# To look at: the camera's cone turns from red to green (friendly), then it
# hangs its head (off); it no longer fills V's detection meter.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --device-answer friendly,hacked --device-after 3
expect 'joined \(player [0-9]+' 20
cmd teleport "${DEVICES_AT:-home}"
pause 4
cmd device
expect 'devices: [0-9]+ followed, [1-9][0-9]* in our world' 10
cmd device near camera
expect 'devices: nearest camera [0-9a-f]+ SurveillanceCameraControllerPS at ' 10
shot devices-camera-before
mark
# V points it out (the session learns of it), the other player hacks it.
cmd device tell camera
expect 'devices: told the session of camera' 5
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): [a-z]+, [a-z+]+, value -?[0-9]+ by player 1, played' 10
expect_peer 'asked the host for device [0-9a-f]+ .*friendly.* \(--device-answer\)' 10
expect 'devices: player [0-9]+ operated [0-9a-f]+' 10
expect "devices: [0-9a-f]+ SurveillanceCameraControllerPS after player [0-9]+'s request: [a-z]+, [a-z+]*hacked\+friendly" 10
shot devices-camera-friendly
# V switches it off: the other player's copy goes off too.
cmd device off camera
expect 'devices: [0-9a-f]+ SurveillanceCameraControllerPS is now off, ' 10
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): off, [a-z+]+, value -?[0-9]+ by player 1, played' 10
shot devices-camera-off
reject 'watchdog: game thread silent'
