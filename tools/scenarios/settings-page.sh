# The pause menu's co-op settings, as the host then as a guest, unattended: the host's one short page (the name, the host's rules, its own screen; nothing technical), a rule changed there reaching the guest at once; the guest's page (no rule rows, only its own settings) and its read-only "Host's rules" status line; Esc goes up to the Co-op page (shots: settings-host, settings-changed, settings-guest)
# shellcheck shell=bash disable=SC2317,SC2154
# ---- as the host, a mock guest beside us ----
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

# ---- as a guest of a mock host ----
pkill -x tandem-peer || true
ensure join
mark
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
