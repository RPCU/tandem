# As a guest in the host's moment: a walk and talk is played from our own V, a cutscene and a dive are watched through the host's camera, each given back at once
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V, so we share its moment.
peer host --mode front --body male --story tools/scenarios/spectate-guest.story
expect 'in the session as player' 30
# 3 s to 10 s: the tier 2 walk and talk, played: in the moment, not watching.
pause 4
cmd story spectate report
expect 'in the host.s moment true, watching false' 5
reject 'spectate: watching'
# 10 s: the cutscene (a).
expect 'spectate: watching .*moment through their camera, our V parked: \(a\) a cutscene' 15
expect 'story: GameplayRestriction.NoMovement on, with the host.s moment' 5
expect 'story: BaseStatusEffect.Invulnerable on, with the host.s moment' 5
expect 'spectate: watching through the host.s camera \(fov 70' 10
cmd story spectate report
expect 'watching true \(why: \(a\)' 5
expect 'story: our story flags 32' 5
# 17 s: the cutscene is over, the moment goes on: our own V again at once.
expect 'spectate: our own V again \(the reason is over\): \(a\)' 10
expect 'story: GameplayRestriction.NoMovement off' 5
expect 'spectate: our own camera again' 5
mark
cmd story spectate report
expect 'in the host.s moment true, watching false' 5
# 24 s: the host's V dives into cyberspace (b); 30 s: back, played again.
expect 'spectate: watching .*our V parked: \(b\) the host.s V in another world' 15
expect 'spectate: our own V again \(the reason is over\): \(b\)' 15
reject 'spectate: the camera body has no camera component'
reject 'watchdog: game thread silent'
# A human checks once (docs/architecture/story.md, "Spectating", in-game
# checks): during the walk and talk our V moves and sees the host from its
# own eyes; during the cutscene "Watching through tandem-peer's eyes." on
# screen, the "Stop watching" hint, the picture behind and above the host's
# puppet, our own V out of the picture, no input moving it; our eyes back
# after each.
