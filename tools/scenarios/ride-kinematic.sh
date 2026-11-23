# As a guest riding in the host's car copy with vehicle_move = kinematic: every vehicle the host runs is a kinematic body the engine's own move puts where the host says (no teleport, no fall, the speed the host gives it), the street's proxies spawn from the seat, and each body gets its physics back once nobody moves it
# shellcheck shell=bash disable=SC2317,SC2154
ensure join vehicle_move=kinematic
peer host --mode drive --vehicle Vehicle.v_standard2_archer_hella_player --body female --drive-seconds 120 --traffic 4 --traffic-at 30
expect 'in the session as player' 30
expect 'vehicles: the vehicles other games run are kinematic bodies' 60
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_(front_right|back_left|back_right)" 10
# The street comes while we ride: each proxy gets its body from the seat
# (the kinematic move is no teleport of our V's mount: vehicles.md §13.141).
expect_peer 'street: 4 cars of .* round player' 45
expect 'street: vehicle [0-9]+ .* spawning at' 15
pause 6
cmd street count
expect 'street count: [0-9]+ \(0 run by us, 5 shown as proxies; 1 parked, 0 wrecks.* 5 proxy body\(ies\) here' 10
shot ride-kinematic
# Moved by the engine, held kinematic, put where told (the error of a moved
# body under 5 cm), none teleported but for the first placements.
cmd vehicle moves
expect 'vehicle moves: path kinematic, [1-9][0-9]* moved by the engine, [1-9][0-9]* held kinematic now' 5
expect 'vehicle moves: .* 0 over 8\.0+ m, 0 of bodies that do not follow, 0 with the path off(, [0-9]+ not held yet)?\), 0 body\(ies\) that do not follow, 0 refused, 0 frame\(s\) our seat.s car was not teleported, largest error 0\.0[0-4]' 5
cmd vehicle unride
expect 'player [0-9]+: on foot|vehicle ride: out' 30
reject "street: check: vehicle [0-9]+'s body was gone"
reject 'does not follow the kinematic move'
reject 'the kinematic move moves nothing here'
reject 'watchdog: game thread silent'
