# As a host: a turret the mock guest turns friendly takes neither our V nor the guest's puppet, even when the guest's copy says it has its V; turned back, it is its own again
# shellcheck shell=bash disable=SC2317,SC2154
# Where: TURRET_AT (a fixture or "X Y Z": an enemy's security turret that is
# on, within 40 m and in sight of V's front; default drake-ave, as
# turret-host).
# To look at: the turret's light turns green, it follows neither V nor the
# puppet in front of V and fires at neither; a marker in the other player's
# colour, "Hacked by NAME", over it while it lasts.
ensure host
at "${TURRET_AT:-drake-ave}"
# A run before may have left it friendly: hostile, and on, before the guest
# joins.
cmd device hostile turret
pause 2
mark
cmd device near turret
expect 'devices: nearest turret [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest turret [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "turret $turret"
cmd device on turret
expect 'devices: on [0-9a-f]+ SecurityTurretControllerPS at ' 10
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --device "$turret:SecurityTurretControllerPS:friendly,hacked@3" \
  --turret-seen "$turret@10" \
  --device "$turret:SecurityTurretControllerPS:hostile@40"
expect 'joined \(player [0-9]+' 30
expect "devices: player [0-9]+ operated $turret" 20
expect "devices: $turret SecurityTurretControllerPS turned for every V here: " 10
# The guest's copy says it has its V: ours, turned, does not take the puppet.
expect_peer "turret $turret: our copy has our V \(--turret-seen\)" 30
expect "turrets: player [0-9]+'s copy of $turret has its V: our turret takes its puppet \(false\)" 10
pause 5
query attitude device turret attitude
expect_value "$attitude" = friendly
query puppet device turret puppet
expect_value "$puppet" != hostile
query target device turret target
expect_value "$target" '~' '^(none|other)$'
shot turret-friendly
# The guest turns it back: its own side again.
expect "devices: $turret SecurityTurretControllerPS back to its own attitude for every V here" 40
query attitude_back device turret attitude
expect_value "$attitude_back" != friendly
reject 'watchdog: game thread silent'
