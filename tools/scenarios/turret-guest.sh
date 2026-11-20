# As a guest: our copy of a turret never fires on its own; it draws the mock host's turret's aim and shots, and fires for real at our V only on the host's word
# shellcheck shell=bash disable=SC2317
# Where: TURRET_AT ("X Y Z": V in sight of a hostile security turret that
# is on, within 60 m; V's flat has none, so name one). V is shot at: the
# test reset's god mode keeps it alive.
# To look at: `turret shot` first: the copy turns to what V looks at and one
# tracer leaves its barrels. Then the copy locks V, stays silent for half a
# second (the host's word on its way), and fires ten shots at V with its
# own tracers and sounds; V's health falls by our own game's hits.
ensure join
cmd teleport "${TURRET_AT:-home}"
pause 4
peer host --mode idle --body female --turret-shots 10
expect 'in the session as player' 30
mark
cmd device near turret
expect 'devices: nearest turret [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest turret [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "turret $turret"
# The levers alone: the host's aim and one drawn shot on our copy.
cmd turret shot
expect "turrets: $turret draws a shot there \(drawn [1-9]" 5
shot turret-guest-drawn
# Our copy locks our V: said to the host, which fires at us through it.
expect "turrets: our copy of $turret has our V: said to the host" 60
expect_peer "turret $turret: player [0-9]+'s copy has the V of player [0-9]+" 10
expect "turrets: the host's turret $turret fires at our V: our copy's real shot" 10
shot turret-guest-fire
expect_peer "turret $turret: follows nobody" 15
# The mock host then says nothing more of it: left to our own game.
expect "turrets: the host is silent about $turret: our copy fires on its own" 10
cmd turret
expect 'turrets: [0-9]+ spoken of; shots told 0, drawn [0-9]+, real at our V [1-9]' 5
reject 'watchdog: game thread silent'
