# As host: our traffic and parked cars shared with a guest, its rams applied to the real cars, a car it takes from its driver becomes its own
# shellcheck shell=bash disable=SC2317,SC2154
# Needs traffic around V: the test field has parked cars only (2026-11-21:
# the guest took a parked one, nobody to put out). TRAFFIC_AT (a fixture or
# "X Y Z [YAW]", default drake-ave, testing.md "Fixtures") is a busy street.
ensure host
at "${TRAFFIC_AT:-drake-ave}"
pause 5
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --ram-traffic 4 --steal-at 25 --steal-driven --drive-seconds 15
expect 'joined \(player [0-9]+' 20
# Every car and bike of our world near a player is announced, and the guest
# hears of each one once.
expect_peer 'street: vehicle 0x[0-9a-f]+ ' 30
pause 3
cmd street count
expect 'street count: [0-9]+ \([1-9][0-9]* run by us' 10
# The guest's car rams one: our game applies it to the real car (its driver
# honks, panics or is knocked out, the game's own way).
expect_peer 'street: rammed vehicle' 30
expect 'street: player [0-9]+ hit vehicle [0-9]+ for' 10
# It gets into one with a driver: that car is the guest's from now on, its
# driver out beside it, and its puppet sits at the wheel of the real car, not
# in a second copy nor beside the driver.
expect_peer 'street: getting into vehicle 0x[0-9a-f]+ \(driven\)' 40
expect "street: vehicle [0-9]+ is player [0-9]+'s car now" 10
expect_peer 'taken: it is player [0-9]+.s car now \(ours\)' 10
expect 'street: [A-Za-z0-9_.]+ put out of seat_front_left' 5
expect 'drives the vehicle it took from the street' 10
expect "seated in seat_front_left of player [0-9]+'s car" 20
reject 'is taken here\) of player'
reject 'copy spawning'
query seat mounted puppet
expect_value "$seat" '~' ' seat_front_left '
shot traffic-host-taken
expect_peer 'street: out of the car we took' 40
expect 'player [0-9]+: on foot' 20
reject 'watchdog: game thread silent'
