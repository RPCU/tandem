# The party HUD: the partner's entry (name, health), in a menu, hurt, down; off screen with distance and arrow; the nameplate up close
# shellcheck shell=bash disable=SC2317
ensure host
# Round V at walking speed: in view in front of it, off screen behind it.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode ghost --body female --name Panam \
  --hurt 5 --menu-at 8 --go-down 30
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
# (The widget is built once per game: "party shown" says it is there.)
expect 'hud: party shown \(1 partners\)' 10
expect 'hud: entry for player [0-9]+ \(Panam\)' 10
expect 'hud: player [0-9]+ "Panam" health [0-9]+ % status up, [0-9]+ m, in view, nameplate on' 30
shot presence-hud-up
expect 'hud: player [0-9]+ "Panam" health [0-9]+ % status [a-z]+( \[[A-Za-z ]+\])?, [0-9]+ m, off screen \(arrow side [1-3]\), nameplate off' 30
expect_peer 'our V: health [0-9]+ %, in a menu' 20
expect 'hud: player [0-9]+ "Panam" health [0-9]+ % status menu \[In a menu\]' 10
expect 'hud: player [0-9]+ "Panam" health (75|50|25) % status' 20
expect_peer 'our V is down' 40
expect 'hud: player [0-9]+ "Panam" health 0 % status down \[Down\]' 10
shot presence-hud-down
# The camera's field of view against the engine's own projection (one run
# settles whether it is horizontal or vertical: presence hud fov h|v).
cmd presence hud camera
expect 'hud: camera fov [0-9.]+; player [0-9]+.s head: engine ProjectPoint' 5
cmd presence report
expect 'hud: party shown, 1 entries, widget built' 5
# To look at (a person there): the entry under the health bar in the HUD's
# own font and colours; the nameplate over the head, fading from 12 to 20 m;
# all of it gone in photo mode and in a cinematic.
reject 'watchdog: game thread silent'
