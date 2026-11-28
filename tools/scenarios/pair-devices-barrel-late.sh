# Two games: a barrel our game blew up while the partner was out of the session, standing whole in front of the partner, is its wreck on the partner's screen the moment it joins, with no blast
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a spot 10 to 40 m from a whole
# explosive barrel; unset: found in game, find_spot barrel 10 40).
# To look at: on the partner's screen the barrel stands whole until the
# join, then is its wreck at once, silently (the blast was ours, before it
# came); on ours, the wreck since our blast.
ensure host
at_device DEVICES_AT barrel 10 40
partner_join
mark
cmd device near barrel
line=$(await 'devices: nearest barrel [0-9a-f]+ [A-Za-z]+ at ' 10) || fail "no whole barrel within 200 m"
[[ $line =~ nearest\ barrel\ ([0-9a-f]+)\ ([A-Za-z]+)\ at ]] || fail "unreadable: $line"
barrel=${BASH_REMATCH[1]} cls=${BASH_REMATCH[2]}
note "the barrel: $barrel $cls"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
pmark
pcmd device near barrel
expect_partner "devices: nearest barrel $barrel " 10
pquery plook device "$barrel" look
expect_value "$plook" = whole
# The partner leaves; our V blows the barrel up; the partner, still beside
# it, its own copy whole, joins again.
pcmd coop leave
expect_partner 'devices: session over, ' 15
pause 3
mark
cmd device explode barrel
# Its fuse, then its mesh swapped for the wreck (the game's own delays).
for _ in 1 2 3 4 5 6; do
  pause 2
  query look device "$barrel" look
  [ "$look" != wreck ] || break
done
expect_value "$look" = wreck
pquery plook device "$barrel" look
expect_value "$plook" = whole
pquery before health
shot pair-barrel-late-before
pmark
pcmd coop join "127.0.0.1:$game_port"
expect_partner 'in the session as player [0-9]+' 30
expect_partner "devices: $barrel $cls set as the host's wreck, in front of us" 15
pause 3
pquery plook device "$barrel" look
expect_value "$plook" = wreck
pquery after health
expect_changed "$before" "$after" same
reject_partner "devices: $barrel $cls set off by"
reject_partner "devices: $barrel $cls stays whole here"
shot pair-barrel-late-after
compare devices "$barrel"
reject 'watchdog: game thread silent'
