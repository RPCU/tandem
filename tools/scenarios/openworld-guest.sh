# As a guest: the host's NCPD activities on our map while grouped (done there, open there), one it finishes with us credited once, our own back apart and at the leave
# shellcheck shell=bash disable=SC2317
# Our nearest NCPD pin not done is the one the mock host says done; our
# nearest done one (if our save has one) is open in the host's world.
# Grouped, each shows the host's state (the engine's own phase, read back);
# apart, and when the session ends, our own again. The host finishing one
# more with us near it is a ledger entry, once. Our own activities wait while
# grouped and run apart (the freeze's open-world activity class).
# Unattended: every check is read from the log; the verdict is the last line.
ensure join
mark
cmd story activity "open"
expect 'openworld: activity [0-9]+ phase [1-3] at .* \(open\)' 10
open=$(since | grep -oE 'openworld: activity [0-9]+ phase [1-3] at .* \(open\)' | tail -1)
marker=$(awk '{ print $3 }' <<<"$open")
own=$(awk '{ print $5 }' <<<"$open")
cmd story activity "done"
expect 'openworld: (activity [0-9]+ phase 0 at .*|no done NCPD activity pin on our map)' 10
ours=$(since | grep -oE 'openworld: activity [0-9]+ phase 0 at .* \(done\)' | tail -1 | awk '{ print $3 }')
note "open here: $marker (phase $own); done here: ${ours:-none}"
# The one the host finishes with us later: a marker no map has (the
# ledger's path only).
later=2050821214
peer host --mode front --body male --activities "$(printf '%x' "$marker")" \
  --activities-then "$(printf '%x,%x' "$marker" "$later")" --activities-then-at 25
expect 'in the session as player' 30
expect 'openworld: the host.s world has 1 NCPD activity done' 15
expect 'openworld: our NCPD activities wait: the host.s world leads here' 10
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set to the host.s' 10
mark
cmd story activity "$marker"
expect "openworld: activity $marker phase 0 at " 10
if [ -n "$ours" ]; then
  cmd story activity "$ours"
  expect "openworld: activity $ours phase 2 at " 10
fi
# Finished with us near the host: credited once.
mark
expect 'openworld: the host.s world has 2 NCPD activities done' 30
expect "ledger: activity $later done with " 10
pause 4
[ "$(since | grep -c "ledger: activity $later done with ")" = 1 ] || fail "activity $later credited more than once"
# Apart: our own pins back, our activities run.
mark
cmd quest apart
expect 'openworld: our NCPD activities run: ours lead here' 10
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set back to our own \(apart\)' 10
cmd story activity "$marker"
expect "openworld: activity $marker phase $own at " 10
cmd quest grouped
expect 'openworld: our NCPD activities wait: the host.s world leads here' 10
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set to the host.s' 10
# The host goes: our own again before saving unlocks.
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) back to our own \(leave\)' 20
expect 'save: our story is back as it was' 20
# (Put back after the journal, and looked at again for 6 s: a pin the
# journal's put-back moved is set back once more, said.)
pause 2
cmd story activity "$marker"
expect "openworld: activity $marker phase $own at " 10
if [ -n "$ours" ]; then
  cmd story activity "$ours"
  expect "openworld: activity $ours phase 0 at " 10
fi
reject 'openworld: setting a map pin.s phase faulted'
reject 'watchdog: game thread silent'
