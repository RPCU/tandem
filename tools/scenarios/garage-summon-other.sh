# A partner drives its summoned car, gets out and summons another: the old copy stands while our V sees it, then goes once out of sight, as the garage takes its own; the new car's copy is the only one of its record
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body female \
  --drive-radius 80 --summon 5 --summon-board 5 --drive-seconds 10 --summon-other 5
expect_peer 'summon: our car Vehicle\.[A-Za-z0-9_]+ drives in from 50 m' 40
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+: copy spawned, driving in" 15
expect_peer 'summon: our car parked by us' 30
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ parked here" 10
# Its owner drives it (its puppet in that copy), then gets out 80 m away.
expect_peer 'summon: we get in our car' 20
expect "player [0-9]+: drives Vehicle\.[A-Za-z0-9_]+, its parked copy" 15
expect_peer 'out of the car' 30
# Another car of theirs: the first one's copy is old.
expect_peer 'summon: our other car Vehicle\.[A-Za-z0-9_]+ drives in from 50 m' 20
expect "summons: player [0-9]+ summoned another car \(Vehicle\.[A-Za-z0-9_]+\): the old copy goes once out of sight" 10
cmd garage cars face old
expect 'garage cars: our V faces toward the old copy' 10
expect_peer 'summon: our other car parked by us' 30
# Seen, beyond the distance and free for longer than the wait: it stands.
pause 8
query inview summons oldseen
expect_value "$inview" = yes
query old summons old
expect_value "$old" = 1
query oldcopies summons oldcopies
expect_value "$oldcopies" = 1
shot summon-other-seen
# Out of sight: it goes, the new car's copy stays, alone of its record.
cmd garage cars face away
expect 'garage cars: our V faces away from the old copy' 10
expect "summons: player [0-9]+'s old Vehicle\.[A-Za-z0-9_]+ removed out of sight" 10
pause 2
query old summons old
expect_value "$old" = 0
query removed summons removed
expect_value "$removed" = 1
query oldcopies summons oldcopies
expect_value "$oldcopies" = 0
query copies summons copies
expect_value "$copies" = 1
shot summon-other-gone
reject 'summons: .*no copy could be spawned'
reject 'watchdog: game thread silent'
