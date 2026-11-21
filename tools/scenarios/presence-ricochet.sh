# A player shoots past V every 3 s and each bullet ricochets: the tracer is drawn from its puppet's muzzle to the bounce, then the ricochet's leg to what it hit
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --hold Items.Preset_Lexington_Default --shoot 3 --ricochet
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect 'player [0-9]+: holds Items.Preset_Lexington_Default' 20
expect_peer 'ricochet' 15
# The shot: the drawn bullet (impact, decal, sounds) and the weapon's own
# hitscan trail from the muzzle (Tracers.reds).
expect 'bullet: player [0-9]+.s shot 1 drawn from its puppet.s weapon .*trail true' 20
# The bounce: the weapon's ricochet trail on from where the shot ended.
expect 'bullet: player [0-9]+.s ricochet 1 drawn on to' 10
# From here on every ricochet has its shot (one fired before the weapon
# was in the puppet's hand has none, and is rightly not drawn: the puppet
# was built again as the mock joined, 2026-11-21).
mark
expect 'bullet: player [0-9]+.s ricochet [0-9]+ drawn on to' 10
reject 'ricochet came with no shot of theirs just before'
cmd presence report
expect 'bullets drawn [1-9][0-9]* \(trails [1-9][0-9]*, ricochets [1-9]' 5
# Seen (a person there): one tracer per shot, not two (else `cmd presence
# tracer bullet` or `tracer trail`), from the muzzle to the point 6 m past
# V, then a ricochet's streak 3 m aside and up; nothing takes damage.
reject 'watchdog: game thread silent'
