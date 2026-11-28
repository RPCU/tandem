# Our call to a partner as the game's own (link = ask): the phone system places it as ours to an NPC, its call view while it connects, on the call once they answer; the call's own button (PhoneReject held) hangs up: the link breaks and the phone's call ends
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --link-answer-after 4
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'comms: calling tandem-peer \(link was 0\)' 10
expect 'holocall: calling tandem-peer: the phone.s own call' 5
pause 1
cmd comms holocall
expect 'holocall: mode 2 .*the phone: tandem_partner_[0-9]+ phase 1 ours' 5
shot holocall-calling
# The mock picks up 4 s later (at once, the call view was never up:
# answered 17 ms after the call, 2026-11-21).
expect_peer 'accepted player 1.s link request' 15
expect 'comms: link with tandem-peer: 1 -> 3' 10
expect 'holocall: on the call with tandem-peer: the phone.s own call' 5
shot holocall-answered
# What PhoneReject held does on the call.
cmd comms hangup
expect 'holocall: hung up on tandem-peer \(link was 3\)' 5
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect 'holocall: the call with tandem-peer ended' 5
cmd comms holocall
expect 'holocall: mode 0 .*the phone: tandem_partner_[0-9]+ phase 3 ' 5
reject 'degraded: fallback partner call'
reject 'phone: our call with tandem_partner'
reject 'watchdog: game thread silent'
