# Our own summoned car is said from its arrival: driving in on its own (our stream is it), then parked where it stopped; the mock player hears it where our game has it
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode idle --body female
expect 'joined \(player [0-9]+' 30
cmd garage summon
expect 'garage: summon asked \(true\)' 10
expect 'summons: our Vehicle\.[A-Za-z0-9_]+ is out, (driving in|parked)' 40
expect_peer "player [0-9]+'s summoned car 0x[0-9a-f]+: (driving in|parked)" 10
shot summon-host-arriving
expect 'summons: our Vehicle\.[A-Za-z0-9_]+ is out, parked' 60
expect_peer "player [0-9]+'s summoned car 0x[0-9a-f]+: parked" 10
pause 2
# Where our game has it, and where the mock player heard it stands.
query ours summons ours
expect_value "$ours" != none
heard=$(grep -E "player [0-9]+'s summoned car 0x[0-9a-f]+: parked at" "$peer_log" | tail -1 |
  sed -E 's/.*at \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\).*/\1 \2 \3/')
[ -n "$heard" ] || fail "the mock player heard no parked place"
gap=$(awk -v a="$ours" -v b="$heard" 'BEGIN { split(a, p, " "); split(b, q, " ");
  printf "%.2f\n", sqrt((p[1] - q[1]) ^ 2 + (p[2] - q[2]) ^ 2) }')
note "our car $gap m from where the mock player heard it stands"
expect_value "$gap" '<' 1.0
cmd garage cars
expect 'garage cars: ours Vehicle\.[A-Za-z0-9_]+ .* said parked' 10
shot summon-host-parked
reject 'summons: our car not said'
reject 'watchdog: game thread silent'
