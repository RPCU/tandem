# As a guest, an NPC's melee hit waits for its proxy's swing on our screen, then runs on our V by our rules (block, parry, dodge)
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
host_world recordings/npcs.rec --mode idle --body female --hit-you 3 --hit-you-melee on
expect 'in the session as player' 30
query before health
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ hit player [0-9]+ for 10 physical \(melee\)' 20
expect 'npcs: hit by npc [0-9]+: 10(\.0+)? physical' 10
# Held while the proxy's swing is still to come (a replayed NPC has a track).
expect "npcs: npc [0-9]+'s melee hit on our V held [0-9.]+ ms" 10
expect 'combat: npc hit applied to our V: [0-9.]+ damage' 10
# Not "health A -> A": the hit changed it, and the game holds it lower.
hit=$(since | grep -oE 'combat: npc hit applied to our V: [0-9.]+ damage, health [0-9.]+ -> [0-9.]+' | tail -1 || true)
from=${hit#*health }
expect_changed "${from%% -> *}" "${hit##* -> }" down
query after health
expect_changed "$before" "$after" down
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
