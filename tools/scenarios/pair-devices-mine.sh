# Two games: the partner sets off a placed trap (a mine: ExplosiveTriggerDevice); it goes up on both screens, from the partner's puppet on ours, and stays gone on both
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a spot 10 to 40 m from a
# placed trap still whole; unset: found in game, find_spot mine 10 40).
# To look at and listen to: one blast on each screen, with its sound, at the
# same moment give or take a ping; the wreck left on both; neither V hurt
# at that distance.
ensure host
at_device DEVICES_AT mine 10 40
partner_join
cmd device near mine
expect 'devices: nearest mine [0-9a-f]+ [A-Za-z]+ at ' 10
mine=$(since | grep -oE 'nearest mine [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the mine: $mine"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pmark
pcmd device near mine
expect_partner "devices: nearest mine $mine " 10
shot pair-mine-before
mark
pmark
pcmd device explode mine
expect_partner "devices: $mine [A-Za-z]+ changed here \\([a-z]+, [a-z+]*exploded[a-z+]*, value -?[0-9]+\\): asked of the host" 10
expect "devices: player [0-9]+ operated $mine" 10
# From the partner's puppet: its kills and its crime are the partner's.
expect "devices: $mine [A-Za-z]+ set off by player [0-9]+.s puppet" 5
expect "devices: $mine [A-Za-z]+ after player [0-9]+.s request: [a-z]+, [a-z+]*exploded" 10
pause 2
shot pair-mine-blast
compare devices "$mine"
# Gone on both: neither game finds it whole any more.
mark
pmark
cmd device near mine
expect 'devices: (nearest mine [0-9a-f]+ |no mine within)' 10
reject "nearest mine $mine "
pcmd device near mine
expect_partner 'devices: (nearest mine [0-9a-f]+ |no mine within)' 10
reject_partner "nearest mine $mine "
reject 'watchdog: game thread silent'
