# A guest grabs, takes down, carries and throws the host's NPCs: the host validates each request and plays the NPC's side
# shellcheck shell=bash disable=SC2317
# The grapple may take as the workspot or as the fallback (below): both are read.
allow_fallback "grapple held"
ensure host
cmd npc spawn 3
expect 'npc spawn: 3 gangers' 10
pause 3
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --grab-npcs 10 --carry-npcs 10
expect_peer 'grab npc [0-9]+' 40
expect 'takedowns: player [0-9]+ asks grab on [0-9]+: (accepted|refused)' 10
expect_peer 'npc [0-9]+: action 1 (accepted|refused)' 10
# The guest's puppet's body, looked at as it came: which way a grapple by
# it goes (the game's mount when it has the grapple slot).
expect 'takedowns: player [0-9]+.s puppet: grapple by the (game.s mount|fallback: )' 10
# The NPC's side with the guest's puppet: on its grapple slot (the game's
# mount), in the takedown effect's workspot, or held in front of it (the
# fallback, with why). (A first grab may be refused: the peer takes the next NPC.)
expect 'takedowns: [0-9]+ grappled by player [0-9]+: grapple by the (game.s mount|game.s workspot|fallback: )' 45
expect 'takedowns: [0-9]+ taken down by player [0-9]+ \((kill|setUnconscious)\)' 15
# Its hold ends with the animation's workspot, or at its limit.
expect 'takedowns: [0-9]+: player [0-9]+.s hold \(how 3\) ended after [0-9.]+ s \((its workspot.s end|the [0-9.]+ s limit)' 15
expect 'npcs: [0-9]+ died \(killer: player ([2-9]|[1-9][0-9]+)\)' 20
# A body: picked up onto the puppet's shoulder, thrown.
expect_peer 'pick up npc [0-9]+' 30
expect 'takedowns: [0-9]+ carried by player [0-9]+' 10
expect 'takedowns: [0-9]+ thrown by player [0-9]+' 10
shot takedowns-host
reject 'watchdog: game thread silent'
