# A player walks round V, shoots, holsters, draws and is hurt: its puppet posts the game's own 3D audio events, the bullet is drawn, it flinches
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female \
  --hold Items.Preset_Lexington_Default --shoot 3 --hurt 4 --holster-at 14
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect 'presence: our V.s health [0-9]+ % and flags [0-9]+ sent' 10
# Footsteps: the body's own locomotion clip raises them, or ours are planted.
expect 'audio: player [0-9]+ footstep (1 \(foot planted|from its own animation)' 20
# The shot: the weapon's effects, then its bullet to where the mock player
# says it ended.
expect 'player [0-9]+: holds Items.Preset_Lexington_Default' 20
expect_peer 'gunshot' 10
expect 'audio: player [0-9]+ shot \(the weapon.s own shoot effects' 10
expect 'bullet: player [0-9]+.s shot 1 drawn from its puppet.s weapon' 10
# Hurt: the pain voice, the twitch and the flinch; its place stays its player's.
expect_peer 'our V: health 75 %' 15
expect 'audio: player [0-9]+ hurt \(voice onPlayerHit\)' 10
expect 'reaction: player [0-9]+.s puppet flinches' 5
# Holster and draw.
expect_peer 'weapon holstered' 20
expect 'audio: player [0-9]+ holster \(unequip notification' 10
expect_peer 'weapon drawn' 10
expect 'audio: player [0-9]+ draw' 10
cmd presence report
expect 'presence: player [0-9]+: health [0-9]+ %, flags 0; footsteps ours [0-9]+, its animation.s [0-9]+; other sounds [1-9]' 5
# Heard and seen (a person there): footsteps on the right surface at the gait
# walked; the report of the right gun, 3D, quieter behind a wall; the tracer
# and the impact with its decal behind V; the grunt and the flinch.
reject 'watchdog: game thread silent'
