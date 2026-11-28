# A join that cannot work says why in words and leaves the player in their own world: an address where no game hosts is retried, then given up after a minute ("no answer"); a join code with no relay set is refused in the menu before any load; with RELAY=HOST:PORT a code nobody has is refused at once
# shellcheck shell=bash disable=SC2317,SC2154
ensure off campaign=closed
ini="$dir/red4ext/plugins/tandem/tandem.ini"
old=$(remote "sed -n 's/^relay *= *//p' '$ini'" | head -1)
cmd coop set relay
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
# Nobody hosts at this documentation address.
cmd coop join 192.0.2.1:27450
expect 'coop: main menu page save:' 15
cmd coop pick continue
expect 'session: the join arrives beside the host' 10
expect 'session: starting as join of 192\.0\.2\.1' 120
expect 'coop notice: Can.t reach 192\.0\.2\.1:[0-9]+ yet .*\[Session-JoinRetrying\]' 120
expect 'join: given up on 192\.0\.2\.1:[0-9]+: no answer for a minute' 120
expect 'coop notice: Couldn.t join 192\.0\.2\.1:[0-9]+: .*\[Session-JoinGaveUp\]' 10
cmd coop status
expect 'coop: Not in a session' 10
shot coop-join-errors
# A join code with no relay in tandem.ini: refused in the menu, no load.
cmd exit menu
expect 'exit: to the main menu' 15
pause 5
mark
cmd coop join K7Q-2M9
expect 'coop: main menu page save:' 15
cmd coop pick continue
expect 'coop: main menu: K7Q-2M9 not joined: a join code needs a relay \(popup\)' 10
pause 3
reject 'session: starting as join'
# With a relay: a code no session has, refused at once, in words.
if [[ -n ${RELAY:-} ]]; then
  cmd coop set relay "$RELAY"
  expect 'setting: relay = .* \(saved to tandem.ini\)' 10
  cmd coop join AAA-AAA
  expect 'coop: main menu page save:' 15
  cmd coop pick continue
  expect 'session: starting as join of code AAA-AAA' 120
  expect 'join: given up on code AAA-AAA: (no session has this join code|the join code led to no session)' 120
  expect 'coop notice: Couldn.t join code AAA-AAA: .*\[Session-JoinGaveUp\]' 10
else
  note 'RELAY=HOST:PORT not set: the wrong code case is left out'
fi
cmd coop set relay "$old"
# The co-op menu's choice lasts for the game's run: a fresh one next.
$game stop >/dev/null
