# A fight's bodies lie the same on both games: the host's gangers killed and thrown, each guest copy's ragdoll laid as the host's lies (every body within 0.3 m), both NPC lists compared, both screens shot
# shellcheck shell=bash disable=SC2154 # evidence and the helpers: tools/pair.sh
on h npc spawn 3
expect h 'npc spawn: 3 gangers ahead' 10
sleep 6 # the guest's proxies spawn
on h test query corpses kill
expect h 'corpse test: [1-9] ganger' 10
expect h 'corpse: npc [0-9]+ at rest after .* its bodies sent' 20
expect g 'corpse: proxy [0-9]+ laid as its owner.s body' 10
sleep 6 # laid twice, measured 1 s after the second lay
on g test query corpses
on h test query corpses
expect g 'corpse: sent 0 .*laid [1-9]' 6
on h npc list
on g npc list
sleep 3
lines h 'npc list: ' >"$evidence/npcs-host.log"
lines g 'npc list: ' >"$evidence/npcs-guest.log"
python3 tools/npc-compare.py --names host guest --near 40 "$evidence/npcs-host.log" "$evidence/npcs-guest.log" \
  >"$evidence/npc-compare.txt" 2>&1 || true
shot bodies
lines h 'corpse: ' | sed 's/^.*corpse: /host: /' >>"$evidence/measures.txt"
lines g 'corpse: ' | sed 's/^.*corpse: /guest: /' >>"$evidence/measures.txt"
# Every copy measured, each within 0.3 m of its owner's body.
apart=$(lines g 'lies as its owner.s: its bodies within' | sed -n 's/.*within \([0-9.-]*\) m.*/\1/p')
[ -n "$apart" ] || fail "no copy measured"
for m in $apart; do
  awk -v m="$m" 'BEGIN { exit !(m >= 0 && m <= 0.3) }' || fail "a copy lies $m m from its owner's body (> 0.3 m)"
done
measure "guest copies apart (m): $apart"
reject h 'degraded: fallback corpse pose'
reject g 'degraded: fallback corpse pose'
reject h 'degraded: ragdoll state is off'
reject g 'degraded: ragdoll state is off'
