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
# 10 physical before our armour: a few points off V's health each time.
expect 'combat: npc hit applied to our V: [0-9.]+ damage, health [0-9.]+ -> [0-9.]+' 10
# Not "health A -> A": the hit changed it.
hit=$(since | grep -oE 'combat: npc hit applied to our V: [0-9.]+ damage, health [0-9.]+ -> [0-9.]+' | tail -1 || true)
from=${hit#*health }
expect_changed "${from%% -> *}" "${hit##* -> }" down
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
