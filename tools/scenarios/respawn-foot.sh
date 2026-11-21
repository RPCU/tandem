# As host: our V is out with the mock player on foot in front of it: it gets up at the mock's last spot on foot, 1.5 m off it when that is clear
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
# The mock's spot seen on foot (twice a second) before we fall.
pause 3
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive out
expect 'revive: our V is out \(a test\)' 10
expect_peer 'player 1 is out' 5
expect 'revive: respawn beside tandem-peer \(out of combat\): at its last spot on foot' 15
expect 'revive: our V is up at 40 %' 15
expect 'revive: up: the state machine is past its death state' 10
cmd revive report
expect 'revive report: tandem-peer life 0.*a spot on foot true' 5
reject 'revive: no spot to get up at'
# Look for: our V standing beside the mock, not inside it or a wall.
reject 'watchdog: game thread silent'
