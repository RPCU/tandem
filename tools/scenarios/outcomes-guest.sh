# As a guest, the game plays at the host's difficulty while in the session and a remote V's grenade hurts our V by our own rules
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body female --replay recordings/npcs.rec --difficulty story --area-hit 3
expect 'in the session as player' 30
expect 'outcomes: the host.s difficulty 3' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
expect_peer 'our grenade hit player [0-9]+ for 20 physical' 15
expect "outcomes: player [0-9]+'s area effect on our V: 20(\.0+)? physical" 10
expect 'combat: npc hit applied to our V: [0-9.]+ damage' 10
# Few hits: stop the host, and our own difficulty comes back.
pkill -x tandem-peer
expect 'session ended' 60
expect 'outcomes: our own difficulty [0-9] back' 10
reject 'watchdog: game thread silent'
