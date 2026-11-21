# As a host: a turret in the mock guest's hands turns where the guest points it and draws its fire on our copy, which fires nothing of its own; let go, it is our game's again
# shellcheck shell=bash disable=SC2317
# Where: TURRET_AT (a fixture or "X Y Z": V in sight of a security turret
# within 60 m, as turret-guest; default arasaka-park).
# To look at: the turret turns to V and holds there for 6 s; from 2 s to
# 4 s in, tracers leave its barrels toward V (they hurt nobody: the guest's
# game decides its hits); then it goes back to its own sweep.
ensure host
at "${TURRET_AT:-arasaka-park}"
mark
cmd device near turret
expect 'devices: nearest turret [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest turret [0-9a-f]+' | tail -1 | awk '{ print $4 }')
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
note "turret $turret, V at $x $y $z"
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --turret-hands "$turret:$x,$y,$z@6"
expect 'joined \(player [0-9]+' 30
expect_peer "turret $turret: in our V's hands" 20
expect "turrets: $turret is in player [0-9]+'s hands: our copy follows it" 5
expect "turrets: player [0-9]+'s turret $turret fires: drawn from our copy" 5
shot turret-hands-fire
expect_peer "turret $turret: our V lets go of it" 10
expect "turrets: $turret is out of player [0-9]+'s hands: our copy is our game's again" 5
cmd turret
expect 'turrets: .* in others. hands 0, their shots drawn [1-9]' 5
reject 'watchdog: game thread silent'
