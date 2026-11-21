# As a guest beside the host: its scripted moment is watched through its camera, our V parked, and given back when it ends
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V, so we share its moment.
peer host --mode front --body male --story tools/scenarios/spectate-guest.story
expect 'in the session as player' 30
expect 'spectate: watching .*moment through their camera, our V parked' 15
expect 'story: GameplayRestriction.NoMovement on, with the host.s moment' 5
expect 'story: BaseStatusEffect.Invulnerable on, with the host.s moment' 5
expect 'spectate: watching through the host.s camera \(fov 70' 10
cmd story spectate report
expect 'watching true, camera the host.s' 5
expect 'story: our story flags 32' 5
# The moment ends at 13 s (the host's flags; its hold is the host's own).
expect 'spectate: our own V again \(the moment is over' 15
expect 'story: GameplayRestriction.NoMovement off' 5
expect 'spectate: our own camera again' 5
cmd story spectate report
expect 'watching false' 5
reject 'spectate: the camera body has no camera component'
reject 'watchdog: game thread silent'
# A human checks once (docs/architecture/story.md, "Spectating", in-game
# checks): "Watching through tandem-peer's eyes." on screen, the "Stop
# watching" hint, the picture behind and above the host's puppet, our own V
# out of the picture, no input moving it, and our eyes back after.
