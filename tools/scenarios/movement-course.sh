# A player runs, jumps and climbs (a real V's, else the course ahead of V): placements reached, bodies left behind teleported
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
# A real V's jumps, stairs, climbs (recordings/play/movement.tplay), else the
# synthetic course (heights relative to V: the puppet floats where the ground
# differs).
play movement --mode course --body male
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
if [ "$played" = 1 ]; then
  expect_peer 'play: started' 30
  expect 'puppet [0-9]+: first air placement' 120
  expect 'puppet [0-9]+: first climb placement' 120
else
  expect_peer 'course: jump \(air\)' 20
  expect 'puppet [0-9]+: first air placement' 10
  expect 'puppet [0-9]+: first climb placement' 15
  expect 'puppet [0-9]+: first ladder placement' 15
  expect 'puppet [0-9]+: first swim placement' 15
  expect 'puppet [0-9]+: first platform placement' 20
  # The lap ends 40 m ahead and starts again at the start: a teleport.
  expect 'body teleported to its player' 30
fi
# How far the body stayed from the stream, per placement, is in the health lines.
expect 'puppet [0-9]+: [0-9]+ frames, .* m from target' 10
pause 5
reject 'watchdog: game thread silent'
