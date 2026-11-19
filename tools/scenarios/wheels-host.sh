# As host, at the wheel: a guest's shots on our car and its flat tyre applied by our game, the guest on our car's gun, its car ramming our NPC, its pushes on our car
# shellcheck shell=bash disable=SC2317
# Needs our V at the wheel of a car on a street with people around (summon
# one and get in before running it: no command puts V at the wheel).
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --shoot-cars 5 --gunner 8 --ram-npcs 6
expect 'joined \(player [0-9]+' 20
expect 'wheels: our car is ' 30
# Its shots: our game applies them to our real car, the copy there follows.
expect_peer 'shot player 1.s car for 150' 30
expect 'wheels: player [0-9]+ hit vehicle 1 for 150' 10
expect_peer 'vehicle 0x1 \(of player 1\): health 0\.[0-9]+' 10
expect 'wheels: player [0-9]+ hit vehicle 1 for 150 \(flags 4, tyre 1\)' 20
shot wheels-host-tyre
# It asks for our gun: ours to give, its aim and shots come to us.
expect_peer 'gunner: the seat is ours' 30
expect 'wheels: vehicle 1: gunner [0-9]+' 10
# Its car runs into one of our NPCs: our NPC is knocked down for real.
expect_peer 'our car ran into npc' 30
expect "wheels: player [0-9]+'s car ran into npc [0-9]+ \(flags 1\)" 10
shot wheels-host-ram
cmd wheels
expect 'wheels: our car .* [1-9][0-9]* taken' 10
reject 'watchdog: game thread silent'
