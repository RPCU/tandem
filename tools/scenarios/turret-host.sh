# As a host: the mock guest's copy of a turret near V "has its V"; our turret takes the guest's puppet as a target, turns to it, fires, and every shot is told
# shellcheck shell=bash disable=SC2317
# Where: TURRET_AT (a fixture or "X Y Z": a security turret that is on,
# within 60 m and in sight of V's front; default drake-ave: arasaka-park's are the police's, not followed).
# To look at: the turret turns to the other player's puppet (in front of V)
# and fires at it; the puppet takes no damage here (its player's game
# decides); V is fired at only when the turret has V too.
ensure host
at "${TURRET_AT:-drake-ave}"
mark
cmd device near turret
expect 'devices: nearest turret [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest turret [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "turret $turret"
# On: a save may hold it off (the survey found every fixture's turret off).
cmd device on turret
expect 'devices: on [0-9a-f]+ SecurityTurretControllerPS at ' 10
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --turret-seen "$turret@8"
expect 'joined \(player [0-9]+' 30
expect_peer "turret $turret: our copy has our V \(--turret-seen\)" 30
expect "turrets: player [0-9]+'s copy of $turret has its V: our turret takes its puppet \(true\)" 10
shot turret-host-aim
expect "turrets: our turret $turret fires at [0-9a-f]+: told to the guests" 20
expect_peer "turret $turret: the host's fires at target kind 3 id [0-9]+ at " 10
shot turret-host-fire
expect_peer "turret $turret: our copy lost our V" 20
expect "turrets: player [0-9]+'s copy of $turret lost its V: our turret lets its puppet go" 10
cmd turret
expect 'turrets: [0-9]+ spoken of; shots told [1-9]' 5
reject 'watchdog: game thread silent'
