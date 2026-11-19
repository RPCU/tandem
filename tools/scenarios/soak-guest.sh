# Soak: the game as a guest of the simulated host (PERSONA, default companion; SEED; MINUTES, default 10): its NPCs fight, its story plays, nothing the referee rejects either way
# shellcheck shell=bash disable=SC2317
ensure join
peer sim --role host --persona "${PERSONA:-companion}" --seed "${SEED:-1}" --minutes "${MINUTES:-10}"
expect 'in the session as player' 30
expect_peer 'sim: player [0-9]+ \(.*\) joined' 30
expect 'npcs: guest, own crowd off' 30
pause $((${MINUTES:-10} * 60 - 60))
cmd npc count
expect "npcs shared: [0-9]+, own hidden: [0-9]+, in the session's record: [0-9]+" 10
reject 'watchdog: game thread silent'
reject 'consistency: proxy [0-9]+ (has no NPC on the host|was missing)'
# The sim's own checks: its world and the session's record never drifted apart.
reject_peer 'sim-check: '
expect_peer 'sim summary: .* 0 violation\(s\)' 90
