# As a guest beside the host: its cutscene's camera is our picture while its views come (a cut included), and our own eyes are back when they stop
# shellcheck shell=bash disable=SC2317
# Scene replay is off by default (not yet verified in game): turned on here.
ensure join scene_replay=on
peer host --mode front --body male --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --story tools/scenarios/scene-camera.story
expect 'in the session as player' 30
expect "scene: the host's scene begins \(play 1, tier 5" 20
expect 'scene: a camera for the host.s cutscene asked for' 10
expect 'scene: watching through the host.s camera \(fov 45' 10
cmd story scene report
expect 'camera followed, [1-9][0-9]* view\(s\) heard' 5
# The views stop (camera off at 13 s): a second later the picture is ours.
expect 'scene: our own camera again' 15
expect "scene: the host's scene is over" 10
cmd story scene report
expect 'camera ours' 5
reject 'the camera body has no camera component'
reject 'watchdog: game thread silent'
# A human checks once (docs/architecture/scene-replay.md, in-game checks):
# the picture goes to a view behind and above the host's puppet, jumps to
# the other side at the cut without sweeping, and comes back to our V's eyes
# with a short blend; our V's body is whole in the picture if it is in it.
