# Join by address (LAN, VPN): refusals in words (IPv6, a bad address), the mock player joined directly on the default port, the address kept as last_address and joined again with an empty answer
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
ini="$dir/red4ext/plugins/tandem/tandem.ini"
last=$(remote "sed -n 's/^last_address *= *//p' '$ini'" | head -1)
game_ip=$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')
here=$(ip -4 -o route get "$game_ip" | awk '{ for (i = 1; i < NF; i++) if ($i == "src") print $(i + 1) }')
cmd coop address '[2001:db8::1]:27450'
expect 'coop: join by address: "\[2001:db8::1\]:27450" refused \(ipv6\)' 10
cmd coop address 192.0.2.300
expect 'coop: join by address: "192.0.2.300" refused \(name\)' 10
cmd coop address host.example:0
expect 'refused \(port\)' 10
cmd coop status
expect 'coop: Hosting on port 27450' 10
# The game as the mock player's guest, by its address alone (port 27450).
peer host --mode front --body male
cmd coop address "$here"
expect "coop: join by address: \"$here\" -> $here:27450" 10
expect "setting: last_address = $here:27450" 10
expect 'session: starting as join of' 10
expect 'in the session as player [0-9]+' 20
remote "grep -q '^last_address = $here:$game_port\$' '$ini'" || fail "last_address not saved in $ini"
cmd coop leave
expect 'coop: left the session' 10
# An empty answer: the last address.
cmd coop address
expect "coop: join by address: \"\" -> $here:27450" 10
expect 'in the session as player [0-9]+' 20
cmd coop leave
expect 'coop: left the session' 10
# Back as the harness had it: hosting, last_address as it was.
cmd coop set last_address "$last"
expect 'setting: last_address = ' 10
cmd coop host
expect 'session: starting as host on UDP 27450' 10
reject '\[error|watchdog: game thread silent'
