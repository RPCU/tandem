# Our V hits proxies as a guest: damage, status effects and instant kills go to the host; health, death and kill credit come back
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body female --replay recordings/npcs.rec
expect 'puppet 6[0-9]{4}: clips walk' 60
# A plain hit: damage computed here (our side and the proxy's armour), applied by the host.
cmd npc punch 40
expect 'combat: hit on proxy [0-9]+ for [0-9.]+, for the host' 10
expect 'outcomes: strike on proxy [0-9]+: [0-9.]+ health, [0-9.]+ poise' 5
expect_peer 'npc [0-9]+ struck by player [0-9]+: [1-9][0-9]* health' 10
expect_peer 'npc [0-9]+ health [0-9]+%' 5
# A hit that sets it on fire: the effect goes to the host too.
cmd npc punch burn
expect 'combat: status effect [A-Za-z_.]+ on proxy [0-9]+ sent to the host' 10
expect_peer 'npc [0-9]+ gets effect 0x[0-9a-f]+ from player [0-9]+' 10
# An instant kill, credited to our V.
cmd npc punch
expect 'combat: instant kill on proxy [0-9]+, for the host' 10
expect_peer 'npc [0-9]+ died' 5
expect 'npcs: proxy [0-9]+ died by our hit' 10
reject 'watchdog: game thread silent'
