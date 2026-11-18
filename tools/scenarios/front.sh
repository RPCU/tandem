# A player joins in front of V: puppet spawned, dressed, moving, posed
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect 'look of [0-9]+ keys' 20
expect 'player [0-9]+: [0-9]+ states, 0 dropped' 20
expect 'pose stream: player [0-9]+ drawn' 20
expect_peer 'look from player 1'
pause 5
reject 'watchdog: game thread silent'
