# A partner fast travels: its puppet is put there at once, not run across the map
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --jump-at 12
expect 'spawning ' 30
expect_peer 'fast travel: 1.5 km away' 30
expect 'player [0-9]+: jumped to' 5
# Settled where it landed (the health line: within a metre of its track).
pause 6
expect 'puppet [0-9]+: [0-9]+ frames, body .* 0\.[0-9]+ m from target' 10
reject 'watchdog: game thread silent'
