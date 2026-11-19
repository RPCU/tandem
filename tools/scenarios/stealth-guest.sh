# As a guest: a host NPC noticing us fills our stealth meter, detecting us puts our V in combat, losing us takes it out
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle --body female --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --detect-at 15 --detect-for 10
expect 'in the session as player' 30
expect 'stealth: ours sent' 20
expect_peer 'player [0-9]+ stealth: flags' 10
expect_peer 'npcs notice the guests' 40
expect 'stealth: detection meter (25|50|75)%' 10
expect_peer 'npcs have detected the guests' 10
expect 'stealth: [0-9]+ host NPC\(s\) detect us: in combat' 10
# The game's own combat state follows (PSM Combat 1: music, HUD, perks).
expect 'stealth: combat state 1' 5
expect_peer 'npcs lost the guests' 20
expect 'stealth: no host NPC detects us: out of combat' 10
expect 'stealth: combat state [23]' 5
expect 'stealth: detection meter 0%' 5
reject 'watchdog: game thread silent'
