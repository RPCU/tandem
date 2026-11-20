# As a guest with scene_rigs on: the speaking actor's face rig is bound on its proxy, the host's rig poses are heard, and the face is let go with the scene
# shellcheck shell=bash disable=SC2317
# Scene replay and its other rigs are off by default (not yet verified in game).
ensure join scene_replay=on scene_rigs=on
peer host --mode front --body male --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --story tools/scenarios/scene-face.story
expect 'in the session as player' 30
expect "scene: the host's scene begins \(play 1, tier 4" 20
expect 'scene: actor 50000 follows the host.s pose' 15
expect "pose stream: npc 50000's face rig written on graph instance" 10
# The proxy's face rig as the game has it: its bone count is a fact to note.
expect "pose stream: npc 50000's face rig 0x[0-9a-f]+: [0-9]+ bones, mapped" 10
pause 5
cmd story scene report
expect '[1-9][0-9]* rig poses heard' 5
expect "pose stream: npc 50000's face rig let go" 20
reject 'watchdog: game thread silent'
# The mock player's face is not the proxy's rig: nothing moves here. Lips
# are checked between two games (docs/architecture/scene-replay.md, checks).
