# As host: the NPCs near a V go 30 times a second with their velocity, the others 10 (netcode.md, NPC motion)
# shellcheck shell=bash disable=SC2317
ensure host
# A busy street: NPCs within 35 m of our V and beyond. NPCS_AT: a fixture or
# "X Y Z [YAW]" (testing.md, "Fixtures").
at "${NPCS_AT:-kabuki-market}"
pause 5
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female
expect_peer 'in the session' 20
pause 12
cmd npc count
expect 'npcs shared: [1-9]'
# The nearest are heard 20 to 39 times a second (10 before protocol 21).
expect_peer 'npc states: [0-9]+ NPCs, (2[0-9]|3[0-9]) a second for the most heard' 15
cmd perf
expect 'traffic: up' 10
reject 'watchdog: game thread silent'
