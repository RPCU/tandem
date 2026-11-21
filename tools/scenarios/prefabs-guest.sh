# As a guest: the host's quest prefabs load in our world while grouped, go when apart, come back, and go when the session ends
# shellcheck shell=bash disable=SC2317
# The mock host names a prefab no sector of ours belongs to (an unknown node
# ref: the streaming system answers "unknown" and loads nothing): the path is
# the real one (the handler's mode, the streaming queue), nothing appears.
# The real interiors are checked by hand with two games (below).
ensure join
peer host --mode front --body male --prefabs 7a3d0c5e9b1f2468
expect 'in the session as player' 30
expect 'prefabs: the host.s quests have 1 prefab\(s\) on' 15
expect 'prefabs: 1 of the host.s on, 0 off \(grouped\)' 5
cmd story prefabs
expect 'the host.s set 1, 1 of them ours' 5
# Apart: ours off; grouped again: on again.
cmd quest apart
expect 'prefabs: 0 of the host.s on, 1 off \(apart\)' 5
cmd quest grouped
expect 'prefabs: 1 of the host.s on, 0 off \(grouped\)' 5
# The host goes: what we loaded goes with it.
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'prefabs: 0 of the host.s on, 1 off \(leave\)' 20
expect 'save: our story is back as it was' 20
reject 'prefabs: .*differ from 2.50'
reject 'watchdog: game thread silent'
# By hand, two games: the host at The Pickup's All Foods plant or The Heist's
# Konpeki Plaza, the guest beside it: the guest's log says "N of the host's
# on", the interior, its props and doors are there for the guest; after the
# leave they are gone and the guest's own save is untouched.
