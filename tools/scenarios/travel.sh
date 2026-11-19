# Fast travel together: our fast travel is offered to the other player, theirs to us; accepting brings V next to them
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --travel-at 70 --regroup-answer yes
expect 'joined \(player [0-9]+' 20
# Ours: a fast travel to one of our points 500 m away or more, offered on arrival.
cmd travel
expect 'travel: fast travelling to' 5
expect 'travel: fast travel to .* under way' 10
expect 'travel: arrived at .*, offered the others to join' 60
expect_peer 'player 1 offers to join them' 10
expect 'travel: tandem-peer is on the way' 10
# Theirs: the mock player "fast travels" to the teleport home spot and offers it.
expect_peer 'fast travelled to' 60
expect 'travel: showing offer [0-9]+ of tandem-peer' 10
cmd regroup accept
expect 'travel: joining tandem-peer' 5
expect 'travel: arrived next to tandem-peer' 60
# Drawn again after it: the fast travel's loading screen ended (it was left black).
expect 'travel: screen restored' 5
reject 'travel: still black'
shot travel-arrived
expect_peer 'another V is with us again' 20
