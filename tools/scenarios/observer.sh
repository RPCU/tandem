# The observer (observer = on) as host with the mock player: the story trail with the session's start, a capture asked for with its log and context, the summary, its cost; no frame judged at the session's start; files only in observer/
# shellcheck shell=bash disable=SC2317,SC2154
ensure host observer=on
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
cmd teleport home
expect 'teleport: ' 10
pause 10
cmd observer capture scenario
expect 'observer: capture asked for' 5
expect 'observer: capture [0-9]+ \(manual\) written: captures/' 20
cmd observer summary
expect 'observer: summary written \(asked for\)' 5
cmd observer
expect 'observer: on, [0-9]+ trail row' 5
obs="$dir/red4ext/plugins/tandem/observer"
trail=$(remote "ls -t '$obs'/session-*.tsv | head -1")
[ -n "$trail" ] || fail "no session-*.tsv in $obs"
remote "grep -q 'start as host' '$trail'" || fail "no session start in $trail"
remote "grep -q 'manual (50) captures/' '$trail'" || fail "no capture row in $trail"
# The session's first tick (in the attach frame, 0.1 s after the start)
# judges no frame: the load's and the attach's are not ours (observer.md).
# The frames after it are timed as any (115 ms after a load, 2026-11-27).
early=$(remote "awk -F'\t' '\$3 == \"session\" { s = \$2 } \$3 == \"trigger\" && \$4 ~ /^frame / && s != \"\" && \$2 - s < 0.15 { print \$4 }' '$trail'" | head -1)
[ -z "$early" ] || fail "a frame judged at the session's start: $early"
note "trail $trail: $(remote "tail -n +2 '$trail' | cut -f3 | sort | uniq -c | tr -s ' \n' ' '")"
capture=$(remote "ls -t '$obs'/captures/*-manual.txt | head -1")
remote "grep -q '^>>> ' '$capture' && grep -q '^story now: ' '$capture'" || fail "capture $capture has no marked trigger or no context"
note "capture $capture: $(remote "grep -c '^\[' '$capture'") log lines"
remote "ls '$obs'/summary-*.txt >/dev/null" || fail "no summary in $obs"
reject 'watchdog: game thread silent|observer: off'
