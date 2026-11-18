# Dropped items are shared: a player's drop lies in our world and V picks it up; V's drop goes to the player who takes it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --drop-item Items.FirstAidWhiffV0 --take-items 3
expect 'joined \(player [0-9]+' 20
expect_peer 'dropped world item Items.FirstAidWhiffV0' 30
# Their drop: numbered by us (the host), a copy spawned from their puppet, found.
expect 'loot: world item [0-9]+ from player [0-9]+ lies at' 10
expect 'loot: world item [0-9]+ from player [0-9]+: copy spawning \(true\)' 10
expect 'loot: world item [0-9]+ found in our world' 10
shot loot-copy
# V picks it up ("take all"): a claim, granted, and the player hears it.
cmd loot take
expect 'loot take: world item [0-9]+, true' 10
expect 'loot: we picked up world item [0-9]+ stack 0' 5
expect 'loot: world item [0-9]+ stack 0 \(Items.FirstAidWhiffV0 x1\) is ours' 5
expect_peer 'world item [0-9]+ stack 0 taken by player 1' 10
# V drops one: shared, and the player takes it: gone from our bag.
cmd loot drop Items.FirstAidWhiffV0
expect 'loot drop: Items.FirstAidWhiffV0 x1, shared true' 10
expect 'loot: our drop \(token [0-9]+\) is world item [0-9]+' 10
expect 'loot: world item [0-9]+ \(ours\) found in our world' 10
expect_peer 'world item [0-9]+ from player 1: 1 stack' 10
expect_peer 'claimed world item [0-9]+ stack 0' 10
expect 'loot: tandem-peer took world item [0-9]+ stack 0 \(Items.FirstAidWhiffV0 x1\): 1 removed from our copy' 10
cmd loot
expect 'loot: 0 world item\(s\)' 10
reject 'loot: world item [0-9]+: our copy never showed up'
reject 'watchdog: game thread silent'
