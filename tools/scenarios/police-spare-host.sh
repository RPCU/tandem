# As host: units we spawn for a wanted guest hunt its puppet 12 m off and never take our V on (neutral to it, never their threat): our V's health stays whole; they stand down with the guest's stars
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
# A street (POLICE_AT, as police-host-cars): room around the puppet.
at "${POLICE_AT:-drake-ave}"
# Our V's health now, and the watch of its lowest from here.
query before health
# The puppet 12 m off our V (mirror: our V's moves, offset), so the units'
# fire at it is not fire at our V.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset 12,0,0 --body female --heat 3 --heat-at 8 --heat-for 50
expect 'joined \(player [0-9]+' 20
expect 'police: player [0-9]+ wanted level 0 -> 3' 30
expect 'police: unit .* out for player [0-9]+' 40
# They hunt the puppet a while; none is on our V.
pause 20
cmd police report
expect 'police report: our wanted level 0 .* [1-9] unit\(s\) out for the others \(0 on our V\)' 10
shot police-spare-host
reject 'police: our wanted level [1-5] '
reject 'police: our V attacked a unit'
# The guest's stars go: the units stand down.
expect 'player [0-9]+: wanted level 0 ' 60
expect 'police: unit for player [0-9]+ stands down' 10
query low health low
expect_changed "$before" "$low" same
reject 'watchdog: game thread silent'
