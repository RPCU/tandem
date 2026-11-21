# As a guest: our fire on a weak spot of a host NPC's proxy breaks it on our screen and goes to the NPC's owner, whose real weak spot takes the same share (a boss's phases move on its game)
# by hand: a person shoots the weak spot (a scripted shot at one 23 m off never registered, four tries 2026-11-21); tools/sweep.sh leaves it out
# shellcheck shell=bash disable=SC2317
# WEAKSPOT_RECORD: a character with weak spots (default
# Character.main_boss_adam_smasher; its proxy, brain off, stands 20 m off).
# The hits are real bullets of the gun V draws (`npc weakspot shot`, one at
# each open weak spot: a built hit has no hit shapes and took nothing of
# one). The gun: V's last or first ranged weapon, else the game's stand-in
# pistol (a save whose last weapon was no gun drew none, 2026-11-21).
# To look at: V draws, tracers go to the boss's weak spots, which show
# their damage.
ensure join
peer host --mode region --region-far 20 --region-walk-at -1 --region-npcs 1 \
  --region-record "${WEAKSPOT_RECORD:-Character.main_boss_adam_smasher}" --region-immortal said
expect 'in the session as player' 30
expect 'regions: linked with player 1' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
pause 3
cmd npc weakspot draw
expect 'npc weakspot: our V (draws its weapon|holds [^ ]+)' 5
pause 4
cmd npc weakspot shot
expect 'npc weakspot: a shot of [^ ]+ at each of [1-9][0-9]* open weak spots of [0-9]+ ' 10
expect 'combat: our hit took [0-9.]+% of weak spot [0-9]+ \([^)]+\) of proxy [0-9]+, for its owner' 5
expect_peer "player [0-9]+'s fire took [1-9][0-9]*% of weak spot [0-9]+ \(0x[0-9a-f]+\) of npc [0-9]+" 10
cmd npc weakspot shot
cmd npc weakspot shot
pause 2
shot npcs-weakspot
reject 'watchdog: game thread silent'
