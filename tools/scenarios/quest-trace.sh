# Spike C2 on a game with nobody joined: the quest trace logs each phase's chain and the journal nodes run in it
# shellcheck shell=bash disable=SC2317
ensure host
cmd quest trace on
expect 'quest trace: on' 5
# Phases run all the time in the open world (fixers, the city's own).
expect 'quest trace: phase 0x[0-9a-f]+ path [0-9.]+ resource 0x[0-9a-f]+ chain' 60
note "drive or walk into a gig with the trace on: its 'quest trace: ... journal' lines name its journal quest"
cmd quest trace off
expect 'quest trace: off' 5
reject 'watchdog: game thread silent'
