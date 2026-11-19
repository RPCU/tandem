# As a guest: the host's car damaged on our copy the same way (dents, a flat tyre, the bumper off, fire, one explosion), our shot and our V on its gun go to the host, it bails out on the move and its car rolls on, its street car chases us
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --body female --car-damage 40 --exit-at 60 --traffic 2 --chase 15 \
  --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
# Our shot on its car is its game's to apply; its answer is the copy's damage.
cmd wheels shoot
expect "wheels: our shot on player [0-9]+'s car sent to them" 10
expect_peer 'our car: player [0-9]+ hit it for 150, tyre 1 flat' 10
expect 'wheels: vehicle 0x1 of player 1: health 0\.85' 10
# Our V on its gun: the host gives the seat, our aim goes to it.
cmd wheels gun
expect 'wheels: vehicle 1: gunner [0-9]+' 10
expect_peer 'vehicle 0x1: gunner [0-9]+ ' 10
pause 3
cmd wheels ungun
expect_peer 'vehicle 0x1: gunner 0 \(free\)' 10
# Its street car chases our V (the host's own AI, the vehicle shared).
expect 'wheels: vehicle 0x[0-9a-f]+ chases player [0-9]+' 30
expect 'wheels: vehicle [0-9]+ chases our V' 10
# Dents, the tyre, the bumper off; then fire; then the game's own explosion, once.
expect_peer 'our car: hit: dents' 40
expect 'wheels: vehicle 0x1 of player 1: health 0\.55.*1 part\(s\) off' 10
shot wheels-guest-dents
expect_peer 'our car burns' 10
expect_peer 'our car blew up' 10
expect 'wheels: vehicle 1 blows up' 10
shot wheels-guest-wreck
# It bails out on the move: the copy rolls on, then stops, a wreck parked.
expect_peer 'out of the car the hard way \(kind 1\)' 30
expect "wheels: player 1's car rolls on with nobody at the wheel" 10
expect 'car left parked' 20
pause 5
cmd wheels
expect 'wheels: our car none .*damaged copies' 10
reject 'watchdog: game thread silent'
