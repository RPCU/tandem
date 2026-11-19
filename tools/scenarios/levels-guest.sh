# As a guest, our V's level reaches the host, the session's level is told, and each proxy takes the level the host says its NPC has
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --level 50 --npc-level 31
expect 'in the session as player' 30
expect 'levels: our V is level [0-9]+' 20
expect_peer "the session's level is [0-9]+ \(2 players\)" 20
expect "levels: the session's level is [0-9]+ \(2 players" 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# The host's word for its NPCs, on our proxies (logged when a proxy's own
# level differs: our game made it for our V's level).
expect 'levels: proxy [0-9]+ level [0-9.]+ -> 31(\.0+)? \(the host.s 31' 30
cmd progress
expect 'levels: ours [0-9]+, session [0-9]+ \(2 players\), 0 NPCs moved; .* [1-9][0-9]* host levels known' 10
shot levels-guest
# The host goes: nothing of its levels is kept.
pkill -x tandem-peer
expect 'session ended' 60
pause 3
cmd progress
expect 'levels: ours 0, session 0 \(0 players\), 0 NPCs moved; .* 0 host levels known' 10
reject 'watchdog: game thread silent'
