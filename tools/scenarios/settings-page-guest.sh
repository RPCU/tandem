# The pause menu's co-op settings as a guest: the host's rules in one read-only status line (friendly fire, enemy scaling, level gap, as the host said them), no row for them, only the player's own settings; a guest's own friendly_fire is refused in the session (the shots: the page and its status line)
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
# The mock host says its rules: friendly fire on, enemy scaling on (the mock
# host's level gap: off, the plain mean).
peer host --mode front --body male --friendly-fire on --enemy-scaling on
expect 'in the session as player' 30
cmd coop status
expect "coop: Host's rules: friendly fire on" 15
expect "coop: Host's rules: .*enemy scaling on" 5
expect "coop: Host's rules: .*level gap off" 5
$game key 1 >/dev/null
pause 2
cmd coop menu settings
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Notices: [a-z]*\]' 10
expect 'coop: pause menu page settings: .*\[Notices: [a-z]*\] \[Markers: [a-z]*\]' 10
expect 'coop: pause menu page settings: .*\[Names: [a-z]*\] \[Party list: [a-z]*\] \[Back\]' 10
pause 1
shot settings-guest
# The host's rule is not the guest's to change.
cmd coop pick friendly fire
expect 'coop: pause menu: no item friendly fire' 10
cmd coop set friendly_fire on
expect "coop: setting friendly_fire refused: the host's rule while we are its guest" 10
cmd coop settings
expect "coop: setting friendly_fire = on \(the host's\)" 10
# A personal one is the guest's own.
mark
cmd coop pick markers
expect 'coop: setting markers changed' 10
expect 'coop: pause menu page settings: .*\[Notices: [a-z]*\] \[Markers: [a-z]*\]' 10
# Back as it was.
mark
cmd coop pick markers
expect 'coop: setting markers changed' 10
mark
$game key 1 >/dev/null
expect 'coop: pause menu page coop' 10
cmd coop menu root
expect 'coop: pause menu page root' 10
$game key 1 >/dev/null
pause 2
reject 'watchdog: game thread silent'
