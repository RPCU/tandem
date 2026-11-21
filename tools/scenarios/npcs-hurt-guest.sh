# As a guest of a replayed host whose NPC shoots it every 2 s: each hit runs on our V by our game's rules and lowers its health
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
host_world recordings/npcs.rec --mode idle --body female --hit-you 2
expect 'in the session as player' 30
query before health
expect 'npcs: guest, own crowd off' 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ hit player [0-9]+ for 10 physical' 15
expect 'npcs: hit by npc [0-9]+: 10(\.0+)? physical' 10
# The mock host names no attack record: a plain bullet stands in.
expect 'combat: attack .* unknown here, stood in by Attacks.REMOVE_BulletWithDamage' 10
# 10 physical before our armour: a few points off V's health each time.
expect 'combat: npc hit applied to our V: [0-9.]+ damage, health [0-9.]+ -> [0-9.]+' 10
# (Our log line's health is read around DealDamages, before the game's
# pool takes the damage: the queries are the measure.)
# The next one, 2 s later.
mark
expect 'combat: npc hit applied to our V: [0-9.]+ damage' 10
reject 'npc hit on our V dealt no damage'
query after health
expect_changed "$before" "$after" down
# Few hits: stop the host before V is in danger.
# Only what comes after: the previous host's goodbye may be just past the start.
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
