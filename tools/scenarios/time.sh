# A V in a Sandevistan slows only the NPCs around it, for everyone; the host's own dilation never slows the shared world
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# An NPC 12 m ahead, neutral to our V: shared with the mock player, inside its bubble.
cmd npc hostile puppets
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --sandevistan-at 12 --sandevistan-for 5 --sandevistan-scale 0.25
expect 'joined \(player [0-9]+' 20
expect_peer 'sandevistan on' 30
expect 'player [0-9]+: time abilities 0x1 \(world speed 0.25\)' 5
expect 'time: player [0-9]+ in a sandevistan' 5
expect 'time: bubble of player [0-9]+: [1-9][0-9]* NPC' 5
expect 'time: npc [0-9]+ at 0.25' 5
# Our V stands in that bubble with no ability of its own: slowed like the NPCs.
expect 'time: our V slowed to 0.25' 5
cmd time report
expect 'time: world speed here 1' 5
expect_peer 'sandevistan off' 10
expect 'time: player [0-9]+.s sandevistan ended' 5
expect 'time: bubble of player [0-9]+ ended' 5
expect 'time: npc [0-9]+ back to normal speed' 5
expect 'time: our V back to its own speed' 5
# The host's own dilation (time_host = full, the default): its own game
# slows as in single player, the NPC it shares is set to the bubble's speed
# whatever the game's own factor, and the mock player hears of it.
cmd time test 0.25 4
expect 'time: our abilities 1 \(world speed 0.25' 5
expect 'time: world speed 0\.2[0-9]* over the test' 5
expect 'time: npc [0-9]+ at 0\.25[0-9]* .*ignoring our global factor true' 5
cmd time report
expect 'time: what the game.s fire-rate formula reads .*sandevistan active true, factor 0\.2' 5
expect 'time: bubble of player 1: [1-9][0-9]* NPC' 5
expect_peer 'player 1: time abilities 0x1 \(world speed 0.25\)' 5
expect 'time: test dilation over' 10
expect 'time: our abilities 0 ' 5
expect_peer 'player 1: time abilities 0x0' 5
# Two Vs in a Sandevistan at once: one bubble each, the NPC at the strongest.
# The mock player's is the weaker (0.5): our V is slowed by it, until its
# own, stronger one (0.25) starts: then it stands in the bubble unslowed.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --sandevistan-at 8 --sandevistan-for 12 --sandevistan-scale 0.5
expect_peer 'sandevistan on' 40
expect 'time: bubble of player [2-9]: [1-9][0-9]* NPC' 5
expect 'time: npc [0-9]+ at 0.5' 5
expect 'time: our V slowed to 0.5' 5
mark
cmd time test 0.25 5
expect 'time: bubble of player 1: [1-9][0-9]* NPC' 5
expect 'time: our V back to its own speed' 5
expect 'time: npc [0-9]+ at 0.25' 5
reject 'time: npc [0-9]+ at 0.125'
# Ours ends first: the mock player's bubble holds the NPC and slows us again.
expect 'time: test dilation over' 10
expect 'time: npc [0-9]+ at 0.5' 5
expect 'time: our V slowed to 0.5' 5
expect_peer 'sandevistan off' 15
expect 'time: our V back to its own speed' 5
cmd npc hostile clear
cmd god off
# The NPC's shots at the mock player are its game's to decide
# (Shots.reds): the host never forwards a ranged hit of a shot it announced.
reject 'hit player [0-9]+.s puppet: .*type 10, .*sent to their game'
reject 'watchdog: game thread silent'
