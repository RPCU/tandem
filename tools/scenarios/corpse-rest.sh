# As a guest of a replayed host: an NPC of the host's dies and its body's rest comes 1 s later (CorpsePose, a real ganger's 22 ragdoll bodies): our copy's ragdoll is laid so, read back within 0.3 m
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --corpse-npcs 8
expect 'in the session as player' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
expect_peer 'npc [0-9]+ at rest: its pose sent' 30
expect 'corpse: proxy [0-9]+ laid as its owner.s body' 15
pause 4 # laid again 1 s after, then asleep
cmd test query corpses
expect 'corpse: sent 0 \(our NPCs. bodies\), laid [1-9][0-9]* \(copies' 5
# Each copy read back from its ragdoll: within 0.3 m of the host's word.
expect 'corpse: proxy [0-9]+ now 0\.([0-2][0-9]|30) m from its owner.s body' 5
reject 'corpse: proxy [0-9]+ now (-1|0\.(3[1-9]|[4-9])|[1-9])'
reject 'degraded: fallback corpse pose'
# To look at (a person there): the copy lies crumpled as the recorded body
# lay, not as its own fall left it, and stays so; no twitch, no slide.
reject 'watchdog: game thread silent'
