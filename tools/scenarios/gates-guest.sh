# As a guest: our story gates go to the host, the host's further story is told in words at the join
# shellcheck shell=bash disable=SC2317
# The mock host says its story has lifted the Watson lockdown and opened
# Dogtown (gates 3). Our save decides ours: one in Act 2 or later says the
# same gates open; one in Act 1 hears the Watson note.
ensure join
peer host --mode front --body male --gates 3
expect 'in the session as player' 30
expect 'gates: ours now: Watson lockdown (lifted|on), Dogtown (open|shut)' 10
expect 'gates: .* says Watson lockdown lifted, Dogtown open' 10
expect 'gates: with .*: (the same gates open|behind the host.s story:.*)' 10
expect_peer 'story gates: player [0-9]+ flags [0-9]+' 10
cmd story gates
expect 'story: gates ours Watson lockdown' 5
reject 'watchdog: game thread silent'
# By hand, two games: a guest save in Act 1 (Watson) joins a host in Act 2:
# both see the lockdown note; the host drives over a bridge, the guest on
# foot at the barricade gets "... is past the Watson lockdown ..." once, and
# gets across seated in the host's car.
