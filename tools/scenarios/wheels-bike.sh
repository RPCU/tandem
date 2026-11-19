# As a guest: the host rides a bike round our V and is knocked off it; the bike goes on without a rider, then lies where it stopped
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --body male --vehicle Vehicle.v_sportbike2_arch_player --exit-at 25 --exit-kind knocked
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
shot wheels-bike-riding
expect_peer 'out of the car the hard way \(kind 3\)' 40
expect 'wheels: player 1 left vehicle 0x1 the hard way \(kind 3' 10
expect "wheels: player 1's car rolls on with nobody at the wheel" 10
shot wheels-bike-knocked
expect 'player 1: on foot' 20
pause 8
shot wheels-bike-lying
reject 'watchdog: game thread silent'
