# A scripted host for scene-face.sh: a scene whose actor speaks, its face
# sent as rig poses while it does. Format: peer/src/main.cpp (scene, cast,
# voice, face). The mock player's face is a 40-joint stand-in: a real
# proxy's face rig is another one, so nothing is written on it; the
# scenario checks the path, not the picture.
3 tier 4
3 scene base/quest/main_quests/prologue/q001/scenes/q001_01_intro.scene 4
4 cast own
6 voice 1000001 own 8
6 face own on
14 face own off
17 scene off
17 tier 1
