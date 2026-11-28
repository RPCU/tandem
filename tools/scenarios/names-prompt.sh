# The pseudo: the first co-op use with the default name (V) asks for one in the game's text popup; a bad one is asked again, a good one saved to tandem.ini
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
ini="$dir/red4ext/plugins/tandem/tandem.ini"
# Out of the session, the name still the harness's V, the prompt armed again.
cmd coop leave
expect 'coop: left the session' 10
cmd coop name reset
expect 'coop: name "V", the co-op menu asks for one: yes' 10
# The pause menu's Co-op page: Host this game asks for a name first.
$game key 1 >/dev/null
pause 2
cmd coop menu coop
expect 'coop: pause menu page coop' 10
cmd coop pick host this game
expect 'coop: pause menu: asking for a name \(still the default\)' 10
shot names-prompt
# Refused (a comment sign tandem.ini cannot hold): asked again, saying why.
cmd "coop name answer bad;name"
expect 'coop: name refused \(; or #\)' 10
shot names-prompt-again
# Taken, trimmed and saved; then the host it was picked for.
cmd "coop name answer   Alice  "
expect 'setting: name = Alice \(saved to tandem.ini\)' 10
expect 'session: starting as host' 15
remote "grep -q '^name = Alice\$' '$ini'" || fail "name = Alice not in $ini"
cmd coop name
expect 'coop: name "Alice", the co-op menu asks for one: no' 10
# Back as the harness had it (the name waits for the session's end: leave,
# set, host again).
$game key 1 >/dev/null
cmd coop leave
expect 'coop: left the session' 10
cmd coop set name Victor
expect 'setting: name = Victor' 10
cmd coop host
expect 'session: starting as host' 15
# To look at: the popup in the game's style (title, the rules, the text
# field); that a triggered answer closes it (if it stays drawn, the answer
# command must close it the way Codeware's Close does).
# No reject of the watchdog: the pause menu stays open about 10 s, across
# the host's start, and a paused game is silent by its definition (a hang
# fails the next command).
