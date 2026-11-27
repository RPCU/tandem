# Joining a partner 300 m away with no fast travel point at their spot: one move behind the fast travel's loading screen, no visible hop
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset 300,0,0 --body male
expect 'joined \(player [0-9]+' 20
pause 3
cmd regroup
expect 'travel: joining tandem-peer behind the fast travel.s loading screen \([0-9]+ m\)' 10
expect 'travel: screen restored' 60
reject 'travel: still black'
shot regroup-hidden
reject 'watchdog: game thread silent'
