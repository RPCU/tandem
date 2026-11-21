# As a guest, the pilot's real V sneaks up on one of the host's NPCs and takes it down: the game's own takedown, asked of the host
# needs the pilot
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body male
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
# Crouch, walk up behind the nearest proxy, the non-lethal takedown
# (tools/pilot/routines/stealth.pilot): the real move, with its animation.
pilot stealth
# Our V crouched for real: its stealth state, sent to the host (B did
# nothing while the game had not yet taken the new pad's input).
expect 'stealth: ours sent: camo [a-z]+ .*, crouch true' 5
expect 'takedowns: our [a-z -]+ on [0-9]+ sent to the host' 5
expect_peer 'npc [0-9]+: player [0-9]+ asks action [0-9]+: (accepted|refused)' 10
reject 'watchdog: game thread silent'
