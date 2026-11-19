# As a guest, the pilot fights the host's NPCs with our real V: its shots, quickhack and punches go to the host as a player's would
# needs the pilot
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# Gun, quickhack, fists on the nearest proxy (tools/pilot/routines/fight.pilot):
# real weapon fire by the pad's trigger, the game's own quickhack upload.
pilot fight
expect 'combat: hit on proxy [0-9]+ for [0-9.]+, for the host' 5
expect_peer 'npc [0-9]+ struck by player [0-9]+: [1-9][0-9]* health' 10
expect 'hacks: our upload of QuickHack.OverheatLvl1Hack on proxy [0-9]+ sent to the host' 5
expect_peer 'player [0-9]+ uploads 0x[0-9a-f]+ on npc [0-9]+' 10
reject 'watchdog: game thread silent'
