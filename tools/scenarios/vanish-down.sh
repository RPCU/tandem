# As host: the mock player starts reviving our downed V and vanishes (no goodbye): the revive bar goes, and with nobody left our V dies the game's way (leaving or losing the session is no way out of being down)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --revive 3 --vanish-at 25
expect 'joined \(player [0-9]+' 20
expect 'revive: another player is here: our V goes down instead of dying' 20
pause 12
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'revive: tandem-peer is reviving us' 15
expect_peer 'vanishing \(--vanish-at\)' 20
# 5 s of silence, then the host's own rules: the reviver is gone, nobody is up.
expect 'player [0-9]+ left' 15
expect 'coop notice: tandem-peer left the session' 5
expect 'revive: (our reviver \(player [0-9]+\) is gone|tandem-peer stopped reviving us)' 5
expect 'revive: our V dies: death screen' 15
reject 'revive: our V is up at 40 %'
reject 'watchdog: game thread silent'
