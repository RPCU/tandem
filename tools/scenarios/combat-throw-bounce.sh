# A guest's grenade bounces off something our game does not have: its copy here is put back on the real one's path and explodes where the real one did
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female --throw 6 --throw-bounce
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect_peer 'we throw grenade [0-9]+' 20
expect "throws: player [0-9]+'s grenade [0-9]+ \(Items.GrenadeFragRegular\) launched from their puppet" 10
expect_peer 'our grenade [0-9]+ bounced: its path is said' 5
expect "throws: player [0-9]+'s throw [0-9]+ was [0-9.]+ m off the real one's path: put on it \(flying on\)" 5
expect "throws: player [0-9]+'s throw [0-9]+ ended here \(0\)" 10
cmd throws report
expect 'throws: ours in flight.*put back on their real path [1-9]' 5
# The copy lives until the real one's end (it was destroyed at its launch).
reject 'its copy gone before its end'
# Seen (a person there): the copy leaves its arc a second in, flies on from
# 3 m to the side and explodes there, with no pause in the air.
reject 'watchdog: game thread silent'
