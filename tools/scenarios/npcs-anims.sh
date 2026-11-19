# As a guest of a replayed host: proxies take the host's NPC animation states (alert, aim, shoot, crouch, reload, cover) and shots
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --anims tools/scenarios/npcs-anims.anims
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'anims: line 1 for [1-9][0-9]* NPC' 20
# Whichever NPCs the recording holds, the script's states and weapon (the
# recording's own animation states are left out with --anims). A proxy
# ready after line 2 starts from that one.
expect 'npc anims: proxy [0-9]+ follows \(high [02], upper [06], stance 3, weapon Items.Preset_Pulsar_Neon' 15
expect_peer 'anims: line 3 for [1-9][0-9]* NPC.*, 5 shots' 15
# Seen and heard (a person watching): weapons drawn and pointed at your V,
# muzzle flashes and reports, crouching, the reload sound, back to relaxed.
expect_peer 'anims: line 7 ' 25
cmd npc anims
expect 'npc anims: [0-9]+ sent, [1-9][0-9]* followed, [1-9][0-9]* applied, [1-9][0-9]* shots'
reject 'watchdog: game thread silent'
