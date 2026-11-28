# Start a new co-op game (main menu): the game's own New Game, then hosting once the new V's world loads, its join code on screen, and the mock player joins by it. Needs RELAY=HOST:PORT (task relay:run) and the game's relay set to it
# by hand: a person in game does what its notes say (tools/sweep.sh leaves it out)
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
[[ -n ${RELAY:-} ]] || fail "RELAY=HOST:PORT: a relay the game and the mock player reach (task relay:run)"
ini="$dir/red4ext/plugins/tandem/tandem.ini"
old=$(remote "sed -n 's/^relay *= *//p' '$ini'" | head -1)
cmd coop set relay "$RELAY"
cmd exit menu
expect 'exit: to the main menu' 15
pause 10
cmd coop menu coop
expect 'coop: main menu page coop:.*Start a new co-op game' 15
cmd coop pick start a new co-op game
expect 'coop: main menu: a new game, the session \(role 1\) starts as its world loads' 10
note 'the game shows its New Game: pick a start, a lifepath, make a V, confirm (character creation by hand)'
mark
expect 'session: starting as host on UDP 27450' 900
# Hosted from the co-op menu: a co-op campaign, its world noted.
expect 'campaign: this world is a co-op campaign' 30
expect 'coop notice: Co-op join code: [0-9A-Z-]+ \[Session-Code\]' 120
code=$(since | sed -n 's/.*coop notice: Co-op join code: \([0-9A-Z-]*\) \[Session-Code\].*/\1/p' | tail -1)
shot coop-new-host
peer join --code "$code" --relay "$RELAY" --mode front --body male
expect 'joined \(player [0-9]+' 30
note 'the new V is in its lifepath start: the game left as it is (the next scenario loads the test save again)'
cmd coop set relay "$old"
