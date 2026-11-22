# As a guest riding in the host's car copy (moved every frame by its driver's word): the host's street announced during the ride appears round us, every proxy with its body, a vehicle of our own spawns too, no body asked for twice
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
peer host --mode drive --vehicle Vehicle.v_standard2_archer_hella_player --body female --drive-seconds 120 --traffic 4 --traffic-at 30
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
# Our V into a free seat of the copy, before the host's street is there.
cmd vehicle ride
expect "vehicle ride: in player [0-9]+'s car, seat seat_(front_right|back_left|back_right)" 10
query ours mounted
expect_value "$ours" '~' '^Vehicle\.v_standard2_archer_hella_player seat_[a-z_]+ [0-9]+$'
# The street comes while we ride: five proxies asked for (four driving
# round us, one parked), and each gets its body (a teleport of our seat's car
# every frame kept every one of them from spawning: vehicles.md §13.141).
expect_peer 'street: 4 cars of .* round player' 45
expect 'street: vehicle [0-9]+ .* spawning at' 15
pause 6
cmd street count
expect 'street count: [0-9]+ \(0 run by us, 5 shown as proxies; 1 parked, 0 wrecks.* 5 proxy body\(ies\) here' 10
shot ride-traffic
# A vehicle of our own asked for from the seat spawns as well.
cmd vehicle probe
expect 'vehicle probe: [0-9]+ frames' 10
cmd vehicle probe stop
# How they were moved: by teleports (the default), our seat's car not in the
# frames a body of ours waited to spawn; or by the engine's kinematic move
# (kinematic_ride = on), each body following.
cmd vehicle moves
expect 'vehicle moves: path (teleport|kinematic), ' 5
expect 'vehicle moves: .* 0 body\(ies\) that do not follow, 0 refused' 5
query riding mounted
expect_value "$riding" '~' '^Vehicle\.v_standard2_archer_hella_player seat_[a-z_]+ [0-9]+$'
cmd vehicle unride
expect 'player [0-9]+: on foot|vehicle ride: out' 30
# No proxy was asked for again for want of a body.
reject "street: check: vehicle [0-9]+'s body was gone"
reject 'does not follow the kinematic move'
reject 'the kinematic move moves nothing here'
reject 'watchdog: game thread silent'
