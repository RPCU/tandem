# Our own pause menu in a session: the watchdog says paused (the engine's frames run), never silent, no stall capture; the frames after each close are timed, a long pause and a short one
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
pause 3
# Held 8 s: past the watchdog's 5 s.
$game key 1 >/dev/null
expect 'pause: our pause menu opened, pausing everyone' 10
expect 'watchdog: game thread paused for 5 s' 10
$game key 1 >/dev/null
expect 'pause: our pause menu closed' 10
expect 'watchdog: game thread back after' 10
expect 'pause: after our menu closed, the first frame took [0-9.]+ ms, the longest of the next 30 [0-9.]+ ms' 10
# Under a second: read as a frame that long before (2026-11-23).
mark
$game key 1 >/dev/null
expect 'pause: our pause menu opened, pausing everyone' 10
$game key 1 >/dev/null
expect 'pause: our pause menu closed' 10
expect 'pause: after our menu closed, the first frame took [0-9.]+ ms, the longest of the next 30 [0-9.]+ ms' 10
reject 'watchdog: game thread silent'
reject 'observer: capture [0-9]+ \(stall\)'
