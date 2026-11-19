# The scanner's slow motion is a time bubble, like a Sandevistan's: the NPCs and the other Vs around the scanning V slow, nobody else's world does
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# An NPC 12 m ahead, neutral to our V: shared with the mock player, inside its bubble.
cmd npc hostile puppets
# The mock player "scans" (its scanner's dilation at 0.2) for 5 s.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --scan-at 12 --sandevistan-for 5 --sandevistan-scale 0.2
expect 'joined \(player [0-9]+' 20
expect_peer 'scanning on' 30
expect 'player [0-9]+: time abilities 0x8 \(world speed 0.20\)' 5
expect 'time: bubble of player [0-9]+: [1-9][0-9]* NPC' 5
expect 'time: npc [0-9]+ at 0\.2' 5
# Our V stands in that bubble with no ability of its own: slowed like the NPCs.
expect 'time: our V slowed to 0\.2.*a scanner.s' 5
expect_peer 'scanning off' 10
expect 'time: bubble of player [0-9]+ ended' 5
expect 'time: npc [0-9]+ back to normal speed' 5
expect 'time: our V back to its own speed after [0-9]' 5
# Our own scanner's dilation (as its state sets it, under "focusMode"): kept
# on our screen (time_host = full, the default), the NPC we share set to its
# speed, and the mock player told.
cmd time scan 0.2 4
expect 'time: our abilities 8 \(world speed 0.2' 5
expect 'time: world speed 0\.2[0-9]* over the test' 5
expect 'time: npc [0-9]+ at 0\.2[0-9]* .*ignoring our global factor true' 5
expect 'time: bubble of player 1: [1-9][0-9]* NPC' 5
expect_peer 'player 1: time abilities 0x8 \(world speed 0.20\)' 5
expect 'time: test dilation over' 10
expect 'time: our scanner.s bubble ran [0-9]' 5
expect 'time: our abilities 0 ' 5
expect_peer 'player 1: time abilities 0x0' 5
cmd time report
expect 'time: our scanner.s bubbles: 1 for [0-9.]+ s; our V slowed by the others. bubbles for [0-9.]+ s, [0-9.]+ s of it by a scanner.s alone' 5
# Slow time cancels slow time, by strength. The mock player's Sandevistan
# (0.1) is stronger than our scanner (0.2): scanning in it, our V is slowed
# by the ratio 0.1 / 0.2 alone, not by the product.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --sandevistan-at 8 --sandevistan-for 14 --sandevistan-scale 0.1
expect_peer 'sandevistan on' 40
expect 'time: our V slowed to 0\.1' 5
mark
cmd time scan 0.2 5
expect 'time: our V slowed to 0\.5' 5
cmd time report
expect 'time: our V reads ' 5
reject 'time: our V slowed to 0\.02'
expect 'time: test dilation over' 10
expect 'time: our V slowed to 0\.1' 5
expect_peer 'sandevistan off' 15
expect 'time: our V back to its own speed' 5
# Both scan at 0.2: neither slows the other.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --scan-at 8 --sandevistan-for 14 --sandevistan-scale 0.2
expect_peer 'scanning on' 40
expect 'time: our V slowed to 0\.2' 5
cmd time scan 0.2 5
expect 'time: our V back to its own speed' 5
expect 'time: test dilation over' 10
expect 'time: our V slowed to 0\.2' 5
expect_peer 'scanning off' 15
expect 'time: our V back to its own speed' 5
cmd npc hostile clear
cmd god off
# The scanner is never switched off any more.
reject 'the scanner.s slow motion is off'
reject 'watchdog: game thread silent'
