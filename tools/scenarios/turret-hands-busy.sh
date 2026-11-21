# As a host: the mock guest holds a turret; our V's takeover of it is refused (first come), our V is let go of it at once and told who has it, and the guest's hold goes on
# shellcheck shell=bash disable=SC2317
# Where: TURRET_AT (a fixture or "X Y Z": V in sight of a security turret
# within 60 m, as turret-hands; default arasaka-park).
# To look at: V takes control of the turret and is back in her body within
# a moment, with the notice "NAME is controlling this turret"; the turret
# keeps turning where the guest points it until the guest lets go (20 s).
ensure host
at "${TURRET_AT:-arasaka-park}"
mark
cmd device near turret
expect 'devices: nearest turret [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest turret [0-9a-f]+' | tail -1 | awk '{ print $4 }')
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
note "turret $turret, V at $x $y $z"
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --turret-hands "$turret:$x,$y,$z@4" --turret-hands-for 20
expect 'joined \(player [0-9]+' 30
expect_peer "turret $turret: in our V's hands" 20
expect "turrets: $turret is in player [0-9]+'s hands: our copy follows it" 5
cmd turret take
expect "turrets: our V takes control of $turret" 5
# The claim itself: without it nothing is refused (a police turret's was
# never said, 2026-11-21).
expect "turrets: our V holds $turret: where it points said to the others" 5
expect "turrets: our claim on $turret refused: player [0-9]+ holds it; our V lets go" 5
expect 'coop notice: .+ is controlling this turret \[Turrets-Busy\]' 5
shot turret-hands-busy
# Out of the turret for real (the takeover system's own word), and our
# copy still follows the guest's hold.
cmd turret
expect 'turrets: .* in others. hands 1, .* claims refused 1; our V controls a device: false' 5
expect_peer "turret $turret: our V lets go of it" 30
expect "turrets: $turret is out of player [0-9]+'s hands: our copy is our game's again" 5
# One hold from end to end: our V's takeover did not end the guest's.
held=$(since | grep -cE "turrets: $turret is in player [0-9]+'s hands: our copy follows it")
[ "$held" = 1 ] || fail "the guest's hold of $turret began $held times: our V's takeover ended it"
reject 'watchdog: game thread silent'
