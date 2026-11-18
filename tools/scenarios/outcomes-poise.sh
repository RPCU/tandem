# A guest's strong melee hits break the host's NPC's guard: the poise it took is applied there and comes back to the guests
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female --hit-npcs 1 --strike poise
expect_peer 'hit npc [0-9]+ \(poise\): 10 health, 40 poise' 30
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health, 40(\.0+)? poise' 15
expect_peer 'npc [0-9]+ poise [0-9]+%' 10
reject 'watchdog: game thread silent'
