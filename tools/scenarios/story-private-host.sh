# As host in a private scene: the guest's puppet fades out near us, the guest sees us busy and held where it began
# shellcheck shell=bash disable=SC2317
# `story private on` stands in for a real private scene (a romance or a
# minor chat; engine/scenes.cpp classifies those: "scenes: … is private").
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
cmd story private on
expect 'story: our private scene begins' 5
expect 'story: private scene: 1 puppet\(s\) hidden' 5
expect_peer 'story state: player 1 tier [0-9]+ flags 8' 5
cmd story private off
expect 'story: private scene over' 5
expect_peer 'story state: player 1 tier [0-9]+ flags 0' 5
reject 'watchdog: game thread silent'
