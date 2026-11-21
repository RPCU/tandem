# A guest quickhacks the host's NPCs: the host's NPC is being hacked for the guest's upload time, gets the effect and tells everyone
# shellcheck shell=bash disable=SC2317
ensure host
# NPCs to hack whatever the street holds (rain and night empty it).
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --hack-npcs 6
expect_peer 'upload QuickHack.BlindHack on npc [0-9]+' 30
expect 'hacks: player [0-9]+ uploads QuickHack.BlindHack on npc [0-9]+ \(1.5' 15
expect_peer 'npc [0-9]+: upload 0x[0-9a-f]+ of player [0-9]+ step 0' 10
expect "hacks: player [0-9]+'s upload of QuickHack.BlindHack on npc [0-9]+ done" 10
expect_peer 'npc [0-9]+: upload 0x[0-9a-f]+ of player [0-9]+ step 1' 10
expect 'npcs: [0-9]+ gets BaseStatusEffect.Blind from player [0-9]+' 10
# To check by eye: the NPC reacts while being hacked; with a Quickhack-tagged
# effect (--hack-effect), "hacks: npc N noticed player P's quickhack" and the
# trace sent to the peer ("npc N hacks us with ...").
reject 'watchdog: game thread silent'
