# As a guest of a replayed host: the host's NPCs' voice lines and voice triggers (NpcVoice) said by our proxies' own voices, and the count in the presence report
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --voice-npcs 2 --voice-trigger tandem_test
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ says start_combat' 20
expect 'voice: proxy [0-9]+ says the owner.s' 10
expect_peer 'npc [0-9]+ says trigger tandem_test variation [1-3] of 3' 10
expect 'voice: proxy [0-9]+ says the owner.s trigger tandem_test variation [1-3] of 3' 10
expect_peer 'npc [0-9]+ says stlh_death' 20
cmd presence report
expect 'voice: sent [0-9]+ \(our NPCs\), said [1-9][0-9]* \(proxies\), [0-9]+ for no proxy here; triggers sent [0-9]+, said [1-9][0-9]*, a proxy.s own [0-9]+' 5
# To hear (a person there): each line in the proxy's own voice, from where it
# stands, in this game's language; nothing said twice when we shoot it. The
# test trigger's name is no real one (silent): a real trigger is heard in a
# fight with the host's NPCs (presence-npc-voices-host's log names them).
reject 'watchdog: game thread silent'
