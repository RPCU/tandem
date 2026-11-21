# As a guest: the host's NPC netrunner's trace runs to its end on our V: our game ends it as alone, and the squad part (its squadmates set on us) goes to the NPC's owner
# shellcheck shell=bash disable=SC2317,SC2154
# V must stand still out of combat for the trace to run its course.
ensure join
host_world recordings/npcs.rec --mode idle --body male --hack-player 120
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ hacks player [0-9]+ with AIQuickHack.HackRevealPosition' 25
expect 'hacks: npc [0-9]+ hacks our V with AIQuickHack.HackRevealPosition \(trace\)' 10
# The trace's upload runs on our V (its pool fills): it stopped at its first
# tick while the trace's start was unset (LastPlayerHackPosition).
pause 3
query early upload
pause 3
query later upload
expect_changed "$early" "$later" up
reject 'hacks: npc [0-9]+.s trace of our V stops at'
expect 'hacks: npc [0-9]+.s trace of our V ran to its end: its owner sets its squad on us' 60
expect_peer "npc [0-9]+'s trace of player [0-9]+ ran to its end: its squad is set on them" 10
query after upload
expect_value "$after" = none
reject 'watchdog: game thread silent'
