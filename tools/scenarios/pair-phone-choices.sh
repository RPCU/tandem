# Two games: on a call between players, its choices are what the phone's row of that player offers (trade, go to them, ask them here, hang up last), drawn by the game's own choice widget on both games; one picked is done and ends the call, the link on
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# To look at (the shots): pair-call-choices, the game's holocall choices on
# our screen beside the call view, titled with the partner's name; on the
# partner's, the same with ours.
ensure host link=ask trade=on
partner_join
# The partner's consent at the join rang already: it asks again, and rings.
pcmd comms unlink V
pause 2
mark
pmark
# From its phone (the row's Call: its own call view, then on the call).
pcmd phone call V
expect 'holocall: ringing from .*: the phone.s own call' 20
cmd comms accept
expect 'holocall: on the call with .*: the phone.s own call' 5
# The phone's rows of each game: ours with trade (trade=on), join, ask,
# hang up; the partner's without trade (its own tandem.ini: off).
expect 'choice: a call.s choices shown \(4 options\)' 5
expect_partner 'choice: a call.s choices shown \(3 options\)' 10
pause 2
cmd comms callhub
expect 'holocall: choices with .+: Trade \| Join .+ \| Ask .+ to join you \| Hang up \(drawn true\)' 5
pcmd comms callhub
expect_partner 'holocall: choices with .+: Join .+ \| Ask .+ to join you \| Hang up \(drawn true\)' 5
shot pair-call-choices
pgame shot pair-call-choices-partner >/dev/null && cp "$cache/shots/pair-call-choices-partner.png" "$out/" 2>/dev/null || true
# Ours picks "Ask NAME to join you": a regroup offer on theirs, the call
# over on both, the link kept.
mark
pmark
name=$(since | grep -oE 'holocall: choices with .+: Trade' | tail -1 | sed -E 's/holocall: choices with (.+): Trade/\1/')
cmd comms callpick Ask "$name" to join you
expect 'holocall: Ask .+ to join you chosen on the call with ' 5
expect 'choice: a call.s choices closed' 5
expect 'phone: invite .+: true' 5
# (It joined beside our V: its game may hold the offer as together already.)
expect_partner 'travel: offer [0-9]+ (from .+ \(reason 1\)|of .+ not shown: together already)' 15
pcmd comms holocall
expect_partner 'holocall: mode [03] ' 5
cmd comms holocall
expect 'holocall: mode 0 ' 5
reject 'comms: link with .*-> 0'
reject 'watchdog: game thread silent'
reject_partner 'watchdog: game thread silent'
