# As a guest: our V a passenger in the host's car copy (spawned and moved by us, the host's puppet at the wheel, no AI driver) draws a gun and leans out (the vehicle state Combat) as in any car of that record; weapons stay ours in that seat
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_standard2_archer_hella_player --body female --drive-seconds 40
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
# Our V into a free seat of the copy (beside the puppet, or behind it).
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_(front_right|back_left|back_right)" 10
pause 2
# A gun, the game's way from a seat.
cmd wheels draw
expect 'wheels: draw in seat_[a-z_]+ of vehicle [0-9]+ \(copy of player 1\): no weapons false, no combat false' 5
# The player state machine's vehicle state Combat (2): mode 2 said.
expect 'wheels: our weapons in vehicle [0-9]+: mode 2 ' 10
shot passenger-combat
reject 'wheels: draw .*no weapons true'
cmd vehicle unride
expect 'player [0-9]+: on foot|vehicle ride: out' 30
reject 'watchdog: game thread silent'
