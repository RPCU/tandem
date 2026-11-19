# The party HUD lists linked partners: name, health and state in their colour; a broken link takes the entry and the plate's health line away at once, a vanish everything
# shellcheck shell=bash disable=SC2317
ensure host
cmd coop set nameplates near
expect 'setting: nameplates = near' 10
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --name Panam \
  --hurt 5 --unlink-at 15 --vanish-at 30
expect 'joined \(player [0-9]+' 20
# link = auto on both sides: linked from the start.
expect 'comms: link with Panam: [0-9] -> 3' 20
expect 'hud: player [0-9]+ "Panam" health [0-9]+ % status up, [0-9]+ m, in view, nameplate on, with health, linked: in the party list' 30
shot hud-linked
# Hurt: the bar follows, in the entry and on the plate.
expect 'hud: player [0-9]+ "Panam" health (75|50|25) % .*linked: in the party list' 20
# The link broken from its side: no entry, the name alone over the head.
expect_peer 'broke every link' 30
expect 'comms: link with Panam: 3 -> [0-2]' 5
expect 'hud: player [0-9]+ "Panam" .*nameplate (on, name only|off), not linked: no entry' 5
shot hud-unlinked
# Always: the plate at any distance (to look at: smaller and fainter far away).
cmd coop set nameplates always
expect 'setting: nameplates = always' 10
cmd presence report
expect 'hud: party shown, 1 entries, widget built, at [0-9.]+ [0-9.]+, nameplates always' 5
# It vanishes: its plate and marker go with it.
expect_peer 'vanishing \(--vanish-at\)' 30
expect 'hud: player [0-9]+.s entry removed' 15
expect 'marker: player [0-9]+ removed' 10
cmd coop set nameplates near
expect 'setting: nameplates = near' 10
reject 'watchdog: game thread silent'
