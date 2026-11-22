# As host, the Trade prompt on another player is the game's own interaction: the puppet's Interaction component (its own or the donor's), our choice on its ReturnTalk layer (else GenericTalk), put there as the layer activates, the layer active and the game's prompt holding "Trade" once our V looks at the body from 1.2 m; drawn by us again after `prompts off`; and a choice event on the body asks the trade
# shellcheck shell=bash disable=SC2317,SC2154
# On purpose: `prompts off` draws them for a while, and the mock player is told.
allow_fallback "drawn prompts"
# The mock player copies our V 1.2 m beside it (mirror), so it stays in
# reach wherever our V turns. The key itself is not pressed here: `prompts
# press` queues the game's choice event on the body (the way from the event
# to the request); the real key is the pilot's or a hand's (prompts.md).
ensure host trade=on
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset 1.2,0,0 --body female
expect 'joined \(player [0-9]+' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
# Which way the body's prompts go, said once when the first one is offered.
cmd prompts
expect 'prompts: the game.s own interactions, 1 bodies' 10
expect 'prompts: tandem-peer \(a player.s puppet, entity [0-9a-f]+\): the game.s own interaction \(its Interaction component, layer [A-Za-z]+\)' 10
reject 'prompts: .*drawn prompts'
query path prompt
note "before our V looks at it: $path"
# In front of it, looking at it: the game's scanner activates the layer and
# its own prompt holds our choice.
cmd prompts face
expect 'prompts face: our V [0-9.]+ m from player [0-9]+.s puppet' 5
expect 'prompts: tandem-peer: its layer is active for our V' 10
expect 'prompts: trade shown on tandem-peer by the game \(hub -?[0-9]+: .+ key [A-Za-z0-9_]+ hold (true|false)' 10
note "$(since | grep -aoE 'prompts: trade shown on .*' | tail -1)"
query path prompt
expect_value "$path" '~' '^game shown layer on active true shown true choice trade '
shot prompts-game
# None of ours drawn beside it.
reject 'trade: .* is beside us: trade prompt shown'
# Drawn by us, as tandem.ini native_prompts = off: the same body, the prompt
# we write into the game's UI ourselves.
mark
cmd prompts off
expect 'prompts: drawn by us \(prompts off\), every body adopted again' 5
expect 'prompts: tandem-peer \(a player.s puppet, entity [0-9a-f]+\): drawn prompts \(prompts off\)' 10
expect 'trade: tandem-peer is beside us: trade prompt shown' 10
query path prompt
expect_value "$path" '~' '^drawn drawn layer off '
shot prompts-drawn
# And the game's own again.
mark
cmd prompts on
expect 'prompts: the game.s own interactions, every body adopted again' 5
expect 'prompts: trade shown on tandem-peer by the game' 15
# The game's choice event on the body: our request to trade.
cmd prompts press
expect 'prompts press: \(test\) a trade choice event queued on tandem-peer' 5
expect 'prompts: trade pressed on tandem-peer: the game.s choice event \([A-Za-z_0-9]+, layer [A-Za-z]+\)' 5
expect 'trade: asked tandem-peer \(player [0-9]+\) to trade' 5
expect_peer 'trade: player [0-9]+ asks us to trade' 10
reject 'prompts: .*back to drawn prompts'
reject 'threads: hook Prompts'
reject 'watchdog: game thread silent'
