# As host: the mock player is down and our V goes down too: nobody is up, the session is lost and our game shows the death screen
# runs last
# shellcheck shell=bash disable=SC2317
# Leaves the game on its death screen: run it last, then load a save.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --go-down 10
expect 'joined \(player [0-9]+' 20
expect 'revive: tandem-peer is down' 25
cmd revive down
expect 'revive: our V died with nobody up: the session is lost' 10
expect 'session lost: no V is up' 5
expect_peer 'session lost: no V is up' 5
reject 'revive: our V is down'
