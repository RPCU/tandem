# A dual wielding guest's off-hand shots hit the host's NPC: strikes like the right hand's (the same path, the same rules), made with the weapon in its puppet's left hand
# shellcheck shell=bash disable=SC2317
ensure host
# Earlier scenarios may have killed everyone around V: three gangers to share.
cmd npc spawn 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female \
  --hold Items.Preset_Lexington_Default --hold-left Items.Preset_Unity_Default --hit-npcs 2 --strike offhand
expect 'player [0-9]+: dual wielding, a weapon in each hand' 30
expect_peer 'hit npc [0-9]+ \(offhand\): 35 health' 30
# (A strike before the pistols were in the puppet's hands is its active
# weapon's, and says so.)
expect 'outcomes: [0-9]+ struck by player [0-9]+ with its puppet.s left weapon \(Items.Preset_Unity_Default\)' 20
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health' 10
expect_peer 'npc [0-9]+ health' 10
reject 'watchdog: game thread silent'
