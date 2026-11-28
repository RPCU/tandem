# A co-op campaign continued alone with the game's own Continue opens to friends as it loads (campaign = open): the host just plays, and the mock player drops in a while later, no restart; with campaign = closed the same load plays alone. No save is written
# shellcheck shell=bash disable=SC2317,SC2154
ensure off campaign=open
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
gport=${TANDEM_GAME_PORT:-$([ "${TANDEM_INSTANCE:-a}" = main ] && echo 27450 || echo 27550)}
# The next world loads as one whose save says co-op campaign (what a save
# made after hosting it from the co-op menu holds).
cmd coop campaign next
expect 'campaign: the next world loads as a co-op campaign' 10
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
cmd coop continue
expect "coop: main menu: the game's own Continue" 10
expect 'campaign: a world loaded at [0-9.]+ s of playthrough time, playthrough .*: a co-op campaign' 120
expect 'campaign: a co-op campaign loaded: open, hosting as it loads' 10
expect "session: starting as host on UDP $gport" 10
expect 'coop notice: Co-op campaign open: friends can drop in \[Session-CampaignOpen\]' 120
# Alone for a while: it is just playing.
pause 20
cmd coop status
expect "coop: Hosting on port $gport" 10
reject 'coop notice: .*\[Session-HostFailed\]'
# The mate comes online and drops in.
peer join "$game_ip:$gport" --mode front --body male
expect 'joined \(player [0-9]+' 30
expect_peer 'in the session' 20
expect 'coop notice: .*joined the session' 10
shot coop-campaign-open
pkill -x tandem-peer || true
cmd coop leave
expect 'coop: left the session' 10
# Closed: the same campaign loads alone, and says how to open it.
cmd coop set campaign closed
expect 'setting: campaign = closed \(saved to tandem.ini\)' 10
cmd coop campaign next
expect 'campaign: the next world loads as a co-op campaign' 10
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
mark
cmd coop continue
expect 'campaign: a co-op campaign loaded: closed, played alone' 120
expect 'coop notice: Co-op campaign, closed: .*\[Session-CampaignClosed\]' 120
pause 5
reject 'session: starting as host'
cmd coop status
expect 'coop: Not in a session' 10
cmd coop set campaign open
expect 'setting: campaign = open \(saved to tandem.ini\)' 10
