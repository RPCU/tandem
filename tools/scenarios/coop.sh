# The co-op menu's runtime switches, with no restart: leave the session, join the mock player as its guest, leave, host again and take it in
# shellcheck shell=bash disable=SC2317
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
cmd coop status
expect 'coop: Hosting on port 27450' 10
cmd coop leave
expect 'coop: left the session' 10
expect 'session: chosen off' 5
cmd coop status
expect 'coop: Not in a session' 10
# The game as a guest of the mock player, from the menu's Join.
peer host --mode front --body male
cmd coop join "$here:27450"
expect 'session: starting as join of' 10
expect 'in the session as player [0-9]+' 20
expect 'coop notice: Joined' 10
pause 5
cmd coop status
expect 'coop: In .*session' 10
cmd coop leave
expect 'coop: left the session' 10
# Hosting again from the menu: the mock player joins it.
cmd coop host
expect 'session: starting as host on UDP 27450' 10
# (Joins in the first 3 s of a session are not announced.)
pause 4
peer join "$game_ip:27450" --mode front --body male
expect 'joined \(player [0-9]+' 30
expect 'coop notice: .*joined the session' 10
reject 'watchdog: game thread silent'
