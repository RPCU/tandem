# Our call to a partner drawn as the game's (link = ask): the call view with its name while it connects, the audio call view once it answers; the call's own button (PhoneReject held) hangs up: the link breaks and the call ends on screen
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --link-answer-after 4
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'comms: calling tandem-peer \(link was 0\)' 10
expect 'holocall: calling tandem-peer: the call view shown' 5
shot holocall-calling
# The mock picks up 4 s later (at once, the call view was never up:
# answered 17 ms after the call, 2026-11-21).
expect_peer 'accepted player 1.s link request' 15
expect 'comms: link with tandem-peer: 1 -> 3' 10
expect 'holocall: on the call with tandem-peer: the audio call view shown' 5
shot holocall-answered
# What PhoneReject held does on the call.
cmd comms hangup
expect 'holocall: hung up on tandem-peer \(link was 3\)' 5
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect 'holocall: the call with tandem-peer ended' 5
reject 'holocall: a call of the game.s'
reject 'watchdog: game thread silent'
