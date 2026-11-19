# As a guest: V opens a door at once and the host follows; the host closes it here; leaving puts our own world back
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT ("X Y Z", default V's flat: its own door is 200 m around).
ensure join
peer host --mode idle --body female --device-answer closed --device-after 3
expect 'in the session as player' 30
expect 'devices: following [0-9]+ around us \(guest\)' 30
cmd teleport "${DEVICES_AT:-home}"
pause 4
cmd device
expect 'devices: [0-9]+ followed, [1-9][0-9]* in our world' 10
cmd device goto door
expect 'devices: at door [0-9a-f]+ [A-Za-z]+: ' 10
cmd device close door
pause 2
mark
# V opens it: open here at once, and a request to the host.
cmd device open door
expect 'devices: [0-9a-f]+ [A-Za-z]+ changed here \([a-z]+, [a-z+]*open[a-z+]*, value -?[0-9]+\): asked of the host' 10
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): [a-z]+, [a-z+]*open[a-z+]*, value -?[0-9]+ operated by player [0-9]+: taken as said' 10
shot devices-guest-open
# The host closes it 3 s later: its word, played here (the door shuts).
expect_peer 'our device [0-9a-f]+ .* \(--device-answer\)' 10
expect 'devices: [0-9a-f]+ [A-Za-z]+ by player 1: [a-z]+, [a-z+]+, value -?[0-9]+, played' 10
pause 2
shot devices-guest-closed
reject 'devices: [0-9a-f]+ [A-Za-z]+ will not become'
# V opens it again and leaves the session: our own save's door comes back (closed).
cmd device open door
expect 'devices: [0-9a-f]+ [A-Za-z]+ changed here' 10
pause 2
cmd coop leave
expect 'devices: session over, [0-9]+ followed, [1-9][0-9]* put back as our own save had them' 15
pause 2
shot devices-guest-left
reject 'watchdog: game thread silent'
