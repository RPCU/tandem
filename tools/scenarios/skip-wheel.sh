# The time skip wheel as the host: the guest is asked, agrees, and both clocks move; then declined, nothing moves
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --skip-answer yes
expect 'joined \(player [0-9]+' 20
pause 4
# Our request (the wheel's path without the wheel: the same Ask).
cmd skip ask 3
expect 'activities: asked to skip [0-9]+ -> [0-9]+ s \(reason 0\)' 5
expect_peer 'player 1 asks to skip 3.0 h' 10
expect_peer 'time skip [0-9]+ of player 1: accepted' 5
expect 'activities: time skip [0-9]+ of player 1 ended: applied' 10
expect 'activities: our clock moved by 10800 s' 5
expect 'epoch: our world was rebuilt \(time skip\)' 5
expect 'world: shared time' 15
