# The main menu's join is kept across the load: Co-op, Join a game (the mock player's address), Continue; as the save plays the session starts, our own story waits from the load (not from the welcome), and our V goes beside the host behind the fast travel's loading screen. The game hosts again at the end
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
peer host --mode region --region-far 300 --region-walk-at -1 --region-npcs 0 --body male
cmd exit menu
expect 'exit: to the main menu' 15
pause 10
# The main menu's Join a game, by address (a join code goes the same way).
cmd coop join "$here:27450"
expect 'coop: main menu page save:.*Create a new V.*Continue' 15
cmd coop pick continue
expect 'session: the join arrives beside the host' 10
expect 'session: starting as join of' 90
expect 'quest: own quests held from the load' 30
expect 'in the session as player [0-9]+' 30
expect 'arrival: joining tandem-peer behind the fast travel.s loading screen' 60
expect 'travel: screen restored' 60
expect 'coop notice: Joined tandem-peer.s session \[Session-JoinedOf\]' 10
shot coop-arrive-load
reject 'quest: own quests resumed, no welcome came'
cmd coop leave
expect 'coop: left the session' 10
expect 'quest: own quests resumed' 20
cmd coop host
expect 'session: starting as host on UDP 27450' 10
# The co-op menu's choice lasts for the game's run: a fresh one next.
$game stop >/dev/null
