# Continue co-op campaign (main menu): a world hosted from the co-op menu is a co-op campaign; the main menu learns its save from the saves' list and offers it first; picking it loads that save hosting, and the mock player drops in. No save is written
# shellcheck shell=bash disable=SC2317,SC2154
ensure off campaign=open
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
gport=${TANDEM_GAME_PORT:-$([ "${TANDEM_INSTANCE:-a}" = main ] && echo 27450 || echo 27550)}
# This world, hosted as the pause menu's Host this game hosts it: a co-op
# campaign from now on.
cmd coop host
expect "session: starting as host on UDP $gport" 10
cmd coop campaign mark
expect 'campaign: this world is a co-op campaign' 10
cmd coop leave
expect 'coop: left the session' 10
cmd exit menu
expect 'exit: to the main menu' 15
# The main menu reads its saves: the one this world was loaded from is the
# campaign's.
expect 'campaign: reading the metadata of [0-9]+ save' 30
expect 'campaign: learnt from the saves: [1-9][0-9]* co-op campaign' 30
cmd coop campaign
expect 'campaign: the co-op campaign to continue: save [0-9]+' 10
cmd coop menu coop
expect 'coop: main menu page coop: \[Continue co-op campaign\] \[Start a new co-op game\]' 15
cmd coop menu shown
expect 'coop: main menu shows page coop as built' 10
expect 'coop: main menu status: Co-op campaign: level [0-9]+' 10
shot coop-continue-campaign-menu
cmd coop pick continue co-op campaign
expect 'coop: main menu: continuing the co-op campaign: save [0-9]+' 10
expect 'campaign: a world loaded at [0-9.]+ s of playthrough time, playthrough [^n]' 120
expect "session: starting as host on UDP $gport" 30
expect 'campaign: this world is a co-op campaign \(playthrough [^n]' 10
expect 'playing' 120
# Hosting alone changes nothing; then the mock player drops in.
pause 10
reject 'coop notice: .*\[Session-HostFailed\]'
peer join "$game_ip:$gport" --mode front --body male
expect 'joined \(player [0-9]+' 30
expect_peer 'in the session' 20
shot coop-continue-campaign
# What this test added, taken back: this world is no campaign of the tests.
cmd coop campaign unmark
expect 'campaign: this world is no longer a co-op campaign' 10
