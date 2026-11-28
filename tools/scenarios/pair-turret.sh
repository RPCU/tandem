# Two games: the host's turret fires at the partner's V: the partner's copy locks it, says so, and fires the host's shots for real; turned friendly by the partner it fires at nobody on either game
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: TURRET_AT (a fixture or "X Y Z": a sentry, a security turret that
# is not the police's, within 40 m, as `device friendly` and the query
# take it; unset: found in game, find_spot sentry 10 40).
# To look at and listen to: both copies of the turret turn to the partner's
# V together and fire with their sounds; the partner is hurt by its own
# copy's shots only; green and silent on both once turned.
ensure host
at_device TURRET_AT sentry 10 40
partner_join
cmd device near sentry
expect 'devices: nearest sentry [0-9a-f]+ [A-Za-z]+ at ' 10
turret=$(since | grep -oE 'nearest sentry [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the turret: $turret"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 2.0 }') $y $z"
mark
pmark
# The partner's copy takes the partner's V: said, the host's turret takes
# its puppet and fires; the partner's copy fires those shots at its V.
pcmd turret provoke
expect_partner "turrets: our copy of $turret has our V: said to the host" 15
expect "turrets: player [0-9]+'s copy of $turret has its V: our turret takes its puppet \\(true\\)" 10
expect "turrets: our turret $turret fires at [0-9a-f]+: told to the guests" 20
expect_partner "turrets: the host's turret $turret fires at our V: our copy's real shot" 10
reject_partner "turrets: the host is silent about $turret"
shot pair-turret-firing
# Turned friendly by the partner: no V is its target on either game.
mark
pmark
pcmd device friendly sentry
expect "devices: player [0-9]+ operated $turret" 10
expect "devices: $turret [A-Za-z]+ turned for every V here: " 10
pause 4
query target device sentry target
expect_value "$target" '~' '^(none|other)$'
pquery ptarget device sentry target
expect_value "$ptarget" '~' '^(none|other)$'
compare devices "$turret"
# Back to its own side for the next test.
pcmd device hostile sentry
expect "devices: $turret [A-Za-z]+ back to its own attitude for every V here" 15
reject 'watchdog: game thread silent'
