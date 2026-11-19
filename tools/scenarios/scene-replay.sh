# As a guest beside the host: its story scene plays here (an actor follows the host's pose, lines are voiced, a skip cuts them, the end frees the actor), and a scene the host vanishes in is stopped
# shellcheck shell=bash disable=SC2317
# Scene replay is off by default (not yet verified in game): turned on here.
ensure join scene_replay=on
# front: the host stands 3 m in front of our V and shows our own pose back;
# its NPC (the scene's actor) takes the same pose, so the proxy mimics our V.
peer host --mode front --body male --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --story tools/scenarios/scene-replay.story --vanish-at 30
expect 'in the session as player' 30
expect 'scene: replay on' 5
expect "scene: the host's scene begins \(play 1, tier 4" 20
expect 'story: weapons lowered' 10
expect 'scene: actor 50000 joins' 10
expect 'scene: actor 50000 follows the host.s pose' 15
expect 'subtitle: shown' 10
expect 'scene: line voiced \(npc 50000, player 0, from 0' 10
expect 'scene: line voiced \(npc 0, player 1' 10
expect 'scene: line cut' 15
expect 'scene: the host skips ahead' 10
cmd story scene report
expect 'scene: host play 0 \(off\), heard 0x[0-9a-f]+ play 1 with 1 actor' 5
expect 'actor.s. read .*1 proxy.ies. posed \([1-9][0-9]* writes' 5
expect 'scene: actor 50000 released' 15
expect "scene: the host's scene is over" 5
expect 'story: weapons free' 10
# The second scene: the host vanishes mid-line. The scene stops on the
# silence, the actor is its own again, nothing stays held.
expect "scene: the host's scene begins \(play 2, tier 3" 20
expect_peer 'vanishing \(--vanish-at\)' 30
expect "scene: the host's scene is over \(interrupted\)" 15
expect 'session ended: the host is gone' 10
cmd story scene report
expect 'scene: mode 2, on false, tier 0, flags 0, 0 actor\(s\), 0 voice\(s\)' 5
reject 'watchdog: game thread silent'
# A human checks once (docs/architecture/scene-replay.md, in-game checks):
# the Tyger Claw in front copies our V's pose (crouch: it crouches) while
# the scene lasts and goes back to its own idle after; with real line ids
# in the .story, the lines are heard from the speakers, in our language,
# with one subtitle each.
