# As host: our NPC firing at the mock guest's puppet decides whether each shot may land by a V's time between hits (group and cover coefficients, a player's base), not an NPC target's
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# An NPC 12 m ahead, hostile to the puppets only: it fires at the mock player.
cmd npc hostile puppets
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect "shots: npc [0-9]+'s shots at player [0-9]+ are fired and decided on their game" 40
expect "shots: shots at guests land by a V's time between hits \(base [0-9.]+ for the puppet's [0-9.]+, with the group and cover coefficients\)" 5
expect_peer 'npc [0-9]+ fires at us \(weapon 0x[0-9a-f]+, speed [0-9.]+, may land\)' 30
pause 10
cmd npc hostile clear
cmd god off
reject 'watchdog: game thread silent'
