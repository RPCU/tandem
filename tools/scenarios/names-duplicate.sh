# Duplicate names: two mock players join as V and v while we host as V: we show them as V (2) and v (3), each hears its own, and markers and the HUD say so
# shellcheck shell=bash disable=SC2317
ensure host
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
cmd coop name
expect 'coop: name "V"' 10
peer join "$address:27450" --mode mirror --offset 2,0,0 --body female --name V
expect 'V \(2\) joined \(player [0-9]+' 20
expect_peer 'the session shows us as V \(2\) \(V is taken\)' 20
peer2 join "$address:27450" --mode mirror --offset -2,0,0 --body male --name v
expect 'v \(3\) joined \(player [0-9]+' 20
expect_peer2 'the session shows us as v \(3\) \(v is taken\)' 20
# Each guest learns the other under its shown name.
expect_peer '\+ player [0-9]+ \(v \(3\)\)' 20
expect_peer2 '\+ player [0-9]+ \(V \(2\)\)' 20
expect 'marker: player [0-9]+ \(V \(2\)\) on the puppet' 30
expect 'marker: player [0-9]+ \(v \(3\)\) on the puppet' 30
expect 'hud: entry for player [0-9]+ \(V \(2\)\)' 30
expect 'hud: entry for player [0-9]+ \(v \(3\)\)' 30
# Two plates, two names, two colours: look at the screenshot.
shot names-duplicate
pkill -f -- 'tandem-peer .*--name v$' || true
expect 'player [0-9]+ left' 45
reject 'watchdog: game thread silent'
