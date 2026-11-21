# A partner's call drawn as the game's (link = ask): it rings with the incoming call widget and its name; answered, the audio call view with its name, "Linked", for a few seconds; then the view closes and the link stays
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --link-at 10
expect 'joined \(player [0-9]+' 20
expect 'comms: link with tandem-peer: 0 -> 2' 30
expect 'holocall: tandem-peer is calling: the incoming call widget shown' 5
shot holocall-ringing
# Answered (from the phone, as the phone key's press does).
cmd phone call tandem-peer
expect 'comms: link with tandem-peer: 2 -> 3' 10
expect 'holocall: on the call with tandem-peer: the audio call view shown' 5
shot holocall-on-call
expect 'holocall: the call view of tandem-peer closed, the link stays' 15
# (Its hiding animation: gone within 4 s.)
pause 5
cmd comms holocall
expect 'holocall: mode 0 with nobody, drawn 0' 5
cmd phone
expect 'phone: tandem-peer \(Linked\): Hang up \| ' 10
reject 'holocall: a call of the game.s'
reject 'watchdog: game thread silent'
