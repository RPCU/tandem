# As a guest: a prop the mock host moved before we joined lies here where the session's record has it
# shellcheck shell=bash disable=SC2317,SC2154
# Where: PROPS_AT (as props-host: V looking at a placed prop with a physics
# body; default kabuki-market).
# Verdict: our body is put at the record's rest within 0.25 m (test query
# prop), never shown moving, and is not kinematic.
# To look at: the prop where the host left it as soon as we arrive.
ensure join
# V turns at the spot until it looks at a prop (eight headings at most: a
# fixture's own heading faced none, 2026-11-28).
read -r px py pz _ <<<"$(spot "${PROPS_AT:-kabuki-market}")"
for yaw in 0 45 90 135 180 225 270 315; do
  at "$px $py $pz $yaw"
  mark
  cmd props look
  expect 'props: (looks at [0-9a-f]+|V looks at no placed entity)' 5
  since | grep -qE 'props: looks at .* kinematic false' && break
done
since | grep -q 'props: looks at ' ||
  fail "V looks at no prop at ${PROPS_AT:-kabuki-market}, whichever way it turns: set PROPS_AT to a spot facing one"
# A body kinematic by design (a vending machine) is no physics prop: no
# game moves it, so it proves nothing here.
since | grep -qE 'props: looks at .* kinematic false' ||
  fail "V looks at a kinematic body at ${PROPS_AT:-kabuki-market} ($(since | grep -oE 'props: looks at [0-9a-f]+ \([A-Za-z]+\)' | tail -1)), whichever way it turns: set PROPS_AT to a spot facing a bin, a crate or a chair"
read -r prop px py pz < <(since | grep -oE 'props: looks at [0-9a-f]+ .* part 0 at [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $4, $(NF - 2), $(NF - 1), $NF }')
note "prop $prop at $px $py $pz"
peer host --mode front --body male --prop "$prop:$px,$py,$pz@0"
expect_peer "prop $prop: at rest at \\(.*\\), told" 10
expect 'in the session as player' 30
expect "props: player [0-9]+'s $prop part 0 at rest at \\(.*\\): put there" 15
read -r rx ry rz < <(tail -n +$((peer_mark + 1)) "$peer_log" | grep -oE "prop $prop: at rest at \([-0-9. ]+\), told" | tail -1 | sed 's/.*(//; s/).*//')
query body prop "$prop"
read -r bx by bz how _ kin _ <<<"$body"
expect_value "$how" = still
expect_value "$kin" = false
apart=$(awk -v a="$bx" -v b="$by" -v c="$bz" -v x="$rx" -v y="$ry" -v z="$rz" 'BEGIN { printf "%.3f", sqrt((a - x) ^ 2 + (b - y) ^ 2 + (c - z) ^ 2) }')
note "our body at $bx $by $bz, the record's rest at $rx $ry $rz: $apart m apart"
expect_value "$apart" '<' 0.25
shot props-latecomer
reject 'props: player [0-9]+ moves'
reject 'watchdog: game thread silent'
