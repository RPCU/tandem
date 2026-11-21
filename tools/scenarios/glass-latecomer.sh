# As a guest: what broke in the mock host's game before we joined is broken in ours, at once where V stands and, 300 m away, only when V comes there
# shellcheck shell=bash disable=SC2317
# Where: GLASS_AT (as glass-guest: V looking at a pane; default kabuki-market). The mock
# host breaks a piece of the world where V stands and one 300 m east, both
# before we join (the session's record of breaks, devices.md).
# To look at: the pane at V shatters right after the join; nothing happens
# 300 m away until V is teleported there.
ensure join
at "${GLASS_AT:-kabuki-market}"
mark
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
far=$(awk -v x="$x" 'BEGIN { print x + 300 }')
note "V at $x $y $z, the far break at $far $y $z"
peer host --mode front --body male \
  --fracture "${GLASS_BREAK:-$x,$y,$z}@0" --fracture "$far,$y,$z@0"
expect_peer 'something broke here at \(' 10
expect 'in the session as player' 30
expect 'glass: player [0-9]+ broke something at \(.*\): an impulse there' 15
reject 'which waited for us'
shot glass-latecomer-here
cmd glass
expect 'glass: replayed here: .* [1-9][0-9]* waiting' 5
cmd teleport "$far $y $z"
pause 4
expect 'glass: player [0-9]+ broke something at \(.*\), which waited for us: an impulse there' 15
shot glass-latecomer-far
reject 'watchdog: game thread silent'
