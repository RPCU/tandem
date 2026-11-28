# Two games: a pane the partner breaks breaks on our screen too, and the session's record of what broke is the same on both
# shellcheck shell=bash disable=SC2317
# needs two games
# Where: GLASS_AT (a fixture or "X Y Z [YAW]": V looking at a window or a
# glass pane, a shop front, a bus shelter; default kabuki-market).
# To look at: the pane the partner's V looks at shatters on both screens (the
# pieces fall each game's own way), nobody is hurt.
ensure host
at "${GLASS_AT:-kabuki-market}"
partner_join
# The partner where our V stands, looking where it looks.
pat "$(spot "${GLASS_AT:-kabuki-market}")"
mark
pmark
cmd glass
expect 'objects: [0-9]+ break\(s\) told, [0-9]+ replayed' 5
# The partner breaks what it looks at (an impulse there, as a blast would):
# its game sees the break and tells it. A pane an earlier run broke stays
# broken while the street is loaded (2026-11-28): it turns until something
# it looks at breaks, eight headings at most.
read -r gx gy gz _ <<<"$(spot "${GLASS_AT:-kabuki-market}")"
broke=''
for yaw in 0 45 90 135 180 225 270 315; do
  pat "$gx $gy $gz $yaw"
  pmark
  pcmd glass break
  expect_partner 'glass: an impulse of [0-9.]+ m at \(' 5
  sleep 6
  if psince | grep -qE 'glass: (part [A-Za-z0-9_]+ of [0-9a-f]+|a piece of the world) broke here at \(.*\): told'; then
    broke=$yaw
    break
  fi
done
[ -n "$broke" ] || fail "nothing the partner looked at broke (eight headings)"
note "the partner broke something looking at $broke degrees"
expect 'glass: player [0-9]+ broke (part|something)' 10
pause 2
shot pair-glass
cmd glass
expect 'objects: [0-9]+ break\(s\) told, [1-9][0-9]* replayed' 5
compare glass
reject 'watchdog: game thread silent'
