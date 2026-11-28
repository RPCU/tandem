# Two games: street clutter (bins, cones, bags: mostly world nodes, not shared, props.md) photographed on both games from one spot after a car copy drives through it, a V walks through it and a blast beside it, for a count of what lies differently
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# by hand: a measurement (props.md, "Which ones"); its result is the count,
# by eye, of the items that lie clearly differently in each pair of pictures.
# Where: CLUTTER_AT (a fixture or "X Y Z YAW": a street with clutter ahead
# of V, where the partner walks and drives; default kabuki-market).
# Each step: the partner acts, both Vs then stand at the spot looking the
# same way, a picture of each game (clutter-STEP-ours.png, -partner.png).
ensure host
spot_at="${CLUTTER_AT:-kabuki-market}"
at "$spot_at"
partner_join
cmd god on
pcmd god on
pshot() {
  pgame shot "$1" >/dev/null && cp "$(dirname "$cache")/$partner/shots/$1.png" "$out/" 2>/dev/null || true
}
look() {
  local where=$2 x y z yaw
  read -r x y z yaw <<<"$where"
  # Ours at the spot, the partner a step to its side (both on one spot: our
  # teleport did not take, 2026-11-28), looking the same way.
  at "$where"
  pat "$(awk -v x="$x" 'BEGIN { print x + 1.2 }') $y $z ${yaw:-0}"
  pause 4
  shot "clutter-$1-ours"
  pshot "clutter-$1-partner"
  note "pictures: clutter-$1-ours.png, clutter-$1-partner.png"
}
here=$(spot "$spot_at")
look before "$here"
# 1. A car copy through it: the partner drives its car through the street.
pgame pilot run clutter-drive >/dev/null 2>&1 || note "the partner's drive did not pass (its pilot log)"
pause 6
look car "$here"
# 2. A V on foot through it: the partner walks there and back.
pgame pilot run clutter-walk >/dev/null 2>&1 || note "the partner's walk did not pass (its pilot log)"
pause 4
look walk "$here"
# 3. A blast beside clutter: the nearest barrel, exploded by our V.
mark
cmd device goto barrel
if await 'devices: at barrel [0-9a-f]+ [A-Za-z]+: ' 10 >/dev/null; then
  read -r bx by bz < <(since | sed -nE 's/.*devices: at barrel [0-9a-f]+ [A-Za-z]+: \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3/p' | tail -1)
  near="$bx $by $bz 0"
  look blast-before "$near"
  cmd device explode barrel
  pause 8
  look blast "$near"
else
  note "no barrel within 200 m: no blast"
fi
reject 'watchdog: game thread silent'
reject_partner 'watchdog: game thread silent'
