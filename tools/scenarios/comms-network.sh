# As a host: the mock guest's Ping quickhack reaches a device near V; linked, our copy of the device pulses its network and draws its beams
# shellcheck shell=bash disable=SC2317
# Where: COMMS_AT (a fixture or "X Y Z": a device on a network within 40 m,
# an access point or a camera, COMMS_KIND; default h11-basement, testing.md
# "Fixtures").
# To look at: the beams from the device to the devices it is linked with,
# as after our own Ping quickhack, and the device outlined for 10 s.
ensure host
at "${COMMS_AT:-h11-basement}"
mark
cmd device near "${COMMS_KIND:-ap}"
expect 'devices: nearest [a-z]+ [0-9a-f]+ [A-Za-z]+ at ' 10
device=$(since | grep -oE 'devices: nearest [a-z]+ [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "device $device"
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --reveal-device "$device@6"
expect 'comms: link with tandem-peer: [0-9] -> 3' 40
expect_peer "pulsed the network from device $device for 10 s \(ping quickhack\)" 20
expect 'comms: outlined a reveal of a linked player' 10
expect "comms: network pulsed from device $device for a linked player's ping" 5
shot comms-network
reject 'comms: outline of a target not here'
reject 'watchdog: game thread silent'
