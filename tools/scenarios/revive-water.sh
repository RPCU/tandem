# As host: our V goes down as if 3 m under water (a test depth: deep water's rule as for real water): out at once, nobody revives it there, and it gets up at Viktor's clinic through the fast travel's loading screen
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive water 3
expect 'revive water: as if 3' 5
cmd revive down
expect 'revive: our V is down \(revivable\)' 10
expect 'revive: our V is down in deep water, [0-9.]+ m over its feet: out, no revive there' 5
expect_peer 'player 1 is out' 5
expect "revive: respawn at Viktor's clinic \(deep water" 5
expect 'revive: on the way, the fast travel.s loading screen' 5
expect 'revive: there, the loading screen ended' 60
expect 'coop notice: Vik patched you up' 5
expect 'revive: our V is up at 40 %' 15
# Up in the clinic (Viktor's spot -1546.73 1227.39 11.52, within 10 m).
query at position
expect_value "$at" '~' '^-15(3[6-9]|4[0-9]|5[0-6])\.[0-9]+ 12(1[7-9]|2[0-9]|3[0-7])\.'
expect_peer 'player 1 is up' 10
cmd revive water off
expect 'revive water: the real water' 5
# In real water (not run yet): stand in water deeper than V, then
# "revive down": out, up at Viktor's. Look for: V standing in the clinic,
# facing the ripper's chair; the mock's party HUD and marker following.
reject 'revive: revived by tandem-peer'
reject 'no end of the loading screen'
reject 'watchdog: game thread silent'
