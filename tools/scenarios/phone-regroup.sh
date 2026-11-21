# Apart, from the phone: "Ask tandem-peer to join you" offers it a regroup (it declines, we are told), then "Join tandem-peer" brings V next to it
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --travel-at 60 --regroup-answer no
expect 'joined \(player [0-9]+' 20
# Apart: we fast travel away, it travels home at 60 s (its offer declined here).
cmd travel
expect 'travel: arrived at .*, offered the others to join' 60
expect 'travel: showing offer [0-9]+ of tandem-peer' 60
cmd regroup decline
expect 'travel: declined offer' 5
mark
# Our invitation from the phone: it declines, we are told.
cmd phone open
expect 'phone: the contacts list shows 1 player\(s\) first' 10
pause 1
cmd phone pick tandem-peer
expect "phone: tandem-peer's actions: .*Join tandem-peer \| Ask tandem-peer to join you" 10
cmd phone pick Ask tandem-peer to join you
expect 'phone: invite tandem-peer: true' 10
expect 'coop notice: Asked tandem-peer to join you \[Phone-Invited\]' 5
expect 'travel: tandem-peer stays where they are' 15
# Then we go to them, as the world map's Join does.
mark
cmd phone open
expect 'phone: the contacts list shows 1 player\(s\) first' 10
pause 1
cmd phone pick tandem-peer
cmd phone pick Join tandem-peer
expect 'phone: join tandem-peer: true' 10
expect_peer 'player 1 is on the way' 10
expect 'travel: arrived next to tandem-peer' 60
expect 'travel: screen restored' 5
shot phone-regroup-arrived
# The join's loading screen stalls the game thread (the watchdog says so):
# only what comes after the arrival.
mark
pause 3
reject 'watchdog: game thread silent'
