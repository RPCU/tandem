# As host: a guest's cursor on our choice hub is marked on the option it hovers (a chip and its name in its colour); the marks go with the hub. (Its suggestion's mark: story-host.)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --hover 1
expect 'joined \(player [0-9]+' 20
cmd story hub
expect 'choice: our hub 4242 sent \(2 options\)' 10
expect_peer "hub: 4242 'Test' with 2 options" 10
expect_peer 'hover option 1 of hub 4242' 5
expect 'choice: marks on hub 4242: tandem-peer on 1' 10
shot dialogue-mark-hover
cmd story hub close
expect 'choice: our hub closed' 10
expect 'choice: marks cleared' 5
reject 'watchdog: game thread silent'
