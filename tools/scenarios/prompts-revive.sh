# As host, a downed player's body carries Revive on the game's own interaction, before Trade; a choice event on it starts our 10 s hold (test: no key) and the mock player gets up, its body offering Trade again
# shellcheck shell=bash disable=SC2317,SC2154
# Not asked here: that the game shows the prompt on a body lying on the
# ground (our V must look down at it: the pilot's or a hand's, prompts.md).
ensure host trade=on
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset 1.2,0,0 --body female --go-down 10
expect 'joined \(player [0-9]+' 20
# An earlier scenario's revive must not make this fall an "out" (300 s cooldown).
cmd revive fresh
expect 'revive fresh: no recent revive' 5
# Up: its body offers Trade.
expect 'prompts: trade offered on tandem-peer through the game.s interaction' 20
expect_peer 'our V is down \(--go-down\)' 20
expect 'revive: tandem-peer is down' 5
# Down: Revive, on the same body.
expect 'prompts: revive offered on tandem-peer through the game.s interaction' 5
query path prompt
expect_value "$path" '~' '^game [a-z]+ layer on active (true|false) shown (true|false) choice revive '
# None of ours drawn over it.
reject 'revive: tandem-peer is down beside us: revive prompt shown'
cmd prompts press
expect 'prompts press: \(test\) a revive choice event queued on tandem-peer' 5
expect 'prompts: revive pressed on tandem-peer: the game.s choice event' 5
expect 'revive: reviving tandem-peer \(test hold\)' 5
expect_peer 'player 1 starts reviving us' 5
expect 'revive: we revived tandem-peer' 15
expect_peer 'our V is up \(revived by player 1\)' 5
expect 'revive: tandem-peer is up' 5
# Up again: Trade again.
mark
expect 'prompts: trade offered on tandem-peer through the game.s interaction' 10
reject 'prompts: .*back to drawn prompts'
reject 'threads: hook Prompts'
reject 'watchdog: game thread silent'
