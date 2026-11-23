# A player's world marker keeps its distance in metres, near (the nameplate's name), far (the marker's name before it), with nameplates off and always, on a marker made again for a player who came back, and in kilometres past 1 km (the shots: never "{0, number, 0}M")
# shellcheck shell=bash disable=SC2317
ensure host
address="$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450
# Near: 3 m in front, in the nameplate's range: the distance alone.
peer join "$address" --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'marker: player [0-9]+ \(.*\) on the puppet, slot .*, registered true' 30
pause 3
shot markers-near
# Nameplates off: no name anywhere, the distance alone.
cmd coop set nameplates off
expect 'setting: nameplates = off' 10
pause 2
shot markers-near-plates-off
cmd coop set nameplates near
expect 'setting: nameplates = near' 10
# Far, 40 m ahead (the test field faces +Y), out of the nameplate's range:
# the name before the distance. The player leaves and comes back: a marker
# made again, maybe on the pooled controller of the last one.
pkill -x tandem-peer || true
expect 'marker: player [0-9]+ removed' 45
peer join "$address" --mode mirror --offset 0,40,0 --body female
expect 'joined \(player [0-9]+' 20
expect 'marker: player [0-9]+ \(.*\) on the puppet, slot .*, registered true' 30
pause 3
shot markers-far
# Always: the nameplate at any distance, so the marker's distance alone.
cmd coop set nameplates always
expect 'setting: nameplates = always' 10
pause 2
shot markers-far-plates-always
cmd coop set nameplates near
expect 'setting: nameplates = near' 10
pause 2
shot markers-far-again
cmd markers
expect 'markers: player [0-9]+ \(.*\) on the puppet at ' 10
# Past a kilometre: kilometres, as the game writes a quest's.
pkill -x tandem-peer || true
expect 'marker: player [0-9]+ removed' 45
peer join "$address" --mode mirror --offset 0,1500,0 --body female
expect 'joined \(player [0-9]+' 20
expect 'marker: player [0-9]+ \(.*\) on the puppet, slot .*, registered true' 30
pause 3
shot markers-km
reject 'watchdog: game thread silent'
