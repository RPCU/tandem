# As a guest: the host's NPC netrunner traces our V through its proxy, and our quickhack on a proxy goes to the host
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body male --hack-player 10
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# The host's NPC hacks our V: the game runs it from the proxy.
expect_peer 'npc [0-9]+ hacks player [0-9]+ with AIQuickHack.HackRevealPosition' 20
expect 'npc [0-9]+ hacks our V' 10
expect 'hacks: npc [0-9]+ (hacks our V with AIQuickHack.HackRevealPosition \(trace\)|would trace our V: in combat or already traced)' 10
# Our V quickhacks the nearest proxy (needs the hack in the deck and the RAM).
cmd hack npc QuickHack.OverheatLvl1Hack
expect 'hack npc: QuickHack.OverheatLvl1Hack on [0-9]+' 10
expect 'hacks: our upload of QuickHack.OverheatLvl1Hack on proxy [0-9]+ sent to the host' 10
expect_peer 'player [0-9]+ uploads 0x[0-9a-f]+ on npc [0-9]+' 10
expect 'hacks: the host runs our upload of QuickHack.OverheatLvl1Hack on npc [0-9]+' 10
reject 'watchdog: game thread silent'
