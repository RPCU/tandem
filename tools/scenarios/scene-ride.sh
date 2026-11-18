# As a guest: the host's quest car comes by with an NPC at the wheel; copy, driver and host seated, our V given a free seat, out when the scene ends
# shellcheck shell=bash disable=SC2317
ensure join
# A four-seat car: the NPC drives, the host sits beside it, two seats left.
peer host --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --seat seat_front_right --scene --driver-npc Character.Panam --body male --drive-seconds 45
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_right' 30
expect 'player 1: ride 3 \(flags 0x2' 10
# The NPC's proxy at the wheel of the copy, not left standing.
expect 'npcs: proxy 50001 seated in seat_front_left' 30
# Our V on foot within 20 m of the copy (the car circles 12 m round it).
expect "ride: seated in seat_[a-z_]+ of player 1's scene ride" 30
shot scene-ride
expect_peer 'player [0-9]+ sits in .* \(seat .*car of player 1\)' 10
# The scene ends (the host gets out): out again.
expect_peer 'out of the car' 60
expect "ride: player 1's scene ride is over, out of its seat" 10
reject 'watchdog: game thread silent'
