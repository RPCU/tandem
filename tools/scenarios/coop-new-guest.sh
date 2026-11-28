# Join a game, then Create a new V (main menu): the code's popup, the game's own New Game, and once the new V's world loads its own prologue waits, the session is joined and the new V goes beside the host behind the fast travel's loading screen, no prompt. The mock player hosts
# by hand: a person in game does what its notes say (tools/sweep.sh leaves it out)
# shellcheck shell=bash disable=SC2317
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
peer host --mode region --region-far 300 --region-walk-at -1 --region-npcs 0 --body male
cmd exit menu
expect 'exit: to the main menu' 15
pause 10
cmd coop join "$here:27450"
expect 'coop: main menu page save:.*Create a new V' 15
cmd coop pick create a new v
expect 'session: the join arrives beside the host' 10
expect 'coop: main menu: a new game, the session \(role 2\) starts as its world loads' 10
note 'the game shows its New Game: pick a start, a lifepath, make a V, confirm (character creation by hand)'
expect 'session: starting as join of' 900
expect 'quest: own quests held from the load' 60
expect 'in the session as player [0-9]+' 60
note 'the lifepath opening scene may play out first: the arrival waits for it (90 s at most)'
expect 'arrival: joining tandem-peer behind the fast travel.s loading screen|arrival: beside tandem-peer already' 150
expect 'coop notice: Joined tandem-peer.s session \[Session-JoinedOf\]' 90
# The new V is the one this host is joined with next time.
expect 'campaign: our V noted for [0-9.]+:[0-9]+' 10
shot coop-new-guest
note 'look: the new V stands beside the mock player, free to move (no prologue restriction left on it), its own prologue quest not advancing'
pause 20
reject 'arrival: given up'
