# Two games, the story without playing it: our game hosts and drives its quest systems by command (a phone call, a line, a dialogue hub with a choice, a reward, a quest item, a journal entry, a gated area); the partner mirrors each one, read from its own game
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# What it cannot reach (story.md, "Story probes"): a real cinematic's
# camera and cast, a vehicle scene, a mission's own branch.
ensure host
partner_join
mark
pmark
expect_partner 'quest: own quests frozen' 20
expect_partner 'journal: snapshot [0-9]+ entries' 20
# A phone call: rings on the partner's phone, ends there with ours.
cmd story call tandem_test
expect 'phone: our call with tandem_test, phase 1, sent' 10
expect_partner 'phone: incoming call from tandem_test' 15
cmd story call end tandem_test
expect_partner 'phone: (call with tandem_test ended|mirrored call closed)' 15
# A line: its subtitle on the partner's screen.
cmd story line Hello from the host
expect 'subtitle: sent \(type 1\)' 10
expect_partner 'subtitle: shown' 10
# A dialogue hub: drawn on the partner's choice widget; our pick closes it.
cmd story hub
expect 'choice: our hub 4242 sent \(2 options\)' 10
expect_partner 'choice: hub shown \(2 options\)' 10
shot story-probe-hub
pshot=$(pgame shot story-probe-hub-partner) || fail "no screenshot of the partner"
mkdir -p "$out"
cp "$pshot" "$out/"
cmd story hub close
expect_partner 'choice: hub closed' 10
# A reward: given on the partner's game too (its money goes up).
pquery money money
cmd story reward QuestRewards.MinigameMoney
expect 'reward: 0x[0-9a-f]+ \(quest 0x[0-9a-f]+\) to 1 guest' 10
expect_partner 'reward: given' 15
pause 2
pquery rewarded money
[ "${rewarded%% *}" -gt "${money%% *}" ] 2>/dev/null || fail "the partner's money did not go up ($money -> $rewarded)"
# The partner's story is frozen and follows ours.
pcmd story state
expect_partner 'story: you [0-9]+, host 1, frozen true, save locked true' 5
pcmd quest report
expect_partner 'quest freeze on \((grouped|apart)\): [0-9]+ nodes refused' 5
reject 'watchdog: game thread silent'
reject_partner 'watchdog: game thread silent'
