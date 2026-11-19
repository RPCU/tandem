# As a guest, a quest prop the session lends our V is ours for the session only: when the host goes it is taken back out before saving unlocks (iconics would stay); the ledger notes it, nothing is saved (leave = stay)
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female
expect 'in the session as player' 30
# As if the host's quest had lent us three (the path of a keycard or a
# shard tagged Quest, without a quest).
cmd loot prop Items.CommonMaterial1 3
expect "loot: Items.CommonMaterial1 x3 is the host's quest's: ours for the session only" 10
cmd story carry
expect 'ledger: [0-9]+ quests taken part in .* some session props held' 10
pkill -x tandem-peer
expect 'session ended' 60
expect "loot: the host's quest props given back: 3 of 3 item\(s\) in 1 record\(s\)" 20
expect 'save: back from co-op: our own world is back, saving unlocked \(leave = stay\)' 40
cmd story carry
expect 'ledger: .* no session props held' 10
reject 'save: back from co-op: quick save asked'
reject 'save: stays locked'
reject 'watchdog: game thread silent'
