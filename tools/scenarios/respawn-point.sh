# As host: our V is out, no spot beside the mock player fits and Viktor's is out of the story's reach (both a test): it gets up at its fast travel point nearest to where it lay
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
pause 3
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive nospot
expect 'revive nospot: no spot beside a partner fits' 5
cmd revive novik
expect "revive novik: Viktor's out of reach" 5
cmd revive out
expect 'revive: our V is out \(a test\)' 10
expect "revive: respawn at our fast travel point .* \(out of combat; Viktor's: a test\)" 15
expect 'revive: there, the loading screen ended' 60
expect 'coop notice: Patched up, back on your feet at' 5
expect 'revive: our V is up at 40 %' 15
expect_peer 'player 1 is up' 10
cmd revive fresh
expect 'revive fresh: no recent revive' 5
reject "respawn at Viktor's clinic"
reject 'no end of the loading screen'
# The respawn's loading screen stalls the game thread (the watchdog says so,
# then that it is back): only what comes after it.
mark
pause 3
reject 'watchdog: game thread silent'
