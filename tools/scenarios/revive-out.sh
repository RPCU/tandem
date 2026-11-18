# As host: revived once, our V goes down again within 5 minutes: out (no revive), then up beside the mock player once it is out of combat
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --revive 3
expect 'joined \(player [0-9]+' 20
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'revive: revived by tandem-peer' 20
expect 'revive: our V is up at 40 %' 15
pause 5
cmd revive down
expect 'revive: our V is out \(down again [0-9]+ s after a revive\)' 10
expect_peer 'player 1 is out' 5
expect 'revive: respawn beside tandem-peer \(out of combat\)' 15
expect 'revive: our V is up at 40 %' 15
expect_peer 'player 1 is up' 5
reject 'our V dies: death screen'
reject 'watchdog: game thread silent'
