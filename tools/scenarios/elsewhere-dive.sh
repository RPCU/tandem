# Our V dives into another world as the host: the others keep its body where it was, we stop seeing them, and both come back
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
pause 4
# Jacked in where we stand: the others hear of it, and we stop seeing them.
cmd elsewhere test cyberspace
expect 'elsewhere: our V is in cyberspace \(a test\)' 5
expect_peer 'player 1 is in cyberspace \(in place\)' 5
expect 'elsewhere: we are where the others are not' 5
expect "elsewhere: player [0-9]+'s puppet hidden" 5
expect 'marker: player [0-9]+ off for now' 5
# The scene takes our V away (300 m up): its body stays at home, out of sight.
cmd teleport -915 1879.5 336.3
expect 'elsewhere: our body stays at \(-915' 5
expect 'elsewhere: away from our body, which stays out of sight' 5
expect_peer 'player 1 is in cyberspace, body left at \(-915' 5
# Our NPCs are that world's: none shared while away.
cmd elsewhere state
expect 'elsewhere: our V in cyberspace \(a test\), settled false, told place 1 flags 1, body held true' 5
# Out of cyberspace, then back home: back with our body, the others seen again.
cmd elsewhere test off
cmd teleport home
expect 'elsewhere: back with our body' 10
expect_peer 'player 1 is back in the city' 10
expect 'elsewhere: we are back: the others shown again' 5
expect "elsewhere: player [0-9]+'s puppet shown" 5
expect 'marker: player [0-9]+ back' 5
cmd god off
reject 'watchdog: game thread silent'
