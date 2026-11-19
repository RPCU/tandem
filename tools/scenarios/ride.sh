# A partner rides in a car an AI drives (a cab, a quest car): its game runs it, the copy follows, the puppet in its seat
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --seat seat_front_right --body female --drive-seconds 20
expect 'copy spawning' 30
expect 'seated in seat_front_right' 30
expect 'player [0-9]+: ride 2 ' 10
shot ride
# Our V gets in too: a free seat of the copy, never the puppet's.
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_back_(left|right)" 10
cmd ride
expect 'ride: its seats:.*seat_front_right\(taken\)' 5
pause 3
cmd vehicle unride
expect 'player [0-9]+: on foot' 30
expect 'player [0-9]+: ride 0 ' 10
expect 'movement controller back' 10
reject 'watchdog: game thread silent'
