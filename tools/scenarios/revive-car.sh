# As guest: our V goes down in a passenger seat of the host's moving car: it stays seated, the host's revive waits until the car stops, then our V gets up in its seat
# shellcheck shell=bash disable=SC2317
ensure join
# The host drives round V for 40 s, then gets out beside its car.
peer host --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --body female --drive-seconds 40 --revive 2
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
# An earlier scenario's revive must not make this fall an "out" (300 s cooldown).
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_[a-z_]+" 10
pause 2
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'revive: our V is down in its seat \(seat_[a-z_]+\)' 5
expect_peer 'player [0-9]+ is down' 10
expect_peer 'reviving player [0-9]+ \(10 s\)' 10
expect 'revive: tandem-peer is reviving us' 5
# The mock's done comes while the car still circles at 8 m/s: it waits.
expect "revive: tandem-peer's revive waits: our car still moves" 15
expect 'revive: our car has stopped: the waiting revive goes on' 40
expect 'revive: revived by tandem-peer' 5
expect 'revive: our V is up at 40 %' 15
expect_peer 'player [0-9]+ is up' 5
cmd revive report
expect 'revive report: our V life 0' 5
reject 'revive: our car is .*: our V is put out'
# Look for: our V slumped in its seat while down, the stand-up in the seat.
cmd vehicle unride
expect 'vehicle ride: out' 5
reject 'watchdog: game thread silent'
