# As host: a guest's gunshots are heard from its puppet, its optical camo hides the puppet from our NPCs, our stealth reaches it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --camo 15 --shoot 3
expect 'joined \(player [0-9]+' 20
expect_peer 'player 1 stealth: flags' 20
expect 'stealth: player [0-9]+ makes noise: stim 28 \(Gunshot\)' 30
expect_peer 'optical camo on' 30
expect 'stealth: player [0-9]+ camo on' 10
expect_peer 'optical camo off' 10
expect 'stealth: player [0-9]+ camo off' 10
reject 'watchdog: game thread silent'
