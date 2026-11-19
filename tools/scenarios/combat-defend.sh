# As the host, a guest's parry and block of our NPC's attack (decided on their game) make our NPC recoil as against our own V
# shellcheck shell=bash disable=SC2317
ensure host
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --defend 3
expect_peer 'we parry npc [0-9]+.s attack' 30
expect "outcomes: player [0-9]+ parried [0-9]+'s attack: it gets the recoil" 10
expect_peer 'we block npc [0-9]+.s attack' 10
expect "outcomes: player [0-9]+ blocked [0-9]+'s attack: it gets the recoil" 10
reject 'watchdog: game thread silent'
