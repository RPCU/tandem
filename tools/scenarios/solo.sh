# Solo (no role, recorder off): tandem changes nothing in the game: no session, no socket, no co-op line
# shellcheck shell=bash disable=SC2317,SC2154
ensure off recorder=off
log_file=$(remote "cd '$dir'; ls -t red4ext/logs/tandem-*.log | head -1")
remote "cd '$dir'; grep -q 'tandem: solo, passive' '$log_file'" || fail "no 'tandem: solo, passive' startup line"
note "$(remote "cd '$dir'; grep -m1 'tandem: solo, passive' '$log_file'" | cut -c1-200)"
cmd where
expect 'where: '
cmd teleport home
expect 'teleport: '
pause 20
cmd coop status
expect 'coop: Not in a session'
# What any co-op system logs when it acts.
reject 'session: starting|hosting on UDP|joining |in the session as|own crowd off|own quests frozen|pause: world paused|story: hosting|revive: |npcs: |recorder: segment|puppet for player'
if remote "ss -Hlun 'sport = :27450' | grep -q ."; then fail "something listens on UDP 27450"; fi
note "nothing listens on UDP 27450"
reject '\[error|watchdog: game thread silent'
