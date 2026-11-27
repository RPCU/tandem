# The partner arriving from afar (here: the mock player taking our fast travel's offer) is hidden for its jump and fades in beside us, never a pop
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --regroup-answer yes
expect 'joined \(player [0-9]+' 20
cmd travel
expect 'travel: arrived at .*, offered the others to join' 60
expect_peer 'player 1 offers to join them' 10
expect 'player [0-9]+: arriving from [0-9]+ m away: hidden for the jump' 30
expect 'player [0-9]+: arrived, faded in' 5
pause 1
shot arrival-fade
reject 'watchdog: game thread silent'
