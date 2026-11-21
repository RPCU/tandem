# As a guest, an NPC proxy firing at our V with its senses on (test): does its line of sight let our game's time between hits land a shot?
# shellcheck shell=bash disable=SC2317
# Proxies' senses are off (Proxies.reds, Pacify): the time between hits reads
# the shooter's line of sight, 0 without them, and every shot was aimed to
# miss (npcs-shoot-guest, 2026-11-21). This one tries them on.
ensure join
host_world recordings/npcs.rec --mode idle --body female --shoot-you 3
expect 'in the session as player' 30
cmd shots proxy senses on
expect "shots: proxies' senses on while they fire at us \(test\)" 5
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect 'shots: npc [0-9]+.s proxy: senses on \(test\)' 30
expect 'shots: npc [0-9]+.s shot at us fired from its proxy \(5 so far' 30
expect 'shots: npc [0-9]+.s shot hit our V \(our game.s decision\), reported to the host' 20
cmd shots proxy senses off
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
