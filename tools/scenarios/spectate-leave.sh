# As a guest watching the host's moment: "Stop watching" gives our V back at once, and the same moment is not offered again
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode front --body male --story tools/scenarios/spectate-guest.story
expect 'in the session as player' 30
expect 'spectate: watching .*moment through their camera' 15
# The hint's key, as the game would hear it: the test command does the same.
cmd story spectate off
expect 'spectate: our own V again \(we left it' 5
expect 'spectate: watching off for this session' 5
cmd story spectate report
expect 'watching false' 5
cmd story spectate on
expect 'spectate: watching allowed' 5
reject 'watchdog: game thread silent'
