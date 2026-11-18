# As a guest beside the host: its dives into other worlds hide or keep its body, never hold us, and leave nothing hidden
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V, so we are near it.
peer host --mode front --body male --story tools/scenarios/story-worlds.story
expect 'in the session as player' 30
# Jacked in from a chair: its body stays, sitting, where it was.
expect 'elsewhere: player 1 is in cyberspace, body left sitting or lying where it was' 15
# Its scene is another world's: no weapons lowered, no offer to join.
pause 3
# On with its body out of sight: hidden, marker off.
expect 'elsewhere: player 1 is in cyberspace, body out of sight: hidden' 10
expect "elsewhere: player 1's puppet hidden" 5
expect 'marker: player 1 off for now' 5
# Back: shown again where it is.
expect 'elsewhere: player 1 is back from cyberspace' 10
expect "elsewhere: player 1's puppet shown" 5
expect 'marker: player 1 back' 5
# Johnny in the city: the puppet stays.
expect "elsewhere: player 1 is in Johnny's body, body in place" 10
expect "elsewhere: player 1 is back from Johnny's body" 10
# The host drops in a braindance away from its body: nothing stays hidden.
expect 'elsewhere: player 1 is in a braindance, body out of sight: hidden' 10
pkill -x tandem-peer
expect 'session ended' 60
expect 'elsewhere: the session is over, 1 player\(s\) back in the city' 10
reject 'story: weapons lowered'
reject 'story: join offer shown'
reject 'watchdog: game thread silent'
