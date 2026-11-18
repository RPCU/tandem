# The host goes and comes back (a save load restarts its session, its NPC ids start from 1 again): the guest says so once, joins again, and nothing of the old session lingers
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body female --replay recordings/npcs.rec
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
pkill -x tandem-peer
expect 'session ended' 60
expect 'coop notice: Co-op session ended' 5
cmd npc count
expect 'npcs shared: 0'
mark
peer host --mode idle --body female --replay recordings/npcs.rec
expect 'joining again' 15
expect 'in the session as player' 30
expect 'coop notice: Joined' 5
# The new session's NPC 1 is another NPC: its states are taken (no frozen proxy) and no old proxy is left.
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
pause 10
cmd npc count
expect "npcs shared: [1-9][0-9]*, own hidden: [0-9]+, in the session's record: [1-9]"
reject 'consistency: proxy [0-9]+ has no NPC on the host'
reject 'consistency: spawn of npc [0-9]+ ignored'
reject 'watchdog: game thread silent'
