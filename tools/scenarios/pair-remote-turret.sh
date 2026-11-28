# Two games: our V takes a turret: the partner's copy follows our hands, the partner's own takeover of it is refused with who holds it, our time bubble centres on the turret on both games, the partner's party entry says "At a turret", and no scripted moment is ours
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: TURRET_AT (a fixture or "X Y Z": a sentry, a security turret that
# is not the police's, within 60 m; none is known yet, testing.md
# "Fixtures": `fixture check` lists the nearest).
# To look at (the pair of screenshots): ours from the turret, the partner's
# copy pointing where ours looks; "NAME is controlling this turret" on the
# partner's screen.
ensure host
[ -n "${TURRET_AT:-}" ] || fail "TURRET_AT names no spot near a sentry (none is known: testing.md, Fixtures)"
cmd god on
at "$TURRET_AT"
partner_join
pcmd god on
cmd device near sentry
expect 'devices: nearest sentry [0-9a-f]+ [A-Za-z]+ at ' 10
turret=$(since | grep -oE 'nearest sentry [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the turret: $turret"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 2.0 }') $y $z"
mark
pmark
cmd remote turret
expect "remote: our V takes control of turret $turret" 5
expect "remote: our V holds turret $turret, [0-9.]+ m from its body" 5
expect "turrets: our V holds $turret: where it points said to the others" 5
expect_partner "turrets: $turret is in player [0-9]+.s hands: our copy follows it" 10
expect_partner 'hud: player [0-9]+ ".*" health [0-9]+ % status remote' 10
pquery theirs remote player 1
expect_value "$theirs" '~' '^turret '
reject 'spectate: our V is in a scripted moment'
# The partner's takeover of it meanwhile: refused, told who holds it.
pcmd remote turret
expect_partner "turrets: our claim on $turret refused: player 1 holds it" 10
expect_partner 'coop notice: .* \[Turrets-Busy\]' 5
# Our scanner at the turret: our bubble on it, on both games.
mark
pmark
cmd time scan 0.2 3
expect 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 5
expect_partner 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 10
shot pair-remote-turret
pshot pair-remote-turret-partner
compare devices "$turret"
expect 'time: test dilation over' 10
mark
pmark
cmd remote off
expect 'remote: our V is back in its body' 5
expect_partner "turrets: $turret is out of player [0-9]+.s hands" 10
reject 'watchdog: game thread silent'
