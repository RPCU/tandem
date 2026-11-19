# As a guest of a scripted host story: frozen, saves locked, journal, call, lines, hub, offer, reward; put back when the host leaves
# shellcheck shell=bash disable=SC2317
# The freeze is off by default (not yet proven in game): this scenario turns
# it on; without it the journal is not mirrored.
ensure join quest_freeze=on
peer host --mode idle --body male --story tools/scenarios/story-host.story
expect 'in the session as player' 30
expect 'save: locked \(tandem\)' 20
expect 'quest: own quests frozen' 10
expect 'journal: snapshot [0-9]+ entries' 20
expect 'phone: incoming call from judy' 20
expect 'phone: call with judy started' 15
expect 'subtitle: shown' 10
expect 'choice: hub shown \(2 options\)' 10
expect 'choice: the host picked 0' 15
expect 'choice: hub closed' 5
expect 'phone: call with judy ended' 10
# The host's tier 4 with the host not drawn near us: the offer to join.
expect 'story: join offer shown' 10
cmd story join
expect 'story: join offer accepted' 5
expect 'reward: given' 15
cmd story state
expect 'story: you [0-9]+, host 1, frozen true, save locked true' 5
cmd quest report
expect 'quest freeze on: [0-9]+ nodes refused' 5
# The host goes: everything put back, our own story resumes.
# Only what comes after: the previous host's goodbye may be just past the start.
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'phone: mirrored call closed|story: leaving the host' 10
expect 'journal: restored, digest matches baseline' 10
expect 'quest: own quests resumed' 10
expect 'save: unlocked' 10
reject 'journal: restore mismatch'
reject 'watchdog: game thread silent'
