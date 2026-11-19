# As a guest, our own garage is ours: we summon our own car in the host's world; a vehicle the host's garage gets while we take part is credited in our ledger once, and not unlocked in our garage (vehicle_rewards = credit, the default)
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --unlock-vehicle Vehicle.v_standard2_archer_hella_player
expect 'in the session as player' 30
cmd garage
expect 'garage: our cars lent to (crew|owner|never)' 10
expect 'garage: [0-9]+ vehicles in our garage, summon (allowed|restricted); earned in co-op [0-9]+' 10
note "$(since | grep -E 'garage: [0-9]+ vehicles' | tail -1)"
# The host's "quest" gives a car: credited (once: a rerun without a restart
# finds it credited already), never in our own garage.
expect_peer 'vehicle Vehicle.v_standard2_archer_hella_player unlocked in our garage: player [0-9]+ earned it with us' 30
expect 'garage: Vehicle.v_standard2_archer_hella_player with .*credited' 20
cmd garage
expect 'garage: [0-9]+ vehicles in our garage, .*earned in co-op [1-9][0-9]* \(0 kept in our garage; vehicle_rewards = credit\)' 10
cmd garage summon
expect 'garage: summon asked \((true|false)\)' 10
# Our own car arrives (unless the game restricts summoning here).
expect 'garage: our Vehicle\.[A-Za-z0-9_]+ is here' 40
shot garage-summon
reject 'garage: .* unlocked in our own garage'
reject 'watchdog: game thread silent'
