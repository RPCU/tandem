# As host: our V goes down as if 3 m under water (a test depth: shore and surface searched as for real water): it is lifted to the shore, where the mock player revives it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --revive 3
expect 'joined \(player [0-9]+' 20
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive water 3
expect 'revive water: as if 3' 5
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'revive: our V is down in deep water \([0-9.]+ m over its feet\): lifted to the shore' 5
expect 'revive: revived by tandem-peer' 20
expect 'revive: our V is up at 40 %' 15
cmd revive water off
expect 'revive water: the real water' 5
# In real water (not run yet): stand in water deeper than V, then
# "revive down": lifted to the shore within 40 m, or to the surface.
reject 'lifted to the surface'
reject 'watchdog: game thread silent'
