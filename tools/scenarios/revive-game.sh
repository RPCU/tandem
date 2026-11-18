# As host: the mock player goes down in front of us, our V holds the revive for 10 s (test hold, no key), the mock player gets up
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --go-down 10
expect 'joined \(player [0-9]+' 20
expect_peer 'our V is down \(--go-down\)' 20
expect 'revive: tandem-peer is down' 5
cmd revive hold
expect 'revive: reviving tandem-peer \(test hold\)' 5
expect_peer 'player 1 starts reviving us' 5
expect 'revive: we revived tandem-peer' 15
expect_peer 'player 1 revived us' 5
expect_peer 'our V is up \(revived by player 1\)' 5
expect 'revive: tandem-peer is up' 5
reject 'stopped reviving tandem-peer'
reject 'watchdog: game thread silent'
