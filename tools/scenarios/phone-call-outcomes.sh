# Our call to a partner, each outcome the game's way (link = ask): busy, declined, no answer, unavailable (left), then answered and hung up; each one on the phone's own call (its view, then its end) with its line, and a missed call kept on the receiver's side
# shellcheck shell=bash disable=SC2317
# To look at (the shots): phone-calling, the game's call view placing our
# call; phone-declined, the view saying "tandem-peer declined the call".
ensure host link=ask
cmd teleport home
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }'):27450
# Busy: declined at once.
peer join "$address" --mode front --body male --link ask --link-outcome busy
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'holocall: calling tandem-peer: the phone.s own call' 5
expect_peer 'declined player 1.s link request \(reason 3\)' 10
expect 'holocall: tandem-peer: tandem-peer is busy' 5
pause 3
cmd comms holocall
expect 'holocall: mode 0 .*the phone: tandem_partner_[0-9]+ phase 3 ' 5
# Declined, 3 s after it rang there.
peer join "$address" --mode front --body male --link ask --link-outcome decline --link-answer-after 3
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'holocall: calling tandem-peer: the phone.s own call' 5
pause 1
cmd comms holocall
expect 'holocall: mode 2 .*the phone: tandem_partner_[0-9]+ phase 1 ours' 5
shot phone-calling
expect 'comms: tandem-peer did not take our call \(reason 2\)' 10
expect 'holocall: tandem-peer: .*declined' 3
shot phone-declined
pause 3
cmd comms holocall
expect 'holocall: mode 0 .*the phone: tandem_partner_[0-9]+ phase 3 ' 5
# Not answered: it rang out there (15 s), a missed call on its side.
peer join "$address" --mode front --body male --link ask --link-outcome ignore
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'holocall: calling tandem-peer: the phone.s own call' 5
expect 'holocall: tandem-peer: tandem-peer didn.t answer' 20
# Unavailable: it leaves while our call rings.
peer join "$address" --mode front --body male --link ask --link-outcome ignore
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'holocall: calling tandem-peer: the phone.s own call' 5
pause 3
pkill -x tandem-peer || true
expect 'comms: tandem-peer left while we called' 20
expect 'holocall: tandem-peer: .*unavailable' 3
# Answered, then hung up with the call's own button: both ends close.
peer join "$address" --mode front --body male --link ask --link-answer-after 2
expect 'joined \(player [0-9]+' 20
cmd phone call tandem-peer
expect 'comms: link with tandem-peer: 1 -> 3' 10
expect 'holocall: on the call with tandem-peer: the phone.s own call' 5
cmd comms hangup
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect 'holocall: the call with tandem-peer ended' 5
expect_peer "player 1's link: mode 2, 0 listed: not linked|player 1's link: .*not linked" 10
# Their request ignored here: a missed call kept on our side.
peer join "$address" --mode front --body male --link ask --link-at 2
expect 'joined \(player [0-9]+' 20
expect 'comms: showing the link request of tandem-peer' 15
expect 'comms: link request of tandem-peer left unanswered: a missed call' 20
expect_peer 'player 1 did not take our link request \(reason 4\)' 5
reject 'degraded: fallback partner call'
reject 'phone: our call with tandem_partner'
reject 'watchdog: game thread silent'
