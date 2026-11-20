# As the host, a hostile NPC with two mock players beside it is scaled for three (health x2.2), the guests hear the count, and it goes back to one, its share of health kept, as they leave
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# A hostile ganger 12 m ahead of our V: ours to run, shared, an enemy.
cmd npc hostile
expect 'npc hostile: .* spawning \(true\)' 10
pause 3
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer join "$address:27450" --mode front --near-npc --body female --name peer-a
expect 'peer-a joined \(player [0-9]+' 20
expect_peer 'we stand by npc [0-9]+' 20
peer2 join "$address:27450" --mode front --near-npc --body male --name peer-b
expect 'peer-b joined \(player [0-9]+' 20
expect "outcomes: the session's rules: friendly fire (on|off), enemy scaling on, health \+60 % per extra player" 30
# Our V and the two puppets within 100 m of it: three players.
expect 'scaling: npc [0-9]+ .*-> 3 players, health x2\.2' 40
expect_peer 'npc [0-9]+ is scaled for 3 players' 20
expect_peer2 'npc [0-9]+ is scaled for 3 players' 20
cmd npc scale
expect 'scaling: rule on, health \+0\.6' 10
expect 'scaling: npc [0-9]+ .* for 3 players' 10
note "$(since | grep -E 'scaling: npc [0-9]+ ' | tail -3)"
shot enemy-scaling
# One guest leaves: two, then one; the share of health is kept each time.
pkill -f -- 'tandem-peer .*--name peer-b' || true
expect 'scaling: npc [0-9]+ .*3 -> 2 players, health x1\.6' 40
pkill -x tandem-peer || true
expect 'scaling: npc [0-9]+ .*2 -> 1 players' 40
cmd npc scale
expect 'scaling: npc [0-9]+ .* for 1 players' 10
cmd npc hostile clear
cmd god off
reject 'watchdog: game thread silent'
