# Two games: the partner turns an enemy camera friendly; on both games it is friendly to both Vs and marked "Hacked by" on ours; turned back, it is hostile again on both
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": an enemy's camera within 40 m;
# default h11-basement, the Tyger Claws' cameras, hostile unless the gig is
# done).
# To look at: the camera's cone and lights go green on both screens, no
# "intruder detected" for either V, the partner's colour on our minimap's
# mark; then red again on both.
ensure host
at "${DEVICES_AT:-h11-basement}"
partner_join
cmd device near camera
expect 'devices: nearest camera [0-9a-f]+ [A-Za-z]+ at ' 10
camera=$(since | grep -oE 'nearest camera [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the camera: $camera"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pmark
pcmd device near camera
expect_partner "devices: nearest camera $camera " 10
query before device camera attitude
note "the camera to our V before: $before"
mark
pmark
# The partner's Friendly Mode: its copy at once, asked of us, ours turned.
pcmd device friendly camera
expect_partner "devices: $camera [A-Za-z]+ changed here \\([a-z]+, [a-z+]*friendly[a-z+]*, value -?[0-9]+\\): asked of the host" 10
expect "devices: player [0-9]+ operated $camera" 10
expect "devices: $camera [A-Za-z]+ turned for every V here: " 10
pause 3
query ours device camera attitude
expect_value "$ours" = friendly
query theirs device camera puppet
expect_value "$theirs" = friendly
pquery pours device camera attitude
expect_value "$pours" = friendly
pquery ptheirs device camera puppet
expect_value "$ptheirs" = friendly
# Marked on ours as the partner's hack (an enemy's device another V turned).
expect "devices: $camera [A-Za-z]+ marked: hacked by player [0-9]+" 10
shot pair-camera-friendly
compare devices "$camera"
# Back: hostile again on both, unmarked.
mark
pmark
pcmd device hostile camera
expect "devices: $camera [A-Za-z]+ back to its own attitude for every V here" 15
expect_partner "devices: $camera [A-Za-z]+ back to its own attitude for every V here" 15
expect "devices: $camera [A-Za-z]+ unmarked" 10
pause 3
query after device camera attitude
expect_value "$after" = "$before"
compare devices "$camera"
reject 'watchdog: game thread silent'
