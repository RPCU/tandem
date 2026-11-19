# As the host, our NPCs are scaled to the mean of the players' levels, the guest is told each one's level, and they are ours again when it leaves
# shellcheck shell=bash disable=SC2317
ensure host
# NPCs to share whatever the street holds (rain and night empty it).
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
# Alone: our own level is the session's, nothing is moved.
expect 'levels: our V is level [0-9]+' 20
# A level 1 guest: the mean is under our level (unless our V is level 1 too).
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --level 1
expect 'joined \(player [0-9]+' 20
expect_peer 'our V is level 1' 10
expect "levels: the session's level is [0-9]+ \(2 players" 20
expect_peer "the session's level is [0-9]+ \(2 players\)" 10
# Each NPC we share: its level told, moved if its content scales with the player.
expect_peer 'npc [0-9]+ is level [0-9.]+' 20
cmd progress
expect 'levels: ours [0-9]+, session [0-9]+ \(2 players\), [0-9]+ NPCs moved; seen [0-9]+ scaled by a curve, [0-9]+ of a constant level, [0-9]+ with no content assignment' 10
note "$(since | grep -E 'levels: npc [0-9]+ .* level' | head -3)"
shot levels-host
# The guest leaves: the session's level is ours again, no modifier is left.
pkill -x tandem-peer
expect "levels: the session's level is [0-9]+ \(1 players" 30
pause 2
cmd progress
expect 'levels: ours [0-9]+, session [0-9]+ \(1 players\), 0 NPCs moved' 10
reject 'watchdog: game thread silent'
