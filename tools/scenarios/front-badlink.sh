# front through a bad link (120 ms each way, 40 ms jitter, 5% lost, 2% reordered): puppet smooth, looks and weapons arrive
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --latency 120 --jitter 40 --loss 5 --reorder 2
expect 'joined \(player [0-9]+' 30
expect 'controller handed to the engine' 30
expect 'look of [0-9]+ keys' 30
# The delay widens with the link (65 ms on a clean one), within the 400 ms cap.
expect 'player [0-9]+: [0-9]+ states, [0-9]+ dropped, [0-9]+ poses, drawn (1[5-9][0-9]|[23][0-9][0-9]|400) ms behind' 30
expect 'pose stream: player [0-9]+ drawn' 30
expect_peer 'look from player 1' 20
expect_peer 'bad network: [0-9]+ datagrams through, [1-9][0-9]* lost' 20
pause 10
reject 'watchdog: game thread silent'
reject 'session ended'
