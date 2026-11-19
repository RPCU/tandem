# Out of the mirror, the ripperdoc or the wardrobe our look is sent again; unchanged, nobody redresses the puppet (in game: open and close the mirror by hand)
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
expect 'our look sent' 20
note 'open the bathroom mirror, change the hair, accept: the mock player logs a second look'
expect 'activities: in mirror' 120
expect 'activities: out of mirror' 120
expect 'our look sent' 10
