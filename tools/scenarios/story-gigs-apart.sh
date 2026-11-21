# As a guest: apart from the host (the regions switch, by hand), our gigs run and their journal entries are ours; grouped again, and the journal put back exactly on leave
# shellcheck shell=bash disable=SC2317
ensure join quest_freeze=on
peer host --mode idle --body male --story tools/scenarios/story-host.story
expect 'in the session as player' 30
expect 'quest: own quests frozen' 20
expect 'journal: snapshot [0-9]+ entries' 20
cmd quest apart
expect 'quest: apart from the host: our gigs run' 5
expect 'story: apart from the host: our gigs are ours, [0-9]+ gig journal entries changed hands' 5
# Ours given back exactly (the engine's rules: Story.reds RestoreEntries).
expect 'journal: apart: [0-9]+ entries put back \([0-9]+ set, [0-9]+ passes\), 0 not as before' 5
cmd quest report
expect 'quest freeze on \(apart\)' 5
pause 10
cmd quest grouped
expect 'quest: grouped with the host: our gigs wait' 5
expect 'story: grouped with the host, [0-9]+ gig journal entries changed hands' 5
# Symmetric: every gig entry given back is hidden again (nothing of ours
# moved in these seconds at the test field).
expect 'of ours hidden: ([0-9]+) of the \1 given back, 0 new while apart; 0 given back and since cleared' 5
mark
cmd quest apart
expect 'story: apart from the host' 5
expect 'journal: apart: [0-9]+ entries put back \([0-9]+ set, [0-9]+ passes\), 0 not as before' 5
# The host goes while we are apart: back to grouped, journal as it was.
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'journal: leave: [0-9]+ entries put back \([0-9]+ set, [0-9]+ passes\), 0 not as before' 15
expect 'journal: own entries: [0-9]+ at the join, [0-9]+ now \([0-9]+ untouched checked, 0 differ' 5
expect 'journal: restored, digest matches baseline' 5
expect 'quest: own quests resumed' 10
reject 'journal: restore mismatch'
reject 'journal: [a-z]+: not as'
reject 'watchdog: game thread silent'
