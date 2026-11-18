# drive through a bad link: the car copy follows smoothly, the seat (reliable) arrives, getting out parks it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode drive --body male --drive-seconds 20 --latency 120 --jitter 40 --loss 5 --reorder 2
expect 'copy spawning' 40
expect 'seated in seat_front_left' 40
expect 'player [0-9]+: [0-9]+ states, [0-9]+ dropped, [0-9]+ poses, drawn (1[5-9][0-9]|[23][0-9][0-9]|400) ms behind' 30
expect 'on foot' 50
expect 'car left parked' 15
expect 'movement controller back' 15
pause 5
reject 'watchdog: game thread silent'
reject 'session ended'
