# A fight's bodies lie the same on both games: the host's gangers killed and thrown, each guest copy's ragdoll laid as the host's lies (every body within 0.3 m, read from both ragdolls), both NPC lists compared, both screens shot
# shellcheck shell=bash disable=SC2154 # evidence and the helpers: tools/pair.sh
# The guest stands 2.5 m to the host's right, facing the same way.
on h where
expect h 'where: ' 6
read -r x y z yaw < <(lines h 'where: ' | tail -1 | sed -n 's/.*where: \([-0-9.]*\) \([-0-9.]*\) \([-0-9.]*\) yaw \([-0-9.]*\).*/\1 \2 \3 \4/p') || true
[ -n "${yaw:-}" ] || fail "the host's place not read"
read -r gx gy < <(awk -v x="$x" -v y="$y" -v w="$yaw" 'BEGIN { r = w * 3.14159265 / 180; printf "%.2f %.2f\n", x + 2.5 * cos(r) + 0.8 * sin(r), y + 2.5 * sin(r) - 0.8 * cos(r) }') || true
on g teleport "$gx" "$gy" "$z" "$yaw"
expect g 'teleport: ' 6
on h npc spawn 3
expect h 'npc spawn: 3 gangers ahead' 10
sleep 6 # the guest's proxies spawn
on h test query corpses kill
expect h 'corpse test: [1-9] ganger' 10
expect h 'corpse: npc [0-9]+ at rest after .* its bodies sent' 20
expect g 'corpse: proxy [0-9]+ laid as its owner.s body' 10
sleep 20 # every body sent (again if it slid), laid twice, settled
on g test query corpses
on h test query corpses
expect g 'corpse: sent 0 .*laid [1-9]' 6
expect g 'corpse: proxy [0-9]+ (now|has no copy)' 6
on h npc list
on g npc list
sleep 3
lines h 'npc list: ' >"$evidence/npcs-host.log"
lines g 'npc list: ' >"$evidence/npcs-guest.log"
python3 tools/npc-compare.py --names host,guest --near 40 "$evidence/npcs-host.log" "$evidence/npcs-guest.log" \
  >"$evidence/npc-compare.txt" 2>&1 || true
shot bodies
lines h 'corpse: ' | sed 's/^.*corpse: /host: /' >>"$evidence/measures.txt"
lines g 'corpse: ' | sed 's/^.*corpse: /guest: /' >>"$evidence/measures.txt"
# Every copy the host told of, read now from its ragdoll: within 0.3 m of
# the host's body.
now=$(lines g 'corpse: proxy [0-9]+ (now|has no copy)')
[ -n "$now" ] || fail "no copy measured"
! grep -q 'has no copy' <<<"$now" || fail "a told body has no copy on the guest"
# shellcheck disable=SC2013 # one number a word
for m in $(sed -n 's/.* now \([-0-9.]*\) m from.*/\1/p' <<<"$now"); do
  awk -v m="$m" 'BEGIN { exit !(m >= 0 && m <= 0.3) }' || fail "a copy lies $m m from its owner's body (-1: unreadable; > 0.3 m)"
done
measure "guest copies now apart (m): $(sed -n 's/.* now \([-0-9.]*\) m from.*/\1/p' <<<"$now" | tr '\n' ' ')"
reject h 'degraded: fallback corpse pose'
reject g 'degraded: fallback corpse pose'
reject h 'degraded: ragdoll state is off'
reject g 'degraded: ragdoll state is off'
