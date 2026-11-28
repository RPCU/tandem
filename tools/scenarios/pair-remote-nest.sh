# Two games: our V takes a sniper nest: the partner's copy of it turns with our hands and draws its fire, its party entry says "Remote control", and our time bubble centres on the nest
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: NEST_AT (a fixture or "X Y Z": a sniper nest within 60 m, Phantom
# Liberty's Dogtown; none is known yet, testing.md "Fixtures": `fixture
# check` lists the devices near a spot).
# To look at (the pair of screenshots): ours through the nest's scope, the
# partner's copy pointing where ours looks, its shots' tracers from it.
ensure host
[ -n "${NEST_AT:-}" ] || fail "NEST_AT names no spot near a sniper nest (none is known: testing.md, Fixtures)"
cmd god on
at "$NEST_AT"
partner_join
pcmd god on
cmd device near nest
expect 'devices: nearest nest [0-9a-f]+ [A-Za-z]+ at ' 10
nest=$(since | grep -oE 'nearest nest [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the nest: $nest"
pnear nest "$nest"
mark
pmark
cmd remote nest
expect "remote: our V takes control of nest $nest" 5
expect "remote: our V holds device $nest, [0-9.]+ m from its body" 5
expect "turrets: our V holds $nest: where it points said to the others" 5
expect_partner "turrets: $nest is in player [0-9]+.s hands: our copy follows it" 10
expect_partner 'hud: player [0-9]+ ".*" health [0-9]+ % status remote' 10
reject 'spectate: our V is in a scripted moment'
cmd time scan 0.2 3
expect 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 5
expect_partner 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 10
shot pair-remote-nest
pshot pair-remote-nest-partner
compare devices "$nest"
mark
pmark
cmd remote off
expect 'remote: our V is back in its body' 5
expect_partner "turrets: $nest is out of player [0-9]+.s hands" 10
reject 'watchdog: game thread silent'
