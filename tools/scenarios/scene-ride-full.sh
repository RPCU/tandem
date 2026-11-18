# As a guest: the host's two-seat quest car (an NPC drives, the host beside it) has no seat for us; asked, we ride along hidden, and get out beside it when the scene ends
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_sport2_porsche_911turbo --seat seat_front_right --scene --driver-npc Character.Panam --body male --drive-seconds 40
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_right' 30
expect 'npcs: proxy 50001 seated in seat_front_left' 30
# Our V on foot within 20 m of the copy: the question, then the default.
expect 'ride: asked to ride with player 1 or follow' 30
shot scene-ride-asked
expect "ride: no free seat in player 1's scene ride: riding along" 15
expect 'our ride: kind 4' 5
cmd ride
expect 'ride: .*carried by player 1' 5
shot scene-ride-along
# The scene ends: out, beside the car, every restriction off.
expect_peer 'out of the car' 60
expect "ride: out of player 1's ride along \(the ride is over\)" 10
expect 'our ride: kind 0' 5
reject 'watchdog: game thread silent'
