# A guest's V is remembered per host: joining the mock player once (Join a game, Continue) notes our V for it; the next time, Join a game lists that host, its V page offers that V first, and one pick joins with it and arrives beside the host
# shellcheck shell=bash disable=SC2317,SC2154
ensure off campaign=closed
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
peer host --mode region --region-far 300 --region-walk-at -1 --region-npcs 0 --body male
cmd exit menu
expect 'exit: to the main menu' 15
pause 10
# The first join: by address, with the latest save.
cmd coop join "$here:27450"
expect 'coop: main menu page save:.*Create a new V.*Continue' 15
cmd coop pick continue
expect 'session: the join arrives beside the host' 10
expect 'session: starting as join of' 90
expect 'in the session as player [0-9]+' 60
expect 'campaign: our V noted for [0-9.]+:[0-9]+ \(playthrough' 30
expect 'coop notice: Joined tandem-peer.s session \[Session-JoinedOf\]' 150
cmd coop leave
expect 'coop: left the session' 10
# Back in our own world: the save it can make now, offered in words.
expect 'coop notice: Back in your own world\. Your V keeps all it earned in tandem-peer.s game.*\[Saves-Offer\]' 60
# The next time: the host in Join a game, our V from last time first.
cmd exit menu
expect 'exit: to the main menu' 15
expect 'campaign: reading the metadata of [0-9]+ save' 30
pause 5
cmd coop menu join
expect 'coop: main menu page join:.*tandem-peer' 15
cmd coop pick tandem-peer
expect 'coop: main menu page save: \[Your V from last time \(level [0-9]+, .*\)\] \[Create a new V\]' 15
cmd coop menu shown
expect 'coop: main menu shows page save as built' 10
expect 'coop: main menu status: Your V to join tandem-peer: the one from last time' 10
shot coop-remembered-v
mark
cmd coop pick your v from last time
expect 'coop: main menu: joining with your V from last time: save [0-9]+' 10
expect 'session: the join arrives beside the host' 10
expect 'session: starting as join of' 120
expect 'in the session as player [0-9]+' 60
expect 'coop notice: Joined tandem-peer.s session \[Session-JoinedOf\]' 150
cmd coop leave
expect 'coop: left the session' 10
# The co-op menu's choice lasts for the game's run: a fresh one next.
$game stop >/dev/null
