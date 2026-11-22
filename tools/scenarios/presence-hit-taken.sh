# As a guest: each hit the host's V takes from an NPC is shown on its puppet as the hit it was, through the game's own pipeline, with no flinch of ours for the fall of its health; a hit by nobody known twitches the body
# shellcheck shell=bash disable=SC2317
ensure join
# The mock host's V is hit every 4 s by the NPC it runs (our proxy of it): a
# bullet on its left leg, from the east.
peer host --mode front --body female --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --hurt 4 --hurt-hit npc
expect 'in the session as player' 30
expect 'controller handed to the engine' 30
expect_peer 'our V took a hit on its left leg: by npc [0-9]+' 20
# Queued on the puppet with the proxy as its source: the hit reaction
# component picked the reaction (the direction is the hit's way against the
# body, the part its left leg: 5). `twitches` here instead would say what
# the pipeline did not play, and why.
expect 'hit taken: player [0-9]+.s puppet reacts by the game.s pipeline \(twitch, direction [0-4], part 5' 15
# The health's fall is that hit: its voice, no flinch of ours.
expect 'audio: player [0-9]+ hurt \(voice onPlayerHit\)' 10
pause 6
reject 'reaction: player [0-9]+.s puppet flinches'
reject 'the game.s pipeline played nothing'
shot presence-hit-taken
# A hit by nobody both games know: the body's own twitch, from the hit's way.
cmd presence hit
expect 'hit taken: player [0-9]+.s puppet twitches \(direction [1-4], part 5, intensity 1\): by nobody the others know' 5
cmd presence report
expect 'hit taken: ours told [0-9]+ .*; the others. shown by the game.s pipeline [1-9]' 5
reject 'watchdog: game thread silent'
