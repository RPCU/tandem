# As a guest of a lossy replayed host (a fifth of what it sends never leaves, reliable messages too; a fifth of its state goes twice): digests find what diverged, resyncs heal it, the proxies follow the session's record
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body female --replay recordings/npcs.rec --drop 0.2 --dup 0.2
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# What the host lost shows in two digests in a row (3 s apart); the host sends it again.
expect 'resync: asking the host for' 60
expect_peer 'resync: player [0-9]+ asked for' 15
expect 'notice: Resyncing with the host' 5
pause 20
cmd npc count
expect "npcs shared: [1-9][0-9]*, own hidden: [0-9]+, in the session's record: [1-9]"
# The losses were the network's: the game's own checks never had to fix a proxy.
reject 'consistency: proxy [0-9]+ (has no NPC on the host|was missing)'
reject 'consistency: puppet of player'
reject 'watchdog: game thread silent'
pkill -x tandem-peer
expect 'session ended' 60
cmd npc count
expect 'npcs shared: 0'
