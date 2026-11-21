# As a guest, saving is locked in the session; when the host goes our own world comes back (weather, difficulty, crowd, bodies), nothing of the session is left, and saving unlocks. No save is written (leave = stay, the default)
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --difficulty story --weather 24h_weather_rain
expect 'in the session as player' 30
expect 'save: leave solo -> session' 20
expect 'outcomes: the host.s difficulty 3' 20
expect 'world: weather to the host.s 24h_weather_rain' 30
cmd progress
expect 'saves: leave = stay, step session, our lock true' 10
# The host goes (no goodbye needed): everything is put back, then the verdict.
# (Only what comes after: the previous session's end said the same lines.)
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'save: leave session -> cleaning' 10
expect 'world: our weather is the game.s again' 10
expect 'outcomes: our own difficulty [0-9] back' 10
expect 'save: left of the session: nothing' 30
expect 'save: back from co-op: our own world is back, saving unlocked \(leave = stay\)' 30
cmd progress
expect 'saves: leave = stay, step solo, our lock false, .* left of a session: nothing' 10
# Never a save of ours in a test, and never a verdict that co-op state stayed.
reject 'save: back from co-op: quick save asked'
reject 'save: stays locked'
reject 'watchdog: game thread silent'
