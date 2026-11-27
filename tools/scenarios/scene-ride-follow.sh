# As a guest: asked to ride in the host's quest car (a free back seat), we follow instead: nothing is done to our V, and it is not asked again for that ride
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --seat seat_front_right --scene --driver-npc Character.Panam --body male --drive-seconds 30
expect 'in the session as player' 30
expect 'ride: asked to ride with player 1 or follow' 40
cmd ride follow
expect 'ride: following player 1 \(chosen\)' 5
pause 8
reject 'ride: seated in'
expect_peer 'out of the car' 60
reject 'watchdog: game thread silent'
