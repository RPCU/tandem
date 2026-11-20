# A scripted host for scene-camera.sh: a full cutscene (tier 5) beside the
# guest, its camera's view sent every frame, a cut, then the views stop
# before the scene ends. Format: peer/src/main.cpp (scene, cast, camera).
3 tier 5
3 scene base/quest/main_quests/prologue/q001/scenes/q001_01_intro.scene 5
4 cast own
5 camera on 45
9 camera cut
13 camera off
17 scene off
17 tier 1
