# As the host: its NPCs' animation states reach a guest (states, weapon, workspot) and so do their shots
# shellcheck shell=bash disable=SC2317
ensure host
# Someone sitting, leaning or smoking: the nearest NPC in a workspot around
# NPCS_AT (a fixture or "X Y Z", default kabuki-market: its stalls, busy by
# day; testing.md "Fixtures"), V moved within sharing range of it. Rain and
# night empty the street: then this step fails, not the sync.
at "${NPCS_AT:-kabuki-market}"
pause 5
cmd npc seek 150
expect 'npc seek: [1-9][0-9]* in a workspot' 10
# The game's own instance map has it, and its resource (Codeware's
# GetWorkspotInstance found none: every NPC went with workspot 0).
expect 'npc seek: its workspot: found, resource [1-9][0-9]*, entry' 5
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --record .cache/npcs-anims.rec
expect_peer 'npc [0-9]+ animates: high [0-9]+, upper [0-9]+, stance [0-9]+' 30
expect_peer 'npc [0-9]+ animates: .*workspot 0x[0-9a-f]{6,}' 30
cmd npc anims
expect 'npc anims: [1-9][0-9]* sent, 0 followed'
reject 'watchdog: game thread silent'
