# The pilot walks our real V through the street while another V plays beside it: both sides move, nothing stalls, no save is written
# needs the pilot
# shellcheck shell=bash disable=SC2317
ensure host
# A real V walking the streets (recordings/play/street.tplay), else one in front of ours.
play street --mode front --body female
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
# Our own V walks, jogs, sprints and crouches by the virtual pad, each
# waypoint checked on the plugin's state (.cache/pilot.log).
pilot street
expect 'pilot: on \(saving locked\)' 5
expect 'player [0-9]+: [0-9]+ states, 0 dropped' 20
# The mock player saw our V move (its states carry our position).
expect_peer 'player [0-9]+' 10
reject 'watchdog: game thread silent'
