# As a guest of a replayed host: an NPC of the host's dies and its body's rest comes 1 s later (CorpsePose): our proxy is laid where the owner's lies and drawn in its pose
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --corpse-npcs 8
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ at rest: its pose sent' 30
expect 'corpse: proxy [0-9]+ drawn in its owner.s rest pose' 10
expect 'pose stream: actor [0-9]+ drawn on graph instance' 10
cmd presence report
expect 'corpse: sent 0 \(our NPCs. bodies\), drawn [1-9][0-9]* \(copies' 5
# To look at (a person there): the copy lies in the owner's pose (the mock
# host's is a neutral one: straight, arms out), not as its own ragdoll left
# it, and stays so; no twitch, no slide. As host with a real guest: each body
# a fight leaves lies the same on both screens.
reject 'watchdog: game thread silent'
