# Players apart: offers declined both ways (each told), then our regroup action brings V to the other player
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --travel-at 60 --regroup-answer no
expect 'joined \(player [0-9]+' 20
cmd travel
expect 'travel: arrived at .*, offered the others to join' 60
expect_peer 'offer [0-9]+ of player 1: declined' 10
expect 'travel: tandem-peer stays where they are' 10
# Theirs, from the teleport home spot: declined here.
expect 'travel: showing offer [0-9]+ of tandem-peer' 60
cmd regroup decline
expect 'travel: declined offer' 5
expect_peer 'player 1 declined: stays' 10
# Free to stay apart; then we go to them (the map's "Join" does the same).
pause 5
# By name: ids are the host's count of joins (24 after a day of scenarios).
cmd regroup tandem-peer
expect 'travel: regroup with tandem-peer: on the way' 5
expect_peer 'player 1 is on the way' 10
expect 'travel: arrived next to tandem-peer' 60
# Drawn again after it: the fast travel's loading screen ended (it was left black).
expect 'travel: screen restored' 5
reject 'travel: still black'
shot travel-arrived
expect_peer 'another V is with us again' 20
