# A joined player's puppet has an attitude group of its own, and the NPCs around see it as they see V
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'player [0-9]+: puppet in the tandem_player_[1-3] attitude group' 20
# The relations are copied from V's within 2 s of NPCs being seen.
pause 5
cmd puppet attitude
# Every NPC within 60 m of V (at least one) sees the puppet as it sees V;
# V and the puppet are friends.
expect 'puppet attitude: player [0-9]+ in tandem_player_[1-3] \(V in [^)]+\), ([1-9][0-9]*) NPCs, \1 see it as V, V to it 0, it to V 0' 10
reject 'puppet attitude: player [0-9]+ has no body'
reject 'watchdog: game thread silent'
