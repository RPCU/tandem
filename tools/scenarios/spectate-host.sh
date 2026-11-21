# As the host: a test moment is said to the guests as one to watch (kStoryMoment, kStoryWatch) with our camera's views, a private scene never is
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
cmd story spectate on
expect 'spectate: test moment on' 5
expect 'spectate: our V is in a scripted moment \(reasons 32.*only our view shows it' 5
expect 'story: our story flags 80' 5
cmd story spectate report
expect 'host moment true \(reasons 32, to watch true' 5
# A private scene ends it at once and says nothing more.
cmd story private on
expect 'spectate: our moment is over' 5
cmd story private off
expect 'spectate: our V is in a scripted moment' 5
cmd story spectate off
expect 'spectate: our moment is over' 5
cmd story spectate report
expect 'host moment false' 5
reject 'watchdog: game thread silent'
