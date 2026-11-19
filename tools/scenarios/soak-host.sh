# Soak: the game hosts a simulated guest (PERSONA, default chaos: bad timing, leaving, vanishing, a lossy link; SEED; MINUTES, default 10): the game's world answers every request by its rules, nothing the referee rejects
# shellcheck shell=bash disable=SC2317
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer sim --role join "$game_ip:27450" --persona "${PERSONA:-chaos}" --seed "${SEED:-1}" --minutes "${MINUTES:-10}"
expect_peer 'sim: in the session as player [0-9]+' 30
expect 'joined \(player [0-9]+' 30
pause $((${MINUTES:-10} * 60 - 30))
reject 'watchdog: game thread silent'
reject 'consistency: puppet of player'
reject_peer 'sim-check: '
expect_peer 'sim summary: .* 0 violation\(s\)' 90
