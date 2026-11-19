# What a release adds: the startup check passes on the real game, a guest's install is compared at the join, the bug report is written
# shellcheck shell=bash disable=SC2317,SC2154 # (dir, mark_file: tools/scenario.sh sets them)
ensure host
# The startup check read the game and the mods it needs, and found no problem.
grep_log() { remote "cd '$dir'; grep -E -m1 '$1' '$mark_file'" >/dev/null || fail "not in the log: $1"; }
grep_log 'game 3\.0\.88\.46235 \(built for patch 2\.50'
grep_log 'Codeware: [0-9]+\.[0-9]+'
grep_log 'TweakXL: [0-9]+\.[0-9]+'
grep_log 'tandem scripts [0-9a-f]{16}, tweaks [0-9a-f]{16}; [0-9]+ other mod'
if remote "cd '$dir'; grep -q 'startup check: ' '$mark_file'"; then
  remote "cd '$dir'; grep 'startup check: ' '$mark_file'" >&2
  fail "the startup check found something on the test game"
fi
cmd coop status
expect 'coop: Hosting on port 27450' 10
reject 'coop: off:'
# The mock player says its build and nothing else: compared, nothing differs.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
expect '\(player [0-9]+\) runs tandem [0-9]+\.[0-9]+\.[0-9]+' 10
reject 'notice: .*(runs tandem|differ from yours|other mods than you)'
pause 3
# The bug report, from the running game.
cmd coop report
expect 'report: written to red4ext/plugins/tandem/reports/tandem-report-[0-9-]+\.zip \(versions\.txt, tandem\.log' 10
expect 'coop: report: red4ext/plugins/tandem/reports/tandem-report-' 5
report=$(remote "cd '$dir/red4ext/plugins/tandem/reports' && ls -t tandem-report-*.zip | head -1")
remote "cd '$dir/red4ext/plugins/tandem/reports' && unzip -tq '$report'" >/dev/null || fail "the report is not a zip unzip reads"
# Nothing of the machine's own in it: no token, no user folder.
if remote "cd '$dir/red4ext/plugins/tandem/reports' && unzip -p '$report' | grep -a -E -q 'relay_token *= *[0-9a-f]{16}|/home/[a-z]+/|\\\\Users\\\\[A-Za-z]'"; then
  fail "the report still holds a token or a user's folder"
fi
remote "rm -f '$dir/red4ext/plugins/tandem/reports/$report'"
reject 'watchdog: game thread silent'
