# As a guest, the Relic's arm attacks mark a proxy with no instigator: the mark still reaches the host's NPC
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female
expect 'puppet 6[0-9]{4}: clips walk' 60
cmd relic mark
expect 'relic mark: on the nearest proxy' 10
expect 'combat: status effect BaseStatusEffect.GorillaArmsOnePunchNPCMark on proxy [0-9]+ sent to the host' 10
expect_peer 'npc [0-9]+ gets effect 0x[0-9a-f]+ from player [0-9]+' 10
reject 'watchdog: game thread silent'
