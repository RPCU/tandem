# Two games: our V takes a camera and scans through it: its time bubble centres on the camera on both games, the partner beside the camera is slowed, the partner's copy of the camera turns with ours, its party entry says "In a camera", and no scripted moment is ours
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# Where: REMOTE_CAMERA_AT (a fixture or "X Y Z": a security camera within
# 40 m; default h11-basement, the Tyger Claws' cameras).
# To look at (the pair of screenshots): ours through the camera, the
# partner's copy of it pointing where ours looks; the partner's party entry
# "In a camera", its V's nameplate over our standing body.
ensure host
cmd god on
at "${REMOTE_CAMERA_AT:-h11-basement}"
partner_join
pcmd god on
cmd device near camera
expect 'devices: nearest camera [0-9a-f]+ [A-Za-z]+ at ' 10
camera=$(since | grep -oE 'nearest camera [0-9a-f]+' | tail -1 | cut -d' ' -f3)
note "the camera: $camera"
pnear camera "$camera"
# The partner stands beside the camera, our V where it is.
pcmd device goto camera
pause 4
mark
pmark
# Our V looks through it (as its control gives it): said as our activity,
# where the view is; the partner's party entry says so.
cmd remote camera
expect "remote: our V takes control of camera $camera" 5
expect "remote: our V looks through camera $camera, [0-9.]+ m from its body" 5
expect 'activities: our V is busy in a remote view \(sort 1, ' 5
expect_partner 'activities: player [0-9]+ is busy in a remote view' 10
expect_partner 'hud: player [0-9]+ ".*" health [0-9]+ % status remote' 10
query ours remote
expect_value "$ours" '~' '^camera '
pquery theirs remote player 1
expect_value "$theirs" '~' '^camera '
# The takeover's own NoCameraControl is no scripted moment of ours.
reject 'spectate: our V is in a scripted moment'
# Where it points travels: the partner's copy follows our hands.
expect "turrets: our V holds $camera: where it points said to the others" 5
expect_partner "turrets: $camera is in player [0-9]+.s hands: our copy follows it \\(a camera\\)" 10
# Scanning through the camera: our bubble is centred on it, on both games,
# and the partner beside it is slowed.
mark
pmark
cmd time scan 0.2 4
expect 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 5
expect_partner 'time: bubble of player 1: [0-9]+ NPC\(s\), centred on its remote view at ' 10
expect_partner 'time: our V slowed to 0\.2' 10
shot pair-remote-camera
pshot pair-remote-camera-partner
compare devices "$camera"
expect 'time: test dilation over' 10
expect_partner 'time: our V back to its own speed after [0-9]' 10
# Back in our body: the partner's copy is its game's again, its entry clear.
mark
pmark
cmd remote off
expect 'remote: our V is back in its body' 5
expect_partner "turrets: $camera is out of player [0-9]+.s hands" 10
query back remote
expect_value "$back" = none
pquery pback remote player 1
expect_value "$pback" = none
reject 'watchdog: game thread silent'
