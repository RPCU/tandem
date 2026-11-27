# As host: the mock player goes down in front of us, our V holds the revive for 10 s (test hold, no key), the mock player gets up
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --go-down 10
expect 'joined \(player [0-9]+' 20
# An earlier scenario's revive must not make this fall an "out" (300 s cooldown).
cmd revive fresh
expect 'revive fresh: no recent revive' 5
expect_peer 'our V is down \(--go-down\)' 20
expect 'revive: tandem-peer is down' 5
# The game's own hold prompt on the body, "[Hold F] Revive" with its ring.
pause 2
shot revive-hold-prompt
cmd revive hold
expect 'revive: reviving tandem-peer \(test hold\)' 5
expect_peer 'player 1 starts reviving us' 5
expect 'revive: we revived tandem-peer' 15
expect_peer 'player 1 revived us' 5
expect_peer 'our V is up \(revived by player 1\)' 5
expect 'revive: tandem-peer is up' 5
# The partner is up as our game holds it, and our V, the reviver, is alive.
cmd revive report
expect 'revive report: tandem-peer life 0,' 5
query health health
expect_value "$health" '>' 0
reject 'stopped reviving tandem-peer'
reject 'watchdog: game thread silent'
