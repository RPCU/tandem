# A player's car carries a custom paint (CrystalCoat): our copy of it is painted as it spawns, by the car's own state, and stock again when its owner switches the paint off
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body female \
  --paint c81e1e,1e1ec8@120 --paint-off-at 60
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
expect_peer "our car's paint sent: primary c81e1e, secondary 1e1ec8, lights hue 120" 10
expect "paint: player [0-9]+'s car copy: primary 200,30,30 secondary 30,30,200 lights 120" 15
# The copy's own state holds it (what the game's handlers read).
pause 3
query painted paint puppet
expect_value "$painted" = 'primary 200,30,30 secondary 30,30,200 lights 120'
shot paint-on
# Our V rides in it: the game's own mount does not take the paint off, or
# what it takes off is put back.
cmd vehicle ride
expect 'vehicle ride: in player [0-9]+' 10
pause 5
query riding paint puppet
expect_value "$riding" = 'primary 200,30,30 secondary 30,30,200 lights 120'
cmd vehicle unride
# Its owner switches it off: stock paint here too.
expect_peer "our car's paint sent: stock" 70
expect "paint: player [0-9]+'s car copy: stock" 10
pause 2
query stock paint puppet
expect_value "$stock" = stock
shot paint-off
# Nothing of ours is said: our V drove no car of its own.
reject "paint: our car's paint said: (primary|unique)"
reject 'watchdog: game thread silent'
