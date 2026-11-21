# As a guest (it never saves: our inventory ends as it began), a player gives us two crafting components and we give them back: asked and accepted both ways, nothing moves before both agree, each commit is applied once
# shellcheck shell=bash disable=SC2317,SC2154
ensure join trade=on
# The host asks us (again every 20 s), puts two components up, agrees once we do.
peer host --mode front --body female --trade-offer --trade-accept --trade-give Items.CommonMaterial1:2
expect 'in the session as player' 30
# What our inventory holds as the game counts it, before any trade.
query items item Items.CommonMaterial1
query money money
expect_peer 'trade: asked player [0-9]+ to trade' 40
expect 'trade: .* asks us to trade' 15
cmd trade accept
expect 'trade: accepted .* request' 10
expect 'trade: table open with' 10
expect_peer 'trade: table open with player' 10
expect 'trade: .* side: 1 stacks, 0 eddies, asks 0' 10
# The table's screen, if the game brought it up (the trade works without).
pause 2
shot trade-table
note "$(since | grep -E 'trade: the table.s screen' | tail -1)"
# Nothing has changed hands yet; we agree, they follow, the host commits.
reject 'trade: applied'
cmd trade agree
expect 'trade: we agree to the table' 10
expect_peer 'trade: player [0-9]+ agrees: so do we' 10
expect 'trade: took Items.CommonMaterial1 x2 \(true\)' 15
expect 'trade: applied with .*: gave 0 stacks and 0 eddies, took 1 stacks and 0 eddies' 10
expect_peer 'trade: applied with player [0-9]+: gave 1 stacks and 0 eddies, took 0 stacks' 10
# The two components are in our inventory; no eddies moved.
query got item Items.CommonMaterial1
expect_changed "$items" "$got" +2
query paid money
expect_changed "$money" "$paid" same
# And back: we ask, they accept, we put the two up, agree.
cmd trade ask
expect 'trade: asked .* to trade' 10
expect_peer 'trade: accepted: table open with player' 15
expect 'trade: .* accepted' 10
cmd trade give Items.CommonMaterial1 2
expect 'trade: give Items.CommonMaterial1 x2: true' 10
expect_peer 'trade: their side: 1 stacks, 0 eddies' 10
# What is not for trade is refused with its reason (our money has its own line).
cmd trade give Items.money 1
expect 'trade: Items.money not put on the table: eddies have their own line' 10
cmd trade agree
expect_peer 'trade: applied with player [0-9]+: gave [01] stacks and 0 eddies, took 1 stacks' 15
expect 'trade: applied with .*: gave 1 stacks and 0 eddies, took [01] stacks and 0 eddies' 10
# Ours left our inventory (theirs, if they put the two up again, came in).
took=$(since | grep -oE 'trade: applied with .*: gave 1 stacks and 0 eddies, took [01] stacks' | tail -1 | grep -oE '[01] stacks$' | cut -c1 || true)
query back item Items.CommonMaterial1
expect_changed "$got" "$back" "$((${took:-0} * 2 - 2))"
query after money
expect_changed "$money" "$after" same
cmd trade
expect 'trade: on, none with player 0; .* 2 trades done' 10
at_most 2 'trade: applied with'
reject 'stacks short'
reject 'watchdog: game thread silent'
