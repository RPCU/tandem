# As guest: our V goes down in a seat of the host's parked car, taken as wrecked: it is put out beside the car, where the host revives it
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --body female --drive-seconds 20 --revive 6
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_[a-z_]+" 10
# The host gets out; the car stays parked with our V in it.
expect 'player [0-9]+: on foot' 30
pause 2
cmd revive down
expect 'revive: our V is down in its seat \(seat_[a-z_]+[,)]' 10
cmd revive wreck
expect 'revive wreck: our V.s car taken as wrecked \(down in a seat: true\)' 5
expect 'revive: our car is wrecked: our V is put out beside it' 5
expect 'our seat: .* None \(car of player 0\)' 5
expect 'revive: revived by tandem-peer' 25
expect 'revive: our V is up at 40 %' 15
# Look for: our V out of the car, lying beside its door, then getting up.
reject 'watchdog: game thread silent'
