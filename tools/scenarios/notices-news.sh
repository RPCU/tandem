# News as the game's own side notification (a join), a question as the HUD's warning line: both on screen
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'coop notice: .* joined the session \[Session-PlayerJoined\]' 30
pause 1
shot notices-news-join
reject 'watchdog: game thread silent'
