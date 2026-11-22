# As the host: a fighting ganger's states (combat, aim, shoot, reload, crouch) leave when the game changes them (the wraps of NPCStatesComponent), none is left for the 2 s poll to find
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# front: the puppet 3 m ahead of our V; the ganger 12 m ahead, hostile to both.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
cmd npc hostile
expect 'npc hostile: now hostile' 20
# The game's own change, read and sent on the tick after it (debug: one line
# per change): the upper body as it aims, shoots and reloads, or the stance.
expect 'npc anims: event \([a-z]+\): npc [0-9]+ .*(upper|stance) [0-9]+ -> [0-9]+' 40
expect_peer 'npc [0-9]+ animates: high 2, upper [0-9]+, stance [0-9]+' 20
# Several rounds of the 2 s poll under the fight.
pause 10
cmd npc anims
expect 'npc anims: [1-9][0-9]* sent, .* [1-9][0-9]* by events \(.*missed [0-9]+ \(0 of them states\)' 10
note "$(since | grep -aE 'npc anims: [0-9]+ sent' | tail -1 | sed 's/.*npc anims: //')"
reject 'poll caught a change the events missed: npc [0-9]+.* (high|upper|stance) [0-9]+ -> '
cmd npc hostile clear
cmd god off
reject 'watchdog: game thread silent'
