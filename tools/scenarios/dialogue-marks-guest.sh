# As a guest: the host's cursor on its choice hub is marked on the same option of ours (a chip and its name in its colour), and follows it; the marks go with the hub
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode front --body male --story tools/scenarios/dialogue-marks.story
expect 'in the session as player' 30
expect 'choice: hub shown \(2 options\)' 20
expect_peer 'story step: hub 501 ' 5
expect 'choice: marks on hub 7700501: tandem-peer on 1' 10
shot dialogue-mark-host
expect 'choice: marks on hub 7700501: tandem-peer on 0' 10
expect 'choice: hub closed' 15
expect 'choice: marks cleared' 5
reject 'watchdog: game thread silent'
