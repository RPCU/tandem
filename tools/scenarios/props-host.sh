# As a host: the prop V looks at, pushed by our V, moves and comes to rest in the mock guest's view where it lies here
# shellcheck shell=bash disable=SC2317,SC2154
# Where: PROPS_AT (a fixture or "X Y Z [YAW]": V looking at a placed prop
# with a physics body, a bin, a crate, a chair; default kabuki-market,
# testing.md "Fixtures").
# Verdict: the rest the mock guest heard is where our body lies (within
# 0.25 m, test query prop), nothing holds it here, and the engine told our
# game of the prop's touches (PhysicalCollisionEvent: props.md, Open).
# To look at: the prop tumbling as alone.
ensure host
at "${PROPS_AT:-kabuki-market}"
mark
cmd props look
expect 'props: (looks at [0-9a-f]+|V looks at no placed entity)' 5
since | grep -q 'props: looks at ' ||
  fail "V looks at no prop at ${PROPS_AT:-kabuki-market}: set PROPS_AT to a spot facing one"
# A body kinematic by design (a vending machine) is no physics prop: no
# game moves it, so it proves nothing here.
since | grep -qE 'props: looks at .* kinematic false' ||
  fail "V looks at a kinematic body at ${PROPS_AT:-kabuki-market} ($(since | grep -oE 'props: looks at [0-9a-f]+ \([A-Za-z]+\)' | tail -1)): set PROPS_AT to a spot facing a bin, a crate or a chair"
prop=$(since | grep -oE 'props: looks at [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "prop $prop"
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 30
pause 3
cmd props push
expect "props: pushed $prop at [0-9.]+ m/s: ours" 5
expect_peer "prop $prop moves at \\(" 10
expect_peer "prop $prop at rest at \\(" 20
read -r rx ry rz < <(tail -n +$((peer_mark + 1)) "$peer_log" | grep -oE "prop $prop at rest at \([-0-9. ]+\)" | tail -1 | sed 's/.*(//; s/)//')
query body prop "$prop"
read -r bx by bz how _ <<<"$body"
expect_value "$how" = still
apart=$(awk -v a="$bx" -v b="$by" -v c="$bz" -v x="$rx" -v y="$ry" -v z="$rz" 'BEGIN { printf "%.3f", sqrt((a - x) ^ 2 + (b - y) ^ 2 + (c - z) ^ 2) }')
note "our body at $bx $by $bz, the guest's rest at $rx $ry $rz: $apart m apart"
expect_value "$apart" '<' 0.25
shot props-host
cmd props
expect 'props: here: [1-9][0-9]* touch\(es\)' 5
reject 'props: player [0-9]+ moves'
reject 'watchdog: game thread silent'
