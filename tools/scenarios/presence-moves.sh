# A player jumps, lands, climbs a ladder and swings a blade: each move is heard on its puppet (foot plants, landing efforts, the swing and its impact)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode course --body male \
  --hold Items.Preset_Katana_Default --melee 4
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect_peer 'course: jump \(air\)' 20
expect 'audio: player [0-9]+ jump \(foot planted\)' 15
expect 'audio: player [0-9]+ (very hard |hard )?landing \(voice (regular|hard|veryhard)Landing\)' 15
expect 'audio: player [0-9]+ climb \(voice climbStart\)' 30
# The blade: the swing's effort, then its impact.
expect_peer 'melee swing' 10
expect 'audio: player [0-9]+ swing \(voice meleeAttack\)' 10
expect 'audio: player [0-9]+ melee hit \(the weapon.s own MeleeHit effects' 10
cmd presence report
expect 'presence: player [0-9]+: health' 5
# Heard (a person there): the take-off and the landing, harder from higher;
# rungs on the ladder; nothing in the water; the katana's swing and its hit.
reject 'watchdog: game thread silent'
