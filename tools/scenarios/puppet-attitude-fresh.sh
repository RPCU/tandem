# An NPC that comes into the world beside a joined player's puppet sees it as it sees V from its first tick, not 2 s later
# shellcheck shell=bash disable=SC2317,SC2154
# Whether the declared relations take is what this reads (below).
allow_fallback "attitude relations"
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'player [0-9]+: puppet in the tandem_player_[1-3] attitude group' 20
# Whether the engine took the relations AttitudeTweak.reds declared: the
# line says took, took in part or did NOT take (engine-knowledge §13.17);
# the copy below must hold either way.
cmd puppet relations
expect 'attitudes: declared relations ' 10
# A ganger 8 m ahead of V, new to the world: on our first tick with it, it
# and the puppet see each other as it and V do ("engine alone": before any
# copy of ours, the declared relations at work).
cmd proxy test
expect 'proxy test: .* spawning \(true\)' 10
expect 'attitudes: fresh npc [0-9]+ \([^)]+\) to V [0-2], puppet [0-9]+ engine alone: (same|differs), on its first tick: same' 10
# The game's own answer for that NPC: V's two attitudes, then the puppet's.
query fresh attitude npc fresh
expect_value "$fresh" '~' '^V ([0-2])/([0-2]) puppet \1/\2 '
# No NPC that streamed in meanwhile (the crowd) was left seeing the puppet
# otherwise.
reject 'on its first tick: differs'
cmd proxy clear
reject 'watchdog: game thread silent'
