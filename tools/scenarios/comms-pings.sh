# Pings both ways: the mock player's spot, then its danger and the withdrawal, shown here and gone; ours reaches it; nothing of it stays when it goes
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --ping 4
expect 'joined \(player [0-9]+' 20
# (comms: on … is said once per session, which the harness keeps across
# scenarios: the ping shown below is the check.)
# Its spot in front of us: a marker in the world, on the minimap and the map
# (a person checks the shot: the icon, the label "tandem-peer: Here").
expect 'comms: showing ping [0-9]+ of tandem-peer \(Here, 20(\.0+)? s\) at a place, registered true' 30
shot comms-ping-spot
# Twice at once: the same mark, now a danger (its own icon and sound).
expect 'comms: showing ping [0-9]+ of tandem-peer \(Danger' 20
shot comms-ping-danger
# Pinged again later: taken back.
expect 'comms: hid ping [0-9]+ of player [0-9]+' 20
cmd comms report
expect 'comms: marks sent [0-9]+, heard [1-9][0-9]*' 5
# Ours, at what V looks at: shown here too, heard by the mock player.
cmd comms ping
expect 'comms: ping .*: marked' 5
expect 'comms: showing ping [0-9]+ of ours' 5
expect_peer 'player 1 pinged: mark [0-9]+ kind [1-6]' 10
shot comms-ping-ours
# It goes without a goodbye: its marks go with it.
pkill -x tandem-peer || true
expect 'marker: player [0-9]+ removed' 45
cmd comms report
expect 'comms: .* ping marker\(s\), 0 waypoint marker\(s\) here' 10
reject 'comms: showing ping [0-9]+ of tandem-peer .* registered false'
reject 'watchdog: game thread silent'
