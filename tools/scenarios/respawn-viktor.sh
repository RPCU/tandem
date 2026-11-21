# As host: our V is out and no spot beside the mock player fits (a test: as with no navmesh there): it gets up at Viktor's clinic at once instead of waiting
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
pause 3
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive nospot
expect 'revive nospot: no spot beside a partner fits' 5
cmd revive out
expect 'revive: our V is out \(a test\)' 10
expect_peer 'player 1 is out' 5
expect 'revive: no spot to get up at beside tandem-peer \(a test\)' 15
expect "revive: respawn at Viktor's clinic \(out of combat\)" 5
expect 'revive: there, the loading screen ended' 60
expect 'coop notice: Vik patched you up' 5
expect 'revive: our V is up at 40 %' 15
expect 'revive: up: the state machine is past its death state' 10
expect_peer 'player 1 is up' 10
cmd revive nospot off
expect 'revive nospot: the real spots' 5
reject 'at its last spot on foot'
reject 'no end of the loading screen'
reject 'watchdog: game thread silent'
