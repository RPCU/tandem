# As a guest: the host drives its own car off in free roam under a restriction alone (a moment of tier 1, no scene, no quest ride): we keep our own V and camera, never made to watch (a host driving away put its guest "in a scene", 2026-11-22)
# shellcheck shell=bash disable=SC2317
ensure join
# drive: the host's car circles our V at 35 m, inside the moment's 40 m
# (shared) and past the 30 m a scene's ride may leave us behind
# (spectate-ride: the same drive in a tier 2 scene is watched at once).
peer host --mode drive --drive-radius 35 --body male --story tools/scenarios/spectate-freeroam.story
expect 'in the session as player' 30
expect 'copy spawning' 30
# 3 s: its moment, tier 1: shared, played from our own V.
pause 20
cmd story spectate report
expect 'watching false' 5
reject 'spectate: watching '
reject 'curtain: black'
reject 'watchdog: game thread silent'
# By hand: the party HUD of the host never says "In a scene" for us.
