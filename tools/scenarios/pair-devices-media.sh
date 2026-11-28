# Two games: a radio or TV's station our V picks plays on the partner's copy; the partner picks another and ours follows
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a radio or TV within 40 m; default
# v-door, V's flat).
# To listen to: the same station on both games after each change (the song's
# moment may differ: no playback position is shared).
ensure host
at "${DEVICES_AT:-v-door}"
partner_join
# Beside the radio or TV (the partner a metre off our V found a TV 10 m away
# nearer than our radio, 2026-11-28): both Vs a step from it.
cmd device goto radio
expect 'devices: at radio [0-9a-f]+ [A-Za-z]+: ' 10
read -r radio x y z < <(since | sed -nE 's/.*devices: at radio ([0-9a-f]+) [A-Za-z]+: \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3 \4/p' | tail -1)
note "the radio or TV: $radio"
pat "$(awk -v x="$x" 'BEGIN { print x + 0.8 }') $y $z"
pnear radio "$radio"
cmd device station 0
pause 4
mark
pmark
cmd device station 1
expect "devices: $radio [A-Za-z]+ is now [a-z]+, [a-z+]+, value 1" 10
expect_partner "devices: $radio [A-Za-z]+ by player 1: [a-z]+, [a-z+]+, value 1, played" 10
compare devices "$radio"
pcmd device station 2
expect_partner "devices: $radio [A-Za-z]+ changed here \\([a-z]+, [a-z+]+, value 2\\): asked of the host" 10
expect "devices: player [0-9]+ operated $radio" 10
expect "devices: $radio [A-Za-z]+ after player [0-9]+'s request: [a-z]+, [a-z+]+, value 2" 10
pause 2
compare devices "$radio"
reject "devices: $radio [A-Za-z]+ will not become"
reject_partner "devices: $radio [A-Za-z]+ will not become"
reject 'watchdog: game thread silent'
