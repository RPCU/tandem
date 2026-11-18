# As the host: its NPCs' animation states reach a guest (states, weapon, workspot) and so do their shots
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --record .cache/npcs-anims.rec
expect_peer 'npc [0-9]+ animates: high [0-9]+, upper [0-9]+, stance [0-9]+' 30
# Someone sitting, leaning or smoking near V (Watson market, a bar): its workspot.
expect_peer 'npc [0-9]+ animates: .*workspot 0x[0-9a-f]{6,}' 30
cmd npc anims
expect 'npc anims: [1-9][0-9]* sent, 0 followed'
reject 'watchdog: game thread silent'
