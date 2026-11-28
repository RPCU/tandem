# Two games: the partner summons its own car and drives it remotely: our copy of its car follows its state with nobody in it, the partner's puppet stays on foot, and the copy stays parked where it stopped
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: the test field (a summoned car can reach it).
# To look at (the pair of screenshots): the partner's car rolling on both
# screens with nobody at the wheel, the partner's V standing where it was.
ensure host
partner_join
pid=$(since | grep -oE 'joined \(player [0-9]+' | tail -1 | grep -oE '[0-9]+$')
note "the partner is player $pid"
pcmd garage summon
expect_partner 'garage: summon asked \(true\)' 10
expect_partner 'garage: our Vehicle\.[A-Za-z0-9_]+ is here' 60
pmark
mark
pcmd remote own
expect_partner 'remote: our V takes control of our own car [0-9a-f]+ ' 10
expect_partner 'remote: our V drives remotely [0-9a-f]+, [0-9.]+ m from its body, shared vehicle 0' 10
expect_partner 'our seat: Vehicle\.[A-Za-z0-9_]+ tandemRemote \(car of player 0\)' 10
# Ours: its car followed (the copy it left, or a new one), its puppet on foot.
expect 'player [0-9]+: drives Vehicle\.[A-Za-z0-9_]+ remotely' 10
reject "player [0-9]+: seated in "
query seated mounted puppet
expect_value "$seated" = none
pquery pbefore remote
expect_value "$pbefore" '~' '^vehicle .* 0$'
query before car player "$pid"
pcmd remote push 10
pause 4
query after car player "$pid"
pquery pafter remote
note "the car here: $before -> $after; on the partner's game: $pafter"
awk -v a="$before" -v b="$after" 'BEGIN { split(a, p, " "); split(b, q, " "); d = sqrt((q[1] - p[1]) ^ 2 + (q[2] - p[2]) ^ 2); exit !(d > 3) }' ||
  fail "our copy of the partner's car did not move with its remote drive ($before -> $after)"
awk -v a="$after" -v b="$pafter" 'BEGIN { split(a, p, " "); split(b, q, " "); d = sqrt((q[2] - p[1]) ^ 2 + (q[3] - p[2]) ^ 2); exit !(d < 3) }' ||
  fail "the partner's car stands apart on the two games ($after / $pafter)"
shot pair-remote-own-car
pshot pair-remote-own-car-partner
# Out of it: the copy left parked where it stopped.
mark
pmark
pcmd remote off
expect_partner 'remote: our V is back in its body' 10
expect 'player [0-9]+: car left parked' 10
reject 'watchdog: game thread silent'
