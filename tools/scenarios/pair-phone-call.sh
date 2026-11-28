# Two games: the partner's link request rings on our phone as an NPC's call (the phone system's own call, its widget showing their name), answered it is the phone's call view, then it closes with the link on; our call to them shows as ours to an NPC
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# To look at (the shots): pair-phone-ring, the game's incoming-call widget
# with the partner's name and the unknown avatar, as an NPC's call rings;
# pair-phone-on, the game's audio call view with their name.
ensure host link=ask
partner_join
# The partner's consent at the join rang already (and rang out while it
# joined): the partner asks again, a new request, which rings.
pcmd comms unlink V
pause 2
mark
pmark
pcmd comms link V
expect 'comms: showing the link request of ' 20
expect 'holocall: ringing from .*: the phone.s own call' 5
pause 2
cmd comms holocall
expect "holocall: mode 1 .*the phone: tandem_partner_[0-9]+ phase 1 theirs, contact [^ (]+ PhoneAvatars.Avatar_Unknown; the HUD's ring: [^ ]" 5
shot pair-phone-ring
# Answered: the phone's own call view, then it closes, the link stays.
cmd comms accept
expect 'holocall: on the call with .*: the phone.s own call' 5
pause 2
cmd comms holocall
expect 'holocall: mode 3 .*the phone: tandem_partner_[0-9]+ phase 2 ' 5
shot pair-phone-on
expect 'holocall: the call with .* closed, the link stays' 12
cmd comms holocall
expect 'holocall: mode 0 .*the phone: tandem_partner_[0-9]+ phase 3 ' 5
expect_partner 'comms: link with .*-> 3' 10
# No quest woke: the partner's call wrote no phone fact, sent no story call.
reject 'phone: our call with tandem_partner'
reject 'degraded: fallback partner call'
reject 'watchdog: game thread silent'
reject_partner 'watchdog: game thread silent'
