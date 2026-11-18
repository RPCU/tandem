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
cmd time report
expect 'time: world speed here 1' 5
expect_peer 'sandevistan off' 10
expect 'time: player [0-9]+.s sandevistan ended' 5
expect 'time: bubble of player [0-9]+ ended' 5
expect 'time: npc [0-9]+ back to normal speed' 5
# The host's own dilation: taken back at once, the world keeps normal speed,
# our V's bubble slows the NPC and the mock player hears of it.
cmd time test 0.25 4
expect 'time: our abilities 1 \(world speed 0.25' 5
expect 'time: sandevistan would slow the shared world to 0.25: kept at [01.9]+, still active true' 5
expect 'time: world speed (0\.9[5-9]|1)[0-9.]* over the test' 5
expect 'time: bubble of player 1: [1-9][0-9]* NPC' 5
expect_peer 'player 1: time abilities 0x1 \(world speed 0.25\)' 5
expect 'time: test dilation over' 10
expect 'time: our abilities 0 ' 5
expect_peer 'player 1: time abilities 0x0' 5
# Two Vs in a Sandevistan at once: one bubble each, the NPC at the strongest.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --sandevistan-at 8 --sandevistan-for 6 --sandevistan-scale 0.5
expect_peer 'in the session' 20
pause 6
cmd time test 0.25 6
expect_peer 'sandevistan on' 10
expect 'time: bubble of player [2-9]: [1-9][0-9]* NPC' 5
expect 'time: bubble of player 1: [1-9][0-9]* NPC' 5
cmd time report
expect 'time: [0-9]+ at 0.25' 5
reject 'time: npc [0-9]+ at 0.125'
expect 'time: test dilation over' 10
cmd npc hostile clear
cmd god off
reject 'watchdog: game thread silent'
