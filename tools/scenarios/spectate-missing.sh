# As a guest: the host among quest interiors our game cannot load (a failed layout check, here the test switch): watched through its eyes outside any moment, back when they go
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode front --body male --story tools/scenarios/spectate-missing.story
expect 'in the session as player' 30
pause 3
cmd story spectate missing on
expect 'spectate: watching .*our V parked: \(e\) the host.s V among quest interiors' 10
expect 'spectate: watching through the host.s camera' 10
expect 'story: our story flags 32' 5
pause 4
cmd story spectate missing off
expect 'spectate: our own V again \(the reason is over: \(e\)' 10
expect 'spectate: our own camera again' 5
reject 'curtain: black too long'
reject 'prefabs: the host.s quests have .* we cannot load them: told'
reject 'watchdog: game thread silent'
# By hand: one notice ("Watching tandem-peer: their story is somewhere your
# game can't show."), a short black each way.
