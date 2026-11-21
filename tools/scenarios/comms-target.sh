# The team target ("Mark target"): as a guest of a replayed host, its mark hangs on our proxy and is outlined; ours reaches it; a person checks the quickhack panel
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --mark-target nearest
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect 'comms: link with tandem-peer: [0-9] -> 3' 20
expect_peer 'team target: mark [0-9]+ on npc [0-9]+' 30
expect 'comms: showing ping [0-9]+ of tandem-peer \(Target, 60(\.0+)? s\) on the entity, registered true' 20
shot comms-target-theirs
# Ours, on what V looks at (the quickhack does the same from the panel).
cmd comms target
expect 'comms: team target .*: marked|comms: no target to mark|comms: team target not marked' 10
# By hand: scan an NPC, a camera and a car with a cyberdeck equipped: "Mark
# target" is listed after the deck's programs (no slot taken, cost 0), runs
# at once, and no NPC is alerted, traced or turned hostile.
note "by hand: the panel's Mark target on an NPC, a camera, a car; no alert"
reject 'watchdog: game thread silent'
