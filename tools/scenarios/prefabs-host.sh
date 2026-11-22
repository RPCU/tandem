# As host: our quests' prefabs (the interiors our story loaded) go to the guest, an explicit none when there are none
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
# (The host says its set once a session, at the first guest; a later guest
# is given the kept set by the endpoint: what the mock hears is the check.)
expect_peer 'world prefabs: ([0-9]+ of player 1|none of player 1)' 10
cmd story prefabs
expect 'story: quest prefabs here: ' 5
reject 'prefabs: .*differ from 2.50'
reject 'watchdog: game thread silent'
