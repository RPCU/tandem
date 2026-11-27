# The pause menu's co-op settings: a page of groups, each a page of one-line rows; a change keeps the cursor on its row, Back and Esc go to the groups (the shots: rows on one line inside the panel, the description apart from the status)
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female
expect 'joined \(player [0-9]+' 20
# The harness's friendly fire (off), whatever an earlier scenario left.
cmd coop set friendly_fire off
expect 'setting: friendly_fire = off' 10
$game key 1 >/dev/null
pause 2
cmd coop menu settings
expect 'coop: pause menu page settings: \[Session\] \[Gameplay\]' 10
pause 1
shot settings-groups
cmd coop pick session
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: [a-z]*\]' 10
pause 1
shot settings-session
# A change: the row says its new value, on the same page.
mark
cmd coop pick friendly fire
expect 'coop: setting friendly_fire changed' 10
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: on\]' 10
pause 1
shot settings-changed
mark
cmd coop pick friendly fire
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: off\]' 10
mark
cmd coop pick back
expect 'coop: pause menu page settings: \[Session\]' 10
cmd coop pick advanced
expect 'coop: pause menu page settings: \[Recorder: [a-z]*\]' 10
pause 1
shot settings-advanced
cmd coop pick interface
expect 'coop: pause menu: no item interface' 10
# Esc (the simulated key 1) goes up a page, as the game's own sub-pages do.
mark
$game key 1 >/dev/null
expect 'coop: pause menu page settings: \[Session\]' 10
cmd coop pick interface
expect 'coop: pause menu page settings: \[Notices: [a-z]*\]' 10
pause 1
shot settings-interface
cmd coop menu root
expect 'coop: pause menu page root' 10
$game key 1 >/dev/null
pause 2
reject 'watchdog: game thread silent'
