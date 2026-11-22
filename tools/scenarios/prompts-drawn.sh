# As host with native_prompts = off in tandem.ini: no prompt is the game's own interaction, the puppet's body gets no interaction component from its donor, and the Trade prompt on the player beside us is the one we draw, as before
# shellcheck shell=bash disable=SC2317,SC2154
# On purpose (no silent fallback, docs/architecture/degradations.md): tandem.ini switches the game's own prompts off.
expect_degraded "drawn prompts"
ensure host trade=on native_prompts=off
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode mirror --offset 1.2,0,0 --body female
expect 'joined \(player [0-9]+' 20
expect 'puppet [0-9]+: controller handed to the engine' 30
# The body may be built again within a second (a stand-in once no look
# came): the prompts meet each body within a second of its existing.
pause 2
cmd prompts
expect 'prompts: drawn by us \(tandem.ini native_prompts = off\), 1 bodies' 10
expect 'prompts:   tandem-peer entity [0-9a-f]+: drawn drawn layer off ' 5
note "$(since | grep -aoE 'body: female body has (an|no) Interaction component.*' | tail -1)"
reject 'body: female body given .* Interaction component'
cmd prompts face
expect 'trade: tandem-peer is beside us: trade prompt shown' 10
query path prompt
expect_value "$path" '~' '^drawn drawn layer off active false shown false choice none '
shot prompts-ini-off
# The request by the command the drawn prompt's key runs.
cmd trade ask
expect 'trade: asked tandem-peer \(player [0-9]+\) to trade' 10
reject 'prompts: .*the game.s own interaction \(its Interaction component'
reject 'watchdog: game thread silent'
