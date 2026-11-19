# A linked partner's quickhacks (as a guest of a replayed host): its upload and the effect it leaves show on our proxy in its colour; unlinked, they go
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --hack-npc nearest
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect 'comms: link with tandem-peer: 0 -> 3' 20
expect "comms: showing tandem-peer's upload of .* on npc [0-9]+ .*registered true" 40
# A person checks: the game's own quickhack indicator over the NPC, its icon
# and bar in the partner's colour (player 1: cyan), readable against ours.
shot comms-hack-upload
expect "comms: showing tandem-peer's effect of .* on npc [0-9]+" 10
cmd comms hacks
expect 'comms: [1-9][0-9]* partner quickhack indicator' 5
shot comms-hack-effect
# Unlinked: its indicators go at once; the effect itself stays on the NPC.
cmd comms unlink tandem-peer
expect "comms: hid tandem-peer's (upload|effect) on npc" 10
cmd comms hacks
expect 'comms: 0 partner quickhack indicator' 5
reject 'watchdog: game thread silent'
