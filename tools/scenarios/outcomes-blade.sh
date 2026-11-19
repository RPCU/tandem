# A guest's blade on the host's NPC: its hit reaction on the left arm, and the limb it loses on the kill goes to every proxy
# shellcheck shell=bash disable=SC2317
ensure host
# Earlier scenarios may have killed everyone around V: three gangers to share.
cmd npc spawn 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female --hit-npcs 1 --strike blade
expect_peer 'hit npc [0-9]+ \(blade\): 35 health, 0 poise, flags 0x104' 30
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health, 0(\.0+)? poise, flags 260, zone 2' 15
expect 'npcs: [0-9]+ died \(killer: player ([2-9]|[1-9][0-9]+)\)' 60
# The host's hit reaction cut it (ForceDismember): the guests are told.
expect 'outcomes: [0-9]+ loses part [0-9]' 10
expect_peer 'npc [0-9]+ loses part [0-9]' 10
reject 'watchdog: game thread silent'
