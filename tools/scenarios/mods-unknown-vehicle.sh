# A player drives a car from a mod our game lacks: a vanilla two-seater copy spawns and the ride works, said once
# shellcheck shell=bash disable=SC2317
# On purpose: a record of a mod our game lacks.
allow_fallback "record substituted"
ensure host
# No game has Vehicle.tandem_modded_coupe: a mod's two-seat car.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body male \
  --vehicle-record Vehicle.tandem_modded_coupe:Car:2 --drive-seconds 15
expect 'joined \(player [0-9]+' 20
expect 'mods: unknown vehicle 0x[0-9a-f]+ from .*, shown as Vehicle.v_sport2_porsche_911turbo' 30
expect 'drives Vehicle.v_sport2_porsche_911turbo, copy spawning' 10
expect 'seated in seat_front_left' 30
expect "coop notice: .*'s vehicle comes from a mod you don't have" 10
expect 'on foot' 40
expect 'car left parked' 10
cmd coop mods
expect "coop: mods: .*'s vehicle 0x[0-9a-f]+ - Vehicle.v_sport2_porsche_911turbo" 5
reject 'watchdog: game thread silent'
