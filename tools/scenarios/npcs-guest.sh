# As a guest of a replayed host: proxies follow, own crowd hidden, all gone when the host leaves, back when it returns
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body female --replay recordings/npcs.rec
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect 'puppet 6[0-9]{4}: clips walk' 20
cmd npc count
expect 'npcs shared: [1-9][0-9]*, own hidden'
pkill -x tandem-peer
expect 'session ended' 60
cmd npc count
expect 'npcs shared: 0'
# The host comes back: the guest joins again on its own, NPCs return.
mark
peer host --mode idle --body female --replay recordings/npcs.rec
expect 'joining again' 15
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'look from player' 10
reject 'watchdog: game thread silent'
