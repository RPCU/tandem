# As the host with friendly_fire = on, a guest's grenade hurts our V by our rules; the rule reaches the guest
# shellcheck shell=bash disable=SC2317,SC2154
ensure host friendly_fire=on
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --area-hit 4
expect "outcomes: the session's rules: friendly fire on" 30
expect_peer "the session's rules: friendly fire on" 30
query before health
expect_peer 'our grenade hit player [0-9]+ for 20 physical' 20
expect "outcomes: player [0-9]+'s area effect on our V: 20(\.0+)? physical" 10
# The partner's grenade changed our V's health in the game.
query after health
expect_changed "$before" "$after" down
reject 'watchdog: game thread silent'
