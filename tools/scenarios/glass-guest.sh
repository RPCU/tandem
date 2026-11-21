# As a host: something breaks in the mock guest's game near V (world glass has no name: an impulse at the place); the levers tried alone first
# shellcheck shell=bash disable=SC2317
# Where: GLASS_AT (a fixture or "X Y Z [YAW]": V looking at a window or a glass
# pane, a shop front or a bus shelter; default kabuki-market, testing.md
# "Fixtures") and
# GLASS_BREAK ("X,Y,Z": a point on that pane; default: where V stands,
# which proves the path, not the pane).
# To look at: the pane V looks at shatters at `glass break`; the pane at
# GLASS_BREAK when the guest's break arrives, with nobody shooting; nothing
# and nobody takes damage.
ensure host
at "${GLASS_AT:-kabuki-market}"
mark
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
note "V at $x $y $z"
# The levers alone first (no session needed): an impulse at what V looks at.
cmd glass break
expect 'glass: an impulse of [0-9.]+ m at \(' 5
shot glass-break-command
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --fracture "${GLASS_BREAK:-$x,$y,$z}@6"
expect 'joined \(player [0-9]+' 30
expect_peer 'something broke here at \(' 20
expect 'glass: player [0-9]+ broke something at \(.*\): an impulse there' 10
shot glass-guest
cmd glass
expect 'objects: [0-9]+ break\(s\) told, [1-9][0-9]* replayed' 5
reject 'watchdog: game thread silent'
