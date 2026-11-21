# A V never loses a limb or limps: every way to cut or cripple the guest's puppet (request, event, explosion, crippled leg) is refused, and an area hit on our V cuts nothing
# shellcheck shell=bash disable=SC2317
# Friendly fire on: the guest's grenade reaches our V (off, the default, it never does).
ensure host friendly_fire=on
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --area-hit 3
expect 'player [0-9]+: puppet in the tandem_player' 60
expect 'puppet [0-9]+: controller handed to the engine' 30
cmd puppet limbs
expect 'puppet limbs: dismemberment request, event, explosion and a crippled leg sent to the puppet' 10
expect "outcomes: player [0-9]+'s puppet keeps its limbs \(request, part 0\)" 5
expect "outcomes: player [0-9]+'s puppet keeps its limbs \(event\)" 5
expect "outcomes: player [0-9]+'s puppet keeps its limbs \(explosion\)" 5
expect "outcomes: player [0-9]+'s puppet keeps its limbs \(status effect BaseStatusEffect.CrippledLegLeft\)" 5
# The guest's grenade on our V: our game applies it, nothing comes off.
expect "outcomes: player [0-9]+'s area effect on our V: 20(\.0+)? physical" 15
reject 'loses part|cuts part'
reject 'watchdog: game thread silent'
