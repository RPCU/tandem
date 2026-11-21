# As a guest of a replayed host: its enemy ping hangs on our proxy of that NPC and follows it; its reveals outline the proxies while we are linked
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --ping 5 --reveal 12
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# Linked from the start (link = auto both sides).
expect 'comms: link with tandem-peer: [0-9] -> 3' 20
expect 'comms: showing ping [0-9]+ of tandem-peer \(Enemy, 15(\.0+)? s\) on the entity, registered true' 40
shot comms-enemy-ping
cmd comms report
expect 'comms: ping [0-9]+ of player 1 \(Enemy\) at ' 5
# A reveal of the host's NPCs (the Ping quickhack): outlined here, for 10 s.
expect 'comms: outlined a reveal of a linked player' 30
shot comms-reveal
reject 'comms: outline of a target not here'
reject 'watchdog: game thread silent'
