# As a guest: the host's street comes and goes (a driving car out of range every 8 s, a new vehicle of its record 4 s later): each leaving proxy's body is parked and becomes the next vehicle's, none created or deleted for it; the host gone, the pool keeps three of the record and deletes the rest
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle --body female --traffic 4 --traffic-churn 8
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'street: vehicle [0-9]+ .* spawning at' 30
expect_peer 'street: 4 cars of .* round player' 20
# Five bodies asked of the game for the five proxies, the pool empty.
pause 4
cmd street count
expect 'street count: [0-9]+ \(0 run by us, 5 shown as proxies; .* bodies: 5 created, 0 re-used .* pool [0-9]+ \(0 ready, 0 to delete' 10
# Three rounds: a car leaves (its body parked under the map), a new vehicle
# of its record comes 4 s later and takes that body.
expect_peer 'out of range \(churn\)' 20
expect_peer 'announced in its place \(churn 3\)' 40
expect 'street: vehicle [0-9]+ .* takes a parked body at' 5
pause 1
cmd street count
# Still five bodies created in all: every vehicle since took a parked one,
# as it stood (the same look), and nothing was deleted or lost.
expect 'street count: [0-9]+ \(0 run by us, [45] shown as proxies; .* bodies: 5 created, [3-9] re-used \(0 with another look\), [3-9] parked, 0 deleted, 0 lost; pool [0-9]+ \([01] ready, 0 to delete' 10
shot traffic-pool
# A body back from its parking place follows its owner as any other.
cmd vehicle moves
expect 'vehicle moves: .* 0 body\(ies\) that do not follow, 0 refused' 5
# The host goes: its five proxies are parked; three of a record are kept
# for the next session, the two parked longest deleted (one a sweep, after
# 5 s parked).
pkill -x tandem-peer
expect 'session ended' 60
pause 10
cmd street count
expect 'street count: 0 .* bodies: 5 created, [3-9] re-used .* 2 deleted, 0 lost; pool [0-9]+ \(3 ready, 0 to delete, [0-9]+ kept for good, 0 not here yet\)' 10
# No proxy was asked for again for want of a body, none deleted unborn.
reject "street: check: vehicle [0-9]+'s body was gone"
reject 'entities: body .* deleted before it exists'
reject 'does not follow the kinematic move'
reject 'watchdog: game thread silent'
