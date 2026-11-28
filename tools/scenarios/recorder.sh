# The recorder, solo: segments and their index written, the cost logged, no co-op side effect, no socket
# shellcheck shell=bash disable=SC2317,SC2154
ensure off recorder=on recorder_segment_minutes=1 campaign=closed
log_file=$(remote "cd '$dir'; ls -t red4ext/logs/tandem-*.log | head -1")
remote "cd '$dir'; grep -q 'tandem: solo, passive .*recorder on' '$log_file'" || fail "no 'solo, passive ... recorder on' startup line"
expect 'recorder: recording solo play' 30
cmd where
expect 'where: '
cmd teleport home
expect 'teleport: '
pause 15
cmd recorder cut
expect 'recorder: segment .* closed, .*cost \{"script_ticks":[1-9]' 15
pause 5
cmd recorder status
expect 'recorder: recording [0-9]' 10
auto="$dir/red4ext/plugins/tandem/recordings/auto"
segments=$(remote "ls '$auto' | grep -c '\.tsession$'" || true)
[ "${segments:-0}" -ge 1 ] || fail "no segment in $auto"
remote "test -s '$auto/index.jsonl'" || fail "no index.jsonl in $auto"
note "$segments segment(s); index: $(remote "tail -1 '$auto/index.jsonl'" | cut -c1-220)"
reject 'session: starting|hosting on UDP|joining |in the session as|own crowd off|own quests frozen|pause: world paused|story: hosting|revive: |puppet for player|recorder: off for this run'
if remote "ss -Hlun 'sport = :27450' | grep -q ."; then fail "something listens on UDP 27450"; fi
note "nothing listens on UDP 27450"
reject '\[error|watchdog: game thread silent'
