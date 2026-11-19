# A player joins in front of V: puppet spawned, dressed, moving, posed
# shellcheck shell=bash disable=SC2317
ensure host
# A real V walking the streets (recordings/play/street.tplay), else one in front of ours.
play street --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect 'look of [0-9]+ keys' 20
expect 'player [0-9]+: [0-9]+ states, 0 dropped' 20
expect 'pose stream: player [0-9]+ drawn' 20
expect_peer 'look from player 1'
pause 5
reject 'watchdog: game thread silent'
