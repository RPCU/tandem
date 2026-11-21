# The host's Exit to the main menu (real: a host owes no save, nothing is written): its guest hears the session's end at once, by the host's goodbye, not a timeout. The game is left in the main menu (the next scenario starts it again)
# shellcheck shell=bash disable=SC2317
ensure host
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
pause 4
peer join "$game_ip:27450" --mode idle --body male
expect 'joined \(player [0-9]+' 30
cmd exit check
expect 'exit: owes a save false' 10
mark
cmd exit menu
expect 'exit: the main menu: nothing of a session to save' 10
expect 'exit: to the main menu' 10
expect 'left the game' 30
# The goodbye's reason, not the 5 s silence a lost host would be.
expect_peer 'session ended: left the session' 15
reject 'save: back from co-op: quick save asked'
reject 'watchdog: game thread silent'
