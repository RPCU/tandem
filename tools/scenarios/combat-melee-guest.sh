# As a guest, an NPC's melee hit waits for its proxy's swing on our screen, then runs on our V by our rules (block, parry, dodge), never for more than the host computed
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
# The mock host's hits name no attack record: our game stands in
# Attacks.ForwardPush (Combat.reds AttackRecord), which once took 392.96 of
# a 10-physical hit. 5 s apart: one hit between the two queries below.
host_world recordings/npcs.rec --mode idle --body female --hit-you 5 --hit-you-melee on
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ hit player [0-9]+ for 10 physical \(melee\)' 20
expect 'npcs: hit by npc [0-9]+: 10(\.0+)? physical' 10
expect 'combat: attack .* unknown here, stood in by Attacks.ForwardPush' 10
# Held while the proxy's swing is still to come (a replayed NPC has a track).
expect "npcs: npc [0-9]+'s melee hit on our V held [0-9.]+ ms" 10
expect 'combat: npc hit applied to our V: [0-9.]+ damage' 10
# (Our log line's health is read around DealDamages, before the game's
# pool takes the damage: the queries are the measure.) One hit, measured
# in health points: down, and by at most the 10 the host sent (and 1 %).
query before health points
mark
expect 'combat: npc hit applied to our V: [0-9.]+ damage' 10
query after health points
expect_changed "$before" "$after" down
drop=$(awk -v b="$before" -v a="$after" 'BEGIN { printf "%.2f", b - a }')
expect_value "$drop" '<=' 10.1
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
