# Two games: the partner sets off an explosive barrel; it goes up on both screens, from the partner's puppet on ours, and stays gone on both
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a spot 10 to 40 m from a whole
# explosive barrel; V's flat has none, so name one).
# To look at and listen to: one blast on each screen, with its sound, at the
# same moment give or take a ping; the wreck left on both; neither V hurt
# at that distance.
ensure host
[ -n "${DEVICES_AT:-}" ] || fail "DEVICES_AT names no spot 10 to 40 m from a whole barrel (none is known near a fixture)"
at "$DEVICES_AT"
partner_join
cmd device near barrel
expect 'devices: nearest barrel [0-9a-f]+ [A-Za-z]+ at ' 10
barrel=$(since | grep -oE 'nearest barrel [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the barrel: $barrel"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pmark
pcmd device near barrel
expect_partner "devices: nearest barrel $barrel " 10
shot pair-barrel-before
mark
pmark
pcmd device explode barrel
expect_partner "devices: $barrel [A-Za-z]+ changed here \\([a-z]+, [a-z+]*exploded[a-z+]*, value -?[0-9]+\\): asked of the host" 10
expect "devices: player [0-9]+ operated $barrel" 10
# From the partner's puppet: its kills and its crime are the partner's.
expect "devices: $barrel [A-Za-z]+ set off by player [0-9]+.s puppet" 5
expect "devices: $barrel [A-Za-z]+ after player [0-9]+.s request: [a-z]+, [a-z+]*exploded" 10
pause 2
shot pair-barrel-blast
compare devices "$barrel"
# Gone on both: neither game finds it whole any more.
mark
pmark
cmd device near barrel
expect 'devices: (nearest barrel [0-9a-f]+ |no barrel within)' 10
reject "nearest barrel $barrel "
pcmd device near barrel
expect_partner 'devices: (nearest barrel [0-9a-f]+ |no barrel within)' 10
reject_partner "nearest barrel $barrel "
reject 'watchdog: game thread silent'
