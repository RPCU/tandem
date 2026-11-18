# As host riding in the mock player's car: the driver vanishes (no goodbye): our V is let out, the copy stays parked, nothing freezes
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body female --vanish-at 25
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
cmd vehicle ride
expect 'vehicle ride: in player [0-9]+' 10
expect_peer 'vanishing \(--vanish-at\)' 30
expect 'player [0-9]+ left' 15
expect 'gone at the wheel, our V let out of their car' 5
expect 'coop notice: tandem-peer left the session' 5
expect 'car left parked' 10
pause 8
reject 'watchdog: game thread silent'
