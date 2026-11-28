# One-click rejoin by host ID, the Join list's online badge: the mock player hosts on the relay with a host key; the game joins it once by its code and keeps its ID; the next time the Join list shows it online and joins it with no code; invite only, it shows online but not open; gone, offline. Needs RELAY=HOST:PORT (task relay:run), reached by the game and the mock player
# shellcheck shell=bash disable=SC2317,SC2154
ensure off campaign=closed
[[ -n ${RELAY:-} ]] || fail "RELAY=HOST:PORT: a relay the game and the mock player reach (task relay:run)"
ini="$dir/red4ext/plugins/tandem/tandem.ini"
old=$(remote "sed -n 's/^relay *= *//p' '$ini'" | head -1)
cmd coop set relay "$RELAY"
expect 'setting: relay = .* \(saved to tandem.ini\)' 10
# A test key: random, never kept.
key=$(od -An -tx1 -N16 /dev/urandom | tr -d ' \n')
peer host --relay "$RELAY" --host-key "$key" --mode idle --body male
expect_peer 'host ID [0-9a-f]{32} \(open to a join by it\)' 15
expect_peer 'join code [0-9A-Z-]+ \(relay' 30
code=$(sed -n 's/.*join code \([0-9A-Z-]*\) (relay.*/\1/p' "$peer_log" | tail -1)
# The first join: by its code. The welcome tells the game the host's ID.
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
cmd coop join "$code"
expect 'coop: main menu page save:' 15
cmd coop pick continue
expect 'session: starting as join of code' 120
expect 'in the session as player [0-9]+' 60
expect "the host's ID heard: [0-9a-f]{8}" 30
expect 'campaign: our V noted for id:[0-9a-f]{32}' 10
cmd coop leave
expect 'coop: left the session' 10
# The next time: the Join list asks the relay, and the host is joinable.
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
cmd coop menu join
expect 'presence: [0-9]+ recent host\(s\) asked of the relay' 15
expect 'coop: main menu: the saves. list came: page join built again|coop: main menu page join:.*tandem-peer \(online: join\)' 15
cmd coop menu shown
expect 'coop: main menu shows page join as built' 10
shot coop-host-id-online
mark
cmd coop pick tandem-peer
expect 'coop: main menu: tandem-peer is joinable: by its host ID' 10
expect 'coop: main menu page save:.*Your V from last time' 15
cmd coop pick your v from last time
expect 'join by host ID through relay' 10
expect 'session: starting as join' 120
expect 'in the session as player [0-9]+' 60
reject 'session: starting as join of code'
expect_peer 'in the session|joined \(player' 30
cmd coop leave
expect 'coop: left the session' 10
# Invite only: online, but not open to a join by its ID.
pkill -x tandem-peer || true
peer host --relay "$RELAY" --host-key "$key" --host-invite --mode idle --body male
expect_peer 'host ID [0-9a-f]{32} \(invite only\)' 15
pause 3
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
mark
cmd coop menu join
expect 'coop: main menu page join:.*tandem-peer \(online, not open to you\)' 20
# Gone: offline, once the relay forgets its binding (a minute).
pkill -x tandem-peer || true
pause 70
cmd coop menu coop
pause 2
mark
cmd coop menu join
expect 'coop: main menu page join:.*tandem-peer \(offline\)' 20
shot coop-host-id-offline
cmd coop set relay "$old"
# The co-op menu's choice lasts for the game's run: a fresh one next.
$game stop >/dev/null
