# As host, at the wheel of an armed vehicle (a Basilisk, a car with mounted guns): a guest takes our vehicle's gun; where it aims and fires, our real vehicle fires (seen and heard here and by everyone), and what those shots hit is the guest's game's, never ours; then the turret look-at probe
# by hand: a person in game does what its notes say (tools/sweep.sh leaves it out)
# shellcheck shell=bash disable=SC2317
# Needs our V at the wheel of an armed vehicle (no command puts it there).
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --gunner 5
expect 'joined \(player [0-9]+' 20
expect 'wheels: our car is ' 30
# It asks for our gun: ours to give.
expect_peer 'gunner: the seat is ours' 30
expect 'wheels: vehicle 1: gunner [0-9]+' 10
# Its shots: our real guns fire where it aimed.
expect_peer 'gunner: fire' 10
expect 'wheels: our vehicle 1 fires for its gunner, player [0-9]+ \(1 shot\(s\), armed (true|false)\)' 10
note "armed: $(since | grep -oE 'armed (true|false)' | tail -1)"
shot gunner-turret-fire
# The probe (research/engine/vehicles.md §13.70): a look-at on a part of
# the vehicle's rig turns its turret to the gunner's aim, if the part is
# the turret's. Look for the barrel following the guest's slow sweep.
cmd wheels turret turret
expect 'wheels: turret look-at part turret' 5
pause 4
shot gunner-turret-look
cmd wheels turret off
cmd wheels
expect 'wheels: our car .* [1-9][0-9]* turret shots for a gunner' 10
expect_peer 'gunner: left the seat' 30
reject 'watchdog: game thread silent'
