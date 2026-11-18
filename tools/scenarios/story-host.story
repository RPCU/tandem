# A scripted host story for `tandem-peer host --story` (story-guest.sh).
# SECONDS VERB ARGS..., seconds after the game joined. Verbs: see
# peer/src/main.cpp (LoadStory). Journal hashes: replace the placeholders
# with real ones from the host's `tools/game.sh cmd story dump` (the
# placeholders do not resolve: the guest logs them as unknown).
2 snapshot 1000001:2 1000002:2 1000003:3
3 tracked 1000002
5 journal 1000004 2 notify
8 call judy 1
14 call judy 2
15 line 2 Judy Hey, V. Got a minute?
18 line 1 host Sure, what's up?
20 hub 501 Judy|Tell me more.|Not now.
26 pick 501 0
27 hub 501
28 call judy 3
30 tier 4
36 tier 1
38 reward QuestRewards.MinigameMoney 1000002
40 journal 1000002 3 notify
