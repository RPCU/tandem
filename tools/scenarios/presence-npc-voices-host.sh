# As host: a voice trigger queued on one of our shared NPCs as the engine queues one (entTriggerVOEvent) is heard by our handler and sent to the guest with its variation (NpcVoice, kVoiceTrigger)
# shellcheck shell=bash disable=SC2317
ensure host
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle
expect_peer 'in the session' 20
pause 5
cmd npc count
expect 'npcs shared: [1-9]'
cmd presence voice trigger tandem_test 2 3
expect 'presence: voice trigger tandem_test variation 2 of 3 queued on npc [0-9]+' 5
expect 'voice: npc [0-9]+ says trigger tandem_test variation 2 of 3' 5
expect_peer 'npc [0-9]+ says trigger 0x[0-9a-f]+ variation 2 of 3 \(VO context 5\)' 10
cmd presence report
expect 'triggers sent [1-9][0-9]*, said [0-9]+, a proxy.s own [0-9]+' 5
reject 'watchdog: game thread silent'
