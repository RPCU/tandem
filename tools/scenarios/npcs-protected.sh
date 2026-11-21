# As a guest: a host NPC its owner's game cannot kill (immortal) takes our lethal hit and stands, never a corpse or loot here; one not said to be so comes back 1.5 s after
# shellcheck shell=bash disable=SC2317
ensure join
# The mock host stands 20 m off with one NPC of its own that cannot die,
# and says so (NpcHealth kNpcProtected).
peer host --mode region --region-far 20 --region-walk-at -1 --region-npcs 1 --region-immortal said
expect 'in the session as player' 30
expect 'regions: linked with player 1' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
# An instant kill: sent to its owner, never a kill here.
cmd npc punch
expect 'combat: instant kill on proxy [0-9]+, for the host' 10
expect 'npcs: proxy [0-9]+ takes our lethal hit but cannot die on its owner.s game: no kill here' 5
expect_peer 'region: player [0-9]+ hit our npc [0-9]+: .* it cannot die \(said\)' 10
pause 3
reject 'npcs: proxy [0-9]+ died by our hit'
reject 'npcs: proxy [0-9]+ lives on the host'
cmd npc count
expect 'npcs shared: [0-9]+, .*proxies 1'
# The same NPC, its protection not said (a build or a quest the owner does
# not read): our kill is undone 1.5 s on, with no word of its death.
mark
pkill -x tandem-peer
expect 'session ended' 60
peer host --mode region --region-far 20 --region-walk-at -1 --region-npcs 1 --region-immortal silent
expect 'in the session as player' 30
expect 'regions: linked with player 1' 30
expect 'puppet [0-9]+: controller handed to the engine' 30
cmd npc punch
expect 'npcs: proxy [0-9]+ died by our hit \(ahead of the host\)' 10
expect 'npcs: proxy [0-9]+ lives on the host \(no word of its death in 1.5 s\): our kill was not one, brought back' 6
shot npcs-protected-back
reject 'watchdog: game thread silent'
