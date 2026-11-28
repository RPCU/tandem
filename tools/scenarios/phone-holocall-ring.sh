# A partner's call as the game's own (link = ask): the phone system rings it, its incoming-call widget showing their name; answered, the phone's call view for a few seconds; then the call ends and the link stays
# shellcheck shell=bash disable=SC2317
ensure host link=ask
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --link ask --link-at 10
expect 'joined \(player [0-9]+' 20
expect 'comms: link with tandem-peer: 0 -> 2' 30
expect 'holocall: ringing from tandem-peer: the phone.s own call' 5
pause 2
cmd comms holocall
expect "holocall: mode 1 .*the phone: tandem_partner_[0-9]+ phase 1 theirs, contact tandem-peer PhoneAvatars.Avatar_Unknown; the HUD's ring: [^ ]" 5
shot holocall-ringing
# Answered (from the phone, as the phone key's press does).
cmd phone call tandem-peer
expect 'comms: link with tandem-peer: 2 -> 3' 10
expect 'holocall: on the call with tandem-peer: the phone.s own call' 5
pause 1
cmd comms holocall
expect 'holocall: mode 3 .*the phone: tandem_partner_[0-9]+ phase 2 theirs' 5
shot holocall-on-call
expect 'holocall: the call with tandem-peer closed, the link stays' 15
# (Its hiding animation: gone within 4 s.)
pause 5
cmd comms holocall
expect 'holocall: mode 0 with nobody, placed -1; the phone: tandem_partner_[0-9]+ phase 3 ' 5
# The call view gone: the party list is back under the health bar, full
# (it went below the portrait, compact, while the call was drawn).
expect 'hud: layout below the holocall \(compact\)' 5
cmd presence report
expect 'hud: layout under the health bar \(full\) .*; list shown' 5
shot holocall-over-party-list
cmd phone
expect 'phone: tandem-peer \(Linked\): Hang up \| ' 10
reject 'degraded: fallback partner call'
reject 'phone: our call with tandem_partner'
reject 'watchdog: game thread silent'
