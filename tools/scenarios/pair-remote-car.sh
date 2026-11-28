# Two games: the partner takes control of a parked car our game runs: the car is handed to the partner's game, which drives it from where its V stands; our copy moves with it, our party HUD says "Driving remotely", and the car stays the partner's while it drives
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: the test field (its parked cars are shared by the host's game, the
# area's owner).
# To look at (the pair of screenshots): the car rolling on both screens
# with nobody in it, the partner's V standing where it was.
ensure host
partner_join
# The partner takes the nearest car (the take control quickhack's own
# event): a shared vehicle our game runs.
pcmd remote car
expect_partner 'remote: our V takes control of vehicle [0-9a-f]+ \(shared vehicle [0-9]+\)' 10
car=$(psince | grep -oE 'takes control of vehicle [0-9a-f]+ \(shared vehicle [0-9]+\)' | tail -1 | grep -oE '[0-9]+\)$' | tr -d ')')
note "the shared vehicle: $car"
[ -n "$car" ] && [ "$car" != 0 ] || fail "the partner took a car that is not shared (a player's car?)"
expect_partner "remote: our V drives remotely [0-9a-f]+, [0-9.]+ m from its body, shared vehicle $car" 10
# Handed to the partner's game at once, no teleport, no traffic drive.
expect "street: handover out: vehicle $car to player [0-9]+" 10
expect_partner "street: handover in: vehicle $car from player 1 \\(our V drives it remotely\\)" 10
expect 'hud: player [0-9]+ ".*" health [0-9]+ % status remote' 10
pquery theirs remote
expect_value "$theirs" '~' "^vehicle .* $car\$"
query before traffic "$car" position
note "the car here before: $before"
# The partner drives it on: our copy follows.
pcmd remote push 10
pause 4
query after traffic "$car" position
pquery pafter traffic "$car" position
note "the car here after: $after, on the partner's game: $pafter"
awk -v a="$before" -v b="$after" 'BEGIN { split(a, p, " "); split(b, q, " "); d = sqrt((q[1] - p[1]) ^ 2 + (q[2] - p[2]) ^ 2); exit !(d > 3) }' ||
  fail "our copy of vehicle $car did not move with the partner's drive ($before -> $after)"
awk -v a="$after" -v b="$pafter" 'BEGIN { split(a, p, " "); split(b, q, " "); d = sqrt((q[1] - p[1]) ^ 2 + (q[2] - p[2]) ^ 2); exit !(d < 3) }' ||
  fail "vehicle $car stands apart on the two games ($after / $pafter)"
shot pair-remote-car
pshot pair-remote-car-partner
# Still the partner's while it drives: nothing asks for it back.
reject "street: handover of vehicle $car to player [0-9]+ refused"
reject_partner "street: handover out: vehicle $car "
# Out of the car: the partner is back in its body.
pmark
pcmd remote off
expect_partner 'remote: our V is back in its body' 10
pquery pback remote
expect_value "$pback" = none
reject 'watchdog: game thread silent'
