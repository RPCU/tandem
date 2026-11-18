# A guest's non-lethal hits knock the host's NPC out, not dead: the host's defeat mechanic decides, the guest's proxy goes down
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female --hit-npcs 1 --strike nonlethal
expect_peer 'hit npc [0-9]+ \(nonlethal\): 35 health, 0 poise, flags 0x14' 30
# The guest's puppet hits it, its own reaction plays, its health drops.
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health, 0(\.0+)? poise, flags 20' 15
expect 'npcs: [0-9]+ knocked out \(by player [2-9]\)' 60
expect_peer 'npc [0-9]+ knocked out \(by player [2-9]\)' 10
reject 'npcs: [0-9]+ died'
reject 'watchdog: game thread silent'
