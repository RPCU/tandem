# As a guest of a replayed host whose NPC fires a smart gun at it every 3 s: the NPC's proxy fires guided rounds that home on our V, and our game decides the hits
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
host_world recordings/npcs.rec --mode idle --body female --shoot-you 3 \
  --shot-weapon Items.Preset_Yukimura_Default --shot-smart
expect 'in the session as player' 30
query before health
expect_value "$before" '>' 0
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ fires at player [0-9]+ \(guided\)' 15
# The proxy holds the smart gun and fires a guided round at one of our V's
# targeting components (AIWeapon.Fire's shouldTrackTarget).
expect 'shots: npc [0-9]+.s guided round at us fired from its proxy \(homing on our V.s' 20
reject 'shots: npc [0-9]+.s guided round has no targeting component'
# The time between hits still decides: some land, reported to the host.
expect 'shots: npc [0-9]+.s shot hit our V \(our game.s decision\)' 25
# ...and told to everyone as the hit it was (the puppet reacts to it).
expect 'hit taken: our V.s first, by npc [0-9]+, told to the others' 5
expect_peer 'player [0-9]+ took a hit: by npc [0-9]+' 5
query after health
expect_changed "$before" "$after" down
# Seen (a person there): the rounds bend toward V in flight, as a smart
# gun's do in single player; a round meant to miss flies past.
mark
pkill -x tandem-peer
expect 'session ended' 60
reject 'watchdog: game thread silent'
