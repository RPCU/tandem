# As host: a restriction our story puts on our V reaches the guest, and its lift too
# shellcheck shell=bash disable=SC2317
# `story restrict on REC` stands in for a quest's restriction node
# (questGameplayRestrictions_NodeType): a real one on our V, found by the
# host's scan like any other.
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
cmd story restrict on GameplayRestriction.NoJump
expect 'story: our restrictions for the guests \(tier [0-9]+\): .*GameplayRestriction.NoJump' 5
expect_peer 'story restrictions: [1-9][0-9]* on player 1' 5
cmd story restrict off GameplayRestriction.NoJump
expect 'story: our restrictions lifted' 5
expect_peer 'story restrictions: none on player 1' 5
# A menu block is the host's own, never shared.
cmd story restrict on GameplayRestriction.BlockAllHubMenu
cmd story restrict
expect 'story: restrictions: host moment false' 5
cmd story restrict off GameplayRestriction.BlockAllHubMenu
# The phone kept away and nothing else (a save can hold it through hours of
# free roam): no moment, and not said to the guests.
cmd story restrict on GameplayRestriction.NoPhone
pause 2
cmd story restrict
expect 'story: restrictions: host moment false' 5
cmd story restrict off GameplayRestriction.NoPhone
reject 'story: our restrictions for the guests \(tier 1\): GameplayRestriction.NoPhone$'
reject 'spectate: our V is in a scripted moment \(reasons 2:'
reject 'watchdog: game thread silent'
