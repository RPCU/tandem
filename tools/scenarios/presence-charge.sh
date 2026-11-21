# A player charges a tech rifle every 6 s, then fires: its puppet's weapon plays the charge's effects, fully charged, and lets them go with the shot
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --hold Items.Preset_Achilles_Default --charge 6
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect 'player [0-9]+: holds Items.Preset_Achilles_Default' 20
expect_peer 'charge step 1' 15
# The first charge may come while the rifle is still on its way to the
# puppet's hand (effects: false, 2026-11-21): the next one, 6 s later, has it.
expect 'charge: player [0-9]+.s puppet charges its weapon \([0-9]+ so far; the weapon.s charge effects: true\)' 20
expect 'charge: player [0-9]+.s weapon fully charged' 5
expect 'charge: player [0-9]+.s charge ended \(released\)' 5
expect_peer 'charged shot' 5
expect 'bullet: player [0-9]+.s shot 1 drawn from its puppet.s weapon' 5
# Said every 0.5 s while it lasts: never ended for want of news.
reject 'charge: player [0-9]+.s charge no longer said'
cmd presence report
expect 'charges [1-9]' 5
# Seen (a person there): the rifle's charge glow and hum on the puppet while
# it charges, brighter when full, gone with the shot.
reject 'watchdog: game thread silent'
