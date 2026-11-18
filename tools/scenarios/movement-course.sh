# A player runs the movement course ahead of V: every placement reached, the lap's jump back teleported
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode course --body male
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect_peer 'course: jump \(air\)' 20
expect 'puppet [0-9]+: first air placement' 10
expect 'puppet [0-9]+: first climb placement' 15
expect 'puppet [0-9]+: first ladder placement' 15
expect 'puppet [0-9]+: first swim placement' 15
expect 'puppet [0-9]+: first platform placement' 20
# The lap ends 40 m ahead and starts again at the start: a teleport.
expect 'body teleported to its player' 30
# How far the body stayed from the stream, per placement, is in the health lines.
expect 'puppet [0-9]+: [0-9]+ frames, .* m from target' 10
pause 5
reject 'watchdog: game thread silent'
