# Every named spot of tools/scenarios/fixtures.txt visited in turn: what stands there (fixture check), a screenshot each, for the table in testing.md "Fixtures"
# shellcheck shell=bash disable=SC2317
# Which: FIXTURES ("NAME NAME…", default every one); RADIUS (m, default 60).
# Some spots are hostile (h11-basement, arasaka-park): god mode on for the
# visit (the next test reset ends it).
# To look at: each shot against the table's "what's there"; the glass
# fixture's pane (kabuki-market) after `glass break`.
ensure host
cmd god on
expect 'god on: invulnerable' 5
for name in ${FIXTURES:-$(fixtures)}; do
  at "$name"
  mark
  cmd fixture check "${RADIUS:-60}"
  expect 'fixture: done' 15
  note "$name:"
  note "$(since | grep -aE '\] \[script\] fixture: ' | sed 's/.*\] \[script\] fixture: /    /')"
  shot "fixture-$name"
  if [ "$name" = kabuki-market ]; then
    cmd glass break
    expect 'glass: an impulse of [0-9.]+ m at \(' 5
    shot "fixture-$name-glass"
  fi
done
cmd god off
reject 'watchdog: game thread silent'
