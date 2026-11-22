# As host: a player standing next to us with NPCs of its own vanishes (no goodbye): we inherit its NPCs within the timeout, none duplicated, none frozen as a proxy
# shellcheck shell=bash disable=SC2317
ensure host
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer join "$address":27450 --name far --mode region --region-far 6 --region-walk-at -1 --region-npcs 3 --vanish-at 25
expect 'regions: linked with player [2-9]' 30
expect 'puppet [0-9]{8,}: controller handed to the engine' 30
cmd npc count
expect 'npcs shared: [0-9]+, .*proxies 3'
expect_peer 'vanishing \(--vanish-at\)' 40
expect 'player [0-9]+ left' 15
# Its three NPCs are ours from the moment it is gone: the same bodies, woken up.
expect 'regions: npc [0-9]{8,} is ours to run, from player [0-9]+ \(inherited\)' 5
expect 'regions: handover in: npc [0-9]{8,} from player [0-9]+ health [0-9.]+ effects 0 target player 0 \(inherited\)' 5
expect 'coop notice: far left the session' 5
pause 6
cmd npc count
expect 'npcs shared: [0-9]+, .*ours ([3-9]|[1-9][0-9]+), proxies 0'
reject 'consistency: proxy [0-9]+ has no NPC on the host, removed'
reject 'watchdog: game thread silent'
