# As a guest of a replayed host: the host's NPCs' voice lines (NpcVoice) said by our proxies' own voices, and the count in the presence report
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --voice-npcs 2
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ says start_combat' 20
expect 'voice: proxy [0-9]+ says the owner.s' 10
expect_peer 'npc [0-9]+ says stlh_death' 20
cmd presence report
expect 'voice: sent [0-9]+ \(our NPCs\), said [1-9][0-9]* \(proxies\)' 5
# To hear (a person there): each line in the proxy's own voice, from where it
# stands, in this game's language; nothing said twice when we shoot it.
reject 'watchdog: game thread silent'
