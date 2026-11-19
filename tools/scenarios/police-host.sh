# As host: a wanted guest is chased by police we spawn, hostile to its puppet and not to our V; they stand down when its stars go or it vanishes
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --heat 2 --heat-at 8 --heat-for 40
expect 'joined \(player [0-9]+' 20
cmd police report
expect 'police report: our wanted level 0 .*we answer the others. heat' 10
# The guest is wanted at two stars: we are not, and our game sends units
# after its puppet (the game's own spawn points, on foot).
expect 'player [0-9]+: wanted level 2 ' 30
expect 'police: player [0-9]+ wanted level 0 -> 2' 5
expect 'police: [0-9]+ unit\(s\) asked for player' 15
expect 'police: unit .* out for player [0-9]+' 30
cmd police report
expect 'police report: our wanted level 0 .* [1-9] unit\(s\) out for the others' 10
shot police-host-chase
# Our own V is still clean: no star of ours, however long they shoot at
# the puppet next to us.
pause 10
reject 'police: our wanted level [1-5] '
# The guest's stars go (unseen, then gone): the units stand down.
expect 'player [0-9]+: wanted level 0 ' 60
expect 'police: unit for player [0-9]+ stands down' 10
# Wanted again, then gone without a goodbye in the middle of the chase.
mark
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --heat 3 --heat-at 5 --heat-for 120 --vanish-at 30
expect 'player [0-9]+: wanted level 3 ' 40
expect 'police: unit .* out for player [0-9]+' 30
expect_peer 'vanish' 40
expect 'player [0-9]+: wanted level 0 ' 15
expect 'police: unit for player [0-9]+ stands down' 10
reject 'watchdog: game thread silent'
