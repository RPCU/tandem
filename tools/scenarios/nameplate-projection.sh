# The nameplate is placed by the engine's own screen projection: the path in use, the plate within the screen and near our own projection of the head (the difference is logged), ours again on demand
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
cmd coop set nameplates near
expect 'setting: nameplates = near' 10
# 3 m in front of our V, stepping side to side: the plate follows the head.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --name Panam
expect 'joined \(player [0-9]+' 20
# The carrier is on the HUD and takes our projections (its root's size is
# the projection's space: 3840 x 2160 at 16:9, to read in the log).
expect 'hud: nameplates placed by the engine.s screen projection \(carried by DamageDigitsGameController, its root [0-9]+ x [0-9]+, slot .roleMappin.\)' 30
# (The slot found on the body and its height, or the world point instead.)
expect 'hud: player [0-9]+.s nameplate anchored by the engine: ' 30
expect 'hud: player [0-9]+ "Panam" health [0-9]+ % status up, [0-9]+ m, in view, nameplate on, .*placed by the engine' 30
# To look at: the name right over the head, no lag when the camera turns.
shot nameplate-engine
query path nameplate path
expect_value "$path" = engine
# In the screen, and told by the engine more than once.
query plate nameplate
note "nameplate: $plate"
expect_value "$plate" '~' '^engine [0-9]+ [0-9]+ of [0-9]+ x [0-9]+, within true, in screen true, .*, plate on$'
reject 'hud: the engine.s projection of player [0-9]+ .*: its controller woken'
# Near the head: within a tenth of the screen's height of our own
# projection of a point 2 m above the feet, on the same tick (how far says
# how wrong our own field of view arithmetic is).
query apart nameplate apart
expect_value "$apart" '<' 10
# Our own projection on demand: the same plate, placed by the script.
cmd presence hud projection script
expect 'hud: nameplates placed by our own projection \(asked: presence hud projection script\)' 5
expect 'hud: player [0-9]+ "Panam" .*nameplate on, .*placed by our projection' 10
shot nameplate-script
query own nameplate path
expect_value "$own" = script
query plate nameplate
expect_value "$plate" '~' '^script [0-9]+ [0-9]+ of [0-9]+ x [0-9]+ px, in view true, plate on$'
# And back.
cmd presence hud projection auto
expect 'hud: projection asked: auto' 5
pause 3
query back nameplate path
expect_value "$back" = engine
reject 'watchdog: game thread silent'
