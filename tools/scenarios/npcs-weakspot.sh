# As a guest: our fire on a weak spot of a host NPC's proxy breaks it on our screen and goes to the NPC's owner, whose real weak spot takes the same share (a boss's phases move on its game)
# shellcheck shell=bash disable=SC2317
# WEAKSPOT_RECORD: a character with weak spots (default
# Character.main_boss_adam_smasher; its proxy, brain off, stands 20 m off).
# To look at: the weak spot shows its damage, then breaks on the third hit.
ensure join
peer host --mode region --region-far 20 --region-walk-at -1 --region-npcs 1 \
  --region-record "${WEAKSPOT_RECORD:-Character.main_boss_adam_smasher}" --region-immortal said
expect 'in the session as player' 30
expect 'regions: linked with player 1' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
pause 3
cmd npc weakspot 40
expect 'npc weakspot: [0-9]+ of [0-9]+, [0-9.]+% whole' 10
expect 'combat: our hit took [0-9.]+% of weak spot [0-9]+ \([^)]+\) of proxy [0-9]+, for its owner' 5
expect_peer "player [0-9]+'s fire took [1-9][0-9]*% of weak spot [0-9]+ \(0x[0-9a-f]+\) of npc [0-9]+" 10
cmd npc weakspot 40
cmd npc weakspot 40
pause 2
shot npcs-weakspot
reject 'watchdog: game thread silent'
