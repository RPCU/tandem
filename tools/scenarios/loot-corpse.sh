# As a guest: a proxy we kill is our own corpse to loot (rolled in our game, quest items kept), and leaves a bag when the host's body goes
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --remove-corpses 15
expect 'puppet 6[0-9]{4}: clips walk' 60
cmd npc punch
expect 'combat: instant kill on proxy [0-9]+, for the host' 10
expect 'npcs: proxy [0-9]+ died by our hit' 10
# Its death rolled its loot in our game, from its own record.
expect 'loot: proxy corpse [A-Za-z_.0-9]+: [0-9]+ item\(s\), [0-9]+ important' 5
reject 'loot: proxy corpse .*rolled here'
cmd loot corpse
expect 'loot corpse: [A-Za-z_.0-9]+ [0-9.]+ m away, loot rolled true, looted false' 10
shot loot-corpse
# The host's body goes: what the proxy still holds is left as a bag.
expect_peer "npc [0-9]+'s body taken away" 20
expect 'loot: proxy corpse [A-Za-z_.0-9]+ gone, its loot left as a bag' 10
reject 'watchdog: game thread silent'
