# Quest and important items are every player's: a player's loot reaches V's inventory, V's reaches theirs
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --loot-quest Items.Preset_Overture_Kerry
expect 'joined \(player [0-9]+' 20
expect_peer 'looted quest item Items.Preset_Overture_Kerry: shared' 30
# Theirs: in our V's inventory too (we are the host: our quests see it).
expect 'loot: player [0-9]+ shares item 0x[0-9a-f]+ x1 \(looted, npc 0\)' 10
expect 'loot: tandem-peer looted Items.Preset_Overture_Kerry( x1: given to our V \(true\)|: we have it already)' 10
# Ours: the player gets it.
cmd loot share Items.Preset_Lexington_Wilson
expect 'loot: we got Items.Preset_Lexington_Wilson x1: every player gets it' 10
expect_peer 'shared item 0x[0-9a-f]+ x1 from player 1 \(looted, npc 0\)' 10
# Our own gift was not shared back as if we had looted it.
reject 'loot: we got Items.Preset_Overture_Kerry'
reject 'watchdog: game thread silent'
