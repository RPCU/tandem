# Linking (the mock player with link = ask): our consent (auto) is a request, it accepts; tags then pass both ways; broken from our side, ours stop; the settings page's link = off breaks it at once, auto links again
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --tag 5
expect 'joined \(player [0-9]+' 20
# We consent to everyone (auto): "asked" until it says yes, and it does.
expect 'comms: link with tandem-peer: 0 -> 1' 20
expect_peer 'accepted player 1.s link request' 15
expect 'comms: link with tandem-peer: 1 -> 3' 10
expect 'Linked with|comms: link with tandem-peer: 1 -> 3' 5
shot comms-linked
# Ours reach it (what V looks at, tagged as the scanner would).
cmd comms tag
expect 'comms: tagged npc [0-9]+ .*for the linked|comms: nothing to tag' 10
# Broken from our side: it hears that we no longer consent.
cmd comms unlink tandem-peer
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect_peer "player 1's link: mode 2, 1 listed: we ask them" 10
# Asked again (aiming at its V with the tag key does the same): linked again.
cmd comms link tandem-peer
expect 'comms: link with tandem-peer: 2 -> 3' 10
# The settings page's link = off, in the session: no consent at once, the
# mock player hears it; auto again links back.
cmd coop set link off
expect 'comms: link with tandem-peer: 3 -> 2' 10
expect_peer "player 1's link: mode 0" 10
cmd coop set link auto
expect 'comms: link with tandem-peer: 2 -> 3' 10
reject 'watchdog: game thread silent'
