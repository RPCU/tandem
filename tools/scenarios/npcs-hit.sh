# A guest hits and burns the host's NPCs: the host applies the damage and the effect; health, death and kill credit come back
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --hit-npcs 1
expect_peer 'hit npc [0-9]+ \(plain\): 35 health' 30
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health' 15
expect 'npcs: [0-9]+ gets BaseStatusEffect.Burning from player [0-9]+' 10
# Its burn damage is the guest's to compute: the host's own ticks deal none.
expect "combat: a guest's own hit on [0-9]+ left to their game" 10
expect_peer 'npc [0-9]+ health [0-9]+%' 15
expect 'npcs: [0-9]+ died \(killer: player [2-9]\)' 60
expect_peer 'npc [0-9]+ died' 10
reject 'watchdog: game thread silent'
