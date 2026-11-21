# A guest's second chance (Relic, Sensory Protocol) asks the host to wipe its NPC's memory: the host checks whether its NPC sees the guest's puppet first
# shellcheck shell=bash disable=SC2317
ensure host
# NPCs to hack whatever the street holds (rain and night empty it).
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --hack-npcs 6 --hack-effect BaseStatusEffect.MemoryWipeOnDodge
expect_peer 'upload on npc [0-9]+ done: effect BaseStatusEffect.MemoryWipeOnDodge' 30
expect "reactions: player [0-9]+'s second chance on [0-9]+ refused: it still sees them|npcs: [0-9]+ gets BaseStatusEffect.MemoryWipeOnDodge from player [0-9]+" 10
# To check by eye, with the host's V in combat with that NPC: after a wipe,
# "stealth: N forgot one V, still hunts M" and the host's V stays in combat.
reject 'watchdog: game thread silent'
