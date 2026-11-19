# As a guest of a scripted host story: the host vanishes mid-story (no goodbye): the session ends on the silence, weapons free, the guest's own story and saves back, once
# shellcheck shell=bash disable=SC2317
# Journal mirroring needs the freeze (off by default): turned on here.
ensure join quest_freeze=on
peer host --mode idle --body male --story tools/scenarios/story-host.story --vanish-at 20
expect 'in the session as player' 30
expect 'save: locked \(tandem\)' 20
expect 'journal: snapshot [0-9]+ entries' 20
expect_peer 'vanishing \(--vanish-at\)' 30
expect 'session ended: the host is gone' 15
expect 'coop notice: Co-op session ended' 5
expect 'save: (unlocked|stays locked)' 15
cmd npc count
expect 'npcs shared: 0'
reject 'watchdog: game thread silent'
