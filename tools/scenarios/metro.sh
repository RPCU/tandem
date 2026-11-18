# A partner rides the NCART away: no train copy, the puppet hidden with a notice, the skip, back at the exit station
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode metro --body female --metro-at 8 --metro-for 30
expect 'spawning ' 30
expect_peer 'metro: riding the NCART' 30
expect 'player [0-9]+: ride 1 ' 5
expect 'player [0-9]+: NCART station 5, next 6' 5
# No train of ours where it rides: out of sight, its marker carried along.
expect 'player [0-9]+: on the NCART away from us, hidden' 5
shot metro-hidden
cmd ride
expect 'ride: player [0-9]+ kind 1 .*mode 2, hidden true' 5
expect_peer 'metro: skipping' 30
expect 'player [0-9]+: NCART station 5, next 6, skipping' 5
expect 'player [0-9]+: NCART station 6, next 7' 10
expect_peer 'metro: off the NCART' 30
expect 'player [0-9]+: off the NCART \(station 6\)' 5
shot metro-off
cmd ride
expect 'ride: player [0-9]+ kind 0 .*mode 0, hidden false' 5
# A train is never copied, and the skip (ten times faster) is no jump.
reject 'copy spawning'
reject 'jumped to'
reject 'watchdog: game thread silent'
