# Hosting through a relay: the join code is said on screen as soon as the relay registers it, stays in the pause menu's Invite (the code alone, no address), and the mock player joins by it. Needs RELAY=HOST:PORT, a relay both reach (task relay:run on this machine)
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
[[ -n ${RELAY:-} ]] || fail "RELAY=HOST:PORT: a relay the game and the mock player reach (task relay:run)"
ini="$dir/red4ext/plugins/tandem/tandem.ini"
old=$(remote "sed -n 's/^relay *= *//p' '$ini'" | head -1)
cmd coop leave
expect 'coop: left the session' 10
cmd coop set relay "$RELAY"
expect "setting: relay = $RELAY" 10
mark
cmd coop host
expect 'session: starting as host on UDP 27450' 10
expect 'coop notice: Co-op join code: [0-9A-Z-]+ \[Session-Code\]' 30
code=$(since | sed -n 's/.*coop notice: Co-op join code: \([0-9A-Z-]*\) \[Session-Code\].*/\1/p' | tail -1)
[[ -n $code ]] || fail "no join code in the notice"
reject 'coop notice: Hosting co-op'
cmd coop status
expect "coop: invite: Join code $code: friends enter it in Co-op, Join a game" 10
peer join --code "$code" --relay "$RELAY" --mode front --body male
expect 'joined \(player [0-9]+' 30
expect 'coop notice: .*joined the session' 10
# Back as the harness had it.
cmd coop leave
expect 'coop: left the session' 10
cmd coop set relay "$old"
expect 'setting: relay = ' 10
cmd coop host
expect 'session: starting as host on UDP 27450' 10
