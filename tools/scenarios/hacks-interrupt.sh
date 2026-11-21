# As a guest: the host's NPC netrunner is interrupted while it traces our V (its owner's word): its proxy wears the interruption, and our game stops the upload as it would alone
# shellcheck shell=bash disable=SC2317
# To look at: the trace's bar on our HUD stops a second after it began,
# instead of running to its end.
ensure join
host_world recordings/npcs.rec --mode idle --body male --hack-player 15 --hack-interrupt 1
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ hacks player [0-9]+ with AIQuickHack.HackRevealPosition' 25
expect 'hacks: npc [0-9]+ (hacks our V with AIQuickHack.HackRevealPosition \(trace\)|would trace our V: in combat or already traced)' 10
expect_peer "npc [0-9]+'s hack interrupted \(AIQuickHackStatusEffect.HackingInterrupted\)" 5
expect 'hacks: npc [0-9]+ has AIQuickHackStatusEffect.HackingInterrupted from player 0: on its proxy' 5
shot hacks-interrupt
expect 'hacks: npc [0-9]+ lost AIQuickHackStatusEffect.HackingInterrupted: off its proxy' 5
# Cut short: never run to its end.
reject 'hacks: npc [0-9]+.s trace of our V ran to its end'
reject 'watchdog: game thread silent'
