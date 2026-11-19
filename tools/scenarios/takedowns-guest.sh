# As a guest: our takedown requests reach the host and its answers come back; the host's V grabbing an NPC moves our proxy
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode front --body male --hold-npcs 20
expect 'puppet 6[0-9]{4}: clips walk' 60
# The host's V holds one of its NPCs: our proxy of it goes into the grapple with its puppet.
expect 'takedowns: [0-9]+ held by player 1 \(how 1\)' 30
expect 'takedowns: [0-9]+ held by player 1 \(how 3\)' 10
expect 'takedowns: [0-9]+ released' 10
# Our own requests on the nearest proxy (without the animation).
cmd takedown grab
expect 'takedowns: our grab on [0-9]+ sent to the host' 10
expect_peer 'npc [0-9]+: player [0-9]+ asks action 1: (accepted|refused)' 10
expect 'takedowns: host (accepted|refused) our grab on [0-9]+' 10
cmd takedown kill
expect 'takedowns: host (accepted|refused) our takedown on [0-9]+' 10
cmd takedown list
expect 'takedowns: [0-9]+ held' 5
reject 'watchdog: game thread silent'
