# A quickhack on the copy of a car another player drives goes to the driver's game instead of the copy
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body male
expect 'seated in seat_front_left' 30
cmd hack car
expect "hacks: our car hack 0 on player [0-9]+'s car sent to them" 10
expect_peer 'player 1 hacked our car: 0 \(3.0 s\)' 10
reject 'watchdog: game thread silent'
