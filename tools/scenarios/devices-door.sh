# A door V opens is open for the other player; the other player closes it and it closes here
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT ("X Y Z", default V's flat: its own door is 200 m around).
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --device-answer closed --device-after 3
expect 'joined \(player [0-9]+' 20
cmd teleport "${DEVICES_AT:-home}"
pause 4
cmd device
expect 'devices: [0-9]+ followed, [1-9][0-9]* in our world' 10
cmd device goto door
expect 'devices: at door [0-9a-f]+ [A-Za-z]+: ' 10
# A known start: closed. Then V opens it: the session hears it, the player sees it.
# The mock player answers each change of ours 3 s later and takes what it hears
# for 5 s after as its answer's reply: the door may have changed before (it
# opened as V arrived, 2026-11-22), so the open waits for that window's end.
cmd device close door
pause 9
mark
cmd device open door
expect 'devices: open [0-9a-f]+ [A-Za-z]+ at ' 5
expect 'devices: [0-9a-f]+ [A-Za-z]+ is now [a-z]+, [a-z+]*open' 10
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): [a-z]+, [a-z+]*open[a-z+]*, value -?[0-9]+ by player 1, played' 10
shot devices-door-open
# The other player closes it 3 s later: a request, our door follows the game's way.
expect_peer 'asked the host for device [0-9a-f]+ .* \(--device-answer\)' 10
expect 'devices: player [0-9]+ operated [0-9a-f]+' 10
expect "devices: [0-9a-f]+ [A-Za-z]+ after player [0-9]+'s request: [a-z]+, (plain|[a-z+]*), value" 10
reject "devices: [0-9a-f]+ [A-Za-z]+ after player [0-9]+'s request: [a-z]+, [a-z+]*open"
expect_peer 'device [0-9a-f]+ \(0x[0-9a-f]+\): [a-z]+, [a-z+]+, value -?[0-9]+ by player [2-9], played' 10
shot devices-door-closed
cmd device
expect 'devices: [0-9]+ followed, [0-9]+ in our world' 5
reject 'watchdog: game thread silent'
