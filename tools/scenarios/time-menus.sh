# The host's menus slow nothing while another player is in the session: its world runs at full speed, the mock player's clock with it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body male
expect 'joined \(player [0-9]+' 20
pause 3
# A menu through a profile (the weapon wheel, the hub menu): its slow motion
# never starts, as vanilla's multiplayer branch has it.
mark
cmd time menu wheel 3
expect 'time: the radialMenu menu would slow the shared world: refused' 5
expect 'time: test menu wheel for 3(\.0+)? s \(its slow motion refused\)' 5
expect 'time: world speed (0\.9[5-9]|1\.0)[0-9]* over the test' 5
expect 'time: test menu over' 5
mark
cmd time menu hub 3
expect 'time: the hubMenu menu would slow the shared world: refused' 5
expect 'time: world speed (0\.9[5-9]|1\.0)[0-9]* over the test' 5
expect 'time: test menu over' 5
# Menus set by name (the map, a shop's or the stash's screen, a tutorial
# popup, the breach's screen): unset within a frame.
for menu in stash tutorial breach; do
  mark
  cmd time menu "$menu" 3
  expect 'time: a menu \((VendorStash|UI_TutorialPopup|minigame)\) would slow the shared world: unset' 5
  expect 'time: world speed (0\.9[5-9]|1\.0)[0-9]* over the test' 5
  expect 'time: test menu over' 5
done
# The map held 25 s: the clock the mock player hears from the host keeps its
# pace (8 game seconds a second) across two of its 10 s updates. (If the
# game's clock does not follow the world's slow motion at all, this part
# would pass unfixed: the world speed lines above are the measure.)
mark
cmd time menu map 25
expect 'time: a menu \(WorldMap\) would slow the shared world: unset' 5
expect 'time: world speed (0\.9[5-9]|1\.0)[0-9]* over the test' 5
expect_peer 'host clock [0-9]+ s: [0-9]+ game s in [0-9.]+ s \(rate (7\.[5-9]|8\.[0-4])' 25
pause 12
reject_peer 'host clock [0-9]+ s: [0-9]+ game s in [0-9.]+ s \(rate ([0-6]\.|7\.[0-4])'
expect 'time: test menu over' 15
# None of it reached the mock player as a time bubble (a menu is not a mod's
# slow motion), and nothing the host had left slowed its world.
reject 'time: a slow motion of no reason we know'
reject 'time: an unknown dilation slows the world'
reject_peer 'player 1: time abilities 0x[1-9]'
reject 'watchdog: game thread silent'
