# The pause menu's co-op settings, as the host then as a guest, unattended, read back from the screen: the host's one short page (the name, the host's rules, its own screen; nothing technical), a rule changed there reaching the guest; the guest's page (no rule rows, only its own settings), its read-only "Host's rules" status line, and the host's friendly fire applied on the guest over its own tandem.ini (shots: settings-host, settings-changed, settings-guest)
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
mark
cmd coop menu settings
# The rows built, two by two, in order: the name, the host's rules, our own
# screen, Back; nothing technical.
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: off\]' 10
expect 'coop: pause menu page settings: .*\[Friendly fire: off\] \[Enemy scaling: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Enemy scaling: [a-z]*\] \[Level gap: [^]]*\]' 5
expect 'coop: pause menu page settings: .*\[Level gap: [^]]*\] \[Notices: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Notices: [a-z]*\] \[Markers: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Markers: [a-z]*\] \[Names: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Names: [a-z]*\] \[Party list: [a-z]*\] \[Back\]$' 5
reject 'coop: pause menu page settings: .*\[(Recorder|Relay|Sharing|Pings|Join code|Direct link|Story|Private scenes|Sandevistan|Race car)'
pause 1
# And the screen draws exactly those rows (coop menu shown reads the list's
# widgets back), and its status lines.
cmd coop menu shown
expect 'coop: pause menu shows page settings as built: 9 rows' 10
expect 'coop: pause menu status: Saved to tandem.ini as you change them' 5
reject 'coop: pause menu (row [0-9]+ draws|draws [0-9]+ rows)'
reject "coop: pause menu status: Host's rules"
shot settings-host
# A host's rule: the row says its new value, the guest plays by it at once.
mark
cmd coop pick friendly fire
expect 'coop: setting friendly_fire changed' 10
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Friendly fire: on\]' 10
expect_peer "the session's rules: friendly fire on, .*level gap [0-9]+" 15
pause 1
cmd coop menu shown
expect 'coop: pause menu shows page settings as built: 9 rows' 10
reject 'coop: pause menu (row [0-9]+ draws|draws [0-9]+ rows)'
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
# Our own tandem.ini says friendly fire off: the host's on must win.
ensure join friendly_fire=off
mark
# The mock host's rules: friendly fire on, enemy scaling on (its level gap:
# off, the plain mean); its "grenade" on our V every 20 s, the first as we
# join.
peer host --mode front --body male --friendly-fire on --enemy-scaling on --area-hit 20
expect 'in the session as player' 30
expect "outcomes: the host's rules: friendly fire on, enemy scaling on" 20
# The host's rule applies here: its grenade hurts our V (off, ours would
# refuse it). The first one, then our V's health back (regeneration), then
# the next one's dip below it.
expect "outcomes: player [0-9]+'s area effect on our V: 20(\.0+)? physical" 30
pause 12
query before health
mark
expect "outcomes: player [0-9]+'s area effect on our V: 20(\.0+)? physical" 30
pause 1
query low health low
expect_value "$low" '<' "$before"
$game key 1 >/dev/null
pause 2
mark
cmd coop menu settings
expect 'coop: pause menu page settings: \[Name: [^]]*\] \[Notices: [a-z]*\]' 10
expect 'coop: pause menu page settings: .*\[Notices: [a-z]*\] \[Markers: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Markers: [a-z]*\] \[Names: [a-z]*\]' 5
expect 'coop: pause menu page settings: .*\[Names: [a-z]*\] \[Party list: [a-z]*\] \[Back\]$' 5
reject 'coop: pause menu page settings: .*\[(Friendly fire|Enemy scaling|Level gap)'
pause 1
cmd coop menu shown
expect 'coop: pause menu shows page settings as built: 6 rows' 10
reject 'coop: pause menu (row [0-9]+ draws|draws [0-9]+ rows)'
# The host's rules, one read-only line on the screen, as the host said them.
expect "coop: pause menu status: .*friendly fire on, enemy scaling on, level gap off" 5
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
