# As a guest: the host is in a fist fight with one of its NPCs: we are told, our hits on its opponent are dropped (we watch), and count again when the fight is over
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --contest duel --activity-at 8 --activity-for 15
expect 'puppet 6[0-9]{4}: clips walk' 60
expect_peer 'contest: sort 1, opponent npc [1-9][0-9]*' 30
expect 'contests: player 1 is in a fist fight \(opponent npc [0-9]+\): we watch' 10
# The opponent itself (the nearest proxy may be another NPC of the recording).
opponent=$(since | grep -oE 'fist fight \(opponent npc [0-9]+' | tail -1 | grep -oE '[0-9]+$')
# Our hit on the opponent's proxy never leaves our game.
mark
cmd npc punch 40 npc "$opponent"
expect 'contests: our hit on npc [0-9]+ dropped: player 1.s fist fight, we watch' 10
reject 'combat: hit on proxy [0-9]+ for [0-9.]+, for the host'
cmd contest report
expect 'contests: ours no contest.*others: player 1: a fist fight.*hits dropped [1-9]' 5
# The fight over, a hit counts again.
expect_peer 'free again' 30
expect 'contests: player 1 is no longer in a fist fight' 10
mark
cmd npc punch 40 npc "$opponent"
expect 'combat: hit on proxy [0-9]+ for [0-9.]+, for the host' 10
# Ours, as a test kind, seen by the mock player.
cmd contest race
expect 'contests: our V is in a race' 5
expect_peer 'player [0-9]+ is busy: a contest' 10
cmd contest off
expect_peer 'player [0-9]+ is free again' 10
reject 'watchdog: game thread silent'
