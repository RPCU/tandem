# As a guest beside the host: its cinematic holds us, its relic glitch glitches us too, its Johnny takeover is announced
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V, so we are near it.
peer host --mode front --body male --story tools/scenarios/story-johnny.story
expect 'in the session as player' 30
expect 'story: weapons lowered' 15
expect 'story: effect BaseStatusEffect.JohnnySicknessLow with the host' 10
expect 'story: effect BaseStatusEffect.JohnnySicknessLow over' 10
expect 'story: weapons free' 10
expect 'story: the host plays as Johnny' 10
expect 'story: the host is V again' 10
reject 'story: join offer shown'
reject 'watchdog: game thread silent'
