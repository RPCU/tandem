# The phone's contact list with a partner in it (link = ask): listed first, its actions open on the call key, Call asks it to link and it answers; the row then says Linked and Hang up breaks the link
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask
expect 'joined \(player [0-9]+' 20
cmd phone
expect 'phone: 1 player\(s\) on the contacts list, the list closed' 10
expect 'phone: tandem-peer \(Not linked\): Call \| ' 5
# The list as the phone key's hold opens it: the partner above the game's contacts.
cmd phone open
expect 'phone: the contacts list shows 1 player\(s\) first' 10
pause 1
shot phone-contacts
cmd phone pick tandem-peer
expect "phone: tandem-peer's actions: Call \| " 10
pause 1
shot phone-actions
cmd phone pick Call
expect 'phone: tandem-peer: Call chosen' 5
expect 'comms: calling tandem-peer \(link was 0\)' 10
expect 'comms: link with tandem-peer: 0 -> 1' 10
expect_peer 'accepted player 1.s link request' 15
expect 'comms: link with tandem-peer: 1 -> 3' 10
# Linked: the row says so, and the call is now Hang up.
cmd phone
expect 'phone: tandem-peer \(Linked\): Hang up \| ' 10
cmd phone call tandem-peer
expect 'comms: hanging up on tandem-peer \(link was 3\)' 10
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect 'Link with tandem-peer broken' 5
reject 'phone: .* is one of the game.s contacts'
reject 'watchdog: game thread silent'
