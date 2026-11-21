# V's bed as the host, with the mock player going to bed too: our sleep is asked of it, its own sleep is its yes (two beds at once), the hours pass once in every game and the bed's buff stays on our V (in game: sleep in a bed of V's apartment by hand)
# by hand: a person in game does what its notes say (tools/sweep.sh leaves it out)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --skip-answer sleep
expect 'joined \(player [0-9]+' 20
note "in V's apartment, lie down in the bed and sleep (or nap): the scenario waits for it"
expect 'activities: interaction begins \(scene [0-9]+' 300
# The bed's own jump of the clock, put back while the mock player is asked.
expect 'activities: our clock jumped [0-9]+ -> [0-9]+ s \(.*: a bed\)' 300
expect 'activities: asked to skip [0-9]+ -> [0-9]+ s \(reason 1\)' 5
expect_peer 'player 1 asks to skip [0-9.]+ h \(request [0-9]+, reason 1\)' 10
# Its sleep joins ours: refused to it alone (one at a time), counted as its yes.
expect_peer 'time skip [0-9]+ of player 1: we go to bed too' 5
expect_peer 'time skip [0-9]+ of player [0-9]+: refused \(by 1, why 1\)' 10
expect_peer 'time skip [0-9]+ of player 1: applied' 10
expect 'activities: time skip [0-9]+ of player 1 ended: applied' 10
expect 'activities: our clock moved by [0-9]+ s' 5
expect 'epoch: our world was rebuilt \(time skip\)' 5
# Out of bed: the sleep's buff on our V, nothing of it sent.
expect 'activities: interaction over after [0-9.]+ s .*buffs gained: [A-Z][A-Za-z_]*\.' 300
cmd apartment report
expect 'apartment: buffs: [A-Z][A-Za-z_]*\.' 5
