# The host shares its NPCs: a joining player records spawns and states
# shellcheck shell=bash disable=SC2317
ensure host
# NPCs to share whatever the street holds (rain and night empty it).
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
mkdir -p recordings
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --record recordings/npcs.rec
expect_peer 'in the session' 20
expect_peer 'recorded the spawn of npc [0-9]+' 20
pause 20
cmd npc count
expect 'npcs shared: [1-9]'
expect_peer 'recorded [1-9][0-9]* NPC messages' 20
