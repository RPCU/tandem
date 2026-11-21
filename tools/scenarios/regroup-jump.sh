# The host's V taken far with no travel of its own (a quest's teleport): the guest is offered to join it; a test's teleport is not offered
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --regroup-answer no
expect 'joined \(player [0-9]+' 20
pause 3
# 300 m up and back (god on: the fall is long). A test's teleport is ours:
# nothing offered.
cmd god on
cmd teleport -915 1879.5 336.3
pause 2
cmd teleport home
pause 4
reject 'travel: our V was taken'
# The same, as a quest's teleport would: the others are offered to join.
cmd regroup jump -915 1879.5 336.3
expect 'travel: our V was taken [0-9]+ m with no travel of ours' 10
expect 'travel: offered everyone to join us \(offer [0-9]+, reason 3\)' 10
expect_peer 'player 1 offers to join them \(offer [0-9]+, reason 3' 10
expect_peer 'offer [0-9]+ of player 1: declined' 5
cmd teleport home
pause 3
cmd god off
