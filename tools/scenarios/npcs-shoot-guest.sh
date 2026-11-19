# As a guest of a replayed host whose NPC fires at it every 3 s: our game fires the shot from the NPC's proxy and decides the hit
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --shoot-you 3
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ fires at player [0-9]+' 15
# The proxy is handed the NPC's weapon, then fires it at our V.
# (A shot before its proxy exists, in the replay's first seconds, is lost:
# no NPC here yet to fire it.)
expect 'shots: npc [0-9]+.s shot at us fired from its proxy' 20
# Our game decides: a hit (reported to the host) or a miss by the target's
# own rules. Over a few shots at least one lands on a V standing still.
expect 'shots: npc [0-9]+.s shot hit our V \(our game.s decision\), reported to the host' 20
expect_peer 'player [0-9]+ reports npc [0-9]+.s shot hit their V' 5
# Only what comes after: the previous host's goodbye may be just past the start.
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
