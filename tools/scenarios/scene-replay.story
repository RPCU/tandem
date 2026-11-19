# A scripted host for scene-replay.sh: a story scene beside the guest with
# the --npc NPC as its actor, voiced lines, a skip, the end, then a second
# scene the host vanishes in. Format: peer/src/main.cpp (scene, cast, skip,
# voice, cut). Line ids are string ids: replace them with real ones (a line
# id the host's game logs, or one from the game's subtitle files) to hear
# the voice-over; an unknown id is silent.
3 tier 4
3 scene base/quest/main_quests/prologue/q001/scenes/q001_01_intro.scene 4
4 cast own
6 line 1 Jackie Hey, chica. You hear me?
6 voice 1000001 own 5
9 line 1 host Loud and clear.
9 voice 1000002 host 3
13 voice 1000003 own 20
15 cut 1000003
16 skip on
18 skip off
20 scene off
20 tier 1
# The second scene: the host is gone at 30 s (--vanish-at), mid-line.
24 tier 3
24 scene base/quest/main_quests/prologue/q001/scenes/q001_02_car.scene 3
25 cast own
26 voice 1000004 own 60
