# As a guest: where the host's region overlaps ours, our own crowd thins (overlap = density) or stays out of it (overlap = area), is off deep inside it, our own traffic there is held back, and all of it comes back when the host walks off
# shellcheck shell=bash disable=SC2317
# The mock host stands 100 m from our V with two NPCs (linked: its region
# overlaps ours), walks up to us 25 s in, and off again at 55 s. Stand on a
# street with traffic and a crowd. OVERLAP=area runs it with null areas (set
# `overlap = area` in the game's tandem.ini first; `density` is the default).
ensure join
address=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
: "$address"
peer host --mode region --region-far 100 --region-walk-at 25 --region-leave-at 55 --region-speed 6 --region-npcs 2
expect 'in the session as player' 30
expect 'regions: linked with player 1' 20
cmd overlap
expect "overlap: ${OVERLAP:-density}, 1 cover\\(s\\)" 10
if [ "${OVERLAP:-density}" = area ]; then
  expect 'npcs: crowd null area [0-9]+ over player 1.s region' 15
else
  # 100 m apart: nine tenths of our crowd.
  expect 'npcs: own crowd at 0\.[5-9]' 15
fi
shot regions-overlap-near
# The host walks up: deep in its region our crowd is off, and what is left
# of our traffic there is held back (never announced).
expect 'npcs: guest, own crowd off \(inside player 1.s region\)' 40
pause 5
cmd overlap
expect 'overlap: street: area another game.s, [0-9]+ crowd vehicle\(s\) of our own held back' 10
note "$(since | grep -E 'overlap: ' | tail -2)"
cmd street count
shot regions-overlap-deep
# It walks off: our crowd is back.
expect 'npcs: own region, own crowd on' 60
if [ "${OVERLAP:-density}" = area ]; then
  expect 'npcs: crowd null area over player 1.s region removed' 60
else
  expect 'npcs: own crowd at (0\.9|1)' 60
fi
reject 'watchdog: game thread silent'
