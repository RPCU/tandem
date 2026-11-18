# A player opens the pause menu: our world pauses, the notice names them, and it resumes when they close it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --pause-at 10 --pause-for 3
expect 'joined \(player [0-9]+' 20
expect_peer 'pause menu open' 30
expect 'player [0-9]+ paused \(pause menu\)' 5
expect 'pause: tandem-peer paused the game' 5
expect 'pause: world paused \(1 player\(s\) in the pause menu\)' 5
expect_peer 'pause menu closed' 10
expect 'player [0-9]+ resumed \(pause menu\)' 5
expect 'pause: world resumed' 5
# The world runs again: the puppet keeps getting states (nothing else paused it).
pause 5
expect 'pause: tandem-peer resumed'
reject 'watchdog: game thread silent'
