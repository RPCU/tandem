# Three players: two mock players join our session, hear each other through us, each has a marker in its own colour; one leaving leaves the other
# shellcheck shell=bash disable=SC2317
ensure host
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer join "$address:27450" --mode mirror --offset 2,0,0 --body female --name peer-a
expect 'peer-a joined \(player [0-9]+' 20
peer2 join "$address:27450" --mode mirror --offset -2,0,0 --body male --name peer-b
expect 'peer-b joined \(player [0-9]+' 20
# Each guest learns the other from the roster, and hears its V through us.
expect_peer '\+ player [0-9]+ \(peer-b\)' 20
expect_peer2 '\+ player [0-9]+ \(peer-a\)' 20
expect 'marker: player [0-9]+ \(peer-a\) on the puppet, slot .*, registered true' 30
expect 'marker: player [0-9]+ \(peer-b\) on the puppet, slot .*, registered true' 30
pause 3
cmd markers
expect 'markers: player [0-9]+ \(peer-a\) on the puppet at ' 10
expect 'markers: player [0-9]+ \(peer-b\) on the puppet at ' 10
# Two markers, two colours (by id): look at the screenshot.
shot three-players
# peer-b goes; peer-a stays in the session, its marker with it.
pkill -f -- 'tandem-peer .*--name peer-b' || true
expect 'player [0-9]+ left' 45
expect_peer '^.*- player [0-9]+$' 20
at_most 1 'player [0-9]+ left'
at_most 1 'marker: player [0-9]+ removed'
reject 'watchdog: game thread silent'
# Only what comes after: peer-a's marker is still there.
mark
cmd markers
expect 'markers: player [0-9]+ \(peer-a\) on the puppet at ' 10
reject 'markers: player [0-9]+ \(peer-b\)'
