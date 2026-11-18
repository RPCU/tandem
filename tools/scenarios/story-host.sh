# As host: our journal, call, lines, hub and a reward reach a guest; its answer picks up our phone, its suggestion shows
# shellcheck shell=bash disable=SC2317
# A test contact (tandem_test) keeps the phone facts away from real quests.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --answer 2 --suggest 1
expect 'joined \(player [0-9]+' 20
# Our journal went out when the session began (before this scenario's
# mark); the plugin's record hands it to the joiner.
expect 'story: tandem-peer joins the story' 20
expect_peer 'journal: snapshot of [0-9]+ entries' 20
expect_peer 'tracked: [0-9]+' 10
cmd story call tandem_test
expect 'phone: our call with tandem_test, phase 1, sent' 10
expect_peer 'call: contact 0x[0-9a-f]+ phase 1 answered by 0' 10
expect_peer 'answer the call from' 10
expect 'phone: answered by player [0-9]+' 10
expect_peer 'call: contact 0x[0-9a-f]+ phase 1 answered by [1-9]' 10
cmd story call end tandem_test
expect_peer 'call: contact 0x[0-9a-f]+ phase 3' 10
cmd story line Hello from the host
expect 'subtitle: sent \(type 1\)' 10
expect_peer 'line: type 1 from player 1: Hello from the host' 10
cmd story hub
expect 'choice: our hub 4242 sent \(2 options\)' 10
expect_peer "hub: 4242 'Test' with 2 options" 10
expect 'choice: player [0-9]+ suggests 1' 10
cmd story hub close
expect_peer 'hub: 4242 closed' 10
cmd story reward QuestRewards.MinigameMoney
expect 'reward: 0x[0-9a-f]+ \(quest 0x[0-9a-f]+\) to 1 guest' 10
expect_peer 'reward: 0x[0-9a-f]+' 10
reject 'watchdog: game thread silent'
