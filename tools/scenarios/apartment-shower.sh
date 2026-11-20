# V's shower as the host: traced from start to end, the clothes shown drop (underwear, as our game shows) and our look goes out at once, the mock player hears the bare look and the dressed one after, and a Housing buff is ours (in game: take the shower by hand)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'our look sent' 20
expect_peer 'look from player 1: [0-9]+ keys, [0-9]+ items' 20
note "in V's apartment (H10, or a bought one), take a shower: the scenario waits for it"
expect 'activities: interaction begins \(scene [0-9]+' 300
# The scene that runs it: one of V's homes' (engine/scenes.cpp classifies it too).
cmd apartment report
expect 'apartment: scene [0-9]+ \(apartment (true|false), class [0-9]\), tier [0-9]' 5
expect 'apartment: buffs:.*nudity allowed (true|false)' 5
# Undressed: our look goes again at once, the puppet wears what our game shows.
expect 'activities: clothes shown now [0-9]+: our look goes again' 60
expect 'our look sent: [0-9]+ keys, [0-9]+ clothes' 5
expect_peer 'look from player 1: [0-9]+ keys, [0-9]+ items' 10
# Out of it: dressed again, the buff ours alone (nothing sends it).
expect 'activities: interaction over after [0-9.]+ s \(scene [0-9]+, apartment true.*clothes [0-9]+ -> [0-9]+ -> [0-9]+, clock \+[0-9-]+ s, buffs gained: [A-Z][A-Za-z_]*\.' 300
cmd apartment report
expect 'apartment: buffs: [A-Z][A-Za-z_]*\.' 5
