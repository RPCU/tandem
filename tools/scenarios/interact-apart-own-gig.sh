# As a guest apart from the host (the regions switch, by hand): our use of a placed object is ours, as solo (nothing asked, the devices path as before); grouped again, the host is asked
# shellcheck shell=bash disable=SC2317
# Where: INTERACT_AT ("X Y Z", default V's flat).
ensure join quest_freeze=on
peer host --mode idle --body male --interact-answer host-only
expect 'in the session as player' 30
expect 'quest: own quests frozen' 20
cmd teleport "${INTERACT_AT:-home}"
pause 4
cmd quest apart
expect 'quest: apart from the host: our gigs run' 5
mark
# Apart: our own world's, done here and said the usual way.
cmd story interact use device
expect 'interact use: [A-Za-z]+ on [A-Za-z]+' 10
pause 3
reject 'interact: asked the host about'
reject_peer "interact: player [0-9]+'s use of"
cmd quest grouped
expect 'quest: grouped with the host: our gigs wait' 5
mark
# Grouped: the host's object again, so the host is asked.
cmd story interact use device
expect 'interact: asked the host about ' 10
expect_peer "interact: player [0-9]+'s use of [0-9a-f]+: host only" 10
reject 'watchdog: game thread silent'
