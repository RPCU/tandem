# Two games: a radio or TV's station our V picks plays on the partner's copy; the partner picks another and ours follows
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: DEVICES_AT (a fixture or "X Y Z": a radio or TV within 40 m; default
# v-door, V's flat).
# To listen to: the same station on both games after each change (the song's
# moment may differ: no playback position is shared).
ensure host
at "${DEVICES_AT:-v-door}"
partner_join
cmd device near radio
expect 'devices: nearest radio [0-9a-f]+ [A-Za-z]+ at ' 10
radio=$(since | grep -oE 'nearest radio [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the radio or TV: $radio"
cmd where
expect 'where: [-0-9.]+ ' 5
read -r x y z < <(since | grep -oE 'where: [-0-9.]+ [-0-9.]+ [-0-9.]+' | tail -1 | awk '{ print $2, $3, $4 }')
pat "$(awk -v x="$x" 'BEGIN { print x + 1.0 }') $y $z"
# Its devices stream in after the teleport (none within 200 m 5 s after it
# stood there, 2026-11-28): asked every 3 s, 45 s at most.
found=''
for _ in $(seq 1 15); do
  pmark
  pcmd device near radio
  sleep 3
  if psince | grep -q "devices: nearest radio $radio "; then
    found=1
    break
  fi
done
[ -n "$found" ] || fail "the partner never followed the radio $radio (device near radio, 45 s)"
cmd device station 0
pause 4
mark
pmark
cmd device station 1
expect "devices: $radio [A-Za-z]+ is now [a-z]+, [a-z+]+, value 1" 10
expect_partner "devices: $radio [A-Za-z]+ by player 1: [a-z]+, [a-z+]+, value 1, played" 10
compare devices "$radio"
pcmd device station 2
expect_partner "devices: $radio [A-Za-z]+ changed here \\([a-z]+, [a-z+]+, value 2\\): asked of the host" 10
expect "devices: player [0-9]+ operated $radio" 10
expect "devices: $radio [A-Za-z]+ after player [0-9]+'s request: [a-z]+, [a-z+]+, value 2" 10
pause 2
compare devices "$radio"
reject "devices: $radio [A-Za-z]+ will not become"
reject_partner "devices: $radio [A-Za-z]+ will not become"
reject 'watchdog: game thread silent'
