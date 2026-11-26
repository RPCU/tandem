# As a guest: the host wanted at three stars is shown the game's way: its marker in the police's colours, its stars (the game's wanted bar's art) beside its name in the party list, grey when the police lose it, gone after
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle --body female --heat 3 --heat-at 15 --heat-for 20
expect 'in the session as player' 30
expect 'hud: a wanted partner.s stars in the game.s own art' 30
expect_peer 'police: our wanted level 3 \(seen\)' 40
expect 'marker: player 1 wanted 3 \(state 1\): drawn in the police.s colours' 10
expect 'hud: player 1 wanted: 3 star\(s\), state 1, drawn with the game.s star art' 10
shot police-partner-stars
# The last 5 s unseen: grey stars, the marker its own colour and grey in turn.
expect 'hud: player 1 wanted: 3 star\(s\), state [23], drawn' 30
expect 'marker: player 1 wanted 0 \(state 0\): its own colour' 30
expect 'hud: player 1 wanted: 0 star\(s\)' 10
# To look at (a person there): the stars as the game's own, red then grey;
# the minimap and world markers flashing red and blue; the map's tooltip.
reject 'watchdog: game thread silent'
