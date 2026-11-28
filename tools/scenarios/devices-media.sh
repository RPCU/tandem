# A radio or TV's station V picks is the other player's too; the other player picks another and ours follows
# shellcheck shell=bash disable=SC2317
# Where: DEVICES_AT (a fixture or "X Y Z": a radio or TV within 40 m; default
# v-door: V's flat, its radio and TV).
# To look at and listen to: the station changes with the radio's own sound
# and display both times, nothing else in the flat changes.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --device-answer station=2 --device-after 3
expect 'joined \(player [0-9]+' 20
at "${DEVICES_AT:-v-door}"
cmd device near radio
expect 'devices: nearest radio [0-9a-f]+ [A-Za-z]+ at ' 10
radio=$(since | grep -oE 'nearest radio [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the radio or TV: $radio"
# A known start: station 0 (the mock player answers each change 3 s later,
# and takes what it hears for 5 s after as its answer's reply).
cmd device station 0
pause 10
mark
cmd device station 1
expect "devices: station $radio [A-Za-z]+ at " 5
expect "devices: $radio [A-Za-z]+ is now [a-z]+, [a-z+]+, value 1" 10
expect_peer "device $radio \\(0x[0-9a-f]+\\): [a-z]+, [a-z+]+, value 1 by player 1, played" 10
shot devices-media-ours
# The other player picks station 2: a request, ours follows the game's way.
expect_peer "asked the host for device $radio .*value 2 \\(--device-answer\\)" 10
expect "devices: player [0-9]+ operated $radio" 10
expect "devices: $radio [A-Za-z]+ after player [0-9]+'s request: [a-z]+, [a-z+]+, value 2" 10
expect_peer "device $radio \\(0x[0-9a-f]+\\): [a-z]+, [a-z+]+, value 2 by player [2-9], played" 10
shot devices-media-theirs
reject "devices: $radio [A-Za-z]+ will not become"
reject 'watchdog: game thread silent'
