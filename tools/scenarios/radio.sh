# V rides in a player's car: the car plays the driver's station, V's pocket radio goes quiet and comes back
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body male --radio radio_station_09_downtempo
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
expect_peer 'car radio sent \(at the wheel\): radio_station_09_downtempo' 10
expect 'car radio on, station 4' 10
# Before anyone rides in it, the copy's radio stays off.
pause 3
cmd vehicle ride
expect 'vehicle ride: in player [0-9]+' 10
expect 'radio: riding in player [0-9]+.s car: our pocket radio quiet' 10
expect_peer 'car radio sent \(player [0-9]+ got in\)' 10
expect "radio: player [0-9]+'s car set to the driver's station 4 \(radio_station_09_downtempo\)" 10
# It holds: nothing had to set it again, and nothing gave up on it.
pause 5
reject 'radio: player [0-9]+.s car stays '
shot radio-riding
cmd vehicle unride
expect 'radio: out of player [0-9]+.s car: our pocket radio back' 10
pause 3
reject 'watchdog: game thread silent'
