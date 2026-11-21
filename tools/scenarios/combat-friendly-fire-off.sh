# As the host with friendly fire off (the default), a guest's grenade never reaches our V: the guest's game does not even send it
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --area-hit 3
# (The host logs its rules once per session, which the harness keeps across
# scenarios: the joiner hearing them is the check.)
expect_peer "the session's rules: friendly fire off" 30
query before health
pause 10
# Nothing took health off our V (it may only have grown back).
query after health
expect_value "$after" '>=' "$before"
reject "outcomes: player [0-9]+'s area effect on our V"
reject 'watchdog: game thread silent'
