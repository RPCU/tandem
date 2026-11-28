# A partner's summoned car is seen from its arrival: it drives in on its own (one copy, following its owner's stream), parks, is summoned again (the same copy moves), and its owner gets into that copy (no second one)
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body female \
  --summon 5 --summon-again 15 --summon-board 10
expect_peer 'summon: our car Vehicle\.[A-Za-z0-9_]+ drives in from 50 m' 40
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+: copy spawned, driving in" 15
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ follows its owner's stream, driving in" 10
shot summon-arriving
expect_peer 'summon: our car parked by us' 20
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ parked here" 10
pause 2
shot summon-parked
cmd garage cars
expect 'garage cars: .*copies spawned 1, adopted 0' 10
# Summoned again: the same car comes from the other side, the same copy
# moves (a teleport out of the way, then it drives in).
expect_peer 'summon: our car .* drives in from 50 m \(summoned again\)' 30
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ follows its owner's stream, driving in" 10
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ parked here" 20
cmd garage cars
expect 'garage cars: .*copies spawned 1, adopted 0' 10
# Its owner gets in: its puppet sits in that copy, none spawns.
expect_peer 'summon: we get in our car' 20
expect "player [0-9]+: drives Vehicle\.[A-Za-z0-9_]+, its parked copy" 15
expect 'seated in seat_front_left' 15
shot summon-boarded
reject 'copy spawning'
reject 'summons: .*no copy could be spawned'
reject 'watchdog: game thread silent'
