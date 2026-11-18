# A player drives a car round V, gets out and walks: copy, seat, parked
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body male --drive-seconds 15
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
expect 'on foot' 40
expect 'car left parked' 10
expect 'movement controller back' 10
reject 'watchdog: game thread silent'
