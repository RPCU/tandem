# As a guest: our copy of a turret never fires on its own; it draws the mock host's turret's aim and shots, and fires for real at our V only on the host's word
# shellcheck shell=bash disable=SC2317
# Where: TURRET_AT (a fixture or "X Y Z": a security turret we follow, a
# "sentry", within 200 m: V goes beside it, and `turret provoke` makes it
# take V; default arasaka-park; the police's are not followed, as
# turret-host). V is shot at: the test reset's god mode keeps it alive.
# To look at: `turret shot` first: the copy turns to what V looks at and one
# tracer leaves its barrels. Then the copy locks V, stays silent for half a
# second (the host's word on its way), and fires ten shots at V with its
# own tracers and sounds; V's health falls by our own game's hits.
ensure join
at "${TURRET_AT:-arasaka-park}"
peer host --mode idle --body female --turret-shots 10 --device-answer on
expect 'in the session as player' 30
mark
cmd device near sentry
expect 'devices: nearest sentry [0-9a-f]+ SecurityTurretControllerPS at ' 10
turret=$(since | grep -oE 'devices: nearest sentry [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "turret $turret"
cmd device goto sentry
expect "devices: at sentry $turret SecurityTurretControllerPS: " 10
pause 2
# On: a save may hold it off; the host says yes.
cmd device on sentry
expect "devices: on $turret SecurityTurretControllerPS at " 10
pause 3
# The levers alone: the host's aim and one drawn shot on our copy.
cmd turret shot
expect "turrets: $turret draws a shot there \(drawn [1-9]" 5
shot turret-guest-drawn
# Our copy locks our V (made to: the spot's turret may be friendly to V, or
# V out of its sight): said to the host, which fires at us through it.
cmd turret provoke
expect "turrets: $turret takes our V as a target: true \(followed true" 5
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
