# As the host: its NPCs' animation states reach a guest (states, weapon, workspot) and so do their shots
# shellcheck shell=bash disable=SC2317
ensure host
# Someone sitting, leaning or smoking: the nearest NPC in a workspot around
# V's flat (Watson, busy by day), V moved within sharing range of it. Rain
# and night empty the street: then this step fails, not the sync.
cmd teleport home
pause 8
cmd npc seek 150
expect 'npc seek: [1-9][0-9]* in a workspot' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --record .cache/npcs-anims.rec
expect_peer 'npc [0-9]+ animates: high [0-9]+, upper [0-9]+, stance [0-9]+' 30
expect_peer 'npc [0-9]+ animates: .*workspot 0x[0-9a-f]{6,}' 30
cmd npc anims
expect 'npc anims: [1-9][0-9]* sent, 0 followed'
reject 'watchdog: game thread silent'
