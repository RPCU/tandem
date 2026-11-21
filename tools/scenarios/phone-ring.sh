# A partner calls us (link = ask): our game rings with its name, the answer and decline hints shown; declined, it rings no more and nothing links; then answered from its row in the phone, linked
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --link-at 10
expect 'joined \(player [0-9]+' 20
expect 'comms: link with tandem-peer: 0 -> 2' 30
expect 'comms: showing the link request of tandem-peer \(ringing\)' 5
expect 'coop notice: tandem-peer is calling: link to share scans and pings \[Comms-LinkAsked\]' 5
shot phone-ringing
# Declined (PhoneReject held does the same): no link.
cmd comms decline
expect 'comms: declined the link request of tandem-peer' 5
cmd phone
expect 'phone: tandem-peer \(Calling you\): Answer \| ' 10
# Answered from the phone instead: linked.
cmd phone open
expect 'phone: the contacts list shows 1 player\(s\) first' 10
pause 1
cmd phone pick tandem-peer
expect "phone: tandem-peer's actions: Answer \| " 10
cmd phone pick Answer
expect 'comms: answering tandem-peer \(link was 2\)' 10
expect 'comms: link with tandem-peer: 2 -> 3' 10
reject 'watchdog: game thread silent'
