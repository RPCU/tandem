# Two games: our officers see the wanted partner: its stars are active (red), not grey; its game then runs its own area (it holds its region beside us): our officers' proxies are no longer viewers in its prevention system
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: POLICE_AT (a fixture or "X Y Z": roads near V; default drake-ave).
# What it proves: the partner's stars active while our officers see it (its
# prevention system's own star state, through PoliceAgentRegistry's "police
# in combat with the player": their proxies are in no registry there), and
# TandemPolice.Unwatch. While another game runs its area a
# guest registers that game's officers' proxies as viewers of its V (Watch);
# when its own game runs the area again (here: `region hold on`, the gig
# rule, with both Vs and the officers where they are: the officers stay our
# proxies on its game) they must leave its prevention system's own list, or
# its stars stay seen for as long as the proxies stand there.
ensure host
at "${POLICE_AT:-drake-ave}"
partner_join
# Our units fire on the wanted partner: it is not to die of it (a V that
# leaves its session down dies, revive.md; its next scenario found it dead).
pcmd god on
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 12.0 }') $y $z"
mark
pmark
pcmd police heat 2
expect_partner 'police: our wanted level 2 ' 15
expect 'police: unit .* out for player [0-9]+' 40
# Our officers see the partner: its prevention system has their proxies
# among its viewers (its own list, read from it) and its stars are active
# (state 1: Active; 2 is the grey Searching), 60 s at most.
seen=''
for _ in $(seq 1 20); do
  pmark
  pcmd police report
  sleep 3
  if psince | grep -qE 'police report: our wanted level [1-5] \(state 1\), we follow: .* [1-9][0-9]* of the host.s police see our V, [1-9][0-9]* proxy viewer\(s\) in our prevention system'; then
    seen=1
    break
  fi
done
[ -n "$seen" ] || fail "the partner's V was never seen by our officers with its stars active (police report: state 1, a proxy viewer)"
note "our officers see the partner: its stars active, their proxies its viewers"
shot pair-police-area-seen
# The partner's game runs its own area now, beside us.
pmark
pcmd region hold on
expect_partner 'region hold on' 5
expect_partner 'police: [1-9][0-9]* of the other owner.s police no longer see our V for our prevention system' 10
pcmd region
expect_partner 'region: .*area ours' 10
pause 2
# Its prevention system's own list: none of our proxies in it any more.
pmark
pcmd police report
expect_partner 'police report: our wanted level [0-5] .*we answer the others. heat.* 0 of the host.s police see our V, 0 proxy viewer\(s\) in our prevention system' 10
pcmd region hold off
pcmd police calm
expect_partner 'police: our wanted level 0 ' 60
expect 'police: unit for player [0-9]+ stands down' 20
reject 'police: our wanted level [1-5] '
reject 'watchdog: game thread silent'
