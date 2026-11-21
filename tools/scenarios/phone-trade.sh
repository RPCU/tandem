# Trade from the phone, 40 m apart (as a guest, so nothing reaches a save): the partner's row offers Trade, the request reaches it at that distance, the table stays open apart, and the commit is applied once
# shellcheck shell=bash disable=SC2317
ensure join trade=on
peer host --mode mirror --offset 40,0,0 --body female --trade-accept --trade-give Items.CommonMaterial1:2
expect 'in the session as player' 30
cmd phone
# (Both on link = auto: linked from the start.)
expect 'phone: tandem-peer \(Linked\): Hang up' 10
cmd phone open
expect 'phone: the contacts list shows 1 player\(s\) first' 10
pause 1
cmd phone pick tandem-peer
expect "phone: tandem-peer's actions: .* \| Trade \| " 10
cmd phone pick Trade
expect 'phone: tandem-peer: Trade chosen' 5
expect 'trade: asking tandem-peer from the phone' 10
expect 'trade: asked tandem-peer \(player [0-9]+\) to trade' 5
expect_peer 'trade: accepted: table open with player' 15
expect 'trade: tandem-peer accepted' 10
expect 'trade: table open with tandem-peer \(player [0-9]+\), apart: walking does not end it' 10
expect 'trade: tandem-peer.s side: 1 stacks, 0 eddies, asks 0' 10
# 40 m apart for longer than a frame: the table stays.
pause 6
shot phone-trade-table
reject 'trade: we closed the trade \(reason 4\)'
cmd trade agree
expect 'trade: we agree to the table' 10
expect_peer 'trade: player [0-9]+ agrees: so do we' 10
expect 'trade: took Items.CommonMaterial1 x2 \(true\)' 15
expect 'trade: applied with tandem-peer: gave 0 stacks and 0 eddies, took 1 stacks and 0 eddies' 10
at_most 1 'trade: applied with'
reject 'watchdog: game thread silent'
