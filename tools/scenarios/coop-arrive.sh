# A join of the co-op menu puts our V beside the host at its first welcome, no prompt, behind the fast travel's loading screen (no fast travel point needed): the mock player hosts 300 m away. The game hosts again at the end
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
cmd coop leave
expect 'coop: left the session' 10
peer host --mode region --region-far 300 --region-walk-at -1 --region-npcs 0 --body male
cmd coop join "$here:27450"
expect 'session: the join arrives beside the host' 10
expect 'in the session as player [0-9]+' 20
expect 'arrival: to tandem-peer once our V is free to go' 10
expect 'arrival: joining tandem-peer behind the fast travel.s loading screen' 30
expect 'travel: screen restored' 60
expect 'travel: arrived next to tandem-peer' 10
expect 'coop notice: Joined tandem-peer.s session \[Session-JoinedOf\]' 10
shot coop-arrive
# No offer to answer, nothing the arrival waited on for long.
reject 'travel: showing offer'
reject 'arrival: given up'
cmd coop leave
expect 'coop: left the session' 10
cmd coop host
expect 'session: starting as host on UDP 27450' 10
# The co-op menu's choice lasts for the game's run: a fresh one next.
$game stop >/dev/null
