# As a guest: a barrel the host blew up before we joined, standing whole in front of us, is the host's wreck here at the join (its record), with no blast and no harm
# shellcheck shell=bash disable=SC2317,SC2154
# Where: DEVICES_AT (a fixture or "X Y Z": 10 to 40 m from a whole explosive
# barrel; unset: found in game, find_spot barrel 10 40).
# To look at: at the join the barrel is its wreck at once, silently, with no
# explosion; V is not hurt.
ensure join
peer host --mode idle --body female
expect 'in the session as player' 30
at_device DEVICES_AT barrel 10 40
mark
cmd device near barrel
line=$(await 'devices: nearest barrel [0-9a-f]+ [A-Za-z]+ at [0-9.]+ m: [a-z]+, ' 10) || fail "no whole barrel within 200 m"
[[ $line =~ nearest\ barrel\ ([0-9a-f]+)\ ([A-Za-z]+)\ at\ [0-9.]+\ m:\ ([a-z]+), ]] || fail "unreadable: $line"
barrel=${BASH_REMATCH[1]} cls=${BASH_REMATCH[2]} power=${BASH_REMATCH[3]}
note "the barrel: $barrel $cls ($power)"
query look device "$barrel" look
expect_value "$look" = whole
shot devices-barrel-record-before
# Out of the session; the mock host's world has it blown up before we come
# back (its record), and we join again standing where we are.
cmd coop leave
expect 'devices: session over, ' 15
pause 2
peer host --mode idle --body female --device "$barrel:$cls:$power,exploded@0"
expect_peer "our device $barrel .*exploded.* \\(--device\\)" 10
query before health
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
mark
cmd coop join "$here:27450"
expect 'in the session as player' 30
expect "devices: $barrel $cls set as the host's wreck, in front of us" 15
pause 3
query wreck device "$barrel" look
expect_value "$wreck" = wreck
query after health
expect_changed "$before" "$after" same
reject "devices: $barrel $cls set off by"
reject "devices: $barrel $cls stays whole here"
shot devices-barrel-record-after
# Our own world back when we leave (its state whole again); its entity
# streams in whole once V has been away: V goes far off and the test reset
# brings it back.
cmd coop leave
expect 'devices: session over, [0-9]+ followed, [1-9][0-9]* put back as our own save had them' 15
at arasaka-park
reject 'watchdog: game thread silent'
# Back as the harness has a guest: joining the laptop again (the next
# scenario's mock host).
cmd coop join "$here:27450"
expect 'session: starting as join of' 10
