# As host: our quests' prefabs (the interiors our story loaded) go to the guest, an explicit none when there are none
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'prefabs: the quests system.s prefab handler is as measured on 2.50' 20
expect 'prefabs: our quests have [0-9]+ prefab\(s\) on, said to the guests' 10
expect_peer 'world prefabs: ([0-9]+ of player 1|none of player 1)' 10
cmd story prefabs
expect 'story: quest prefabs here: ' 5
reject 'prefabs: .*differ from 2.50'
reject 'watchdog: game thread silent'
