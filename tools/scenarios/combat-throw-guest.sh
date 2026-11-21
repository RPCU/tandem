# As a guest, the host V's grenades and smart rounds fly on our game from its puppet, and its NPCs' grenades from their proxies
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --throw 4 --smart 3
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'we throw grenade [0-9]+' 20
expect "throws: player [0-9]+'s grenade [0-9]+ \(Items.GrenadeFragRegular\) launched from their puppet" 10
expect "throws: player [0-9]+'s throw [0-9]+ ended here \(0\)" 10
expect_peer 'our smart round at npc [0-9]+' 20
expect "throws: player [0-9]+'s smart round fired from their puppet at npc [0-9]+" 10
# A copy's explosion never reaches our V: a remote V's puppet deals nothing here.
reject 'outcomes: player [0-9]+.s area effect on our V'
# The copy lives until the real one's end (it was destroyed at its launch).
reject 'its copy gone before its end'
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
