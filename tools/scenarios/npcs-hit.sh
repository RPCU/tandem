# A guest hits and burns the host's NPCs: the host applies the damage and the effect; health, death and kill credit come back
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
# Earlier scenarios may have killed everyone around V: three gangers to share.
cmd npc spawn 3
# A real V's fight (recordings/play/fight.tplay, recorded as a guest: its
# hits are on proxies), else plain hits every second.
play fight --mode idle --hit-npcs 1
if [ "$played" = 1 ]; then
  expect_peer 'play: npc [0-9]+ of the recording is npc [0-9]+ here' 300
  expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health' 60
  pause 10
  reject 'watchdog: game thread silent'
  return 0
fi
expect_peer 'hit npc [0-9]+ \(plain\): 35 health' 30
expect 'outcomes: [0-9]+ struck by player [0-9]+: [0-9.]+ of [0-9.]+ health' 15
expect 'npcs: [0-9]+ gets BaseStatusEffect.Burning from player [0-9]+' 10
# Its burn damage is the guest's to compute: the host's own ticks deal none.
expect "combat: another V's own hit on [0-9]+ left to their game" 10
expect_peer 'npc [0-9]+ health [0-9]+%' 15
expect 'npcs: [0-9]+ died \(killer: player ([2-9]|[1-9][0-9]+)\)' 60
expect_peer 'npc [0-9]+ died' 10
reject 'watchdog: game thread silent'
