# As a guest 1 km from the host: our own city is alive (our NPCs run, our crowd is on, nothing hidden), no proxy of the host's NPCs, nothing of ours shown to it
# shellcheck shell=bash disable=SC2317
ensure join
# A host with a region of its own (three NPCs it runs), a kilometre away.
peer host --name far --mode region --region-far 1000 --region-walk-at -1 --region-npcs 3 --body female
expect 'in the session as player' 30
expect_peer 'region: running 3 NPC' 20
# Linked with everyone until the host knows where we are, then with nobody.
expect 'regions: linked with nobody: our world is our own' 20
expect_peer 'region: linked with nobody' 10
# NPCs of our own to run whatever the street holds.
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 12
cmd npc count
expect 'npcs shared: [0-9]+, .*ours [1-9][0-9]*, proxies 0'
# Never quiet: the crowd of a guest alone in its region stays on.
reject 'npcs: guest, own crowd off'
reject 'consistency: proxy [0-9]+ was missing'
reject 'watchdog: game thread silent'
