# Two games: the partner is wanted; our game sends units and a car after its puppet, not after our V; the partner spawns none and its stars are its own; they stand down when its stars go
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: POLICE_AT (a fixture or "X Y Z": roads near V for a car's pursuit
# points; default drake-ave).
# To look at and listen to: on both screens the same officers and car, coming
# for the partner's V (sirens, the car out of its sight first), none on ours;
# the partner's stars on its own HUD only, flashing on our minimap's marker;
# officers walking off at the end.
ensure host
at "${POLICE_AT:-drake-ave}"
partner_join
# Our units fire on the wanted partner: it is not to die of it (a V that
# leaves its session down dies, revive.md; its next scenario found it dead).
pcmd god on
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
# The partner 12 m off our V: a stray round is less likely.
pat "$(awk -v x="$x" 'BEGIN { print x + 12.0 }') $y $z"
# Our V's health now, and the watch of its lowest from here.
query before health
mark
pmark
pcmd police report
expect_partner 'police report: our wanted level 0 .*we follow: our own spawns are off' 10
cmd police report
expect 'police report: our wanted level 0 .*we answer the others. heat' 10
# The partner is wanted at two stars (the quests' own request).
pcmd police heat 2
expect_partner 'police: our wanted level 2 ' 15
expect 'police: player [0-9]+ wanted level 0 -> 2' 15
expect 'police: unit .* out for player [0-9]+' 40
expect 'police: car .* chasing player [0-9]+' 60
pause 10
cmd police report
expect 'police report: our wanted level 0 .* [1-9][0-9]* unit\(s\) out for the others \(0 on our V\)' 10
pcmd police report
expect_partner 'police report: our wanted level [1-5] .*we follow: our own spawns are off' 10
shot pair-police-chase
reject 'police: our wanted level [1-5] '
reject 'police: our V attacked a unit'
# The partner's stars go: our units and car stand down, nobody is wanted.
mark
pmark
pcmd police calm
expect_partner 'police: our wanted level 0 ' 60
expect 'police: player [0-9]+ wanted level 2 -> 0' 15
expect 'police: unit for player [0-9]+ stands down' 10
reject 'police: our wanted level [1-5] '
query low health low
expect_changed "$before" "$low" same
reject 'watchdog: game thread silent'
