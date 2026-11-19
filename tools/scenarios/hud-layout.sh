# The party list's place and what it yields to: a screenshot of each case (holocall, boss bar, choice hub, the game's HUD hidden, party_hud off) for a person to check that nothing overlaps
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --name Panam --hurt 5
expect 'joined \(player [0-9]+' 20
expect 'comms: link with Panam: [0-9] -> 3' 20
expect 'hud: layout under the health bar \(full\) at [0-9.]+ [0-9.]+' 30
expect 'hud: the game.s HUD modules followed: [0-9]+' 10
shot hud-layout-default
# A holocall: below the portrait, compact (to look at: clear of the holo).
cmd presence hud holocall on
expect 'hud: layout below the holocall \(compact\) at [0-9.]+ [0-9.]+, HUD [0-9]+ x [0-9]+, holocall up' 5
shot hud-layout-holocall
cmd presence hud holocall auto
expect 'hud: layout under the health bar \(full\)' 5
# The boss bar: top centre, nothing to yield.
cmd presence hud boss on
expect 'hud: layout under the health bar \(full\) .*boss bar up' 5
shot hud-layout-boss
cmd presence hud boss auto
# The dialogue choice hub: compact in place.
cmd presence hud choices on
expect 'hud: layout under the health bar \(compact\) .*choice hub up' 5
shot hud-layout-choices
cmd presence hud choices auto
# The game hides its HUD: ours goes with it, nameplates too.
cmd presence hud hudhidden on
expect 'hud: party hidden \(the game.s HUD is hidden\)' 5
shot hud-layout-hudhidden
cmd presence hud hudhidden auto
expect 'hud: party shown' 5
# The setting: the list off, the nameplates stay.
cmd coop set party_hud off
expect 'setting: party_hud = off' 10
expect 'hud: party list hidden \(party_hud off\)' 5
shot hud-layout-off
cmd coop set party_hud on
expect 'hud: party list shown' 5
cmd presence report
expect 'hud: layout under the health bar' 5
reject 'watchdog: game thread silent'
