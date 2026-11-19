# A guest's time skip as the host: the prompt shows on the phone key, our yes moves the clock, and a skip we cannot take is declined for us
# shellcheck shell=bash disable=SC2317
ensure host
cmd teleport home
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male --ask-skip 8 --skip-hours 2
expect 'joined \(player [0-9]+' 20
expect 'activities: player [0-9]+ asks to skip [0-9]+ -> [0-9]+ s' 20
expect_peer 'asked to skip 2 h' 5
cmd skip accept
expect 'activities: time skip [0-9]+ of player [0-9]+ accepted' 5
expect 'activities: time skip [0-9]+ of player [0-9]+ ended: applied' 10
expect 'activities: our clock moved by 7200 s' 5
expect_peer 'time skip [0-9]+ of player [0-9]+: applied' 10
