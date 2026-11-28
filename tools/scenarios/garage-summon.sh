# A partner's summoned car is seen from its arrival: it drives in on its own (one copy, on its owner's stream), parks where its owner's game has it, is summoned again (the same copy moves), and its owner gets into that copy (no second one)
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body female \
  --summon 5 --summon-again 15 --summon-board 10
# Metres across the ground between our copy ("x y z", test query summons
# at) and where the mock player's own car parked (its last "parked by us
# at (x, y, z)" line): the two games' places of one car.
apart() {
  local mock
  mock=$(grep -E 'summon: our car parked by us at' "$peer_log" | tail -1 |
    sed -E 's/.*at \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3/')
  [ -n "$mock" ] || fail "the mock player said no parked place"
  awk -v a="$1" -v b="$mock" 'BEGIN { split(a, p, " "); split(b, q, " ");
    printf "%.2f\n", sqrt((p[1] - q[1]) ^ 2 + (p[2] - q[2]) ^ 2) }'
}
expect_peer 'summon: our car Vehicle\.[A-Za-z0-9_]+ drives in from 50 m' 40
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+: copy spawned, driving in" 15
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ follows its owner's stream, driving in" 10
# Driving in: the copy is where its owner's stream is drawn, and alone.
pause 2
query lag summons lag
expect_value "$lag" '<' 1.0
query copies summons copies
expect_value "$copies" = 1
shot summon-arriving
expect_peer 'summon: our car parked by us' 20
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ parked here" 10
pause 3
# Parked: where its owner's game has it, on both games.
query off summons off
expect_value "$off" '<' 1.0
query at summons at
gap=$(apart "$at")
note "parked copy $gap m from the mock player's car"
expect_value "$gap" '<' 1.0
query copies summons copies
expect_value "$copies" = 1
# Arrived: honked and flashed on our copy, once (the game's own arrival).
query arrivals summons arrivals
expect_value "$arrivals" = 1
shot summon-parked
# Summoned again: the same car comes from the other side, the same copy
# moves (a teleport out of the way, then it drives in).
expect_peer 'summon: our car .* drives in from 50 m \(summoned again\)' 30
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ follows its owner's stream, driving in" 10
pause 2
query lag summons lag
expect_value "$lag" '<' 1.0
expect "summons: player [0-9]+'s Vehicle\.[A-Za-z0-9_]+ parked here" 20
pause 3
query at summons at
gap=$(apart "$at")
expect_value "$gap" '<' 1.0
query copies summons copies
expect_value "$copies" = 1
query arrivals summons arrivals
expect_value "$arrivals" = 2
# Its owner gets in: its puppet sits in that copy, none spawns.
expect_peer 'summon: we get in our car' 20
expect "player [0-9]+: drives Vehicle\.[A-Za-z0-9_]+, its parked copy" 15
expect 'seated in seat_front_left' 15
pause 2
query seated summons seated
expect_value "$seated" = yes
query copies summons copies
expect_value "$copies" = 1
shot summon-boarded
reject 'copy spawning'
reject 'summons: .*no copy could be spawned'
reject 'watchdog: game thread silent'
