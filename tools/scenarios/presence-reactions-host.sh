# As host: a guest's hits make our NPCs react, and each reaction they play (and their death) is sent to the guests
# shellcheck shell=bash disable=SC2317
ensure host
# Earlier scenarios may have killed everyone around V: three gangers to hit.
cmd npc spawn 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --hit-npcs 1
expect 'joined \(player [0-9]+' 20
expect_peer 'hit npc [0-9]+ \(plain\): 35 health' 30
expect 'outcomes: [0-9]+ struck by player [0-9]+' 15
expect 'reaction: npc [0-9]+ plays (twitch|impact|stagger|pain|knockdown|ragdoll|death) \(sent to the games linked with us' 20
# Three hits of 35 kill it: its death animation or ragdoll goes out too.
expect 'npcs: [0-9]+ died' 60
expect_peer 'npc [0-9]+ reacts: type [2-7]' 20
reject 'watchdog: game thread silent'
