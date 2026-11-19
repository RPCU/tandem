# As a guest: the host's quest prop is a session item, taken back out when the host vanishes, before saving unlocks
# shellcheck shell=bash disable=SC2317
# Journal mirroring needs the freeze (off by default): turned on here.
ensure join quest_freeze=on
peer host --mode idle --body male --story tools/scenarios/story-host.story --vanish-at 30
expect 'in the session as player' 30
expect 'save: locked \(tandem\)' 20
# As if the host's quest had given our V a prop (a first aid spray: no
# real quest's item is touched).
cmd story item Items.FirstAidWhiffV0
expect 'story: session item Items.FirstAidWhiffV0 x1' 5
cmd story items
expect 'story: 1 session item record\(s\) held' 5
expect_peer 'vanishing \(--vanish-at\)' 40
expect 'session ended: the host is gone' 15
expect 'story: 1 session item record\(s\) taken back out' 10
expect 'save: (unlocked|stays locked)' 15
cmd story items
expect 'story: 0 session item record\(s\) held' 5
reject 'watchdog: game thread silent'
