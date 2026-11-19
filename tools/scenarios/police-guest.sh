# As a guest: our own wanted level is ours alone, a crime the host's NPCs saw gives us the heat, the host's stars are not ours, and we spawn no police
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle --body female --crime-at 12 --heat 3 --heat-at 40 --heat-for 15
expect 'in the session as player' 30
cmd police report
expect 'police report: our wanted level 0 .*we follow: our own spawns are off' 10
# A witness in the host's world called the police on our puppet: our own
# prevention system takes it as our crime (one star, the game's own).
expect_peer 'police: crime of player [0-9]+ reported to its game' 30
expect 'police: crime seen by the area.s owner: kind 1' 10
expect 'police: our wanted level 1 ' 10
expect_peer 'police: player [0-9]+ wanted level 1 ' 10
shot police-guest-star
# Our stars go the game's way (nobody sees our V: no police of the host's
# is near), and the others hear it.
cmd police calm
expect 'police: our wanted level 0 ' 60
expect_peer 'police: player [0-9]+ wanted level 0 ' 10
# The host is wanted now: we hear it, and our own level stays 0.
expect_peer 'police: our wanted level 3 \(seen\)' 60
expect 'player 1: wanted level 3 ' 10
cmd police report
expect 'police report: our wanted level 0 .* player 1: 3 ' 10
expect 'player 1: wanted level 0 ' 30
reject 'watchdog: game thread silent'
