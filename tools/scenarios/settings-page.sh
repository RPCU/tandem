# The pause menu's co-op settings as the host: one short page (the name, the host's rules, the player's own screen), nothing technical; a host's rule changed there reaches the guest at once, the cursor stays on its row; Esc goes up to the Co-op page (the shots: rows on one line inside the panel, the description apart from the status)
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
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: off\]' 10
expect 'coop: pause menu page settings: .*\[Friendly fire: off\] \[Enemy scaling: [a-z]*\]' 10
expect 'coop: pause menu page settings: .*\[Enemy scaling: [a-z]*\] \[Level gap: [^]]*\]' 10
expect 'coop: pause menu page settings: .*\[Level gap: [^]]*\] \[Notices: [a-z]*\]' 10
expect 'coop: pause menu page settings: .*\[Names: [a-z]*\] \[Party list: [a-z]*\] \[Back\]' 10
pause 1
shot settings-host
# A host's rule: the row says its new value, the guest plays by it at once.
mark
cmd coop pick friendly fire
expect 'coop: setting friendly_fire changed' 10
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: on\]' 10
expect_peer "the session's rules: friendly fire on, .*level gap [0-9]+" 15
pause 1
shot settings-changed
mark
cmd coop pick friendly fire
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: off\]' 10
expect_peer "the session's rules: friendly fire off" 15
# The technical knobs are tandem.ini's alone.
cmd coop pick recorder
expect 'coop: pause menu: no item recorder' 10
# Esc (the simulated key 1) goes up a page, as the game's own sub-pages do.
mark
$game key 1 >/dev/null
expect 'coop: pause menu page coop' 10
cmd coop menu root
expect 'coop: pause menu page root' 10
$game key 1 >/dev/null
pause 2
reject 'watchdog: game thread silent'
