# As host: the mock guest quickhacks one of our shared cars; our game applies it to the real car and marks it "Hacked by NAME" while it lasts
# shellcheck shell=bash disable=SC2317,SC2154
# Needs traffic around V: run it from a street (the test field has parked
# cars only; `tools/game.sh cmd teleport ...` to a busy road first).
# To look at: the nearest car brakes hard (its alarm on); a marker in the
# other player's colour, "Hacked by NAME", over it in the world and on the
# minimap for 8 s.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --hack-traffic 10
expect 'joined \(player [0-9]+' 20
expect_peer 'street: vehicle 0x[0-9a-f]+ ' 30
expect_peer 'street: hacked vehicle 0x[0-9a-f]+ \(brakes, 8 s\)' 30
expect 'hacks: player [0-9]+ hacked vehicle [0-9]+: brakes \(8\.0+ s\), applied \(we run it\)' 10
expect 'hacks: vehicle [0-9]+ marked: hacked by player [0-9]+ for 8\.0+ s, registered true' 5
query marked car marks
expect_value "$marked" != none
shot traffic-hack-marked
# The hack is over: the marker goes.
expect 'hacks: vehicle [0-9]+ unmarked \(player [0-9]+.s hack\)' 15
query unmarked car marks
expect_value "$unmarked" = none
reject 'watchdog: game thread silent'
