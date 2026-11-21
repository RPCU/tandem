# A camera the other player turns friendly: friendly here to our V and to the other player's puppet, no alarm while both stand in front of it, marked "Hacked by NAME"; turned back, its own again and the marker gone
# shellcheck shell=bash disable=SC2317,SC2154
# Where: DEVICES_AT (a fixture or "X Y Z": an enemy's surveillance camera
# within 40 m that sees V; default h11-basement, testing.md "Fixtures").
# To look at: the camera's cone turns green; a marker in the other player's
# colour, "Hacked by NAME", over it in the world and on the minimap; no
# "intruder detected" warning while V and the puppet stand in its cone; the
# marker goes when the other player turns it back.
ensure host
at "${DEVICES_AT:-h11-basement}"
# A run before may have left it friendly: hostile, and on, before the other
# player joins.
cmd device hostile camera
pause 2
mark
cmd device near camera
expect 'devices: nearest camera [0-9a-f]+ SurveillanceCameraControllerPS at ' 10
camera=$(since | grep -oE 'devices: nearest camera [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "camera $camera"
cmd device on camera
expect 'devices: on [0-9a-f]+ SurveillanceCameraControllerPS at ' 10
pause 2
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --device "$camera:SurveillanceCameraControllerPS:friendly,hacked@3" \
  --device "$camera:SurveillanceCameraControllerPS:hostile@40"
expect 'joined \(player [0-9]+' 20
expect "devices: player [0-9]+ operated $camera" 20
expect "devices: $camera SurveillanceCameraControllerPS turned for every V here: " 10
expect "devices: $camera SurveillanceCameraControllerPS marked: hacked by player [0-9]+, registered true" 10
# What its security system is now (a sighting before the turn may have
# alerted it: befriending does not calm it, as in solo): it stays so.
query alarm_before device camera alarm
# V and the puppet (in front of V) stand in its cone a while.
pause 10
query attitude device camera attitude
expect_value "$attitude" = friendly
query puppet device camera puppet
expect_value "$puppet" != hostile
query target device camera target
expect_value "$target" '~' '^(none|other)$'
query alarm_after device camera alarm
if [ "$alarm_before" = SAFE ]; then
  expect_value "$alarm_after" = SAFE
else
  note "its security system was $alarm_before already (now $alarm_after): no new alarm not checked"
fi
query marked device camera mark
expect_value "$marked" != 0
shot devices-camera-friendly-marked
# The other player turns it back: its own side again, the marker gone.
expect "devices: $camera SurveillanceCameraControllerPS back to its own attitude for every V here" 40
expect "devices: $camera SurveillanceCameraControllerPS unmarked \(player [0-9]+'s hack\)" 5
query attitude_back device camera attitude
expect_value "$attitude_back" != friendly
query unmarked device camera mark
expect_value "$unmarked" = 0
shot devices-camera-friendly-back
reject 'watchdog: game thread silent'
