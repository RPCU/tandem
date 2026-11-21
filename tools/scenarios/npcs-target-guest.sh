# As host: two hostile gangers by the mock guest's puppet, our V farther; weighed as vanilla weighs an NPC, then as a player (V's base, 1.0 not 0.01), the puppet's threat rises, it is their top threat and their combat target
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
cmd god on
# front: the puppet 3 m ahead of our V, facing it.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'player [0-9]+: puppet in the tandem_player_[1-3] attitude group' 20
# Vanilla's weights first: to our NPCs the puppet is one more NPC.
cmd npc threats npc
expect 'threats: puppets weighed as NPCs \(vanilla\)' 10
# Two gangers 4 m beyond the puppet (about 7 m from our V), hostile to both Vs.
cmd npc threats spawn
expect 'threats: 2 hostiles by player [0-9]+.s puppet' 10
expect 'npc hostile: now hostile' 20
pause 8
cmd npc threats
expect 'threats: npc [0-9]+ top [^;]+; our V [-0-9.]+; player [0-9]+ [0-9.]+' 10
query before threat puppet
# As players: the puppet gets the player's base and terms.
cmd npc threats player
expect 'threats: puppets weighed as players' 10
pause 5
cmd npc threats
expect 'threats: npc [0-9]+ top player [0-9]+ ' 10
query after threat puppet
expect_changed "$before" "$after" up
expect "npc hostile: combat target player [0-9]+'s puppet" 15
note "$(since | grep -E 'threats: npc ' | tail -4)"
cmd npc hostile clear
cmd god off
reject 'watchdog: game thread silent'
