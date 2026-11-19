# Another player busy (a workspot, then photo mode): its puppet is held where it sat and the notices say photo mode; free again, it moves on
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --activity workspot --activity-at 6 --activity-for 12
expect 'joined \(player [0-9]+' 20
expect 'player [0-9]+: busy 1 \(flags 0x1' 20
expect 'activities: player [0-9]+ is busy in a workspot' 5
cmd busy report
expect 'held true' 5
expect 'activities: player [0-9]+ is busy nothing \(was in a workspot\)' 20
pause 3
cmd busy report
expect 'activities: ours nothing.*0 busy puppet' 5
# Ours: photo mode as a test kind, seen by the mock player.
cmd busy photo
expect 'activities: our V is busy in photo mode' 5
expect_peer 'player 1 is busy: photo mode' 10
cmd busy off
expect_peer 'player 1 is free again' 10
