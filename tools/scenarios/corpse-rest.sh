# As a guest of a replayed host: an NPC of the host's dies and its body's rest comes 1 s later (CorpsePose, a real ganger's 22 ragdoll bodies): our copy's ragdoll is laid so and read back (how close: pair-corpse-pose, two real games)
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --corpse-npcs 8
expect 'in the session as player' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
expect_peer 'npc [0-9]+ at rest: its pose sent' 30
expect 'corpse: proxy [0-9]+ laid as its owner.s body' 30
pause 4 # laid again 1 s after, then asleep
cmd test query corpses
expect 'corpse: sent 0 \(our NPCs. bodies\), laid [1-9][0-9]* \(copies' 5
# A laid copy is read back from its own ragdoll. Not within 0.3 m here: the
# mock's body was recorded elsewhere, on other ground, and settles on ours
# (0.76 m, 2026-11-28); two games' bodies: pair-corpse-pose.
expect 'corpse: proxy [0-9]+ now [0-9]+\.[0-9]+ m from its owner.s body' 5
# The replay's crowd (Character.LightCrowd_*) has a ragdoll component with
# no physics body, even made to ragdoll: such a copy lies as its own fall
# left it, registered (regions.md, "Corpses").
allow_fallback 'corpse pose'
# To look at (a person there): the copy lies crumpled as the recorded body
# lay, not as its own fall left it, and stays so; no twitch, no slide.
reject 'watchdog: game thread silent'
