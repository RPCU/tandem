# As a guest: the host's two-seat quest car (an NPC drives, the host beside it) has no seat for us: a seat or nothing, so we are not asked to ride, we follow (told so), never carried unseen
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_sport2_porsche_911turbo --seat seat_front_right --scene --driver-npc Character.Panam --body male --drive-seconds 40
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_right' 30
expect 'npcs: proxy 50001 seated in seat_front_left' 30
# Our V on foot within 20 m of the copy: no question, the notice.
expect "ride: player 1's scene ride has no free seat: following \(no free seat\)" 30
shot scene-ride-full
pause 8
reject 'ride: asked to ride with player 1 or follow'
reject 'our ride: kind 4'
cmd ride
expect 'ride: seated by player 0, asked by player 0' 5
expect_peer 'out of the car' 60
reject 'watchdog: game thread silent'
