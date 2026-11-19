# The settings page's keys: changed at once and saved to tandem.ini, a session's own waits for its end, the session's keys and bad values refused
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
ini="$dir/red4ext/plugins/tandem/tandem.ini"
name=$(remote "sed -n 's/^name *= *//p' '$ini'" | head -1)
cmd coop settings
expect 'coop: setting notices = ' 10
cmd coop set notices important
expect 'setting: notices = important \(saved to tandem.ini\)' 10
remote "grep -q '^notices = important\$' '$ini'" || fail "notices = important not in $ini"
cmd coop set friendly_fire off
expect 'setting: friendly_fire = off \(saved' 10
# The name a running session was set up with: from the next session.
cmd coop set name tandem-settings
expect 'setting: name = tandem-settings, from the next session' 10
cmd coop set role join
expect 'setting: role is not one the settings page changes' 10
cmd coop set notices loud
expect 'setting: notices is all, important or minimal' 10
remote "grep -q '^role = host' '$ini'" || fail "the session's role changed in $ini"
# Back as the harness had them, so the next scenario's tandem.ini is the same.
cmd coop set notices all
expect 'setting: notices = all' 10
cmd coop set friendly_fire on
expect 'setting: friendly_fire = on' 10
cmd coop set name "${name:-V}"
expect "setting: name = ${name:-V}" 10
reject '\[error|watchdog: game thread silent'
