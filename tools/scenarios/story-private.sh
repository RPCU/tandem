# As a guest beside the host: its private scene neither holds nor pulls us in; it shows busy, then back
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V.
peer host --mode front --body male --story tools/scenarios/story-private.story
expect 'in the session as player' 30
expect 'story: weapons lowered' 15
expect 'story: tandem-peer is busy \(private scene\)' 10
expect 'story: weapons free' 5
expect 'story: tandem-peer is back' 15
reject 'story: join offer shown'
reject 'watchdog: game thread silent'
# A human checks once: "tandem-peer is busy." on screen, the host's puppet
# idling on its own (not frozen mid-step) while busy.
