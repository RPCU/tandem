# Two games: an automatic door opens for the partner's V coming up to it, on both games, and shuts on both once the partner is gone from it
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a spot 12 to 40 m from an
# automatic door, far enough that it does not open for our own V; unset:
# found in game, find_spot autodoor 12 40).
# To look at and listen to: the door slides open with its sound on both
# screens as the partner's V reaches it (ours shows its puppet there), and
# slides shut on both after the partner walks off.
ensure host
at_device DEVICES_AT autodoor 12 40
partner_join
spot=$found
mark
cmd device near autodoor
line=$(await 'devices: nearest autodoor [0-9a-f]+ [A-Za-z]+ at [0-9.]+ m: ' 10) || fail "no automatic door within 200 m"
[[ $line =~ nearest\ autodoor\ ([0-9a-f]+)\ ([A-Za-z]+)\ at ]] || fail "unreadable: $line"
door=${BASH_REMATCH[1]}
note "the door: $door ${BASH_REMATCH[2]}"
[[ $line != *open* ]] || fail "the door is open before anyone comes: $line"
read -r x y z <<<"$spot"
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
mark
pmark
# The partner's V at the door: its own game's door opens for it, and asks
# the host; ours opens with it.
pcmd device goto autodoor
expect_partner "devices: at autodoor $door " 10
expect_partner "devices: $door [A-Za-z]+ changed here \\([a-z]+, [a-z+]*open[a-z+]*, value -?[0-9]+\\): asked of the host" 10
expect "devices: player [0-9]+ operated $door" 10
pause 2
mark
cmd device near autodoor
expect "devices: nearest autodoor $door [A-Za-z]+ at [0-9.]+ m: [a-z]+, [a-z+]*open" 10
shot pair-autodoor-open
compare devices "$door"
# The partner walks off (back beside our V): its door shuts itself, and
# ours with it.
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pause 8
mark
pmark
cmd device near autodoor
line=$(await "devices: nearest autodoor $door " 10) || fail "the door is not followed any more"
[[ $line != *open* ]] || fail "still open on our game after the partner left it: $line"
pcmd device near autodoor
expect_partner "devices: nearest autodoor $door " 10
reject_partner "devices: nearest autodoor $door [A-Za-z]+ at [0-9.]+ m: [a-z]+, [a-z+]*open"
shot pair-autodoor-shut
compare devices "$door"
reject 'watchdog: game thread silent'
