# As host: a guest's answer to a romance partner's call is refused and the call rings on; apart from us, a guest cannot decline or answer for us a call that leaves a choice
# shellcheck shell=bash disable=SC2317
# Test contacts named after a partner (judy_tandem_test) and the usual one
# (tandem_test): no real quest listens to their phone facts.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --answer 2
expect 'joined \(player [0-9]+' 20
expect 'story: tandem-peer joins the story' 20
# A romance partner's call is the host's alone, near or far.
cmd story call judy_tandem_test
expect 'phone: our call with judy_tandem_test, phase 1, sent' 10
expect_peer 'answer the call from' 10
expect 'phone: player [0-9]+.s answer refused \(judy_tandem_test: the host.s alone\), it rings on' 10
reject 'phone: answered by player [0-9]+'
cmd story call end judy_tandem_test
expect_peer 'call: contact 0x[0-9a-f]+ phase 3' 10
# Any other contact: the guest near us answers for everyone, as before.
cmd story call tandem_test
expect 'phone: answered by player [0-9]+' 10
cmd story call end tandem_test
expect_peer 'call: contact 0x[0-9a-f]+ phase 3' 10
# A guest a kilometre away (linked with nobody): the call may be declined,
# so its answer is refused.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --name far --mode region --region-far 1000 --region-walk-at -1 --answer 2
expect 'joined \(player [0-9]+' 20
expect 'regions: linked with nobody' 20
mark
cmd story call tandem_test
expect_peer 'answer the call from' 10
expect 'phone: player [0-9]+.s answer refused \(tandem_test: a choice, and they are apart from us\), it rings on' 10
reject 'phone: answered by player [0-9]+'
cmd story call end tandem_test
reject 'watchdog: game thread silent'
