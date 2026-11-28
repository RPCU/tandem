# As a guest: an NCPD pin we track, shown done while grouped with the host (the engine untracks it), tracked again apart and at the leave
# shellcheck shell=bash disable=SC2317
# Our nearest NCPD pin not done is tracked the world map's way, then the
# mock host says it done: grouped, it shows done and is no longer tracked
# (the engine's phase setter untracks a completed pin, §13.162). Apart, it
# is put back to our own phase and tracked again (the system's own
# tracker, §13.165), read back; grouped again and then the session ends:
# tracked again at the leave too. The track is ours to clear: off at the end.
# Unattended: every check is read from the log; the verdict is the last line.
ensure join
mark
cmd story activity "open"
expect 'openworld: activity [0-9]+ phase [1-3] at .* \(open\)' 10
open=$(since | grep -oE 'openworld: activity [0-9]+ phase [1-3] at .* \(open\)' | tail -1)
marker=$(awk '{ print $3 }' <<<"$open")
own=$(awk '{ print $5 }' <<<"$open")
[ -n "$marker" ] || fail "no NCPD pin open on our map"
note "tracked here: $marker (phase $own)"
cmd story activity "track $marker"
expect "openworld: activity $marker tracked by the test: yes" 10
cmd story activity "$marker"
expect "openworld: activity $marker phase $own at .*, tracked yes" 10
peer host --mode front --body male --activities "$(printf '%x' "$marker")"
expect 'in the session as player' 30
expect 'openworld: our NCPD activities wait: the host.s world leads here' 15
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set to the host.s' 15
mark
cmd story activity "$marker"
expect "openworld: activity $marker phase 0 at .*, tracked no" 10
# Apart: our own phase, and our track, back.
mark
cmd quest apart
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set back to our own \(apart\)' 10
expect "openworld: activity $marker tracked again" 5
cmd story activity "$marker"
expect "openworld: activity $marker phase $own at .*, tracked yes" 10
# Grouped again (untracked again), then the host goes: back at the leave.
mark
cmd quest grouped
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) set to the host.s' 10
cmd story activity "$marker"
expect "openworld: activity $marker phase 0 at .*, tracked no" 10
mark
pkill -x tandem-peer
expect 'session ended' 60
expect 'openworld: [1-9][0-9]* of [1-9][0-9]* pin\(s\) back to our own \(leave\)' 20
expect "openworld: activity $marker tracked again" 5
cmd story activity "$marker"
expect "openworld: activity $marker phase $own at .*, tracked yes" 10
cmd story activity "untrack"
expect 'openworld: the player.s track is off' 10
reject 'openworld: activity [0-9]+ not tracked again'
reject 'openworld: activity [0-9]+ left untracked'
reject 'openworld: setting a map pin.s phase faulted'
reject 'openworld: tracking a map pin again faulted'
reject 'watchdog: game thread silent'
