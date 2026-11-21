# As host: a player with NPCs of its own walks into our region (proxies of its NPCs, a hit on one goes to it), then walks off leaving them: handed over to us, same bodies, their brains on
# shellcheck shell=bash disable=SC2317
ensure host
# Two NPCs of our own: they stay ours throughout.
cmd npc spawn 2
expect 'npc spawn: 2 gangers' 10
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
peer join "$address":27450 --name far --mode region --region-far 400 --region-walk-at 10 --region-leave-at 50 --region-speed 20 --region-npcs 3
expect 'in the session as player|joined \(player' 30
# Apart at first: nothing of theirs here.
expect 'regions: linked with nobody' 20
cmd npc count
expect 'npcs shared: [0-9]+, .*proxies 0'
# They walk up: linked 150 m out, their three NPCs shown as proxies.
expect 'regions: linked with player [2-9]' 40
expect_peer 'region: linked with player 1' 5
expect 'puppet [0-9]{8,}: controller handed to the engine' 30
cmd npc count
expect 'npcs shared: [0-9]+, .*ours [1-9][0-9]*, proxies 3'
# Our crowd stays on (we rank first), theirs is the one held back.
reject 'npcs: guest, own crowd off'
# Our hit on one of their NPCs goes to them; its health comes back.
cmd npc punch 40
expect 'combat: hit on proxy [0-9]{8,} for [0-9.]+' 10
expect_peer 'region: player 1 hit our npc [0-9]{8,}: [0-9]+ damage, health 0\.[0-9]+' 10
# They walk off, leaving the three 8 m from us: handed over, no new body.
expect_peer 'region: handed npc [0-9]{8,} over to player 1' 60
expect 'regions: npc [0-9]{8,} is ours to run, from player [0-9]+ \(handed over\)' 10
expect 'regions: handover in: npc [0-9]{8,} from player [0-9]+ health [0-9.]+ effects 0 target player 1' 10
pause 6
cmd npc count
expect 'npcs shared: [0-9]+, .*ours [3-9][0-9]*, proxies 0'
# (A line "did not wake up: spawned again in place" here is the fallback of
# docs/architecture/regions.md §4: note it in docs/research/engine/regions.md.)
reject 'regions: handover in: npc [0-9]+ from player [0-9]+ refused'
reject 'watchdog: game thread silent'
