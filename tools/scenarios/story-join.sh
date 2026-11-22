# As a guest 120 m from the host's cinematic: the prompt to join it, and taking it brings our V beside the host by the regroup action
# shellcheck shell=bash disable=SC2317
ensure join
cmd teleport home
# The mock host 120 m from our V: past the offer's 100 m, within a regroup's
# hop (no fast travel point needed).
peer host --mode mirror --offset 0,120,0 --body male --story tools/scenarios/story-join.story
expect 'in the session as player' 30
expect 'story: join offer shown' 20
shot story-join-offer
cmd story join
expect 'story: join offer accepted: on the way to tandem-peer' 5
expect_peer 'player [0-9]+ is on the way' 10
expect 'travel: arrived next to tandem-peer' 60
# Taken once: nothing left to accept.
cmd story join
expect 'story: no join offer to accept' 5
reject 'travel: still black'
reject 'watchdog: game thread silent'
