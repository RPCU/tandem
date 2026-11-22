# A dual wielding player (a pistol in each hand) shoots every 2 s, each hand in turn, each bullet ricocheting, then holsters and draws both: its puppet holds both, is dual wielding by the game's own test, and each hand's shot and bounce leave that hand's weapon
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --hold Items.Preset_Lexington_Default --hold-left Items.Preset_Unity_Default --shoot 2 --ricochet --holster-at 45
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect_peer 'holding Items.Preset_Lexington_Default / Items.Preset_Unity_Default \(dual wielding\)' 5
# Both hands, from the one Held (a late joiner's puppet gets the same).
expect 'player [0-9]+: holds [A-Za-z_.]+ / Items.Preset_Unity_Default' 20
expect 'player [0-9]+: dual wields \(' 5
expect 'player [0-9]+: dual wielding, a weapon in each hand \(Items.Preset_Lexington_Default / Items.Preset_Unity_Default\)' 15
# The game's own state, not our word: both slots, both weapons a pair.
query held held puppet
expect_value "$held" = 'Items.Preset_Lexington_Default Items.Preset_Unity_Default dual'
# The left hand's shot: its own effects, its bullet from its own muzzle, its
# bounce on from where that bullet ended (the right hand's are the others).
expect_peer 'gunshot \(left hand\)' 10
expect 'audio: player [0-9]+ shot with its left hand.s weapon' 10
expect 'bullet: player [0-9]+.s left hand.s shot 1 drawn from its puppet.s left weapon \(Items.Preset_Unity_Default\)' 10
expect 'bullet: player [0-9]+.s ricochet [0-9]+ drawn on to .*\(flags 1\)' 10
expect 'bullet: player [0-9]+.s ricochet [0-9]+ drawn on to .*\(flags 0\)' 10
cmd presence report
expect 'left hand.s bullets [1-9]' 5
# --holster-at: both away (no pair any more), then both drawn again.
expect 'player [0-9]+: no longer dual wields' 60
query away held puppet
expect_value "$away" = 'none none single'
mark
expect 'player [0-9]+: dual wields \(' 15
expect 'player [0-9]+: dual wielding, a weapon in each hand' 15
# Seen (a person there): a pistol in each hand of the puppet, the muzzle
# flash, the shell and the tracer alternating between the two (never both
# from the right hand), each slide cycling on its own; both gone when
# holstered. The mock sends no pose: the arms are the NPC stance's, not a
# dual wielding V's (that needs a real second game or `--mode mirror`).
reject 'watchdog: game thread silent'
