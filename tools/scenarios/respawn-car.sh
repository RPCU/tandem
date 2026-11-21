# As guest: our V is out while the host drives: it gets up and takes a free seat of the host's car, never the ground behind it
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode drive --vehicle Vehicle.v_standard25_mahir_supron_player --body female --drive-seconds 60
expect 'in the session as player' 30
expect 'copy spawning' 30
expect 'seated in seat_front_left' 30
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive out
expect 'revive: our V is out \(a test\)' 10
expect_peer 'player [0-9]+ is out' 10
expect 'revive: respawn beside tandem-peer \(out of combat\): into a free seat of its car \(seat_[a-z_]+\)' 15
expect 'revive: our V is up at 40 %' 15
expect "revive: seated in seat_[a-z_]+ of tandem-peer's car" 5
expect 'our seat: .* \(car of player 1\)' 5
expect_peer 'player [0-9]+ is up' 5
reject 'revive: respawn beside tandem-peer .*: at its last spot'
cmd revive unseat
expect 'revive unseat: out of the respawn.s seat' 5
reject 'watchdog: game thread silent'
