# As a guest: the host's moment drives off without us (a quest ride with no seat for us): watched at once behind the curtain, our V carried in its car, back beside it when the moment ends
# shellcheck shell=bash disable=SC2317
ensure join
# drive: the host's car circles our V at 35 m, inside the moment's 40 m
# (shared) and past the 30 m a ride may leave us behind.
peer host --mode drive --drive-radius 35 --body male --story tools/scenarios/spectate-ride.story
expect 'in the session as player' 30
# 3 s: the moment (a tier 2 talk in its car): left behind at once, not at 150 m.
expect 'spectate: watching .*our V parked: \(c\) the host.s V taken beyond our reach' 15
# The switch behind our own black ("built on the HUD layer" comes once a
# game: an earlier scenario's switch had built it, 2026-11-21).
expect 'curtain: black' 5
expect 'spectate: watching through the host.s camera' 10
expect 'spectate: our V carried along in the host.s vehicle' 10
cmd story spectate report
expect 'watching true \(why: \(c\)' 5
# 25 s: the moment is over: our own V again, put down beside the car.
expect 'spectate: our own V again \(the reason is over: \(c\)' 30
expect 'spectate: our V off the host.s vehicle' 5
expect 'spectate: our own camera again' 5
expect 'curtain: lifted' 5
reject 'curtain: black too long'
reject 'spectate: joining the host where the moment left it'
reject 'watchdog: game thread silent'
# By hand (docs/architecture/scripted-moments.md, in-game checklist): the
# switch is a short black, never a camera sweep; "Watching tandem-peer: the
# story takes them somewhere you can't follow." once; back on our feet by
# the car, facing it, no loading screen.
