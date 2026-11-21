# As a host: the mock guest's copy of a turret near V "has its V"; our turret takes the guest's puppet as a target, turns to it, fires, and every shot is told
# shellcheck shell=bash disable=SC2317,SC2154
# Where: TURRET_AT (a fixture or "X Y Z": a security turret we follow, a
# "sentry", within 200 m: V goes beside it; default arasaka-park). The
# police's street-corner turrets are not followed (devices.md): a spot
# with only those fails on "no sentry within 200 m" (drake-ave, and the 33 m
# around arasaka-park's gate, 2026-11-21).
# To look at: the turret turns to the other player's puppet (in front of V)
# and fires at it; the puppet takes no damage here (its player's game
# decides); V is fired at only when the turret has V too.
ensure host
at "${TURRET_AT:-arasaka-park}"
mark
cmd device near sentry
expect 'devices: nearest sentry [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest sentry [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "turret $turret"
cmd device goto sentry
expect "devices: at sentry $turret SecurityTurretControllerPS: " 10
pause 2
# On: a save may hold it off.
cmd device on sentry
expect "devices: on $turret SecurityTurretControllerPS at " 10
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --turret-seen "$turret@8"
expect 'joined \(player [0-9]+' 30
expect_peer "turret $turret: our copy has our V \(--turret-seen\)" 30
expect "turrets: player [0-9]+'s copy of $turret has its V: our turret takes its puppet \(true\)" 10
shot turret-host-aim
# The turret's own target, and its lock: ours (TandemTake), at once; the
# mock's copy has its V for 6 s only.
query target device sentry target
expect_value "$target" = puppet
expect "turrets: our turret $turret fires at [0-9a-f]+: told to the guests" 20
expect_peer "turret $turret: the host's fires at target kind 3 id [0-9]+ at " 10
shot turret-host-fire
expect_peer "turret $turret: our copy lost our V" 20
expect "turrets: player [0-9]+'s copy of $turret lost its V: our turret lets its puppet go" 10
cmd turret
expect 'turrets: [0-9]+ spoken of; shots told [1-9]' 5
reject 'watchdog: game thread silent'
