# Two games: our gangers killed and thrown, each partner copy's ragdoll laid as ours lies (every body within 0.3 m, read back from its ragdoll), a screenshot of each
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# To look at (the two screenshots): the three bodies lie the same on both
# screens, limbs and heads the same way; none twitches or slides.
ensure host
partner_join
# The partner 2.5 m to our V's right, facing the same way.
query here position
read -r x y z yaw <<<"$here"
read -r px py < <(awk -v x="$x" -v y="$y" -v w="$yaw" 'BEGIN { r = w * 3.14159265 / 180
  printf "%.2f %.2f\n", x + 2.5 * cos(r) + 0.8 * sin(r), y + 2.5 * sin(r) - 0.8 * cos(r) }')
pat "$px $py $z $yaw"
mark
pmark
cmd npc spawn 3
expect 'npc spawn: 3 gangers ahead' 10
pause 6 # the partner's copies spawn
cmd test query corpses kill
expect 'corpse test: [1-9] ganger' 10
expect 'corpse: npc [0-9]+ at rest after .* its bodies sent' 20
expect_partner 'corpse: proxy [0-9]+ laid as its owner.s body' 15
pause 20 # every body sent (again if it slid on), laid twice, asleep
shot pair-corpse-host
shot_file=$(pgame shot pair-corpse-partner) || fail "no screenshot of the partner"
mkdir -p "$out"
cp "$shot_file" "$out/"
pcmd test query corpses
expect_partner 'corpse: sent 0 .*laid [1-9]' 6
expect_partner 'corpse: proxy [0-9]+ (now|has no copy)' 6
# Every copy of a body we told of, read now from its ragdoll: within 0.3 m
# of our body's.
now=$(psince | grep -E 'corpse: proxy [0-9]+ (now|has no copy)' || true)
note "partner copies: $(sed -n 's/.*corpse: proxy \([0-9]*\) now \([-0-9.]*\) m.*/\1 \2 m/p' <<<"$now" | tr '\n' ',')"
! grep -q 'has no copy' <<<"$now" || fail "a body we told of has no copy on the partner"
while read -r m; do
  awk -v m="$m" 'BEGIN { exit !(m >= 0 && m <= 0.3) }' ||
    fail "a copy lies $m m from our body (-1: unreadable; more than 0.3 m)"
done < <(sed -n 's/.*corpse: proxy [0-9]* now \([-0-9.]*\) m from.*/\1/p' <<<"$now")
reject 'degraded: fallback corpse pose'
reject_partner 'degraded: fallback corpse pose'
reject 'watchdog: game thread silent'
reject_partner 'watchdog: game thread silent'
