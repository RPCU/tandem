# Our V dives into another world as the host: the others keep its body where it was, we stop seeing them, and both come back
# shellcheck shell=bash disable=SC2317

# The Nth "elsewhere state" report matching PATTERN, within SECS.
expect_state() {
  local pattern=$1 count=$2 timeout=${3:-10} start=$SECONDS
  while ((SECONDS - start < timeout)); do
    if (($(since | grep -cE "elsewhere: state #[0-9]+: $pattern") >= count)); then
      echo "  ok: state ($count): $pattern"
      return 0
    fi
    sleep 1
  done
  fail "no state report $count matching '$pattern' within $timeout s"
}

ensure host
cmd elsewhere test off
cmd god on
# Home first, and settled there: a teleport from far away goes through a
# loading screen, its jump is seen when the game ticks again, and a world
# that begins within 4 s of a jump keeps the body where V was before it
# (core/elsewhere.hpp, kLateWorldUs). Asked until the jump is let go.
cmd teleport home
for _ in $(seq 1 30); do
  cmd elsewhere state
  sleep 2
  if since | grep -qE 'elsewhere: state #[0-9]+: our V in the city.*body held false'; then break; fi
done
expect_state 'our V in the city.*body held false' 1 5
pause 5
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
pause 4
# Jacked in where we stand: the body stays there, and we stop seeing the others.
cmd elsewhere test cyberspace
expect 'elsewhere: our V is in cyberspace \(a test\)' 5
expect 'elsewhere: our body stays at \(-91[0-9]' 10
expect 'elsewhere: away from our body, which stays out of sight' 5
expect_peer 'player 1 is in cyberspace, body left at \(-91[0-9]' 10
expect 'elsewhere: we are where the others are not' 5
expect "elsewhere: player [0-9]+'s puppet hidden: we are in cyberspace" 5
expect 'marker: player [0-9]+ off for now' 5
# Moving about in cyberspace (300 m up): the body stays at home.
cmd teleport -915 1879.5 336.3
pause 4
cmd elsewhere state
expect_state 'our V in cyberspace \(a test\), settled false, told place 1 flags 1, body held true' 1 20
# Out, and back home: back with our body, the others seen again.
cmd elsewhere test off
cmd teleport home
# Back down 300 m: the game may stop ticking for a few seconds while the
# city streams in again (4.4 s on 2026-11-19), the return lands after it.
expect 'elsewhere: back with our body' 20
expect_peer 'player 1 is back in the city' 20
expect 'elsewhere: we are back: the others shown again' 20
expect "elsewhere: player [0-9]+'s puppet shown" 20
expect 'marker: player [0-9]+ back' 20
# Johnny in the city: the others still see us where we are.
pause 5
cmd elsewhere test johnny
expect_peer 'player 1 is in Johnny \(in place\)' 5
# A flashback: the scene takes him away; the body stays at home, hidden.
cmd teleport -915 1879.5 336.3
expect_peer 'player 1 is in Johnny, body left at \(-91[0-9]' 20
cmd elsewhere test off
cmd teleport home
pause 4
cmd elsewhere state
# The second report in the city since the start (the first is the wait above).
expect_state '.*told place 0 flags 0, body held false, inside false' 2 30
cmd god off
# No reject of the watchdog line: 'teleport home' from far away loads for
# up to 30 s (the runner tolerates it); a real stall fails the expects.
