# As host with the mock player up: our V goes down (no death screen), the mock player revives it, it gets up at 40 %
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --revive 3
expect 'joined \(player [0-9]+' 20
expect 'revive: another player is here: our V goes down instead of dying' 20
# An earlier scenario's revive must not make this fall an "out" (300 s cooldown).
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'our V: life 1' 5
expect_peer 'player 1 is down' 10
expect_peer 'reviving player 1' 10
expect 'revive: tandem-peer is reviving us' 5
expect_peer 'revived player 1' 15
expect 'revive: revived by tandem-peer' 5
expect 'revive: our V is up at 40 %' 15
expect_peer 'player 1 is up' 5
cmd revive report
expect 'revive report: our V life 0' 5
reject 'our V dies: death screen'
expect 'revive: up: the state machine is past its death state' 10
reject 'watchdog: game thread silent'
