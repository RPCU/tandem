# As host: a guest that owns its own area runs world vehicles there (one driven by its NPC, chasing us once we meet); it walks into our region (its cars shown as proxies), then walks off leaving them: handed over to our game, same bodies, its driver still at the wheel, the moving one back in our traffic
# shellcheck shell=bash disable=SC2317
ensure host
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer join "$address":27450 --name far --mode region --region-far 400 --region-walk-at 10 --region-leave-at 50 \
  --region-speed 20 --region-npcs 2 --region-cars 3 --region-driver --region-chase
expect 'in the session as player|joined \(player' 30
expect_peer 'street: running 3 vehicle\(s\) of our own' 20
# Apart: its street is its own, nothing of it here.
expect 'regions: linked with nobody' 20
cmd street count
expect 'street count: [0-9]+ \([0-9]+ run by us, 0 shown as proxies' 5
# It walks up: its three cars shown as proxies, run by its game.
expect 'regions: linked with player [2-9]' 40
expect 'street: vehicle [0-9]+ .* of player [2-9] \(flags [0-9]+\) spawning at' 30
expect_peer 'street: our vehicle 0x[0-9a-f]+ chases player 1' 20
expect 'wheels: vehicle [0-9]+ chases our V' 20
pause 3
cmd street count
expect 'street count: [0-9]+ \([0-9]+ run by us, [3-9] shown as proxies' 5
shot wheels-guest-area-met
# Meeting moves nothing: its cars stay its own while it stands by them.
reject 'street: handover in: vehicle'
# It walks off, leaving its cars by us: handed over, no new body.
expect_peer 'street: handed vehicle 0x[0-9a-f]+ over to player 1' 60
expect 'street: handover in: vehicle [0-9]+ from player [2-9]' 10
# Its driver comes with the car, still in its seat.
expect 'regions: handover in: npc 16[0-9]{6} from player [2-9] .*\(inherited\)' 10
pause 6
cmd street count
expect 'street count: .* [3-9] taken over' 5
shot wheels-guest-area-taken
reject 'street: handover in: vehicle [0-9]+ from player [0-9]+ refused'
reject 'watchdog: game thread silent'
