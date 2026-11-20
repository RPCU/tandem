# A walk through V's apartment as the host: the mirror, the wardrobe, the stash, the computer, a seat, the coffee, each seen busy by the mock player the way activities.md's table says, and the apartment's quest phases traced (in game: use each one by hand, in order)
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'our look sent' 20
# Every phase met, with its resources: the bought apartments' phases for
# quest_freeze_allow (engine-knowledge §13.85).
cmd quest trace on
cmd apartment report
expect 'apartment: scene [0-9]+ \(apartment (true|false), class [0-9]\)' 5
note 'the mirror: open it, change nothing, close it'
expect 'activities: in mirror' 300
expect_peer 'player 1 is busy: a shop' 10
expect 'activities: out of mirror' 300
note 'the wardrobe: open it, close it'
expect 'activities: in wardrobe' 300
expect 'activities: out of wardrobe' 300
note 'the stash: open it, close it'
expect 'activities: in stash' 300
expect 'activities: out of stash' 300
note "the computer: use its screen, then step back"
expect 'activities: our V is busy at a screen \(flags [0-9]+, workspot [0-9]+, entry [0-9]+, device [1-9][0-9]*\)' 300
expect_peer 'player 1 is busy: a screen' 10
expect_peer 'player 1 is free again' 300
note 'a seat (the couch, a chair): sit, wait 5 s, get up'
expect 'activities: our V is busy in a workspot' 300
expect_peer 'player 1 is busy: a workspot \(flags 0x1, workspot 0x[0-9a-f]+' 10
expect_peer 'player 1 is free again' 300
note 'the coffee machine: make a coffee'
expect 'activities: interaction begins' 300
expect 'activities: interaction over after [0-9.]+ s .*buffs gained: [A-Z][A-Za-z_]*\.' 300
cmd apartment report
expect 'apartment: buffs: [A-Z][A-Za-z_]*\.' 5
cmd quest trace off
